#ifndef BREEZE_H
#define BREEZE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#define strdup        _strdup
#define strtok_r      strtok_s
#define WARN_UNUSED   _Check_return_
#define ALWAYS_INLINE __forceinline
#else
#define WARN_UNUSED   __attribute__((warn_unused_result))
#define ALWAYS_INLINE __attribute__((always_inline))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Value Types ==================== */

typedef enum { TMPL_STRING, TMPL_INT, TMPL_FLOAT, TMPL_DOUBLE, TMPL_BOOL, TMPL_LONG, TMPL_UINT, TMPL_ARRAY } ValueType;

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

typedef enum {
    TMPL_ERR_NONE,
    TMPL_ERR_PARSE,
    TMPL_ERR_SYNTAX,
    TMPL_ERR_RENDER,
    TMPL_ERR_MEMORY,
    TMPL_ERR_IO
} TemplateErrorType;

typedef struct {
    ValueType type;
    TemplateValueUnion value;
} TemplateValue;

typedef struct {
    const char* key;
    TemplateValue value;
} TemplateVar;

typedef struct {
    TemplateVar* vars;
    size_t count;
    size_t capacity;
} TemplateContext;

typedef struct {
    TemplateErrorType type;
    char message[256];
    size_t line;
} TemplateError;

typedef struct {
    char* data;
    size_t size;
    size_t capacity;
} OutputBuffer;

/* ==================== Filter System ==================== */

typedef bool (*BreezeFilterFn)(const TemplateValue* val, const char* arg, OutputBuffer* out);

typedef struct {
    char name[64];
    BreezeFilterFn fn;
} BreezeFilter;

bool breeze_register_filter(const char* name, BreezeFilterFn fn);
void breeze_clear_filters(void);

/* ==================== Dynamic Context ==================== */

TemplateContext* context_new(size_t initial_capacity);
WARN_UNUSED bool context_set(TemplateContext* ctx, const char* key, TemplateValue value);
void context_free(TemplateContext* ctx);

/* ==================== Output Buffer ==================== */

WARN_UNUSED bool buffer_init(OutputBuffer* buf, size_t initial_capacity);

/* ==================== Render API ==================== */

bool render_template(const char* template, const TemplateContext* ctx, OutputBuffer* out, TemplateError* err);
bool render_template_file(const char* path, const TemplateContext* ctx, OutputBuffer* out, TemplateError* err);

/* ==================== Convenience Macros ==================== */

// clang-format off
#define VAR_STRING(k, v)   {k, {TMPL_STRING, .value = {.str = v}}}
#define VAR_INT(k, v)      {k, {TMPL_INT,    .value = {.integer = v}}}
#define VAR_FLOAT(k, v)    {k, {TMPL_FLOAT,  .value = {.floating = v}}}
#define VAR_DOUBLE(k, v)   {k, {TMPL_DOUBLE, .value = {.dbl = v}}}
#define VAR_BOOL(k, v)     {k, {TMPL_BOOL,   .value = {.boolean = v}}}
#define VAR_LONG(k, v)     {k, {TMPL_LONG,   .value = {.long_int = v}}}
#define VAR_UINT(k, v)     {k, {TMPL_UINT,   .value = {.uint = v}}}
// clang-format on

#define VAR_ARRAY(key, ptr_array, item_type_enum)                    \
    {                                                                \
        key, {                                                       \
            TMPL_ARRAY, .value.array = {                             \
                .items = (ptr_array),                                \
                .count = sizeof(ptr_array) / sizeof((ptr_array)[0]), \
                .item_type = (item_type_enum)                        \
            }                                                        \
        }                                                            \
    }

#define VAR_ARRAY_STR(key, str_array) VAR_ARRAY(key, str_array, TMPL_STRING)

#define MAKE_PTR_ARRAY(arr, ptrs, name)        \
    ptrs name[sizeof(arr) / sizeof((arr)[0])]; \
    for (size_t __i = 0; __i < sizeof(arr) / sizeof((arr)[0]); ++__i) name[__i] = &(arr)[__i]

/* ==================== Expression Stack (exposed for tests) ==================== */

typedef enum {
    TOKEN_VALUE,
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_NOT,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
} ExprTokenType;

typedef struct {
    ExprTokenType type;
    bool value;
} ExprToken;

typedef struct {
    ExprToken* tokens;
    size_t size;
    size_t capacity;
} ExprStack;

#ifdef __cplusplus
}
#endif

#endif  // BREEZE_H
