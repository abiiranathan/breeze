/*
 * breeze.c  –  Lightweight C99 template engine
 *
 * Expanded feature set:
 *   - Filters: {{ var | upper }}, {{ var | default:N/A }}, {{ var | truncate:20 }}, etc.
 *   - Loop meta-variables: loop.index, loop.index1, loop.first, loop.last, loop.length
 *   - {% elif expr %} chains
 *   - {% set varname = value %} for in-template variable assignment
 *   - {% raw %}...{% endraw %} verbatim blocks
 *   - render_template_file() helper
 *   - Dynamic context: context_new / context_set / context_free
 *   - User-registerable filters via breeze_register_filter()
 */

#include <ctype.h>
#include <errno.h>
#include <string.h>

#include "breeze.h"

/* ================================================================
   Filter registry
   ================================================================ */

#define BREEZE_MAX_FILTERS 64

static BreezeFilter g_filters[BREEZE_MAX_FILTERS];
static size_t g_filter_count = 0;

void breeze_clear_filters(void) { g_filter_count = 0; }

bool breeze_register_filter(const char* name, BreezeFilterFn fn) {
    if (!name || !fn || g_filter_count >= BREEZE_MAX_FILTERS) return false;
    /* overwrite if name already exists */
    for (size_t i = 0; i < g_filter_count; i++) {
        if (strcmp(g_filters[i].name, name) == 0) {
            g_filters[i].fn = fn;
            return true;
        }
    }
    strncpy(g_filters[g_filter_count].name, name, sizeof(g_filters[0].name) - 1);
    g_filters[g_filter_count].name[sizeof(g_filters[0].name) - 1] = '\0';
    g_filters[g_filter_count].fn = fn;
    g_filter_count++;
    return true;
}

static BreezeFilterFn find_filter(const char* name) {
    for (size_t i = 0; i < g_filter_count; i++) {
        if (strcmp(g_filters[i].name, name) == 0) return g_filters[i].fn;
    }
    return NULL;
}

/* ================================================================
   Output Buffer
   ================================================================ */

WARN_UNUSED bool buffer_init(OutputBuffer* buf, size_t initial_capacity) {
    if (initial_capacity == 0) initial_capacity = 1024;
    buf->data = malloc(initial_capacity);
    if (!buf->data) {
        perror("malloc");
        return false;
    }
    buf->size = 0;
    buf->capacity = initial_capacity;
    buf->data[0] = '\0';
    return true;
}

WARN_UNUSED static bool buffer_append(OutputBuffer* buf, const char* str, size_t len) {
    if (!buf || !str || len == 0) return true;
    if (buf->size + len + 1 > buf->capacity) {
        size_t nc = buf->capacity * 2;
        while (buf->size + len + 1 > nc) nc *= 2;
        buf->data = realloc(buf->data, nc);
        if (!buf->data) {
            perror("realloc");
            return false;
        }
        buf->capacity = nc;
    }
    memcpy(buf->data + buf->size, str, len);
    buf->size += len;
    buf->data[buf->size] = '\0';
    return true;
}

/* Append a C-string */
WARN_UNUSED static bool buffer_append_str(OutputBuffer* buf, const char* str) {
    return str ? buffer_append(buf, str, strlen(str)) : true;
}

/* ================================================================
   Dynamic context
   ================================================================ */

TemplateContext* context_new(size_t initial_capacity) {
    if (initial_capacity == 0) initial_capacity = 8;
    TemplateContext* ctx = malloc(sizeof(TemplateContext));
    if (!ctx) return NULL;
    ctx->vars = malloc(sizeof(TemplateVar) * initial_capacity);
    if (!ctx->vars) {
        free(ctx);
        return NULL;
    }
    ctx->count = 0;
    ctx->capacity = initial_capacity;
    return ctx;
}

WARN_UNUSED bool context_set(TemplateContext* ctx, const char* key, TemplateValue value) {
    if (!ctx || !key) return false;
    /* update existing */
    for (size_t i = 0; i < ctx->count; i++) {
        if (strcmp(ctx->vars[i].key, key) == 0) {
            ctx->vars[i].value = value;
            return true;
        }
    }
    /* add new */
    if (ctx->count >= ctx->capacity) {
        ctx->capacity *= 2;
        ctx->vars = realloc(ctx->vars, sizeof(TemplateVar) * ctx->capacity);
        if (!ctx->vars) return false;
    }
    ctx->vars[ctx->count].key = key;
    ctx->vars[ctx->count].value = value;
    ctx->count++;
    return true;
}

void context_free(TemplateContext* ctx) {
    if (!ctx) return;
    free(ctx->vars);
    free(ctx);
}

/* ================================================================
   Loop Stack
   ================================================================ */

typedef struct {
    const TemplateValue* array;
    size_t index;
    char* item_name;
    const char* loop_start;
} LoopFrame;

typedef struct {
    LoopFrame* frames;
    size_t depth;
    size_t capacity;
} LoopStack;

WARN_UNUSED static bool evaluate_condition(const char* condition, const TemplateContext* ctx,
                                           const LoopStack* loop_stack, TemplateError* err, const char* template_start,
                                           const char* error_pos);

ALWAYS_INLINE WARN_UNUSED static inline bool loop_stack_init(LoopStack* s, size_t cap) {
    s->frames = malloc(sizeof(LoopFrame) * cap);
    if (!s->frames) {
        perror("malloc");
        return false;
    }
    s->depth = 0;
    s->capacity = cap;
    return true;
}

ALWAYS_INLINE WARN_UNUSED static inline bool loop_stack_push(LoopStack* s, const TemplateValue* array,
                                                             const char* item_name, const char* loop_start) {
    if (s->depth >= s->capacity) {
        s->capacity *= 2;
        s->frames = realloc(s->frames, sizeof(LoopFrame) * s->capacity);
        if (!s->frames) {
            perror("realloc");
            return false;
        }
    }
    s->frames[s->depth++] =
        (LoopFrame){.array = array, .index = 0, .item_name = strdup(item_name), .loop_start = loop_start};
    return true;
}

ALWAYS_INLINE static inline void loop_stack_pop(LoopStack* s) {
    if (s->depth > 0) {
        s->depth--;
        free(s->frames[s->depth].item_name);
    }
}

ALWAYS_INLINE static inline LoopFrame* loop_stack_top(LoopStack* s) {
    return s->depth > 0 ? &s->frames[s->depth - 1] : NULL;
}

/* ================================================================
   Conditional Stack  (extended: elif support)
   ================================================================ */

typedef struct {
    bool condition_met; /* true if any branch (if/elif) was taken   */
    bool in_else_branch;
    bool done; /* true once a branch has rendered (skip all others) */
    const char* if_start;
} ConditionalFrame;

typedef struct {
    ConditionalFrame* frames;
    size_t depth;
    size_t capacity;
} ConditionalStack;

