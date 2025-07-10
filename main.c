#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// On Windows, use _strdup and strtok_s
#if defined(_MSC_VER)
#define strdup _strdup
#define strtok_r strtok_s
#define WARN_UNUSED _Check_return_
#else
#define WARN_UNUSED __attribute__((warn_unused_result()))
#endif

typedef enum {
    TMPL_STRING,
    TMPL_INT,
    TMPL_FLOAT,
    TMPL_DOUBLE,
    TMPL_BOOL,
    TMPL_LONG,
    TMPL_UINT,
    TMPL_ARRAY
} ValueType;

typedef struct {
    ValueType type;
    union {
        const char* str;
        int integer;
        float floating;
        double dbl;
        bool boolean;
        long long_int;
        unsigned int uint;
        struct {
            const void* items;
            size_t count;
            ValueType item_type;
        } array;
    } value;
} TemplateValue;

typedef struct {
    const char* key;
    TemplateValue value;
} TemplateVar;

typedef struct {
    TemplateVar* vars;
    size_t count;
} TemplateContext;

typedef struct {
    char* data;
    size_t size;
    size_t capacity;
} OutputBuffer;

typedef enum {
    TMPL_ERR_NONE,
    TMPL_ERR_PARSE,   // Malformed tags, e.g., {{ var
    TMPL_ERR_SYNTAX,  // Incorrect directive syntax, e.g., {% for x from y %}
    TMPL_ERR_RENDER,  // Variable not found, wrong type, etc.
    TMPL_ERR_MEMORY   // malloc/realloc failure
} TemplateErrorType;

typedef struct {
    TemplateErrorType type;
    char message[256];
    size_t line;
} TemplateError;

// --- Loop Stack Frame---
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

// --- Conditional Stack Frame ---
typedef struct {
    bool condition_met;
    bool in_else_branch;
    const char* if_start;
} ConditionalFrame;

typedef struct {
    ConditionalFrame* frames;
    size_t depth;
    size_t capacity;
} ConditionalStack;

// --- Forward Declarations of Helper Functions ---
WARN_UNUSED static bool buffer_init(OutputBuffer* buf, size_t initial_capacity);
WARN_UNUSED static bool buffer_append(OutputBuffer* buf, const char* str, size_t len);
static void loop_stack_init(LoopStack* stack, size_t capacity);
static void loop_stack_push(LoopStack* stack, const TemplateValue* array, const char* item_name,
                            const char* loop_start);
static void loop_stack_pop(LoopStack* stack);
static LoopFrame* loop_stack_top(LoopStack* stack);
static void conditional_stack_init(ConditionalStack* stack, size_t capacity);
static void conditional_stack_push(ConditionalStack* stack, bool condition, const char* if_start);
static void conditional_stack_pop(ConditionalStack* stack);
static ConditionalFrame* conditional_stack_top(ConditionalStack* stack);
static const TemplateValue* context_get(const TemplateContext* ctx, const char* key);

WARN_UNUSED static bool value_to_string(const TemplateValue* val, OutputBuffer* buf);
WARN_UNUSED static bool evaluate_condition(const char* condition, const TemplateContext* ctx,
                                           const LoopStack* loop_stack, TemplateError* err,
                                           const char* template_start, const char* error_pos);

static bool set_error(TemplateError* err, TemplateErrorType type, const char* message, size_t line);
static bool is_truthy(const TemplateValue* val);

/**
 * Calculates the line number in the template source based on a pointer's position.
 */
static inline size_t calculate_line_number(const char* template_start, const char* error_pos) {
    size_t line = 1;
    for (const char* c = template_start; c && c < error_pos; c++) {
        if (*c == '\n') {
            line++;
        }
    }
    return line;
}

