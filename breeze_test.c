#include "breeze.h"
#include <stdio.h>
#include <string.h>

#define TEST_ASSERT(condition)                                                                               \
    do {                                                                                                     \
        if (!(condition)) {                                                                                  \
            fprintf(stderr, "TEST FAILED: %s:%d: %s\n", __FILE__, __LINE__, #condition);                     \
            exit(1);                                                                                         \
        }                                                                                                    \
    } while (0)

#define TEST_ASSERT_EQUAL_INT(expected, actual)                                                              \
    do {                                                                                                     \
        int exp = (expected);                                                                                \
        int act = (actual);                                                                                  \
        if (exp != act) {                                                                                    \
            fprintf(stderr, "TEST FAILED: %s:%d: Expected %d, got %d\n", __FILE__, __LINE__, exp, act);      \
            exit(1);                                                                                         \
        }                                                                                                    \
    } while (0)

#define TEST_ASSERT_EQUAL_STR(expected, actual)                                                              \
    do {                                                                                                     \
        const char* exp = (expected);                                                                        \
        const char* act = (actual);                                                                          \
        if (strcmp(exp, act) != 0) {                                                                         \
            fprintf(stderr, "TEST FAILED: %s:%d: Expected '%s', got '%s'\n", __FILE__, __LINE__, exp, act);  \
            exit(1);                                                                                         \
        }                                                                                                    \
    } while (0)

#define TEST_ASSERT_NULL(ptr)                                                                                \
    do {                                                                                                     \
        if ((ptr) != NULL) {                                                                                 \
            fprintf(stderr, "TEST FAILED: %s:%d: Expected NULL, got %p\n", __FILE__, __LINE__,               \
                    (void*)(ptr));                                                                           \
            exit(1);                                                                                         \
        }                                                                                                    \
    } while (0)

#define TEST_ASSERT_NOT_NULL(ptr)                                                                            \
    do {                                                                                                     \
        if ((ptr) == NULL) {                                                                                 \
            fprintf(stderr, "TEST FAILED: %s:%d: Expected non-NULL\n", __FILE__, __LINE__);                  \
            exit(1);                                                                                         \
        }                                                                                                    \
    } while (0)

#define TEST_PASS()                                                                                          \
    do {                                                                                                     \
        printf("TEST PASSED: %s:%d\n", __FILE__, __LINE__);                                                  \
    } while (0)

#define TEST_RUN(test)                                                                                       \
    do {                                                                                                     \
        printf("Running %s...\n", #test);                                                                    \
        test();                                                                                              \
        printf("%s PASSED\n", #test);                                                                        \
    } while (0)

static void test_simple_variable_substitution(void) {
    TemplateContext ctx = {.vars =
                               (TemplateVar[]){
                                   {"name", {.type = TMPL_STRING, .value.str = "John"}},
                                   {"age", {.type = TMPL_INT, .value.integer = 30}},
                               },
                           .count = 2};

    OutputBuffer out;
    TEST_ASSERT(buffer_init(&out, 0));

    TemplateError err = {0};
    const char* template = "Hello {{ name }}, you are {{ age }} years old.";
    TEST_ASSERT(render_template(template, &ctx, &out, &err));
    TEST_ASSERT_EQUAL_STR("Hello John, you are 30 years old.", out.data);

    free(out.data);
}

static void test_missing_variable(void) {
    TemplateContext ctx = {.vars =
                               (TemplateVar[]){
                                   {"name", {.type = TMPL_STRING, .value.str = "John"}},
                               },
                           .count = 1};

    OutputBuffer out;
    TEST_ASSERT(buffer_init(&out, 0));

    TemplateError err = {0};
    const char* template = "Hello {{ name }}, you are {{ age }} years old.";
    TEST_ASSERT(!render_template(template, &ctx, &out, &err));
    TEST_ASSERT(TMPL_ERR_RENDER == err.type);
    TEST_ASSERT_EQUAL_STR("Missing template variable for 'age'", err.message);

    free(out.data);
}

static void test_for_loop(void) {
    const char* items[] = {"apple", "banana", "cherry"};
    TemplateValue array = {.type = TMPL_ARRAY,
                           .value.array = {.items = items, .count = 3, .item_type = TMPL_STRING}};

    TemplateContext ctx = {.vars =
                               (TemplateVar[]){
                                   {"fruits", array},
                               },
                           .count = 1};

    OutputBuffer out;
    TEST_ASSERT(buffer_init(&out, 0));

    TemplateError err = {0};
    const char* template = "{% for fruit in fruits %}{{ fruit }}, {% endfor %}";
    TEST_ASSERT(render_template(template, &ctx, &out, &err));
    TEST_ASSERT_EQUAL_STR("apple, banana, cherry, ", out.data);

    free(out.data);
}

static void test_nested_loops(void) {
    const char* outer_items[] = {"A", "B"};
    TemplateValue outer_array = {.type = TMPL_ARRAY,
                                 .value.array = {.items = outer_items, .count = 2, .item_type = TMPL_STRING}};

    const char* inner_items[] = {"1", "2", "3"};
    TemplateValue inner_array = {.type = TMPL_ARRAY,
                                 .value.array = {.items = inner_items, .count = 3, .item_type = TMPL_STRING}};

    TemplateContext ctx = {.vars =
                               (TemplateVar[]){
                                   {"letters", outer_array},
                                   {"numbers", inner_array},
                               },
                           .count = 2};

    OutputBuffer out;
    TEST_ASSERT(buffer_init(&out, 0));

    TemplateError err = {0};
    const char* template =
        "{% for letter in letters %}{{ letter }}{% for number in numbers %}{{ number }}{% endfor %}{% endfor "
        "%}";
    TEST_ASSERT(render_template(template, &ctx, &out, &err));
    TEST_ASSERT_EQUAL_STR("A123B123", out.data);

    free(out.data);
}

static void test_if_condition(void) {
    TemplateContext ctx = {.vars =
                               (TemplateVar[]){
                                   {"show_message", {.type = TMPL_BOOL, .value.boolean = true}},
                                   {"message", {.type = TMPL_STRING, .value.str = "Hello!"}},
                               },
                           .count = 2};

    OutputBuffer out;
    TEST_ASSERT(buffer_init(&out, 0));

    TemplateError err = {0};
    const char* template = "{% if show_message %}{{ message }}{% endif %}";
    TEST_ASSERT(render_template(template, &ctx, &out, &err));
    TEST_ASSERT_EQUAL_STR("Hello!", out.data);

    free(out.data);
}

static void test_if_else_condition(void) {
    TemplateContext ctx = {.vars =
                               (TemplateVar[]){
                                   {"show_message", {.type = TMPL_BOOL, .value.boolean = false}},
                                   {"message", {.type = TMPL_STRING, .value.str = "Hello!"}},
                                   {"alt_message", {.type = TMPL_STRING, .value.str = "Goodbye!"}},
                               },
                           .count = 3};

    OutputBuffer out;
    TEST_ASSERT(buffer_init(&out, 0));

    TemplateError err = {0};
    const char* template = "{% if show_message %}{{ message }}{% else %}{{ alt_message }}{% endif %}";
    TEST_ASSERT(render_template(template, &ctx, &out, &err));
    TEST_ASSERT_EQUAL_STR("Goodbye!", out.data);

    free(out.data);
}

static void test_nested_conditionals(void) {
    TemplateContext ctx = {.vars =
                               (TemplateVar[]){
                                   {"condition1", {.type = TMPL_BOOL, .value.boolean = true}},
                                   {"condition2", {.type = TMPL_BOOL, .value.boolean = false}},
                                   {"message1", {.type = TMPL_STRING, .value.str = "First"}},
                                   {"message2", {.type = TMPL_STRING, .value.str = "Second"}},
                                   {"message3", {.type = TMPL_STRING, .value.str = "Third"}},
                               },
                           .count = 5};

    OutputBuffer out;
    TEST_ASSERT(buffer_init(&out, 0));

    TemplateError err = {0};
    const char* template =
        "{% if condition1 %}{{ message1 }}{% if condition2 %}{{ message2 }}{% else %}{{ message3 }}{% endif "
        "%}{% endif %}";
    TEST_ASSERT(render_template(template, &ctx, &out, &err));
    TEST_ASSERT_EQUAL_STR("FirstThird", out.data);

    free(out.data);
}

static void test_complex_expression(void) {
    TemplateContext ctx = {.vars =
                               (TemplateVar[]){
                                   {"a", {.type = TMPL_BOOL, .value.boolean = true}},
                                   {"b", {.type = TMPL_BOOL, .value.boolean = false}},
                                   {"c", {.type = TMPL_BOOL, .value.boolean = true}},
                                   {"message", {.type = TMPL_STRING, .value.str = "Success"}},
                               },
                           .count = 4};

    OutputBuffer out;
    TEST_ASSERT(buffer_init(&out, 0));

    TemplateError err = {0};
    const char* template = "{% if a and (not b or c) %}{{ message }}{% endif %}";
    TEST_ASSERT(render_template(template, &ctx, &out, &err));
    TEST_ASSERT_EQUAL_STR("Success", out.data);

    free(out.data);
}

static void test_html_comments(void) {
    TemplateContext ctx = {.vars =
                               (TemplateVar[]){
                                   {"name", {.type = TMPL_STRING, .value.str = "John"}},
                               },
                           .count = 1};

    OutputBuffer out;
    TEST_ASSERT(buffer_init(&out, 0));

    TemplateError err = {0};
    const char* template = "Hello <!-- This is a comment -->{{ name }}";
    TEST_ASSERT(render_template(template, &ctx, &out, &err));
    TEST_ASSERT_EQUAL_STR("Hello John", out.data);

    free(out.data);
}

static void test_unterminated_comment(void) {
    TemplateContext ctx = {.vars =
                               (TemplateVar[]){
                                   {"name", {.type = TMPL_STRING, .value.str = "John"}},
                               },
                           .count = 1};

    OutputBuffer out;
    TEST_ASSERT(buffer_init(&out, 0));

    TemplateError err = {0};
    const char* template = "Hello <!-- This is an unterminated comment";
    TEST_ASSERT(!render_template(template, &ctx, &out, &err));
    TEST_ASSERT(TMPL_ERR_SYNTAX == err.type);
    TEST_ASSERT_EQUAL_STR("Unterminated HTML comment '<!--'", err.message);

    free(out.data);
}

static void test_unclosed_for_loop(void) {
    const char* items[] = {"apple", "banana"};
    TemplateValue array = {.type = TMPL_ARRAY,
                           .value.array = {.items = items, .count = 2, .item_type = TMPL_STRING}};

    TemplateContext ctx = {.vars =
                               (TemplateVar[]){
                                   {"fruits", array},
                               },
                           .count = 1};

    OutputBuffer out;
    TEST_ASSERT(buffer_init(&out, 0));

    TemplateError err = {0};
    const char* template = "{% for fruit in fruits %}{{ fruit }}";
    TEST_ASSERT(!render_template(template, &ctx, &out, &err));
    TEST_ASSERT(TMPL_ERR_SYNTAX == err.type);
    TEST_ASSERT_EQUAL_STR("Unclosed 'for' loop at end of template", err.message);

    free(out.data);
}

static void test_unclosed_if_statement(void) {
    TemplateContext ctx = {.vars =
                               (TemplateVar[]){
                                   {"condition", {.type = TMPL_BOOL, .value.boolean = true}},
                               },
                           .count = 1};

    OutputBuffer out;
    TEST_ASSERT(buffer_init(&out, 0));

    TemplateError err = {0};
    const char* template = "{% if condition %}Hello";
    TEST_ASSERT(!render_template(template, &ctx, &out, &err));
    TEST_ASSERT(TMPL_ERR_SYNTAX == err.type);
    TEST_ASSERT_EQUAL_STR("Unclosed 'if' statement at end of template", err.message);

    free(out.data);
}

static void test_whitespace_control(void) {
    TemplateContext ctx = {.vars =
                               (TemplateVar[]){
                                   {"name", {.type = TMPL_STRING, .value.str = "John"}},
                                   {"show", {.type = TMPL_BOOL, .value.boolean = true}},
                               },
                           .count = 2};

    OutputBuffer out;
    TEST_ASSERT(buffer_init(&out, 0));

    TemplateError err = {0};
    const char* template = "Hello\n  {% if show %}\n    {{ name }}\n  {% endif %}\nWorld";
    TEST_ASSERT(render_template(template, &ctx, &out, &err));
    TEST_ASSERT_EQUAL_STR("Hello\n    John\nWorld", out.data);

    free(out.data);
}

static void test_all_value_types(void) {
    TemplateContext ctx = {.vars =
                               (TemplateVar[]){
                                   {"str", {.type = TMPL_STRING, .value.str = "text"}},
                                   {"int", {.type = TMPL_INT, .value.integer = 42}},
                                   {"float", {.type = TMPL_FLOAT, .value.floating = 3.14f}},
                                   {"double", {.type = TMPL_DOUBLE, .value.dbl = 2.71828}},
                                   {"bool", {.type = TMPL_BOOL, .value.boolean = true}},
                                   {"long", {.type = TMPL_LONG, .value.long_int = 123456789L}},
                                   {"uint", {.type = TMPL_UINT, .value.uint = 4294967295U}},
                               },
                           .count = 7};

    OutputBuffer out;
    TEST_ASSERT(buffer_init(&out, 0));

    TemplateError err = {0};
    const char* template = "{{ str }},{{ int }},{{ float }},{{ double }},{{ bool }},{{ long }},{{ uint }}";
    TEST_ASSERT(render_template(template, &ctx, &out, &err));
    TEST_ASSERT_EQUAL_STR("text,42,3.1400,2.7183,true,123456789,4294967295", out.data);

    free(out.data);
}

int main(void) {
    TEST_RUN(test_simple_variable_substitution);
    TEST_RUN(test_missing_variable);
    TEST_RUN(test_for_loop);
    TEST_RUN(test_nested_loops);
    TEST_RUN(test_if_condition);
    TEST_RUN(test_if_else_condition);
    TEST_RUN(test_nested_conditionals);
    TEST_RUN(test_complex_expression);
    TEST_RUN(test_html_comments);
    TEST_RUN(test_unterminated_comment);
    TEST_RUN(test_unclosed_for_loop);
    TEST_RUN(test_unclosed_if_statement);
    TEST_RUN(test_whitespace_control);
    TEST_RUN(test_all_value_types);
}