WARN_UNUSED static bool conditional_stack_init(ConditionalStack* s, size_t cap) {
    s->frames = malloc(sizeof(ConditionalFrame) * cap);
    if (!s->frames) {
        perror("malloc");
        return false;
    }
    s->depth = 0;
    s->capacity = cap;
    return true;
}

WARN_UNUSED static bool conditional_stack_push(ConditionalStack* s, bool condition, const char* if_start) {
    if (s->depth >= s->capacity) {
        s->capacity *= 2;
        s->frames = realloc(s->frames, sizeof(ConditionalFrame) * s->capacity);
        if (!s->frames) {
            perror("realloc");
            return false;
        }
    }
    s->frames[s->depth++] = (ConditionalFrame){.condition_met = condition,
                                               .in_else_branch = false,
                                               .done = condition, /* if true, this branch is "done" (rendered) */
                                               .if_start = if_start};
    return true;
}

static void conditional_stack_pop(ConditionalStack* s) {
    if (s->depth > 0) s->depth--;
}

static ConditionalFrame* conditional_stack_top(ConditionalStack* s) {
    return s->depth > 0 ? &s->frames[s->depth - 1] : NULL;
}

/* ================================================================
   Utility
   ================================================================ */

ALWAYS_INLINE static inline bool set_error(TemplateError* err, TemplateErrorType type, const char* msg, size_t line) {
    if (err) {
        err->type = type;
        err->line = line;
        snprintf(err->message, sizeof(err->message) - 1, "%s", msg);
        err->message[sizeof(err->message) - 1] = '\0';
    }
    return false;
}

ALWAYS_INLINE static inline size_t calc_line(const char* start, const char* pos) {
    size_t line = 1;
    for (const char* c = start; c && c < pos; c++)
        if (*c == '\n') line++;
    return line;
}

ALWAYS_INLINE static inline const TemplateValue* context_get(const TemplateContext* ctx, const char* key) {
    for (size_t i = 0; i < ctx->count; i++)
        if (strcmp(ctx->vars[i].key, key) == 0) return &ctx->vars[i].value;
    return NULL;
}

/* Trim a string in-place; returns pointer to first non-space char */
static char* str_trim(char* s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s) {
        char* e = s + strlen(s) - 1;
        while (e > s && isspace((unsigned char)*e)) e--;
        *(e + 1) = '\0';
    }
    return s;
}

/* ================================================================
   value_to_string  (shared helper, also used by filters)
   ================================================================ */

WARN_UNUSED static bool value_to_string(const TemplateValue* val, OutputBuffer* buf) {
    char tmp[128];
    switch (val->type) {
        case TMPL_STRING:
            return buffer_append(buf, val->value.str ? val->value.str : "",
                                 val->value.str ? strlen(val->value.str) : 0);
        case TMPL_INT:
            snprintf(tmp, sizeof(tmp), "%d", val->value.integer);
            return buffer_append_str(buf, tmp);
        case TMPL_FLOAT:
            snprintf(tmp, sizeof(tmp), "%.4f", val->value.floating);
            return buffer_append_str(buf, tmp);
        case TMPL_DOUBLE:
            snprintf(tmp, sizeof(tmp), "%.4f", val->value.dbl);
            return buffer_append_str(buf, tmp);
        case TMPL_BOOL:
            return buffer_append_str(buf, val->value.boolean ? "true" : "false");
        case TMPL_LONG:
            snprintf(tmp, sizeof(tmp), "%ld", val->value.long_int);
            return buffer_append_str(buf, tmp);
        case TMPL_UINT:
            snprintf(tmp, sizeof(tmp), "%u", val->value.uint);
            return buffer_append_str(buf, tmp);
        case TMPL_ARRAY:
            snprintf(tmp, sizeof(tmp), "[array of size %zu]", val->value.array.count);
            return buffer_append_str(buf, tmp);
        default:
            abort();
    }
}

ALWAYS_INLINE static inline bool is_truthy(const TemplateValue* val) {
    if (!val) return false;
    switch (val->type) {
        case TMPL_BOOL:
            return val->value.boolean;
        case TMPL_INT:
            return val->value.integer != 0;
        case TMPL_FLOAT:
            return val->value.floating != 0.0f;
        case TMPL_DOUBLE:
            return val->value.dbl != 0.0;
        case TMPL_LONG:
            return val->value.long_int != 0;
        case TMPL_UINT:
            return val->value.uint != 0;
        case TMPL_STRING:
            return val->value.str && strlen(val->value.str) > 0;
        case TMPL_ARRAY:
            return val->value.array.count > 0;
        default:
            return false;
    }
}

/* ================================================================
   Built-in Filters
   ================================================================ */

static bool filter_upper(const TemplateValue* val, const char* arg, OutputBuffer* out) {
    (void)arg;
    OutputBuffer tmp = {0};
    if (!buffer_init(&tmp, 64)) return false;
    if (!value_to_string(val, &tmp)) {
        free(tmp.data);
        return false;
    }
    for (size_t i = 0; i < tmp.size; i++) tmp.data[i] = (char)toupper((unsigned char)tmp.data[i]);
    bool ok = buffer_append(out, tmp.data, tmp.size);
    free(tmp.data);
    return ok;
}

static bool filter_lower(const TemplateValue* val, const char* arg, OutputBuffer* out) {
    (void)arg;
    OutputBuffer tmp = {0};
    if (!buffer_init(&tmp, 64)) return false;
    if (!value_to_string(val, &tmp)) {
        free(tmp.data);
        return false;
    }
    for (size_t i = 0; i < tmp.size; i++) tmp.data[i] = (char)tolower((unsigned char)tmp.data[i]);
    bool ok = buffer_append(out, tmp.data, tmp.size);
    free(tmp.data);
    return ok;
}

static bool filter_len(const TemplateValue* val, const char* arg, OutputBuffer* out) {
    (void)arg;
    char tmp[64];
    if (val->type == TMPL_STRING) {
        snprintf(tmp, sizeof(tmp), "%zu", val->value.str ? strlen(val->value.str) : 0);
    } else if (val->type == TMPL_ARRAY) {
        snprintf(tmp, sizeof(tmp), "%zu", val->value.array.count);
    } else {
        /* length of string representation */
        OutputBuffer sb = {0};
        if (!buffer_init(&sb, 64)) return false;
        if (!value_to_string(val, &sb)) {
            free(sb.data);
            return false;
        }
        snprintf(tmp, sizeof(tmp), "%zu", sb.size);
        free(sb.data);
    }
    return buffer_append_str(out, tmp);
}

static bool filter_trim(const TemplateValue* val, const char* arg, OutputBuffer* out) {
    (void)arg;
    OutputBuffer tmp = {0};
    if (!buffer_init(&tmp, 64)) return false;
    if (!value_to_string(val, &tmp)) {
        free(tmp.data);
        return false;
    }
    char* s = str_trim(tmp.data);
    bool ok = buffer_append_str(out, s);
    free(tmp.data);
    return ok;
}

