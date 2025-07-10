#ifndef BREEZE_H
#define BREEZE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

// On Windows, use _strdup and strtok_s
#if defined(_MSC_VER)
#define strdup _strdup
#define strtok_r strtok_s
#define WARN_UNUSED _Check_return_
#define ALWAYS_INLINE __forceinline
#else
#define WARN_UNUSED __attribute__((warn_unused_result))
#define ALWAYS_INLINE __attribute__((always_inline))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enum representing different value types that can be used in templates
 */
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

/**
 * @brief Union representing a template value that can be of different types
 */
typedef union {
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
} TemplateValueUnion;

/**
 * @brief Enum representing different error types
 */
typedef enum {
    TMPL_ERR_NONE,    // No error.
    TMPL_ERR_PARSE,   // Malformed tags, e.g., {{ var
    TMPL_ERR_SYNTAX,  // Incorrect directive syntax, e.g., {% for x from y %}
    TMPL_ERR_RENDER,  // Variable not found, wrong type, etc.
    TMPL_ERR_MEMORY   // malloc/realloc failure
} TemplateErrorType;

/**
 * @brief Struct representing a typed template value
 */
typedef struct {
    ValueType type;
    TemplateValueUnion value;
} TemplateValue;

/**
 * @brief Struct representing a template variable (key-value pair)
 */
typedef struct {
    const char* key;
    TemplateValue value;
} TemplateVar;

/**
 * @brief Struct representing the template rendering context
 */
typedef struct {
    TemplateVar* vars;
    size_t count;
} TemplateContext;

/**
 * @brief Struct representing a template error
 */
typedef struct {
    TemplateErrorType type;
    char message[256];
    size_t line;
} TemplateError;

/**
 * @brief Struct representing a dynamically growing output buffer
 */
typedef struct {
    char* data;       // Dynamically allocated buffer data.
    size_t size;      // Buffer size.
    size_t capacity;  // Allocated capacity.
} OutputBuffer;

/**
 * @brief Initialize an output buffer
 * @param buf Pointer to the buffer to initialize
 * @param initial_capacity Initial capacity of the buffer
 * @return true on success, false on memory allocation failure
 */
WARN_UNUSED bool buffer_init(OutputBuffer* buf, size_t initial_capacity);

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
bool render_template(const char* template, const TemplateContext* ctx, OutputBuffer* out, TemplateError* err);

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
    ptrs name[sizeof(arr) / sizeof((arr)[0])];                                                               \
    for (size_t __i = 0; __i < sizeof(arr) / sizeof((arr)[0]); ++__i)                                        \
    name[__i] = &(arr)[__i]

#ifdef __cplusplus
}
#endif

#endif  // BREEZE_H