bool render_template(const char* template, const TemplateContext* ctx, OutputBuffer* out,
                     TemplateError* err) {
    LoopStack loop_stack;
    ConditionalStack conditional_stack;
    loop_stack_init(&loop_stack, 4);
    conditional_stack_init(&conditional_stack, 4);

    const char* p = template;
    while (*p) {
        // Check if we're in a false conditional branch
        ConditionalFrame* current_conditional = conditional_stack_top(&conditional_stack);
        bool should_skip = current_conditional &&
                           ((current_conditional->condition_met && current_conditional->in_else_branch) ||
                            (!current_conditional->condition_met && !current_conditional->in_else_branch));

        if (*p == '{' && *(p + 1) == '{') {
            // --- Handle Variable Substitution {{ var }} ---
            const char* var_start = p + 2;
            const char* var_end = strstr(var_start, "}}");
            if (!var_end) {
                // Calculate line number on-demand.
                return set_error(err, TMPL_ERR_PARSE, "Unterminated '{{' tag",
                                 calculate_line_number(template, p));
            }

            if (!should_skip) {
                char var_name[128];
                size_t var_len = var_end - var_start;
                if (var_len >= sizeof(var_name)) {
                    return set_error(err, TMPL_ERR_PARSE, "Variable name is too long",
                                     calculate_line_number(template, p));
                }
                memcpy(var_name, var_start, var_len);
                var_name[var_len] = '\0';

                char* name = var_name;
                while (isspace((unsigned char)*name))
                    name++;
                char* end = name + strlen(name) - 1;
                while (end > name && isspace((unsigned char)*end))
                    end--;
                *(end + 1) = '\0';

                bool rendered = false;
                LoopFrame* current_loop = loop_stack_top(&loop_stack);
                if (current_loop && strcmp(name, current_loop->item_name) == 0) {
                    TemplateValue item = {.type = current_loop->array->value.array.item_type};
                    switch (item.type) {
                        case TMPL_STRING:
                            item.value.str =
                                ((const char**)current_loop->array->value.array.items)[current_loop->index];
                            break;
                        case TMPL_INT:
                            item.value.integer =
                                *(((int**)current_loop->array->value.array.items)[current_loop->index]);
                            break;
                        case TMPL_FLOAT:
                            item.value.floating =
                                *(((float**)current_loop->array->value.array.items)[current_loop->index]);
                            break;
                        case TMPL_DOUBLE:
                            item.value.dbl =
                                *(((double**)current_loop->array->value.array.items)[current_loop->index]);
                            break;
                        case TMPL_BOOL:
                            item.value.boolean =
                                *(((bool**)current_loop->array->value.array.items)[current_loop->index]);
                            break;
                        case TMPL_LONG:
                            item.value.long_int =
                                *(((long**)current_loop->array->value.array.items)[current_loop->index]);
                            break;
                        case TMPL_UINT:
                            item.value.uint = *((
                                (unsigned int**)current_loop->array->value.array.items)[current_loop->index]);
                            break;
                        default:
                            break;
                    }
                    if (!value_to_string(&item, out)) {
                        // buffer_append failed: realloc
                        return set_error(err, TMPL_ERR_MEMORY, "value_to_string() failed: realloc failed",
                                         calculate_line_number(template, p));
                    }
                    rendered = true;
                }

                if (!rendered) {
                    const TemplateValue* val = context_get(ctx, name);
                    if (!val) {
                        char msg[128];
                        snprintf(msg, sizeof(msg), "Missing template variable for '%s'", name);
                        return set_error(err, TMPL_ERR_PARSE, msg, calculate_line_number(template, p));
                    }

                    if (!value_to_string(val, out)) {
                        // buffer_append failed: realloc
                        return set_error(err, TMPL_ERR_MEMORY, "value_to_string() failed: realloc failed",
                                         calculate_line_number(template, p));
                    }
                }
            }
            p = var_end + 2;

        } else if (*p == '{' && *(p + 1) == '%') {
            // --- Handle Directives {% ... %} ---
            const char* dir_start = p + 2;
            const char* dir_end = strstr(dir_start, "%}");
            if (!dir_end) {
                return set_error(err, TMPL_ERR_PARSE, "Unterminated '{%' tag",
                                 calculate_line_number(template, p));
            }

            char directive[128];
            size_t dir_len = dir_end - dir_start;
            if (dir_len >= sizeof(directive)) {
                return set_error(err, TMPL_ERR_PARSE, "Directive is too long",
                                 calculate_line_number(template, p));
            }
            memcpy(directive, dir_start, dir_len);
            directive[dir_len] = '\0';

            char* cmd = directive;
            while (isspace((unsigned char)*cmd))
                cmd++;
            char* end = cmd + strlen(cmd) - 1;
            while (end > cmd && isspace((unsigned char)*end))
                end--;
            *(end + 1) = '\0';

            const char* tag_start_pos = p;  // Save position for error reporting
            p = dir_end + 2;

            if (strncmp(cmd, "for ", 4) == 0) {
                if (!should_skip) {
                    // ... (Parsing logic for 'for' loop remains the same)
                    char* saveptr;
                    char* parts[4] = {0};
                    char* token = strtok_r(cmd, " ", &saveptr);
                    for (int i = 0; token && i < 4; i++) {
                        parts[i] = token;
                        token = strtok_r(NULL, " ", &saveptr);
                    }

                    if (!parts[0] || !parts[1] || !parts[2] || !parts[3] || strcmp(parts[0], "for") != 0 ||
                        strcmp(parts[2], "in") != 0) {
                        return set_error(err, TMPL_ERR_SYNTAX,
                                         "Invalid 'for' loop syntax. Use: {% for item in items %}",
                                         calculate_line_number(template, tag_start_pos));
                    }

                    const TemplateValue* arr = context_get(ctx, parts[3]);
                    if (!arr || arr->type != TMPL_ARRAY) {
                        return set_error(err, TMPL_ERR_RENDER, "Variable for loop is not a valid array",
                                         calculate_line_number(template, tag_start_pos));
                    }

                    loop_stack_push(&loop_stack, arr, parts[1], p);
                    if (arr->value.array.count == 0) {
                        const char* endfor = strstr(p, "{% endfor %}");
                        if (endfor)
                            p = endfor + strlen("{% endfor %}");
                        loop_stack_pop(&loop_stack);
                    }
                }
            } else if (strcmp(cmd, "endfor") == 0) {
                if (!should_skip) {
                    LoopFrame* current_loop = loop_stack_top(&loop_stack);
                    if (!current_loop) {
                        return set_error(err, TMPL_ERR_SYNTAX, "Found 'endfor' with no matching 'for'",
                                         calculate_line_number(template, tag_start_pos));
                    }

                    current_loop->index++;
                    if (current_loop->index < current_loop->array->value.array.count) {
                        p = current_loop->loop_start;
                    } else {
                        loop_stack_pop(&loop_stack);
                    }
                }
            } else if (strncmp(cmd, "if ", 3) == 0) {
                // Handle {% if condition %}
                const char* condition = cmd + 3;
                bool result = evaluate_condition(condition, ctx, &loop_stack, err, template, p);

                // first check for errors
                if (err->type != TMPL_ERR_NONE) {
                    return false;  // error in evaluating conditional
                }

                conditional_stack_push(&conditional_stack, result, p);

                if (!result) {
                    // Skip to else or endif
                    const char* else_pos = strstr(p, "{% else %}");
                    const char* endif_pos = strstr(p, "{% endif %}");

                    if (else_pos && (!endif_pos || else_pos < endif_pos)) {
                        p = else_pos + strlen("{% else %}");
                        conditional_stack_top(&conditional_stack)->in_else_branch = true;
                    } else if (endif_pos) {
                        p = endif_pos + strlen("{% endif %}");
                        conditional_stack_pop(&conditional_stack);
                    }
                }
            } else if (strcmp(cmd, "else") == 0) {
                // Handle {% else %}
                ConditionalFrame* current_cond = conditional_stack_top(&conditional_stack);
                if (!current_cond) {
                    return set_error(err, TMPL_ERR_SYNTAX, "Found 'else' with no matching 'if'",
                                     calculate_line_number(template, tag_start_pos));
                }

                current_cond->in_else_branch = true;
                if (current_cond->condition_met) {
                    // Skip to endif
                    const char* endif_pos = strstr(p, "{% endif %}");
                    if (endif_pos) {
                        p = endif_pos + strlen("{% endif %}");
                        conditional_stack_pop(&conditional_stack);
                    }
                }
            } else if (strcmp(cmd, "endif") == 0) {
                // Handle {% endif %}
                ConditionalFrame* current_cond = conditional_stack_top(&conditional_stack);
                if (!current_cond) {
                    return set_error(err, TMPL_ERR_SYNTAX, "Found 'endif' with no matching 'if'",
                                     calculate_line_number(template, tag_start_pos));
                }
                conditional_stack_pop(&conditional_stack);
            }

        } else {
            if (!should_skip) {
                if (!buffer_append(out, p, 1)) {
                    return set_error(err, TMPL_ERR_MEMORY, "buffer_append(): realloc of buffer failed",
                                     calculate_line_number(template, p));
                }
            }
            p++;
        }
    }

    if (loop_stack_top(&loop_stack) != NULL) {
        return set_error(err, TMPL_ERR_SYNTAX, "Unclosed 'for' loop at end of template",
                         calculate_line_number(template, p));
    }

    if (conditional_stack_top(&conditional_stack) != NULL) {
        return set_error(err, TMPL_ERR_SYNTAX, "Unclosed 'if' statement at end of template",
                         calculate_line_number(template, p));
    }

    // --- Cleanup ---
    while (loop_stack.depth > 0) {
        loop_stack_pop(&loop_stack);
    }
    free(loop_stack.frames);

    while (conditional_stack.depth > 0) {
        conditional_stack_pop(&conditional_stack);
    }
    free(conditional_stack.frames);

    return true;
}