static bool filter_reverse(const TemplateValue* val, const char* arg, OutputBuffer* out) {
    (void)arg;
    OutputBuffer tmp = {0};
    if (!buffer_init(&tmp, 64)) return false;
    if (!value_to_string(val, &tmp)) {
        free(tmp.data);
        return false;
    }
    /* reverse in place */
    size_t n = tmp.size;
    for (size_t i = 0; i < n / 2; i++) {
        char c = tmp.data[i];
        tmp.data[i] = tmp.data[n - 1 - i];
        tmp.data[n - 1 - i] = c;
    }
    bool ok = buffer_append(out, tmp.data, n);
    free(tmp.data);
    return ok;
}

/* default:<fallback>  –  use fallback if value is falsy */
static bool filter_default(const TemplateValue* val, const char* arg, OutputBuffer* out) {
    if (!is_truthy(val)) {
        return buffer_append_str(out, arg ? arg : "");
    }
    return value_to_string(val, out);
}

/* truncate:<n>  –  cut string to at most n chars, append "..." if cut */
static bool filter_truncate(const TemplateValue* val, const char* arg, OutputBuffer* out) {
    size_t maxlen = arg ? (size_t)atoi(arg) : 20;
    OutputBuffer tmp = {0};
    if (!buffer_init(&tmp, 64)) return false;
    if (!value_to_string(val, &tmp)) {
        free(tmp.data);
        return false;
    }
    bool ok;
    if (tmp.size <= maxlen) {
        ok = buffer_append(out, tmp.data, tmp.size);
    } else {
        ok = buffer_append(out, tmp.data, maxlen) && buffer_append_str(out, "...");
    }
    free(tmp.data);
    return ok;
}

/* capitalize  –  first char upper, rest lower */
static bool filter_capitalize(const TemplateValue* val, const char* arg, OutputBuffer* out) {
    (void)arg;
    OutputBuffer tmp = {0};
    if (!buffer_init(&tmp, 64)) return false;
    if (!value_to_string(val, &tmp)) {
        free(tmp.data);
        return false;
    }
    for (size_t i = 0; i < tmp.size; i++)
        tmp.data[i] = i == 0 ? (char)toupper((unsigned char)tmp.data[i]) : (char)tolower((unsigned char)tmp.data[i]);
    bool ok = buffer_append(out, tmp.data, tmp.size);
    free(tmp.data);
    return ok;
}

/* replace:<from>:<to>  –  replace all occurrences of <from> with <to> */
static bool filter_replace(const TemplateValue* val, const char* arg, OutputBuffer* out) {
    /* arg format: "from:to" */
    if (!arg) return value_to_string(val, out);
    /* split arg on first ':' */
    char arg_copy[256];
    strncpy(arg_copy, arg, sizeof(arg_copy) - 1);
    arg_copy[sizeof(arg_copy) - 1] = '\0';
    char* colon = strchr(arg_copy, ':');
    const char* from = arg_copy;
    const char* to = "";
    if (colon) {
        *colon = '\0';
        to = colon + 1;
    }

    OutputBuffer src = {0};
    if (!buffer_init(&src, 64)) return false;
    if (!value_to_string(val, &src)) {
        free(src.data);
        return false;
    }

    size_t from_len = strlen(from);
    size_t to_len = strlen(to);
    if (from_len == 0) {
        bool ok = buffer_append(out, src.data, src.size);
        free(src.data);
        return ok;
    }

    const char* p = src.data;
    bool ok = true;
    while (*p) {
        if (strncmp(p, from, from_len) == 0) {
            if (to_len > 0) ok = ok && buffer_append(out, to, to_len);
            p += from_len;
        } else {
            ok = ok && buffer_append(out, p, 1);
            p++;
        }
    }
    free(src.data);
    return ok;
}

/* Register all built-in filters */
static void register_builtin_filters(void) {
    if (!find_filter("upper")) breeze_register_filter("upper", filter_upper);
    if (!find_filter("lower")) breeze_register_filter("lower", filter_lower);
    if (!find_filter("len")) breeze_register_filter("len", filter_len);
    if (!find_filter("trim")) breeze_register_filter("trim", filter_trim);
    if (!find_filter("reverse")) breeze_register_filter("reverse", filter_reverse);
    if (!find_filter("default")) breeze_register_filter("default", filter_default);
    if (!find_filter("truncate")) breeze_register_filter("truncate", filter_truncate);
    if (!find_filter("capitalize")) breeze_register_filter("capitalize", filter_capitalize);
    if (!find_filter("replace")) breeze_register_filter("replace", filter_replace);
}

/* ================================================================
   Apply a filter chain  e.g.  "name | upper | truncate:10"
   ================================================================ */

WARN_UNUSED static bool apply_filters(const TemplateValue* val, const char* filter_expr, OutputBuffer* out,
                                      TemplateError* err, size_t line) {
    /* Walk the filter chain (pipe-separated). The first filter receives
     * the original typed value; subsequent filters receive string output
     * from the previous filter. */
    char chain[256];
    strncpy(chain, filter_expr, sizeof(chain) - 1);
    chain[sizeof(chain) - 1] = '\0';

    char* saveptr = NULL;
    char* tok = strtok_r(chain, "|", &saveptr);
    bool first_filter = true;
    OutputBuffer cur = {0};
    while (tok) {
        char* trimmed = str_trim(tok);
        if (*trimmed == '\0') {
            tok = strtok_r(NULL, "|", &saveptr);
            continue;
        }

        /* Split on ':' to get filter name and optional arg */
        char fname[64];
        fname[0] = '\0';
        char farg[192];
        farg[0] = '\0';
        char* colon = strchr(trimmed, ':');
        if (colon) {
            size_t nlen = (size_t)(colon - trimmed);
            if (nlen >= sizeof(fname)) nlen = sizeof(fname) - 1;
            memcpy(fname, trimmed, nlen);
            fname[nlen] = '\0';
            strncpy(farg, colon + 1, sizeof(farg) - 1);
            farg[sizeof(farg) - 1] = '\0';
        } else {
            strncpy(fname, trimmed, sizeof(fname) - 1);
            fname[sizeof(fname) - 1] = '\0';
        }

        BreezeFilterFn fn = find_filter(fname);
        if (!fn) {
            if (cur.data) free(cur.data);
            char msg[128];
            snprintf(msg, sizeof(msg), "Unknown filter '%s'", fname);
            return set_error(err, TMPL_ERR_RENDER, msg, line);
        }

        /* Filter into a new buffer */
        OutputBuffer next = {0};
        if (!buffer_init(&next, 64)) {
            if (cur.data) free(cur.data);
            return set_error(err, TMPL_ERR_MEMORY, "filter next buf", line);
        }

        if (first_filter) {
            if (!fn(val, *farg ? farg : NULL, &next)) {
                if (cur.data) free(cur.data);
                free(next.data);
                return set_error(err, TMPL_ERR_RENDER, "Filter execution failed", line);
            }
            first_filter = false;
        } else {
            TemplateValue cur_val = {.type = TMPL_STRING, .value.str = cur.data};
            if (!fn(&cur_val, *farg ? farg : NULL, &next)) {
                if (cur.data) free(cur.data);
                free(next.data);
                return set_error(err, TMPL_ERR_RENDER, "Filter execution failed", line);
            }
        }

        if (cur.data) free(cur.data);
        cur = next;

        tok = strtok_r(NULL, "|", &saveptr);
    }

    bool ok = buffer_append(out, cur.data, cur.size);
    if (cur.data) free(cur.data);
    return ok;
}

