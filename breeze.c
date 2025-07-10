#include <ctype.h>
#include <string.h>

#include "breeze.h"

/* ==================== Loop Stack Implementation ==================== */

/**
 * @brief Struct representing a loop frame (for loop state)
 */
typedef struct {
    const TemplateValue* array;
    size_t index;
    char* item_name;
    const char* loop_start;
} LoopFrame;

/**
 * @brief Struct representing a stack of loop frames
 */
typedef struct {
    LoopFrame* frames;
    size_t depth;
    size_t capacity;
} LoopStack;

/**
 * @brief Initialize a loop stack
 * @param stack Pointer to the stack to initialize
 * @param capacity Initial capacity of the stack
 * @return true on success, false on memory allocation failure
 */
ALWAYS_INLINE
WARN_UNUSED static inline bool loop_stack_init(LoopStack* stack, size_t capacity) {
    stack->frames = malloc(sizeof(LoopFrame) * capacity);
    if (!stack->frames) {
        perror("malloc");
        return false;
    }
    stack->depth = 0;
    stack->capacity = capacity;
    return true;
}

/**
 * @brief Push a new loop frame onto the stack
 * @param stack Pointer to the stack
 * @param array The array being iterated over
 * @param item_name Name of the loop variable
 * @param loop_start Pointer to start of loop body in template
 * @return true on success, false on memory allocation failure
 */
ALWAYS_INLINE
WARN_UNUSED static inline bool loop_stack_push(LoopStack* stack, const TemplateValue* array,
                                               const char* item_name, const char* loop_start) {
    if (stack->depth >= stack->capacity) {
        stack->capacity *= 2;
        stack->frames = realloc(stack->frames, sizeof(LoopFrame) * stack->capacity);
        if (!stack->frames) {
            perror("realloc");
            return false;
        }
    }
    stack->frames[stack->depth++] =
        (LoopFrame){.array = array, .index = 0, .item_name = strdup(item_name), .loop_start = loop_start};
    return true;
}

/**
 * @brief Pop the top frame from the loop stack
 * @param stack Pointer to the stack
 */
ALWAYS_INLINE
static inline void loop_stack_pop(LoopStack* stack) {
    if (stack->depth > 0) {
        stack->depth--;
        free(stack->frames[stack->depth].item_name);
    }
}

/**
 * @brief Get the top frame from the loop stack
 * @param stack Pointer to the stack
 * @return Pointer to top frame, or NULL if stack is empty
 */
ALWAYS_INLINE
static inline LoopFrame* loop_stack_top(LoopStack* stack) {
    return stack->depth > 0 ? &stack->frames[stack->depth - 1] : NULL;
}

/* ==================== Conditional Stack Implementation ==================== */

/**
 * @brief Struct representing a conditional frame (if/else state)
 */
typedef struct {
    bool condition_met;
    bool in_else_branch;
    const char* if_start;
} ConditionalFrame;

/**
 * @brief Struct representing a stack of conditional frames
 */
typedef struct {
    ConditionalFrame* frames;
    size_t depth;
    size_t capacity;
} ConditionalStack;

/**
 * @brief Initialize a conditional stack
 * @param stack Pointer to the stack to initialize
 * @param capacity Initial capacity of the stack
 * @return true on success, false on memory allocation failure
 */
WARN_UNUSED static bool conditional_stack_init(ConditionalStack* stack, size_t capacity) {
    stack->frames = malloc(sizeof(ConditionalFrame) * capacity);
    if (!stack->frames) {
        perror("malloc");
        return false;
    }
    stack->depth = 0;
    stack->capacity = capacity;
    return true;
}

/**
 * @brief Push a new conditional frame onto the stack
 * @param stack Pointer to the stack
 * @param condition Result of the if condition
 * @param if_start Pointer to start of if block in template
 * @return true on success, false on memory allocation failure
 */
WARN_UNUSED static bool conditional_stack_push(ConditionalStack* stack, bool condition,
                                               const char* if_start) {
    if (stack->depth >= stack->capacity) {
        stack->capacity *= 2;
        stack->frames = realloc(stack->frames, sizeof(ConditionalFrame) * stack->capacity);
        if (!stack->frames) {
            perror("realloc");
            return false;
        }
    }
    stack->frames[stack->depth++] =
        (ConditionalFrame){.condition_met = condition, .in_else_branch = false, .if_start = if_start};
    return true;
}

/**
 * @brief Pop the top frame from the conditional stack
 * @param stack Pointer to the stack
 */