// --- Helper Function Implementations ---

bool set_error(TemplateError* err, TemplateErrorType type, const char* message, size_t line) {
    if (err) {
        err->type = type;
        err->line = line;
        snprintf(err->message, sizeof(err->message) - 1, "%s", message);
        err->message[sizeof(err->message) - 1] = '\0';
    }
    return false;  // Return false for convenient one-line error handling
}

bool buffer_init(OutputBuffer* buf, size_t initial_capacity) {
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

bool buffer_append(OutputBuffer* buf, const char* str, size_t len) {
    if (!buf || !str || len == 0)
        return true;

    if (buf->size + len + 1 > buf->capacity) {
        size_t new_capacity = buf->capacity * 2;
        while (buf->size + len + 1 > new_capacity) {
            new_capacity *= 2;
        }
        buf->data = realloc(buf->data, new_capacity);
        if (!buf->data) {
            perror("realloc");
            return false;
        }
        buf->capacity = new_capacity;
    }
    memcpy(buf->data + buf->size, str, len);
    buf->size += len;
    buf->data[buf->size] = '\0';
    return true;
}

void loop_stack_init(LoopStack* stack, size_t capacity) {
    stack->frames = malloc(sizeof(LoopFrame) * capacity);
    if (!stack->frames) {
        perror("malloc");
        exit(1);
    }
    stack->depth = 0;
    stack->capacity = capacity;
}

void loop_stack_push(LoopStack* stack, const TemplateValue* array, const char* item_name,
                     const char* loop_start) {
    if (stack->depth >= stack->capacity) {
        stack->capacity *= 2;
        stack->frames = realloc(stack->frames, sizeof(LoopFrame) * stack->capacity);
        if (!stack->frames) {
            perror("realloc");
            exit(1);
        }
    }
    stack->frames[stack->depth++] =
        (LoopFrame){.array = array, .index = 0, .item_name = strdup(item_name), .loop_start = loop_start};
}

void loop_stack_pop(LoopStack* stack) {
    if (stack->depth > 0) {
        stack->depth--;
        free(stack->frames[stack->depth].item_name);
    }
}

LoopFrame* loop_stack_top(LoopStack* stack) {
    return stack->depth > 0 ? &stack->frames[stack->depth - 1] : NULL;
}

void conditional_stack_init(ConditionalStack* stack, size_t capacity) {
    stack->frames = malloc(sizeof(ConditionalFrame) * capacity);
    if (!stack->frames) {
        perror("malloc");
        exit(1);
    }
    stack->depth = 0;
    stack->capacity = capacity;
}

void conditional_stack_push(ConditionalStack* stack, bool condition, const char* if_start) {
    if (stack->depth >= stack->capacity) {
        stack->capacity *= 2;
        stack->frames = realloc(stack->frames, sizeof(ConditionalFrame) * stack->capacity);
        if (!stack->frames) {
            perror("realloc");
            exit(1);
        }
    }
    stack->frames[stack->depth++] =
        (ConditionalFrame){.condition_met = condition, .in_else_branch = false, .if_start = if_start};
}

void conditional_stack_pop(ConditionalStack* stack) {
    if (stack->depth > 0) {
        stack->depth--;
    }
}

ConditionalFrame* conditional_stack_top(ConditionalStack* stack) {
    return stack->depth > 0 ? &stack->frames[stack->depth - 1] : NULL;
}

const TemplateValue* context_get(const TemplateContext* ctx, const char* key) {
    for (size_t i = 0; i < ctx->count; i++) {
        if (strcmp(ctx->vars[i].key, key) == 0) {
            return &ctx->vars[i].value;
        }
    }
    return NULL;
}

bool is_truthy(const TemplateValue* val) {
    if (!val)
        return false;

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
            return val->value.str != NULL && strlen(val->value.str) > 0;
        case TMPL_ARRAY:
            return val->value.array.count > 0;
        default:
            return false;
    }
}