/* ================================================================
   Loop item helper
   ================================================================ */

static void get_loop_item(const LoopFrame* f, TemplateValue* item) {
    item->type = f->array->value.array.item_type;
    switch (item->type) {
        case TMPL_STRING:
            item->value.str = ((const char**)f->array->value.array.items)[f->index];
            break;
        case TMPL_INT:
            item->value.integer = *(((int**)f->array->value.array.items)[f->index]);
            break;
        case TMPL_FLOAT:
            item->value.floating = *(((float**)f->array->value.array.items)[f->index]);
            break;
        case TMPL_DOUBLE:
            item->value.dbl = *(((double**)f->array->value.array.items)[f->index]);
            break;
        case TMPL_BOOL:
            item->value.boolean = *(((bool**)f->array->value.array.items)[f->index]);
            break;
        case TMPL_LONG:
            item->value.long_int = *(((long**)f->array->value.array.items)[f->index]);
            break;
        case TMPL_UINT:
            item->value.uint = *(((unsigned int**)f->array->value.array.items)[f->index]);
            break;
        default:
            break;
    }
}

static bool find_loop_item_by_name(const LoopStack* s, const char* name, TemplateValue* item) {
    for (size_t i = s->depth; i > 0; i--) {
        const LoopFrame* lf = &s->frames[i - 1];
        if (strcmp(name, lf->item_name) == 0) {
            get_loop_item(lf, item);
            return true;
        }
    }
    return false;
}

/* ================================================================
   Expression evaluation
   ================================================================ */

WARN_UNUSED static bool expr_stack_init(ExprStack* s, size_t cap) {
    s->tokens = malloc(sizeof(ExprToken) * cap);
    if (!s->tokens) {
        perror("malloc");
        return false;
    }
    s->size = 0;
    s->capacity = cap;
    return true;
}

static char* next_token(char* str, char** saveptr, char* buf, size_t bufsz) {
    if (!str && !*saveptr) return NULL;
    char* start = str ? str : *saveptr;
    if (!*start) return NULL;
    while (isspace(*start)) start++;
    if (!*start) return NULL;
    if (*start == '(' || *start == ')') {
        if (bufsz >= 2) {
            buf[0] = *start;
            buf[1] = '\0';
            *saveptr = start + 1;
            return buf;
        }
        return NULL;
    }
    char* end = start;
    while (*end && !isspace(*end) && *end != '(' && *end != ')') end++;
    size_t len = (size_t)(end - start);
    if (len >= bufsz) return NULL;
    memcpy(buf, start, len);
    buf[len] = '\0';
    *saveptr = end;
    return buf;
}

WARN_UNUSED static bool expr_stack_push(ExprStack* s, ExprToken tok) {
    if (s->size >= s->capacity) {
        s->capacity *= 2;
        s->tokens = realloc(s->tokens, sizeof(ExprToken) * s->capacity);
        if (!s->tokens) {
            perror("realloc");
            return false;
        }
    }
    s->tokens[s->size++] = tok;
    return true;
}

static ExprToken expr_stack_pop(ExprStack* s) { return s->size > 0 ? s->tokens[--s->size] : (ExprToken){0}; }
static ExprToken expr_stack_peek(ExprStack* s) { return s->size > 0 ? s->tokens[s->size - 1] : (ExprToken){0}; }
static void expr_stack_free(ExprStack* s) { free(s->tokens); }

static int op_precedence(ExprTokenType op) {
    switch (op) {
        case TOKEN_NOT:
            return 3;
        case TOKEN_AND:
            return 2;
        case TOKEN_OR:
            return 1;
        default:
            return 0;
    }
}

WARN_UNUSED static bool infix_to_postfix(const char* expr, const TemplateContext* ctx, const LoopStack* loop_stack,
                                         ExprStack* output, TemplateError* err, const char* ts, const char* ep) {
    ExprStack ops = {0};
    char* copy = NULL;
    if (!expr_stack_init(&ops, 16)) goto oom;
    copy = strdup(expr);
    if (!copy) goto oom;
    char* sp = NULL;
    char tb[128];
    char* tok = next_token(copy, &sp, tb, sizeof(tb));
    while (tok) {
        if (strcmp(tok, "and") == 0) {
            while (ops.size > 0 && op_precedence(expr_stack_peek(&ops).type) >= op_precedence(TOKEN_AND))
                if (!expr_stack_push(output, expr_stack_pop(&ops))) goto oom;
            if (!expr_stack_push(&ops, (ExprToken){.type = TOKEN_AND})) goto oom;
        } else if (strcmp(tok, "or") == 0) {
            while (ops.size > 0 && op_precedence(expr_stack_peek(&ops).type) >= op_precedence(TOKEN_OR))
                if (!expr_stack_push(output, expr_stack_pop(&ops))) goto oom;
            if (!expr_stack_push(&ops, (ExprToken){.type = TOKEN_OR})) goto oom;
        } else if (strcmp(tok, "not") == 0) {
            if (!expr_stack_push(&ops, (ExprToken){.type = TOKEN_NOT})) goto oom;
        } else if (strcmp(tok, "(") == 0) {
            if (!expr_stack_push(&ops, (ExprToken){.type = TOKEN_LPAREN})) goto oom;
        } else if (strcmp(tok, ")") == 0) {
            while (ops.size > 0 && expr_stack_peek(&ops).type != TOKEN_LPAREN)
                if (!expr_stack_push(output, expr_stack_pop(&ops))) goto oom;
            if (ops.size == 0) {
                set_error(err, TMPL_ERR_PARSE, "Mismatched parentheses", calc_line(ts, ep));
                goto err;
            }
            expr_stack_pop(&ops);
        } else {
            bool v = evaluate_condition(tok, ctx, loop_stack, err, ts, ep);
            if (err->type != TMPL_ERR_NONE) goto err;
            if (!expr_stack_push(output, (ExprToken){.type = TOKEN_VALUE, .value = v})) goto oom;
        }
        tok = next_token(NULL, &sp, tb, sizeof(tb));
    }
    while (ops.size > 0) {
        if (expr_stack_peek(&ops).type == TOKEN_LPAREN) {
            set_error(err, TMPL_ERR_PARSE, "Mismatched parentheses", calc_line(ts, ep));
            goto err;
        }
        if (!expr_stack_push(output, expr_stack_pop(&ops))) goto oom;
    }
    free(copy);
    expr_stack_free(&ops);
    return true;
oom:
    set_error(err, TMPL_ERR_MEMORY, "Memory allocation failed", calc_line(ts, ep));
err:
    free(copy);
    expr_stack_free(&ops);
    return false;
}