static void conditional_stack_pop(ConditionalStack* stack) {
    if (stack->depth > 0) {
        stack->depth--;
    }
}

/**
 * @brief Get the top frame from the conditional stack
 * @param stack Pointer to the stack
 * @return Pointer to top frame, or NULL if stack is empty
 */
static ConditionalFrame* conditional_stack_top(ConditionalStack* stack) {
    return stack->depth > 0 ? &stack->frames[stack->depth - 1] : NULL;
}

/* ==================== Output Buffer Implementation ==================== */

/**
 * @brief Initialize an output buffer
 * @param buf Pointer to the buffer to initialize
 * @param initial_capacity Initial capacity of the buffer
 * @return true on success, false on memory allocation failure
 */
WARN_UNUSED bool buffer_init(OutputBuffer* buf, size_t initial_capacity) {
    if (initial_capacity == 0) {
        initial_capacity = 1024;
    }

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

/**
 * @brief Append data to an output buffer
 * @param buf Pointer to the buffer
 * @param str Data to append
 * @param len Length of data to append
 * @return true on success, false on memory allocation failure
 */
WARN_UNUSED static bool buffer_append(OutputBuffer* buf, const char* str, size_t len) {
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

/* ==================== Utility Functions ==================== */

/**
 * @brief Set an error state
 * @param err Pointer to error struct to populate
 * @param type Type of error
 * @param message Error message
 * @param line Line number where error occurred
 * @return Always returns false for convenient error handling
 */
ALWAYS_INLINE
static inline bool set_error(TemplateError* err, TemplateErrorType type, const char* message, size_t line) {
    if (err) {
        err->type = type;
        err->line = line;
        snprintf(err->message, sizeof(err->message) - 1, "%s", message);
        err->message[sizeof(err->message) - 1] = '\0';
    }
    return false;
}

/**
 * @brief Calculate line number in template source
 * @param template_start Start of template string
 * @param error_pos Position in template where error occurred
 * @return Line number (1-based)
 */
ALWAYS_INLINE
static inline size_t calculate_line_number(const char* template_start, const char* error_pos) {
    size_t line = 1;
    for (const char* c = template_start; c && c < error_pos; c++) {
        if (*c == '\n') {
            line++;
        }
    }
    return line;
}

/**
 * @brief Get a variable from the context by name
 * @param ctx Template context
 * @param key Variable name to look up
 * @return Pointer to TemplateValue if found, NULL otherwise
 */
ALWAYS_INLINE
static inline const TemplateValue* context_get(const TemplateContext* ctx, const char* key) {
    for (size_t i = 0; i < ctx->count; i++) {
        if (strcmp(ctx->vars[i].key, key) == 0) {
            return &ctx->vars[i].value;
        }
    }
    return NULL;
}

/**
 * @brief Convert a template value to its string representation
 * @param val Value to convert
 * @param buf Buffer to append the string to
 * @return true on success, false on memory allocation failure
 */
WARN_UNUSED static bool value_to_string(const TemplateValue* val, OutputBuffer* buf) {
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

/**
 * @brief Check if a value is "truthy" (should evaluate to true in conditions)
 * @param val Value to check
 * @return true if truthy, false otherwise
 */
ALWAYS_INLINE
static inline bool is_truthy(const TemplateValue* val) {
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

/**
 * @brief Evaluate a condition expression
 * @param condition Condition string to evaluate
 * @param ctx Template context
 * @param loop_stack Current loop stack
 * @param err Error struct to populate if evaluation fails
 * @param template_start Start of template string (for line number calculation)
 * @param error_pos Position in template where error occurred
 * @return Result of condition evaluation
 */
WARN_UNUSED static bool evaluate_condition(const char* condition, const TemplateContext* ctx,
                                           const LoopStack* loop_stack, TemplateError* err,
                                           const char* template_start, const char* error_pos) {
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

/* ==================== Main Template Rendering Function ==================== */

/**
 * @brief Renders a template string with the given context
 * @param template Template string to render
 * @param ctx Context containing variables
 * @param out Output buffer to write result to
 * @param err Error struct to populate if rendering fails
 * @return true on success, false on failure (check err for details)
 *
 * This version includes logic to handle whitespace around block-level tags.
 * If a tag like {% if ... %} or {% endfor %} is on a line by itself (with only
 * whitespace characters around it), the entire line, including the leading
 * whitespace and the trailing newline, will be consumed, producing no output.
 * This results in cleaner, correctly indented HTML.
 */
bool render_template(const char* template, const TemplateContext* ctx, OutputBuffer* out,
                     TemplateError* err) {
    LoopStack loop_stack;
    ConditionalStack conditional_stack;
    if (!loop_stack_init(&loop_stack, 4)) {
        return set_error(err, TMPL_ERR_MEMORY, "malloc failed for loop stack", 1);
    }
    if (!conditional_stack_init(&conditional_stack, 4)) {
        free(loop_stack.frames);
        return set_error(err, TMPL_ERR_MEMORY, "malloc failed for conditional stack", 1);
    };

    bool in_comment = false;
    const char* last_comment = NULL;

    const char* p = template;
    while (*p) {
        // Handle HTML comments <!-- ... -->
        if (!in_comment && *p == '<' && strncmp(p, "<!--", 4) == 0) {
            in_comment = true;
            last_comment = p;
            p += 4;
            continue;
        }
        if (in_comment) {
            if (*p == '-' && strncmp(p, "-->", 3) == 0) {
                in_comment = false;
                p += 3;
            } else {
                p++;
            }
            continue;
        } else if (*p == '-' && strncmp(p, "-->", 3) == 0) {
            return set_error(err, TMPL_ERR_PARSE, "Unmatched comment closing tag '-->'",
                             calculate_line_number(template, p));
        }

        // Check if we're in a false conditional branch and should skip rendering
        ConditionalFrame* current_conditional = conditional_stack_top(&conditional_stack);
        bool should_skip = current_conditional &&
                           ((current_conditional->condition_met && current_conditional->in_else_branch) ||
                            (!current_conditional->condition_met && !current_conditional->in_else_branch));

        if (*p == '{' && *(p + 1) == '{') {
            // --- Handle Variable Substitution {{ var }} ---
            const char* var_start = p + 2;
            const char* var_end = strstr(var_start, "}}");
            if (!var_end) {
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

                // Trim whitespace from variable name
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
                        return set_error(err, TMPL_ERR_MEMORY, "realloc failed in value_to_string",
                                         calculate_line_number(template, p));
                    }
                    rendered = true;
                }

                if (!rendered) {
                    const TemplateValue* val = context_get(ctx, name);
                    if (!val) {
                        char msg[128];
                        snprintf(msg, sizeof(msg), "Missing template variable for '%s'", name);
                        return set_error(err, TMPL_ERR_RENDER, msg, calculate_line_number(template, p));
                    }

                    if (!value_to_string(val, out)) {
                        return set_error(err, TMPL_ERR_MEMORY, "realloc failed in value_to_string",
                                         calculate_line_number(template, p));
                    }
                }
            }
            p = var_end + 2;

        } else if (*p == '{' && *(p + 1) == '%') {
            // --- Handle Directives {% ... %} ---
            const char* tag_block_start = p;
            const char* dir_start = p + 2;
            const char* dir_end = strstr(dir_start, "%}");
            if (!dir_end) {
                return set_error(err, TMPL_ERR_PARSE, "Unterminated '{%' tag",
                                 calculate_line_number(template, p));
            }

            // --- Whitespace Control Logic ---
            // A tag is "standalone" if it's the only thing on its line besides whitespace.
            bool is_standalone = false;
            const char* line_start = tag_block_start;
            while (line_start > template && *(line_start - 1) != '\n') {
                line_start--;
            }
            bool only_whitespace_before = true;
            for (const char* c = line_start; c < tag_block_start; c++) {
                if (!isspace((unsigned char)*c)) {
                    only_whitespace_before = false;
                    break;
                }
            }

            if (only_whitespace_before) {
                const char* scan = dir_end + 2;
                bool only_whitespace_after = true;
                while (*scan != '\0' && *scan != '\n') {
                    if (!isspace((unsigned char)*scan)) {
                        only_whitespace_after = false;
                        break;
                    }
                    scan++;
                }
                if (only_whitespace_after) {
                    is_standalone = true;
                }
            }
            // --- End Whitespace Control Logic ---

            // Advance the pointer `p` and, if needed, clean the output buffer.
            if (is_standalone) {
                // If the tag is standalone, consume the whole line.
                // First, remove the preceding whitespace that was already added to the output.
                if (!should_skip) {
                    ssize_t buffer_line_start = 0;
                    for (ssize_t i = (ssize_t)out->size - 1; i >= 0; i--) {
                        if (out->data[i] == '\n') {
                            buffer_line_start = i + 1;
                            break;
                        }
                    }
                    out->size = buffer_line_start;
                    out->data[out->size] = '\0';
                }

                // Second, advance `p` past the tag and the rest of the line, including the newline.
                p = dir_end + 2;
                while (*p != '\0' && *p != '\n') {
                    p++;
                }
                if (*p == '\n') {
                    p++;
                }
            } else {
                // Not a standalone tag, just advance `p` past the tag itself.
                p = dir_end + 2;
            }

            // The rest of the logic for parsing and handling the directive remains.
            char directive[128];
            size_t dir_len = dir_end - dir_start;
            if (dir_len >= sizeof(directive)) {
                return set_error(err, TMPL_ERR_PARSE, "Directive is too long",
                                 calculate_line_number(template, tag_block_start));
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

            if (strncmp(cmd, "for ", 4) == 0) {
                if (!should_skip) {
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
                                         "Invalid 'for' loop. Use: {% for item in items %}",
                                         calculate_line_number(template, tag_block_start));
                    }

                    const TemplateValue* arr = context_get(ctx, parts[3]);
                    if (!arr || arr->type != TMPL_ARRAY) {
                        return set_error(err, TMPL_ERR_RENDER, "Variable for loop is not a valid array",
                                         calculate_line_number(template, tag_block_start));
                    }

                    if (!loop_stack_push(&loop_stack, arr, parts[1], p)) {
                        return set_error(err, TMPL_ERR_MEMORY, "loop_stack_push failed",
                                         calculate_line_number(template, tag_block_start));
                    };

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
                                         calculate_line_number(template, tag_block_start));
                    }

                    current_loop->index++;
                    if (current_loop->index < current_loop->array->value.array.count) {
                        p = current_loop->loop_start;
                    } else {
                        loop_stack_pop(&loop_stack);
                    }
                }
            } else if (strncmp(cmd, "if ", 3) == 0) {
                const char* condition = cmd + 3;
                bool result = evaluate_condition(condition, ctx, &loop_stack, err, template, p);

                if (err->type != TMPL_ERR_NONE)
                    return false;

                if (!conditional_stack_push(&conditional_stack, result, p)) {
                    return set_error(err, TMPL_ERR_MEMORY, "conditional_stack_push failed",
                                     calculate_line_number(template, p));
                }

                if (!result) {
                    const char* else_pos = strstr(p, "{% else %}");
                    const char* endif_pos = strstr(p, "{% endif %}");

                    if (else_pos && (!endif_pos || else_pos < endif_pos)) {
                        p = else_pos + strlen("{% else %}");
                        conditional_stack_top(&conditional_stack)->in_else_branch = true;
                    } else if (endif_pos) {
                        p = endif_pos + strlen("{% endif %}");
                        conditional_stack_pop(&conditional_stack);
                    }

                    // Consume redundant new lines. (keep tabs)
                    while (*p == '\n')
                        p++;
                }
            } else if (strcmp(cmd, "else") == 0) {
                ConditionalFrame* current_cond = conditional_stack_top(&conditional_stack);
                if (!current_cond) {
                    return set_error(err, TMPL_ERR_SYNTAX, "Found 'else' with no matching 'if'",
                                     calculate_line_number(template, tag_block_start));
                }

                current_cond->in_else_branch = true;
                if (current_cond->condition_met) {
                    const char* endif_pos = strstr(p, "{% endif %}");
                    if (endif_pos) {
                        p = endif_pos + strlen("{% endif %}");
                        conditional_stack_pop(&conditional_stack);

                        // Consume redundant new lines. (keep tabs)
                        while (*p == '\n')
                            p++;
                    }
                }
            } else if (strcmp(cmd, "endif") == 0) {
                ConditionalFrame* current_cond = conditional_stack_top(&conditional_stack);
                if (!current_cond) {
                    return set_error(err, TMPL_ERR_SYNTAX, "Found 'endif' with no matching 'if'",
                                     calculate_line_number(template, tag_block_start));
                }
                conditional_stack_pop(&conditional_stack);
            }

        } else {
            // Append regular text
            if (!should_skip) {
                if (!buffer_append(out, p, 1)) {
                    return set_error(err, TMPL_ERR_MEMORY, "buffer_append failed",
                                     calculate_line_number(template, p));
                }
            }
            p++;
        }
    }

    if (in_comment && last_comment) {
        return set_error(err, TMPL_ERR_SYNTAX, "Unterminated HTML comment '<!--'",
                         calculate_line_number(template, last_comment));
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
    while (loop_stack.depth > 0)
        loop_stack_pop(&loop_stack);
    while (conditional_stack.depth > 0)
        conditional_stack_pop(&conditional_stack);
    free(loop_stack.frames);
    free(conditional_stack.frames);
    return true;
}