bool evaluate_condition(const char* condition, const TemplateContext* ctx, const LoopStack* loop_stack,
                        TemplateError* err, const char* template_start, const char* error_pos) {
    char condition_copy[128];
    strncpy(condition_copy, condition, sizeof(condition_copy) - 1);
    condition_copy[sizeof(condition_copy) - 1] = '\0';

    // Trim whitespace
    char* trimmed = condition_copy;
    while (isspace((unsigned char)*trimmed))
        trimmed++;
    char* end = trimmed + strlen(trimmed) - 1;
    while (end > trimmed && isspace((unsigned char)*end))
        end--;
    *(end + 1) = '\0';

    // Check if it's a loop variable
    LoopFrame* current_loop = (LoopFrame*)loop_stack_top((LoopStack*)loop_stack);
    if (current_loop && strcmp(trimmed, current_loop->item_name) == 0) {
        TemplateValue item = {.type = current_loop->array->value.array.item_type};
        switch (item.type) {
            case TMPL_STRING:
                item.value.str = ((const char**)current_loop->array->value.array.items)[current_loop->index];
                break;
            case TMPL_INT:
                item.value.integer = *(((int**)current_loop->array->value.array.items)[current_loop->index]);
                break;
            case TMPL_FLOAT:
                item.value.floating =
                    *(((float**)current_loop->array->value.array.items)[current_loop->index]);
                break;
            case TMPL_DOUBLE:
                item.value.dbl = *(((double**)current_loop->array->value.array.items)[current_loop->index]);
                break;
            case TMPL_BOOL:
                item.value.boolean = *(((bool**)current_loop->array->value.array.items)[current_loop->index]);
                break;
            case TMPL_LONG:
                item.value.long_int =
                    *(((long**)current_loop->array->value.array.items)[current_loop->index]);
                break;
            case TMPL_UINT:
                item.value.uint =
                    *(((unsigned int**)current_loop->array->value.array.items)[current_loop->index]);
                break;
            default:
                return false;
        }
        return is_truthy(&item);
    }

    // Check context variables
    const TemplateValue* val = context_get(ctx, trimmed);
    if (!val) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Missing template variable for '%s'", trimmed);
        return set_error(err, TMPL_ERR_PARSE, msg, calculate_line_number(template_start, error_pos));
    }

    return is_truthy(val);
}