WARN_UNUSED static bool evaluate_postfix(ExprStack* s) {
    ExprStack ev;
    if (!expr_stack_init(&ev, s->size)) return false;
    for (size_t i = 0; i < s->size; i++) {
        ExprToken t = s->tokens[i];
        switch (t.type) {
            case TOKEN_VALUE:
                if (!expr_stack_push(&ev, t)) {
                    expr_stack_free(&ev);
                    return false;
                }
                break;
            case TOKEN_NOT: {
                if (ev.size < 1) {
                    expr_stack_free(&ev);
                    return false;
                }
                bool v = expr_stack_pop(&ev).value;
                if (!expr_stack_push(&ev, (ExprToken){.type = TOKEN_VALUE, .value = !v})) {
                    expr_stack_free(&ev);
                    return false;
                }
                break;
            }
            case TOKEN_AND: {
                if (ev.size < 2) {
                    expr_stack_free(&ev);
                    return false;
                }
                bool b = expr_stack_pop(&ev).value, a = expr_stack_pop(&ev).value;
                if (!expr_stack_push(&ev, (ExprToken){.type = TOKEN_VALUE, .value = a && b})) {
                    expr_stack_free(&ev);
                    return false;
                }
                break;
            }
            case TOKEN_OR: {
                if (ev.size < 2) {
                    expr_stack_free(&ev);
                    return false;
                }
                bool b = expr_stack_pop(&ev).value, a = expr_stack_pop(&ev).value;
                if (!expr_stack_push(&ev, (ExprToken){.type = TOKEN_VALUE, .value = a || b})) {
                    expr_stack_free(&ev);
                    return false;
                }
                break;
            }
            default:
                expr_stack_free(&ev);
                return false;
        }
    }
    if (ev.size != 1) {
        expr_stack_free(&ev);
        return false;
    }
    bool r = expr_stack_pop(&ev).value;
    expr_stack_free(&ev);
    return r;
}

WARN_UNUSED static bool evaluate_condition(const char* cond, const TemplateContext* ctx, const LoopStack* ls,
                                           TemplateError* err, const char* ts, const char* ep) {
    if (!strstr(cond, " and ") && !strstr(cond, " or ") && !strstr(cond, " not ") && !strchr(cond, '(')) {
        char buf[128];
        strncpy(buf, cond, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char* t = str_trim(buf);

        LoopFrame* lf = loop_stack_top((LoopStack*)ls);
        TemplateValue item = {0};
        if (find_loop_item_by_name(ls, t, &item)) return is_truthy(&item);

        if (lf && strncmp(t, "loop.", 5) == 0) {
            const char* meta = t + 5;
            if (strcmp(meta, "index") == 0) return lf->index != 0;
            if (strcmp(meta, "index1") == 0) return (lf->index + 1) != 0;
            if (strcmp(meta, "first") == 0) return lf->index == 0;
            if (strcmp(meta, "last") == 0) return lf->index == lf->array->value.array.count - 1;
            if (strcmp(meta, "length") == 0) return lf->array->value.array.count != 0;
        }

        const TemplateValue* v = context_get(ctx, t);
        if (!v) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Missing template variable for '%s'", t);
            return set_error(err, TMPL_ERR_PARSE, msg, calc_line(ts, ep));
        }
        return is_truthy(v);
    }
    ExprStack pf;
    if (!expr_stack_init(&pf, 32)) return set_error(err, TMPL_ERR_MEMORY, "expr stack init failed", calc_line(ts, ep));
    if (!infix_to_postfix(cond, ctx, ls, &pf, err, ts, ep)) {
        expr_stack_free(&pf);
        return false;
    }
    bool r = evaluate_postfix(&pf);
    expr_stack_free(&pf);
    return r;
}

/* ================================================================
   Render variable expression  "varname | filter1 | filter2:arg"
   Called when we have determined we should NOT skip output.
   ================================================================ */

WARN_UNUSED static bool render_variable(const char* expr, const TemplateContext* ctx, const LoopStack* loop_stack,
                                        OutputBuffer* out, TemplateError* err, const char* ts, const char* ep) {
    /* Split on first '|' to separate variable name from filters */
    char expr_copy[256];
    strncpy(expr_copy, expr, sizeof(expr_copy) - 1);
    expr_copy[sizeof(expr_copy) - 1] = '\0';

    char* pipe = strchr(expr_copy, '|');
    char* filters_str = NULL;
    if (pipe) {
        *pipe = '\0';
        filters_str = pipe + 1;
    }

    char* name = str_trim(expr_copy);

    /* ---- handle loop meta-variables ---- */
    LoopFrame* lf = loop_stack_top((LoopStack*)loop_stack);

    /* loop.* variables */
    if (strncmp(name, "loop.", 5) == 0 && lf) {
        const char* meta = name + 5;
        TemplateValue meta_val = {0};
        bool found = true;
        if (strcmp(meta, "index") == 0) {
            meta_val.type = TMPL_UINT;
            meta_val.value.uint = (unsigned int)lf->index;
        } else if (strcmp(meta, "index1") == 0) {
            meta_val.type = TMPL_UINT;
            meta_val.value.uint = (unsigned int)(lf->index + 1);
        } else if (strcmp(meta, "first") == 0) {
            meta_val.type = TMPL_BOOL;
            meta_val.value.boolean = (lf->index == 0);
        } else if (strcmp(meta, "last") == 0) {
            meta_val.type = TMPL_BOOL;
            meta_val.value.boolean = (lf->index == lf->array->value.array.count - 1);
        } else if (strcmp(meta, "length") == 0) {
            meta_val.type = TMPL_UINT;
            meta_val.value.uint = (unsigned int)lf->array->value.array.count;
        } else {
            found = false;
        }

        if (found) {
            if (filters_str && *str_trim(filters_str)) {
                return apply_filters(&meta_val, filters_str, out, err, calc_line(ts, ep));
            }
            return value_to_string(&meta_val, out);
        }

        /* unknown loop.* – fall through to context lookup */
    }

    /* ---- resolve from loop item or context ---- */
    const TemplateValue* val = NULL;
    TemplateValue item = {0};
    if (find_loop_item_by_name(loop_stack, name, &item)) {
        val = &item;
    } else {
        val = context_get(ctx, name);
    }

    if (!val) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Missing template variable for '%s'", name);
        return set_error(err, TMPL_ERR_RENDER, msg, calc_line(ts, ep));
    }

    if (filters_str && *str_trim(filters_str)) {
        return apply_filters(val, filters_str, out, err, calc_line(ts, ep));
    }
    return value_to_string(val, out);
}

/* ================================================================
   Dynamic variable storage (for {% set %})
   ================================================================ */

#define SET_VAR_MAX 64

typedef struct {
    char key[64];
    char value[256];
} SetVar;

typedef struct {
    SetVar vars[SET_VAR_MAX];
    size_t count;
} SetVarStore;

static void set_var_store_init(SetVarStore* s) { s->count = 0; }

static bool set_var_store_set(SetVarStore* s, const char* key, const char* value) {
    for (size_t i = 0; i < s->count; i++) {
        if (strcmp(s->vars[i].key, key) == 0) {
            strncpy(s->vars[i].value, value, sizeof(s->vars[i].value) - 1);
            s->vars[i].value[sizeof(s->vars[i].value) - 1] = '\0';
            return true;
        }
    }
    if (s->count >= SET_VAR_MAX) return false;
    strncpy(s->vars[s->count].key, key, sizeof(s->vars[s->count].key) - 1);
    strncpy(s->vars[s->count].value, value, sizeof(s->vars[s->count].value) - 1);
    s->vars[s->count].key[sizeof(s->vars[s->count].key) - 1] = '\0';
    s->vars[s->count].value[sizeof(s->vars[s->count].value) - 1] = '\0';
    s->count++;
    return true;
}

static const char* set_var_store_get(const SetVarStore* s, const char* key) {
    for (size_t i = 0; i < s->count; i++)
        if (strcmp(s->vars[i].key, key) == 0) return s->vars[i].value;
    return NULL;
}

typedef enum { IF_BRANCH_NONE, IF_BRANCH_ELIF, IF_BRANCH_ELSE, IF_BRANCH_ENDIF } IfBranchType;

static IfBranchType find_next_if_branch(const char* from, const char** branch_pos) {
    int depth = 0;
    const char* scan = from;

    while (*scan) {
        if (*scan == '{' && *(scan + 1) == '%') {
            const char* ds = scan + 2;
            const char* de = strstr(ds, "%}");
            if (!de) break;

            char directive[256];
            size_t dlen = (size_t)(de - ds);
            if (dlen >= sizeof(directive)) dlen = sizeof(directive) - 1;
            memcpy(directive, ds, dlen);
            directive[dlen] = '\0';
            char* cmd = str_trim(directive);

            if (strncmp(cmd, "if ", 3) == 0) {
                depth++;
            } else if (strcmp(cmd, "endif") == 0) {
                if (depth == 0) {
                    *branch_pos = scan;
                    return IF_BRANCH_ENDIF;
                }
                depth--;
            } else if (depth == 0 && strncmp(cmd, "elif ", 5) == 0) {
                *branch_pos = scan;
                return IF_BRANCH_ELIF;
            } else if (depth == 0 && strcmp(cmd, "else") == 0) {
                *branch_pos = scan;
                return IF_BRANCH_ELSE;
            }

            scan = de + 2;
            continue;
        }
        scan++;
    }

    *branch_pos = NULL;
    return IF_BRANCH_NONE;
}

static const char* find_matching_endfor(const char* from) {
    int depth = 1;
    const char* scan = from;

    while (*scan && depth > 0) {
        if (*scan == '{' && *(scan + 1) == '%') {
            const char* ds = scan + 2;
            const char* de = strstr(ds, "%}");
            if (!de) break;

            char directive[256];
            size_t dlen = (size_t)(de - ds);
            if (dlen >= sizeof(directive)) dlen = sizeof(directive) - 1;
            memcpy(directive, ds, dlen);
            directive[dlen] = '\0';
            char* cmd = str_trim(directive);

            if (strncmp(cmd, "for ", 4) == 0) {
                depth++;
            } else if (strcmp(cmd, "endfor") == 0) {
                depth--;
                if (depth == 0) return de + 2;
            }

            scan = de + 2;
            continue;
        }
        scan++;
    }

    return scan;
}

static const char* find_matching_endif(const char* from) {
    int depth = 0;
    const char* scan = from;

    while (*scan) {
        if (*scan == '{' && *(scan + 1) == '%') {
            const char* ds = scan + 2;
            const char* de = strstr(ds, "%}");
            if (!de) break;

            char directive[256];
            size_t dlen = (size_t)(de - ds);
            if (dlen >= sizeof(directive)) dlen = sizeof(directive) - 1;
            memcpy(directive, ds, dlen);
            directive[dlen] = '\0';
            char* cmd = str_trim(directive);

            if (strncmp(cmd, "if ", 3) == 0) {
                depth++;
            } else if (strcmp(cmd, "endif") == 0) {
                if (depth == 0) return de + 2;
                depth--;
            }

            scan = de + 2;
            continue;
        }
        scan++;
    }

    return scan;
}

/* ================================================================
   Core render function
   ================================================================ */