bool value_to_string(const TemplateValue* val, OutputBuffer* buf) {
    char temp[128];
    switch (val->type) {
        case TMPL_STRING:
            return buffer_append(buf, val->value.str ? val->value.str : "",
                                 val->value.str ? strlen(val->value.str) : 0);
        case TMPL_INT:
            snprintf(temp, sizeof(temp), "%d", val->value.integer);
            return buffer_append(buf, temp, strlen(temp));
        case TMPL_FLOAT:
            snprintf(temp, sizeof(temp), "%.4f", val->value.floating);
            return buffer_append(buf, temp, strlen(temp));
        case TMPL_DOUBLE:
            snprintf(temp, sizeof(temp), "%.4f", val->value.dbl);
            return buffer_append(buf, temp, strlen(temp));
        case TMPL_BOOL:
            return buffer_append(buf, val->value.boolean ? "true" : "false", val->value.boolean ? 4 : 5);
        case TMPL_LONG:
            snprintf(temp, sizeof(temp), "%ld", val->value.long_int);
            return buffer_append(buf, temp, strlen(temp));
        case TMPL_UINT:
            snprintf(temp, sizeof(temp), "%u", val->value.uint);
            return buffer_append(buf, temp, strlen(temp));
        case TMPL_ARRAY:
            snprintf(temp, sizeof(temp), "[array of size %zu]", val->value.array.count);
            return buffer_append(buf, temp, strlen(temp));

        default:
            abort();
    }
}