bool render_template(const char* template, const TemplateContext* ctx, OutputBuffer* out, TemplateError* err) {
    register_builtin_filters();

    /* Ensure error is initialised */
    if (err) {
        err->type = TMPL_ERR_NONE;
        err->line = 0;
        err->message[0] = '\0';
    }

    LoopStack loop_stack;
    ConditionalStack cond_stack;
    SetVarStore set_vars;

    if (!loop_stack_init(&loop_stack, 4)) return set_error(err, TMPL_ERR_MEMORY, "malloc failed for loop stack", 1);
    if (!conditional_stack_init(&cond_stack, 4)) {
        free(loop_stack.frames);
        return set_error(err, TMPL_ERR_MEMORY, "malloc failed for conditional stack", 1);
    }
    set_var_store_init(&set_vars);

    bool in_comment = false;
    const char* last_comment = NULL;
    bool in_raw = false;

    const char* p = template;

#define CLEANUP()                                                        \
    do {                                                                 \
        while (loop_stack.depth > 0) loop_stack_pop(&loop_stack);        \
        while (cond_stack.depth > 0) conditional_stack_pop(&cond_stack); \
        free(loop_stack.frames);                                         \
        free(cond_stack.frames);                                         \
    } while (0)

    while (*p) {
        /* ----- HTML comment handling ----- */
        if (!in_comment && !in_raw && *p == '<' && strncmp(p, "<!--", 4) == 0) {
            in_comment = true;
            last_comment = p;
            p += 4;
            continue;
        }
        if (in_comment) {
            if (*p == '-' && strncmp(p, "-->", 3) == 0) {
                in_comment = false;
                p += 3;
            } else
                p++;
            continue;
        }
        if (!in_raw && *p == '-' && strncmp(p, "-->", 3) == 0) {
            CLEANUP();
            return set_error(err, TMPL_ERR_PARSE, "Unmatched comment closing tag '-->'", calc_line(template, p));
        }

        /* ----- raw block ----- */
        if (in_raw) {
            if (*p == '{' && strncmp(p, "{%", 2) == 0) {
                const char* ds = p + 2;
                const char* de = strstr(ds, "%}");
                if (de) {
                    char dir[64];
                    size_t dl = (size_t)(de - ds);
                    if (dl < sizeof(dir)) {
                        memcpy(dir, ds, dl);
                        dir[dl] = '\0';
                        if (strcmp(str_trim(dir), "endraw") == 0) {
                            in_raw = false;
                            p = de + 2;
                            if (*p == '\n') p++; /* consume trailing newline */
                            continue;
                        }
                    }
                }
            }
            if (!buffer_append(out, p, 1)) {
                CLEANUP();
                return set_error(err, TMPL_ERR_MEMORY, "buffer_append failed", calc_line(template, p));
            }
            p++;
            continue;
        }

        /* ----- determine skip state ----- */
        ConditionalFrame* cf = conditional_stack_top(&cond_stack);
        bool should_skip =
            cf && ((cf->condition_met && cf->in_else_branch) || (!cf->condition_met && !cf->in_else_branch));

        /* ----- {{ variable }} ----- */
        if (*p == '{' && *(p + 1) == '{') {
            const char* var_start = p + 2;
            const char* var_end = strstr(var_start, "}}");
            if (!var_end) {
                CLEANUP();
                return set_error(err, TMPL_ERR_PARSE, "Unterminated '{{' tag", calc_line(template, p));
            }

            if (!should_skip) {
                char expr[256];
                size_t vl = (size_t)(var_end - var_start);
                if (vl >= sizeof(expr)) {
                    CLEANUP();
                    return set_error(err, TMPL_ERR_PARSE, "Variable expression too long", calc_line(template, p));
                }
                memcpy(expr, var_start, vl);
                expr[vl] = '\0';
                char* trimmed = str_trim(expr);

                /* Check set_var store first */
                const char* sv = set_var_store_get(&set_vars, trimmed);
                if (sv) {
                    if (!buffer_append_str(out, sv)) {
                        CLEANUP();
                        return set_error(err, TMPL_ERR_MEMORY, "buffer_append failed", calc_line(template, p));
                    }
                } else {
                    if (!render_variable(trimmed, ctx, &loop_stack, out, err, template, p)) {
                        CLEANUP();
                        return false;
                    }
                }
            }
            p = var_end + 2;

            /* ----- {% directive %} ----- */
        } else if (*p == '{' && *(p + 1) == '%') {
            const char* tag_start = p;
            const char* dir_start = p + 2;
            const char* dir_end = strstr(dir_start, "%}");
            if (!dir_end) {
                CLEANUP();
                return set_error(err, TMPL_ERR_PARSE, "Unterminated '{%' tag", calc_line(template, p));
            }

            /* Whitespace control – standalone tag detection */
            bool is_standalone = false;
            const char* line_start = tag_start;
            while (line_start > template && *(line_start - 1) != '\n') line_start--;
            bool only_ws_before = true;
            for (const char* c = line_start; c < tag_start; c++)
                if (!isspace((unsigned char)*c)) {
                    only_ws_before = false;
                    break;
                }
            if (only_ws_before) {
                const char* scan = dir_end + 2;
                bool only_ws_after = true;
                while (*scan && *scan != '\n')
                    if (!isspace((unsigned char)*scan)) {
                        only_ws_after = false;
                        break;
                    } else
                        scan++;
                if (only_ws_after) is_standalone = true;
            }

            if (is_standalone) {
                if (!should_skip) {
                    ssize_t bls = 0;
                    for (ssize_t i = (ssize_t)out->size - 1; i >= 0; i--)
                        if (out->data[i] == '\n') {
                            bls = i + 1;
                            break;
                        }
                    out->size = (size_t)bls;
                    out->data[out->size] = '\0';
                }
                p = dir_end + 2;
                while (*p && *p != '\n') p++;
                if (*p == '\n') p++;
            } else {
                p = dir_end + 2;
            }

            /* Parse directive */
            char directive[256];
            size_t dl = (size_t)(dir_end - dir_start);
            if (dl >= sizeof(directive)) {
                CLEANUP();
                return set_error(err, TMPL_ERR_PARSE, "Directive too long", calc_line(template, tag_start));
            }
            memcpy(directive, dir_start, dl);
            directive[dl] = '\0';
            char* cmd = str_trim(directive);

            /* ---- raw ---- */
            if (strcmp(cmd, "raw") == 0) {
                in_raw = true;

                /* ---- for ---- */
            } else if (strncmp(cmd, "for ", 4) == 0) {
                if (!should_skip) {
                    char* sp2;
                    char* parts[4] = {0};
                    char* tok = strtok_r(cmd, " ", &sp2);
                    for (int i = 0; tok && i < 4; i++) {
                        parts[i] = tok;
                        tok = strtok_r(NULL, " ", &sp2);
                    }
                    if (!parts[0] || !parts[1] || !parts[2] || !parts[3] || strcmp(parts[0], "for") != 0 ||
                        strcmp(parts[2], "in") != 0) {
                        CLEANUP();
                        return set_error(err, TMPL_ERR_SYNTAX, "Invalid 'for' loop. Use: {% for item in items %}",
                                         calc_line(template, tag_start));
                    }
                    const TemplateValue* arr = context_get(ctx, parts[3]);
                    if (!arr || arr->type != TMPL_ARRAY) {
                        CLEANUP();
                        return set_error(err, TMPL_ERR_RENDER, "Variable for loop is not a valid array",
                                         calc_line(template, tag_start));
                    }
                    if (!loop_stack_push(&loop_stack, arr, parts[1], p)) {
                        CLEANUP();
                        return set_error(err, TMPL_ERR_MEMORY, "loop_stack_push failed",
                                         calc_line(template, tag_start));
                    }
                    if (arr->value.array.count == 0) {
                        p = find_matching_endfor(p);
                        loop_stack_pop(&loop_stack);
                    }
                }

                /* ---- endfor ---- */
            } else if (strcmp(cmd, "endfor") == 0) {
                if (!should_skip) {
                    LoopFrame* lf = loop_stack_top(&loop_stack);
                    if (!lf) {
                        CLEANUP();
                        return set_error(err, TMPL_ERR_SYNTAX, "Found 'endfor' with no matching 'for'",
                                         calc_line(template, tag_start));
                    }
                    lf->index++;
                    if (lf->index < lf->array->value.array.count) {
                        p = lf->loop_start;
                    } else {
                        loop_stack_pop(&loop_stack);
                    }
                }

                /* ---- if ---- */
            } else if (strncmp(cmd, "if ", 3) == 0) {
                const char* cond = cmd + 3;
                bool result = should_skip ? false : evaluate_condition(cond, ctx, &loop_stack, err, template, p);
                if (!should_skip && err->type != TMPL_ERR_NONE) {
                    CLEANUP();
                    return false;
                }

                if (!conditional_stack_push(&cond_stack, result, p)) {
                    CLEANUP();
                    return set_error(err, TMPL_ERR_MEMORY, "conditional_stack_push failed", calc_line(template, p));
                }

                if (!result) {
                    const char* branch = NULL;
                    IfBranchType bt = find_next_if_branch(p, &branch);
                    if (bt == IF_BRANCH_ELIF && branch) {
                        p = branch;
                    } else if (bt == IF_BRANCH_ELSE && branch) {
                        p = branch;
                    } else if (bt == IF_BRANCH_ENDIF && branch) {
                        const char* de = strstr(branch + 2, "%}");
                        p = de ? de + 2 : branch;
                        conditional_stack_pop(&cond_stack);
                    }
                }

                /* ---- elif ---- */
            } else if (strncmp(cmd, "elif ", 5) == 0) {
                ConditionalFrame* top = conditional_stack_top(&cond_stack);
                if (!top) {
                    CLEANUP();
                    return set_error(err, TMPL_ERR_SYNTAX, "Found 'elif' with no matching 'if'",
                                     calc_line(template, tag_start));
                }

                if (top->done) {
                    p = find_matching_endif(p);
                    conditional_stack_pop(&cond_stack);
                } else {
                    /* Evaluate the elif condition */
                    const char* cond = cmd + 5;
                    bool result = evaluate_condition(cond, ctx, &loop_stack, err, template, p);
                    if (err->type != TMPL_ERR_NONE) {
                        CLEANUP();
                        return false;
                    }

                    top->condition_met = result;
                    top->done = result;
                    top->in_else_branch = false;

                    if (!result) {
                        const char* branch = NULL;
                        IfBranchType bt = find_next_if_branch(p, &branch);
                        if ((bt == IF_BRANCH_ELIF || bt == IF_BRANCH_ELSE) && branch) {
                            p = branch;
                        } else if (bt == IF_BRANCH_ENDIF && branch) {
                            const char* de = strstr(branch + 2, "%}");
                            p = de ? de + 2 : branch;
                            conditional_stack_pop(&cond_stack);
                        }
                    }
                }

                /* ---- else ---- */
            } else if (strcmp(cmd, "else") == 0) {
                ConditionalFrame* top = conditional_stack_top(&cond_stack);
                if (!top) {
                    CLEANUP();
                    return set_error(err, TMPL_ERR_SYNTAX, "Found 'else' with no matching 'if'",
                                     calc_line(template, tag_start));
                }
                top->in_else_branch = true;
                if (top->done) {
                    p = find_matching_endif(p);
                    conditional_stack_pop(&cond_stack);
                    while (*p == '\n') p++;
                }

                /* ---- endif ---- */
            } else if (strcmp(cmd, "endif") == 0) {
                ConditionalFrame* top = conditional_stack_top(&cond_stack);
                if (!top) {
                    CLEANUP();
                    return set_error(err, TMPL_ERR_SYNTAX, "Found 'endif' with no matching 'if'",
                                     calc_line(template, tag_start));
                }
                conditional_stack_pop(&cond_stack);

                /* ---- set ---- */
            } else if (strncmp(cmd, "set ", 4) == 0) {
                if (!should_skip) {
                    /* Format: set varname = value */
                    char* rest = cmd + 4;
                    while (isspace((unsigned char)*rest)) rest++;
                    char* eq = strchr(rest, '=');
                    if (!eq) {
                        CLEANUP();
                        return set_error(err, TMPL_ERR_SYNTAX, "{% set %} requires '=': {% set x = value %}",
                                         calc_line(template, tag_start));
                    }
                    *eq = '\0';
                    char* varname = str_trim(rest);
                    char* value = str_trim(eq + 1);
                    /* strip surrounding quotes if present */
                    if ((*value == '"' || *value == '\'') && value[strlen(value) - 1] == *value) {
                        value++;
                        value[strlen(value) - 1] = '\0';
                    }
                    if (!set_var_store_set(&set_vars, varname, value)) {
                        CLEANUP();
                        return set_error(err, TMPL_ERR_MEMORY, "set variable store full",
                                         calc_line(template, tag_start));
                    }
                }

                /* ---- unknown directive ---- */
            } else {
                char msg[128];
                snprintf(msg, sizeof(msg), "Unknown directive: '%s'", cmd);
                CLEANUP();
                return set_error(err, TMPL_ERR_SYNTAX, msg, calc_line(template, tag_start));
            }

        } else {
            /* ----- plain text ----- */
            if (!should_skip) {
                if (!buffer_append(out, p, 1)) {
                    CLEANUP();
                    return set_error(err, TMPL_ERR_MEMORY, "buffer_append failed", calc_line(template, p));
                }
            }
            p++;
        }
    } /* while *p */

    /* Post-render validation */
    if (in_raw) {
        CLEANUP();
        return set_error(err, TMPL_ERR_SYNTAX, "Unclosed {% raw %} block", calc_line(template, p));
    }
    if (in_comment && last_comment) {
        CLEANUP();
        return set_error(err, TMPL_ERR_SYNTAX, "Unterminated HTML comment '<!--'", calc_line(template, last_comment));
    }
    if (loop_stack_top(&loop_stack)) {
        CLEANUP();
        return set_error(err, TMPL_ERR_SYNTAX, "Unclosed 'for' loop at end of template", calc_line(template, p));
    }
    if (conditional_stack_top(&cond_stack)) {
        CLEANUP();
        return set_error(err, TMPL_ERR_SYNTAX, "Unclosed 'if' statement at end of template", calc_line(template, p));
    }

    CLEANUP();
    return true;
}

/* ================================================================
   render_template_file
   ================================================================ */

bool render_template_file(const char* path, const TemplateContext* ctx, OutputBuffer* out, TemplateError* err) {
    FILE* fp = fopen(path, "r");
    if (!fp) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Cannot open template file '%s': %s", path, strerror(errno));
        return set_error(err, TMPL_ERR_IO, msg, 0);
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fsize <= 0) {
        fclose(fp);
        return set_error(err, TMPL_ERR_IO, "Empty template file", 0);
    }

    char* buf = malloc((size_t)fsize + 1);
    if (!buf) {
        fclose(fp);
        return set_error(err, TMPL_ERR_MEMORY, "malloc failed reading template file", 0);
    }

    size_t n = fread(buf, 1, (size_t)fsize, fp);
    buf[n] = '\0';
    fclose(fp);

    bool ok = render_template(buf, ctx, out, err);
    free(buf);
    return ok;
}