// clang-format off
#define VAR_STRING(k, v)   {k, {TMPL_STRING, .value = {.str = v}}}
#define VAR_INT(k, v)      {k, {TMPL_INT,    .value = {.integer = v}}}
#define VAR_FLOAT(k, v)    {k, {TMPL_FLOAT,  .value = {.floating = v}}}
#define VAR_DOUBLE(k, v)   {k, {TMPL_DOUBLE, .value = {.dbl = v}}}
#define VAR_BOOL(k, v)     {k, {TMPL_BOOL,   .value = {.boolean = v}}}
#define VAR_LONG(k, v)     {k, {TMPL_LONG,   .value = {.long_int = v}}}
#define VAR_UINT(k, v)     {k, {TMPL_UINT,   .value = {.uint = v}}}
// clang-format on

#define VAR_ARRAY(key, ptr_array, item_type_enum)                                                            \
    {                                                                                                        \
        key, {                                                                                               \
            TMPL_ARRAY, .value.array = {                                                                     \
                .items = (ptr_array),                                                                        \
                .count = sizeof(ptr_array) / sizeof((ptr_array)[0]),                                         \
                .item_type = (item_type_enum)                                                                \
            }                                                                                                \
        }                                                                                                    \
    }

#define VAR_ARRAY_STR(key, str_array) VAR_ARRAY(key, str_array, TMPL_STRING)

#define MAKE_PTR_ARRAY(arr, ptrs, name)                                                                      \
    /* declare ptrs[] of pointer-to-element, same length as arr */                                           \
    typeof((arr)[0])*(name)[sizeof(arr) / sizeof((arr)[0])];                                                 \
    /* populate ptrs[i] = &arr[i] */                                                                         \
    for (size_t __i = 0; __i < sizeof(arr) / sizeof((arr)[0]); ++__i)                                        \
        (name)[__i] = &(arr)[__i];

int main() {
    const char* fruits[] = {"Apple", "Banana", "Cherry"};

    int numbers_data[] = {1, 2, 3, 4, 5};
    MAKE_PTR_ARRAY(numbers_data, number_ptrs, number_ptrs);

    TemplateVar vars[] = {
        VAR_STRING("user", "Dr. Nathan"), VAR_BOOL("is_admin", true),
        VAR_BOOL("is_guest", false),      VAR_INT("user_id", 42),
        VAR_DOUBLE("score", 95.5),        VAR_LONG("big_num", 9876543210L),
        VAR_FLOAT("temperature", 36.6f),  VAR_UINT("visits", 4294967295U),
        VAR_ARRAY_STR("fruits", fruits),  VAR_ARRAY("numbers", number_ptrs, TMPL_INT),
    };

    TemplateContext ctx = {.vars = vars, .count = sizeof(vars) / sizeof(vars[0])};

    // --- Template String with Conditionals ---
    const char* template =
        "Hello {{ user }}!\n"
        "{% if is_admin %}"
        "You are an administrator with ID: {{ user_id }}\n"
        "{% else %}"
        "You are a regular user.\n"
        "{% endif %}"
        "\n"
        "{% if is_guest %}"
        "Welcome, guest!\n"
        "{% else %}"
        "Welcome back!\n"
        "{% endif %}"
        "\n"
        "Score: {{ score }}\n"
        "{% if score %}"
        "You have a score!\n"
        "{% endif %}"
        "\n"
        "Fruits:\n"
        "{% for fruit in fruits %}"
        "  - {{ fruit }}"
        "{% if fruit %} (available){% endif %}\n"
        "{% endfor %}"
        "\n"
        "Numbers: {% for num in numbers %}{{ num }} {% endfor %}\n{{ unknown_variable }}";

    // --- Render ---
    OutputBuffer out;
    TemplateError err = {0};
    if (!buffer_init(&out, 1024)) {
        exit(1);
    };

    if (render_template(template, &ctx, &out, &err)) {
        printf("--- Render Successful ---\n%s", out.data);
    } else {
        fprintf(stderr, "\n--- Render Failed ---\n");
        fprintf(stderr, "Error on line %zu: %s\n", err.line, err.message);
    }

    free(out.data);
    return 0;
}
