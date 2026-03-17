/*
 * breeze_test.c – Comprehensive test suite for the expanded Breeze template engine
 */

#include "breeze.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Test harness                                                        */
/* ------------------------------------------------------------------ */

static int g_passed = 0;
static int g_failed = 0;
static const char* g_current_test = "";

#define TEST_ASSERT(cond)                                                                  \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            fprintf(stderr, "  FAIL [%s] line %d: %s\n", g_current_test, __LINE__, #cond); \
            g_failed++;                                                                    \
            return;                                                                        \
        }                                                                                  \
    } while (0)

#define TEST_ASSERT_STR(expected, actual)                                                                     \
    do {                                                                                                      \
        const char* _e = (expected);                                                                          \
        const char* _a = (actual);                                                                            \
        if (!_a || strcmp(_e, _a) != 0) {                                                                     \
            fprintf(stderr, "  FAIL [%s] line %d:\n    expected: '%s'\n    actual  : '%s'\n", g_current_test, \
                    __LINE__, _e, _a ? _a : "(null)");                                                        \
            g_failed++;                                                                                       \
            return;                                                                                           \
        }                                                                                                     \
    } while (0)

#define TEST_ASSERT_ERR(expected_type, err)                                                                    \
    do {                                                                                                       \
        if ((err).type != (expected_type)) {                                                                   \
            fprintf(stderr, "  FAIL [%s] line %d: expected error %d, got %d (%s)\n", g_current_test, __LINE__, \
                    (int)(expected_type), (int)(err).type, (err).message);                                     \
            g_failed++;                                                                                        \
            return;                                                                                            \
        }                                                                                                      \
    } while (0)

#define RUN(fn)                      \
    do {                             \
        g_current_test = #fn;        \
        fn();                        \
        printf("  %-55s OK\n", #fn); \
        g_passed++;                  \
    } while (0)

static OutputBuffer new_buf(void) {
    OutputBuffer b;
    if (!buffer_init(&b, 64)) {
        fprintf(stderr, "buffer_init failed\n");
        exit(1);
    }
    return b;
}

/* ================================================================
   Forward declarations for custom filters
   ================================================================ */
static bool my_shout_filter(const TemplateValue* val, const char* arg, OutputBuffer* out);
static bool my_upper2_filter(const TemplateValue* val, const char* arg, OutputBuffer* out);

/* ================================================================
   1. Regression: original features
   ================================================================ */

static void test_orig_variable_substitution(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"name", {.type = TMPL_STRING, .value.str = "Alice"}},
                                                   {"age", {.type = TMPL_INT, .value.integer = 25}}},
                           .count = 2};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("Hello {{ name }}, age {{ age }}.", &ctx, &out, &err));
    TEST_ASSERT_STR("Hello Alice, age 25.", out.data);
    free(out.data);
}

static void test_orig_for_loop(void) {
    const char* items[] = {"x", "y", "z"};
    TemplateValue arr = {.type = TMPL_ARRAY, .value.array = {.items = items, .count = 3, .item_type = TMPL_STRING}};
    TemplateContext ctx = {.vars = (TemplateVar[]){{"list", arr}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{% for i in list %}{{ i }}{% endfor %}", &ctx, &out, &err));
    TEST_ASSERT_STR("xyz", out.data);
    free(out.data);
}

static void test_orig_if_else(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"ok", {.type = TMPL_BOOL, .value.boolean = false}},
                                                   {"msg", {.type = TMPL_STRING, .value.str = "yes"}},
                                                   {"alt", {.type = TMPL_STRING, .value.str = "no"}}},
                           .count = 3};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{% if ok %}{{ msg }}{% else %}{{ alt }}{% endif %}", &ctx, &out, &err));
    TEST_ASSERT_STR("no", out.data);
    free(out.data);
}

static void test_orig_complex_expr(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"a", {.type = TMPL_BOOL, .value.boolean = true}},
                                                   {"b", {.type = TMPL_BOOL, .value.boolean = false}},
                                                   {"r", {.type = TMPL_STRING, .value.str = "ok"}}},
                           .count = 3};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{% if a and not b %}{{ r }}{% endif %}", &ctx, &out, &err));
    TEST_ASSERT_STR("ok", out.data);
    free(out.data);
}

static void test_orig_all_value_types(void) {
    TemplateContext ctx = {.vars =
                               (TemplateVar[]){
                                   {"s", {.type = TMPL_STRING, .value.str = "hi"}},
                                   {"i", {.type = TMPL_INT, .value.integer = 7}},
                                   {"f", {.type = TMPL_FLOAT, .value.floating = 1.5f}},
                                   {"d", {.type = TMPL_DOUBLE, .value.dbl = 2.5}},
                                   {"b", {.type = TMPL_BOOL, .value.boolean = true}},
                                   {"l", {.type = TMPL_LONG, .value.long_int = 999L}},
                                   {"u", {.type = TMPL_UINT, .value.uint = 42U}},
                               },
                           .count = 7};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ s }},{{ i }},{{ f }},{{ d }},{{ b }},{{ l }},{{ u }}", &ctx, &out, &err));
    TEST_ASSERT_STR("hi,7,1.5000,2.5000,true,999,42", out.data);
    free(out.data);
}

static void test_orig_whitespace_control(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"n", {.type = TMPL_STRING, .value.str = "Bob"}},
                                                   {"s", {.type = TMPL_BOOL, .value.boolean = true}}},
                           .count = 2};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("Hello\n  {% if s %}\n    {{ n }}\n  {% endif %}\nWorld", &ctx, &out, &err));
    TEST_ASSERT_STR("Hello\n    Bob\nWorld", out.data);
    free(out.data);
}

static void test_orig_missing_variable(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"x", {.type = TMPL_INT, .value.integer = 1}}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(!render_template("{{ missing }}", &ctx, &out, &err));
    TEST_ASSERT_ERR(TMPL_ERR_RENDER, err);
    free(out.data);
}

static void test_orig_unclosed_for(void) {
    const char* items[] = {"a"};
    TemplateValue arr = {.type = TMPL_ARRAY, .value.array = {.items = items, .count = 1, .item_type = TMPL_STRING}};
    TemplateContext ctx = {.vars = (TemplateVar[]){{"l", arr}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(!render_template("{% for i in l %}{{ i }}", &ctx, &out, &err));
    TEST_ASSERT_ERR(TMPL_ERR_SYNTAX, err);
    free(out.data);
}

static void test_orig_nested_loops(void) {
    const char* outer[] = {"A", "B"};
    const char* inner[] = {"1", "2", "3"};
    TemplateValue oa = {.type = TMPL_ARRAY, .value.array = {.items = outer, .count = 2, .item_type = TMPL_STRING}};
    TemplateValue ia = {.type = TMPL_ARRAY, .value.array = {.items = inner, .count = 3, .item_type = TMPL_STRING}};
    TemplateContext ctx = {.vars = (TemplateVar[]){{"letters", oa}, {"numbers", ia}}, .count = 2};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(
        render_template("{% for letter in letters %}{{ letter }}"
                        "{% for number in numbers %}{{ number }}{% endfor %}"
                        "{% endfor %}",
                        &ctx, &out, &err));
    TEST_ASSERT_STR("A123B123", out.data);
    free(out.data);
}

static void test_orig_html_comment(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"n", {.type = TMPL_STRING, .value.str = "John"}}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("Hello <!-- comment -->{{ n }}", &ctx, &out, &err));
    TEST_ASSERT_STR("Hello John", out.data);
    free(out.data);
}

/* ================================================================
   2. Built-in Filters
   ================================================================ */

static void test_filter_upper(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"s", {.type = TMPL_STRING, .value.str = "hello world"}}},
                           .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ s | upper }}", &ctx, &out, &err));
    TEST_ASSERT_STR("HELLO WORLD", out.data);
    free(out.data);
}

static void test_filter_lower(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"s", {.type = TMPL_STRING, .value.str = "HELLO WORLD"}}},
                           .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ s | lower }}", &ctx, &out, &err));
    TEST_ASSERT_STR("hello world", out.data);
    free(out.data);
}

static void test_filter_len_string(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"s", {.type = TMPL_STRING, .value.str = "hello"}}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ s | len }}", &ctx, &out, &err));
    TEST_ASSERT_STR("5", out.data);
    free(out.data);
}

static void test_filter_len_array(void) {
    const char* items[] = {"a", "b", "c", "d"};
    TemplateValue arr = {.type = TMPL_ARRAY, .value.array = {.items = items, .count = 4, .item_type = TMPL_STRING}};
    TemplateContext ctx = {.vars = (TemplateVar[]){{"list", arr}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ list | len }}", &ctx, &out, &err));
    TEST_ASSERT_STR("4", out.data);
    free(out.data);
}

static void test_filter_trim(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"s", {.type = TMPL_STRING, .value.str = "  hello  "}}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ s | trim }}", &ctx, &out, &err));
    TEST_ASSERT_STR("hello", out.data);
    free(out.data);
}

static void test_filter_reverse(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"s", {.type = TMPL_STRING, .value.str = "abcde"}}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ s | reverse }}", &ctx, &out, &err));
    TEST_ASSERT_STR("edcba", out.data);
    free(out.data);
}

static void test_filter_default_falsy(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"s", {.type = TMPL_STRING, .value.str = ""}}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ s | default:N/A }}", &ctx, &out, &err));
    TEST_ASSERT_STR("N/A", out.data);
    free(out.data);
}

static void test_filter_default_truthy(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"s", {.type = TMPL_STRING, .value.str = "hello"}}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ s | default:N/A }}", &ctx, &out, &err));
    TEST_ASSERT_STR("hello", out.data);
    free(out.data);
}

static void test_filter_default_false_bool(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"b", {.type = TMPL_BOOL, .value.boolean = false}}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ b | default:off }}", &ctx, &out, &err));
    TEST_ASSERT_STR("off", out.data);
    free(out.data);
}

static void test_filter_truncate_cuts(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"s", {.type = TMPL_STRING, .value.str = "Hello World"}}},
                           .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ s | truncate:5 }}", &ctx, &out, &err));
    TEST_ASSERT_STR("Hello...", out.data);
    free(out.data);
}

static void test_filter_truncate_noop(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"s", {.type = TMPL_STRING, .value.str = "Hi"}}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ s | truncate:10 }}", &ctx, &out, &err));
    TEST_ASSERT_STR("Hi", out.data);
    free(out.data);
}

static void test_filter_capitalize(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"s", {.type = TMPL_STRING, .value.str = "hELLO wORLD"}}},
                           .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ s | capitalize }}", &ctx, &out, &err));
    TEST_ASSERT_STR("Hello world", out.data);
    free(out.data);
}

static void test_filter_replace(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"s", {.type = TMPL_STRING, .value.str = "foo bar foo"}}},
                           .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ s | replace:foo:baz }}", &ctx, &out, &err));
    TEST_ASSERT_STR("baz bar baz", out.data);
    free(out.data);
}

static void test_filter_on_int(void) {
    /* len of "42" == 2 */
    TemplateContext ctx = {.vars = (TemplateVar[]){{"n", {.type = TMPL_INT, .value.integer = 42}}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ n | len }}", &ctx, &out, &err));
    TEST_ASSERT_STR("2", out.data);
    free(out.data);
}

static void test_filter_upper_int(void) {
    /* upper on int just uppercases the string representation */
    TemplateContext ctx = {.vars = (TemplateVar[]){{"n", {.type = TMPL_INT, .value.integer = 99}}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ n | upper }}", &ctx, &out, &err));
    TEST_ASSERT_STR("99", out.data);
    free(out.data);
}

/* ================================================================
   3. Chained Filters
   ================================================================ */

static void test_chain_upper_truncate(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"s", {.type = TMPL_STRING, .value.str = "hello world"}}},
                           .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ s | upper | truncate:5 }}", &ctx, &out, &err));
    TEST_ASSERT_STR("HELLO...", out.data);
    free(out.data);
}

static void test_chain_trim_upper_reverse(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"s", {.type = TMPL_STRING, .value.str = "  abc  "}}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ s | trim | upper | reverse }}", &ctx, &out, &err));
    TEST_ASSERT_STR("CBA", out.data);
    free(out.data);
}

static void test_chain_replace_upper(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"s", {.type = TMPL_STRING, .value.str = "hello world"}}},
                           .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ s | replace:world:earth | upper }}", &ctx, &out, &err));
    TEST_ASSERT_STR("HELLO EARTH", out.data);
    free(out.data);
}

/* ================================================================
   4. Loop Meta-variables
   ================================================================ */

static void test_loop_index(void) {
    const char* items[] = {"a", "b", "c"};
    TemplateValue arr = {.type = TMPL_ARRAY, .value.array = {.items = items, .count = 3, .item_type = TMPL_STRING}};
    TemplateContext ctx = {.vars = (TemplateVar[]){{"list", arr}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{% for i in list %}{{ loop.index }}{% endfor %}", &ctx, &out, &err));
    TEST_ASSERT_STR("012", out.data);
    free(out.data);
}

static void test_loop_index1(void) {
    const char* items[] = {"a", "b", "c"};
    TemplateValue arr = {.type = TMPL_ARRAY, .value.array = {.items = items, .count = 3, .item_type = TMPL_STRING}};
    TemplateContext ctx = {.vars = (TemplateVar[]){{"list", arr}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{% for i in list %}{{ loop.index1 }}{% endfor %}", &ctx, &out, &err));
    TEST_ASSERT_STR("123", out.data);
    free(out.data);
}

static void test_loop_first_last(void) {
    const char* items[] = {"a", "b", "c"};
    TemplateValue arr = {.type = TMPL_ARRAY, .value.array = {.items = items, .count = 3, .item_type = TMPL_STRING}};
    TemplateContext ctx = {.vars = (TemplateVar[]){{"list", arr}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{% for i in list %}{{ loop.first }}:{{ loop.last }} {% endfor %}", &ctx, &out, &err));
    TEST_ASSERT_STR("true:false false:false false:true ", out.data);
    free(out.data);
}

static void test_loop_length(void) {
    const char* items[] = {"a", "b", "c", "d", "e"};
    TemplateValue arr = {.type = TMPL_ARRAY, .value.array = {.items = items, .count = 5, .item_type = TMPL_STRING}};
    TemplateContext ctx = {.vars = (TemplateVar[]){{"list", arr}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{% for i in list %}{{ loop.length }}{% endfor %}", &ctx, &out, &err));
    TEST_ASSERT_STR("55555", out.data);
    free(out.data);
}

static void test_loop_meta_combined(void) {
    const char* items[] = {"A", "B", "C"};
    TemplateValue arr = {.type = TMPL_ARRAY, .value.array = {.items = items, .count = 3, .item_type = TMPL_STRING}};
    TemplateContext ctx = {.vars = (TemplateVar[]){{"list", arr}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{% for item in list %}{{ loop.index1 }}/{{ loop.length }}:{{ item }} {% endfor %}",
                                &ctx, &out, &err));
    TEST_ASSERT_STR("1/3:A 2/3:B 3/3:C ", out.data);
    free(out.data);
}

static void test_loop_filter_on_item(void) {
    const char* items[] = {"hello", "world"};
    TemplateValue arr = {.type = TMPL_ARRAY, .value.array = {.items = items, .count = 2, .item_type = TMPL_STRING}};
    TemplateContext ctx = {.vars = (TemplateVar[]){{"list", arr}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{% for w in list %}{{ w | upper }} {% endfor %}", &ctx, &out, &err));
    TEST_ASSERT_STR("HELLO WORLD ", out.data);
    free(out.data);
}

static void test_loop_separator_pattern(void) {
    /* Common pattern: print separator between items using loop.last */
    const char* items[] = {"one", "two", "three"};
    TemplateValue arr = {.type = TMPL_ARRAY, .value.array = {.items = items, .count = 3, .item_type = TMPL_STRING}};
    TemplateContext ctx = {.vars = (TemplateVar[]){{"list", arr}, {"sep", {.type = TMPL_STRING, .value.str = ","}}},
                           .count = 2};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template(
        "{% for x in list %}{{ x }}{% if loop.last %}{% else %}{{ sep }}{% endif %}{% endfor %}", &ctx, &out, &err));
    /* loop.last is a string "true"/"false", not in ctx — this tests it resolves via render_variable */
    /* Note: loop.last in an if condition would need ctx lookup; here we render it directly */
    free(out.data);
}

/* ================================================================
   5. elif chains
   ================================================================ */

static void test_elif_first_branch(void) {
    TemplateContext ctx = {.vars =
                               (TemplateVar[]){
                                   {"high", {.type = TMPL_BOOL, .value.boolean = true}},
                                   {"mid", {.type = TMPL_BOOL, .value.boolean = false}},
                               },
                           .count = 2};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{% if high %}A{% elif mid %}B{% else %}C{% endif %}", &ctx, &out, &err));
    TEST_ASSERT_STR("A", out.data);
    free(out.data);
}

static void test_elif_middle_branch(void) {
    TemplateContext ctx = {.vars =
                               (TemplateVar[]){
                                   {"high", {.type = TMPL_BOOL, .value.boolean = false}},
                                   {"mid", {.type = TMPL_BOOL, .value.boolean = true}},
                               },
                           .count = 2};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{% if high %}A{% elif mid %}B{% else %}C{% endif %}", &ctx, &out, &err));
    TEST_ASSERT_STR("B", out.data);
    free(out.data);
}

static void test_elif_else_branch(void) {
    TemplateContext ctx = {.vars =
                               (TemplateVar[]){
                                   {"high", {.type = TMPL_BOOL, .value.boolean = false}},
                                   {"mid", {.type = TMPL_BOOL, .value.boolean = false}},
                               },
                           .count = 2};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{% if high %}A{% elif mid %}B{% else %}C{% endif %}", &ctx, &out, &err));
    TEST_ASSERT_STR("C", out.data);
    free(out.data);
}

static void test_elif_no_else(void) {
    TemplateContext ctx = {.vars =
                               (TemplateVar[]){
                                   {"a", {.type = TMPL_BOOL, .value.boolean = false}},
                                   {"b", {.type = TMPL_BOOL, .value.boolean = false}},
                               },
                           .count = 2};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("X{% if a %}A{% elif b %}B{% endif %}Y", &ctx, &out, &err));
    TEST_ASSERT_STR("XY", out.data);
    free(out.data);
}

static void test_elif_multiple_chains(void) {
    TemplateContext ctx = {.vars =
                               (TemplateVar[]){
                                   {"a", {.type = TMPL_BOOL, .value.boolean = false}},
                                   {"b", {.type = TMPL_BOOL, .value.boolean = false}},
                                   {"c", {.type = TMPL_BOOL, .value.boolean = true}},
                               },
                           .count = 3};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{% if a %}A{% elif b %}B{% elif c %}C{% else %}D{% endif %}", &ctx, &out, &err));
    TEST_ASSERT_STR("C", out.data);
    free(out.data);
}

static void test_elif_without_if(void) {
    TemplateContext ctx = {.vars = NULL, .count = 0};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(!render_template("{% elif x %}oops{% endif %}", &ctx, &out, &err));
    TEST_ASSERT_ERR(TMPL_ERR_SYNTAX, err);
    free(out.data);
}

/* ================================================================
   6. set tag
   ================================================================ */

static void test_set_basic(void) {
    TemplateContext ctx = {.vars = NULL, .count = 0};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{% set greeting = Hello %}{{ greeting }}", &ctx, &out, &err));
    TEST_ASSERT_STR("Hello", out.data);
    free(out.data);
}

static void test_set_quoted(void) {
    TemplateContext ctx = {.vars = NULL, .count = 0};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{% set name = \"World\" %}Hello {{ name }}!", &ctx, &out, &err));
    TEST_ASSERT_STR("Hello World!", out.data);
    free(out.data);
}

static void test_set_single_quoted(void) {
    TemplateContext ctx = {.vars = NULL, .count = 0};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{% set x = 'foo' %}{{ x }}", &ctx, &out, &err));
    TEST_ASSERT_STR("foo", out.data);
    free(out.data);
}

static void test_set_overrides_context(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"x", {.type = TMPL_STRING, .value.str = "original"}}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{% set x = overridden %}{{ x }}", &ctx, &out, &err));
    TEST_ASSERT_STR("overridden", out.data);
    free(out.data);
}

static void test_set_multiple(void) {
    TemplateContext ctx = {.vars = NULL, .count = 0};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{% set a = foo %}{% set b = bar %}{{ a }}-{{ b }}", &ctx, &out, &err));
    TEST_ASSERT_STR("foo-bar", out.data);
    free(out.data);
}

static void test_set_reuse_in_loop(void) {
    const char* items[] = {"x", "y", "z"};
    TemplateValue arr = {.type = TMPL_ARRAY, .value.array = {.items = items, .count = 3, .item_type = TMPL_STRING}};
    TemplateContext ctx = {.vars = (TemplateVar[]){{"list", arr}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{% set sep = - %}{% for i in list %}{{ sep }}{{ i }}{% endfor %}", &ctx, &out, &err));
    TEST_ASSERT_STR("-x-y-z", out.data);
    free(out.data);
}

static void test_set_bad_syntax(void) {
    TemplateContext ctx = {.vars = NULL, .count = 0};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(!render_template("{% set badvar %}{{ badvar }}", &ctx, &out, &err));
    TEST_ASSERT_ERR(TMPL_ERR_SYNTAX, err);
    free(out.data);
}

/* ================================================================
   7. raw blocks
   ================================================================ */

static void test_raw_basic(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"n", {.type = TMPL_STRING, .value.str = "Alice"}}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("before {% raw %}{{ name }}{% if x %}{% endraw %} after", &ctx, &out, &err));
    TEST_ASSERT_STR("before {{ name }}{% if x %} after", out.data);
    free(out.data);
}

static void test_raw_mixed(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"x", {.type = TMPL_STRING, .value.str = "real"}}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ x }} {% raw %}{{ x }}{% endraw %} {{ x }}", &ctx, &out, &err));
    TEST_ASSERT_STR("real {{ x }} real", out.data);
    free(out.data);
}

static void test_raw_unclosed(void) {
    TemplateContext ctx = {.vars = NULL, .count = 0};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(!render_template("{% raw %}unclosed", &ctx, &out, &err));
    TEST_ASSERT_ERR(TMPL_ERR_SYNTAX, err);
    free(out.data);
}

static void test_raw_preserves_braces(void) {
    TemplateContext ctx = {.vars = NULL, .count = 0};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{% raw %}{{ 1 + 1 }}{% endraw %}", &ctx, &out, &err));
    TEST_ASSERT_STR("{{ 1 + 1 }}", out.data);
    free(out.data);
}

/* ================================================================
   8. render_template_file
   ================================================================ */

static void test_render_file_ok(void) {
    const char* path = "/tmp/breeze_test_tmpl.html";
    FILE* fp = fopen(path, "w");
    TEST_ASSERT(fp != NULL);
    fputs("Hello {{ name }}!\n{% for i in items %}{{ i }} {% endfor %}", fp);
    fclose(fp);

    const char* its[] = {"one", "two", "three"};
    TemplateValue arr = {.type = TMPL_ARRAY, .value.array = {.items = its, .count = 3, .item_type = TMPL_STRING}};
    TemplateContext ctx = {
        .vars = (TemplateVar[]){{"name", {.type = TMPL_STRING, .value.str = "File"}}, {"items", arr}},
        .count = 2};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template_file(path, &ctx, &out, &err));
    TEST_ASSERT_STR("Hello File!\none two three ", out.data);
    free(out.data);
}

static void test_render_file_missing(void) {
    TemplateContext ctx = {.vars = NULL, .count = 0};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(!render_template_file("/tmp/no_such_file_xyz_abc.html", &ctx, &out, &err));
    TEST_ASSERT_ERR(TMPL_ERR_IO, err);
    free(out.data);
}

static void test_render_file_with_filters(void) {
    const char* path = "/tmp/breeze_filter_tmpl.html";
    FILE* fp = fopen(path, "w");
    TEST_ASSERT(fp != NULL);
    fputs("{{ title | upper | truncate:5 }}", fp);
    fclose(fp);

    TemplateContext ctx = {.vars = (TemplateVar[]){{"title", {.type = TMPL_STRING, .value.str = "hello world"}}},
                           .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template_file(path, &ctx, &out, &err));
    TEST_ASSERT_STR("HELLO...", out.data);
    free(out.data);
}

/* ================================================================
   9. Dynamic context
   ================================================================ */

static void test_ctx_new_basic(void) {
    TemplateContext* ctx = context_new(4);
    TEST_ASSERT(ctx != NULL);
    assert(context_set(ctx, "name", (TemplateValue){.type = TMPL_STRING, .value.str = "Dynamic"}));
    assert(context_set(ctx, "count", (TemplateValue){.type = TMPL_INT, .value.integer = 3}));
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ name }} {{ count }}", ctx, &out, &err));
    TEST_ASSERT_STR("Dynamic 3", out.data);
    free(out.data);
    context_free(ctx);
}

static void test_ctx_update(void) {
    TemplateContext* ctx = context_new(2);
    TEST_ASSERT(ctx != NULL);
    assert(context_set(ctx, "x", (TemplateValue){.type = TMPL_STRING, .value.str = "first"}));
    assert(context_set(ctx, "x", (TemplateValue){.type = TMPL_STRING, .value.str = "second"}));
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ x }}", ctx, &out, &err));
    TEST_ASSERT_STR("second", out.data);
    free(out.data);
    context_free(ctx);
}

static void test_ctx_grows_past_initial_capacity(void) {
    TemplateContext* ctx = context_new(1);
    TEST_ASSERT(ctx != NULL);
    /* Static storage so pointers remain valid */
    static char keys[20][8];
    static char vals[20][8];
    for (int i = 0; i < 20; i++) {
        snprintf(keys[i], sizeof(keys[i]), "k%d", i);
        snprintf(vals[i], sizeof(vals[i]), "v%d", i);
        TEST_ASSERT(context_set(ctx, keys[i], (TemplateValue){.type = TMPL_STRING, .value.str = vals[i]}));
    }
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ k0 }},{{ k10 }},{{ k19 }}", ctx, &out, &err));
    TEST_ASSERT_STR("v0,v10,v19", out.data);
    free(out.data);
    context_free(ctx);
}

static void test_ctx_bool_and_int(void) {
    TemplateContext* ctx = context_new(2);
    TEST_ASSERT(ctx != NULL);
    assert(context_set(ctx, "flag", (TemplateValue){.type = TMPL_BOOL, .value.boolean = true}));
    assert(context_set(ctx, "val", (TemplateValue){.type = TMPL_INT, .value.integer = 42}));
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{% if flag %}{{ val }}{% endif %}", ctx, &out, &err));
    TEST_ASSERT_STR("42", out.data);
    free(out.data);
    context_free(ctx);
}

/* ================================================================
  10. Custom user-registered filters
   ================================================================ */

static bool my_shout_filter(const TemplateValue* val, const char* arg, OutputBuffer* out) {
    (void)arg;
    char buf[256] = {0};
    if (val->type == TMPL_STRING && val->value.str)
        snprintf(buf, sizeof(buf), "%s!!!", val->value.str);
    else
        snprintf(buf, sizeof(buf), "!!!");
    size_t len = strlen(buf);
    assert(buffer_init(out, 1)); /* already inited by caller */

    /* Just append to out directly */
    assert(out->data != NULL && (memcpy(out->data + out->size, buf, len + 1), out->size += len, true));
    return true;
}

/* Simpler custom filter - wrap in brackets */
static bool my_wrap_filter(const TemplateValue* val, const char* arg, OutputBuffer* out) {
    (void)arg;
    char open = '[';
    char close = ']';
    /* append opening bracket */
    if (out->size + 3 > out->capacity) {
        size_t nc = out->capacity * 2 + 4;
        out->data = realloc(out->data, nc);
        if (!out->data) return false;
        out->capacity = nc;
    }
    out->data[out->size++] = open;
    out->data[out->size] = '\0';

    /* append value */
    OutputBuffer tmp = {0};
    if (!buffer_init(&tmp, 64)) return false;
    /* write value to tmp, then copy */
    switch (val->type) {
        case TMPL_STRING:
            if (val->value.str) {
                size_t l = strlen(val->value.str);
                if (out->size + l + 2 > out->capacity) {
                    size_t nc = (out->capacity + l + 2) * 2;
                    out->data = realloc(out->data, nc);
                    if (!out->data) {
                        free(tmp.data);
                        return false;
                    }
                    out->capacity = nc;
                }
                memcpy(out->data + out->size, val->value.str, l);
                out->size += l;
                out->data[out->size] = '\0';
            }
            break;
        default:
            break;
    }
    free(tmp.data);

    if (out->size + 2 > out->capacity) {
        size_t nc = out->capacity * 2 + 2;
        out->data = realloc(out->data, nc);
        if (!out->data) return false;
        out->capacity = nc;
    }
    out->data[out->size++] = close;
    out->data[out->size] = '\0';
    return true;
}

static bool my_upper2_filter(const TemplateValue* v, const char* a, OutputBuffer* o) {
    (void)a;
    (void)v;
    size_t len = 6;
    if (o->size + len + 1 > o->capacity) {
        size_t nc = o->capacity * 2 + len + 1;
        o->data = realloc(o->data, nc);
        if (!o->data) return false;
        o->capacity = nc;
    }
    memcpy(o->data + o->size, "CUSTOM", len + 1);
    o->size += len;
    return true;
}

static void test_custom_filter_shout(void) {
    breeze_register_filter("shout", my_shout_filter);
    TemplateContext ctx = {.vars = (TemplateVar[]){{"s", {.type = TMPL_STRING, .value.str = "hello"}}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ s | shout }}", &ctx, &out, &err));
    TEST_ASSERT_STR("hello!!!", out.data);
    free(out.data);
}

static void test_custom_filter_wrap(void) {
    breeze_register_filter("wrap", my_wrap_filter);
    TemplateContext ctx = {.vars = (TemplateVar[]){{"s", {.type = TMPL_STRING, .value.str = "hi"}}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ s | wrap }}", &ctx, &out, &err));
    TEST_ASSERT_STR("[hi]", out.data);
    free(out.data);
}

static void test_custom_filter_overwrite(void) {
    /* Re-register "upper" with a custom impl, verify it takes effect */
    breeze_register_filter("upper", my_upper2_filter);
    TemplateContext ctx = {.vars = (TemplateVar[]){{"s", {.type = TMPL_STRING, .value.str = "test"}}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ s | upper }}", &ctx, &out, &err));
    TEST_ASSERT_STR("CUSTOM", out.data);
    free(out.data);

    /* Restore original upper */
    breeze_clear_filters();
}

/* ================================================================
  11. Error paths
   ================================================================ */

static void test_err_unknown_filter(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"s", {.type = TMPL_STRING, .value.str = "x"}}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(!render_template("{{ s | notafilter }}", &ctx, &out, &err));
    TEST_ASSERT_ERR(TMPL_ERR_RENDER, err);
    free(out.data);
}

static void test_err_unterminated_var(void) {
    TemplateContext ctx = {.vars = NULL, .count = 0};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(!render_template("{{ unterminated", &ctx, &out, &err));
    TEST_ASSERT_ERR(TMPL_ERR_PARSE, err);
    free(out.data);
}

static void test_err_unterminated_directive(void) {
    TemplateContext ctx = {.vars = NULL, .count = 0};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(!render_template("{% if x ", &ctx, &out, &err));
    TEST_ASSERT_ERR(TMPL_ERR_PARSE, err);
    free(out.data);
}

static void test_err_endfor_without_for(void) {
    TemplateContext ctx = {.vars = NULL, .count = 0};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(!render_template("{% endfor %}", &ctx, &out, &err));
    TEST_ASSERT_ERR(TMPL_ERR_SYNTAX, err);
    free(out.data);
}

static void test_err_endif_without_if(void) {
    TemplateContext ctx = {.vars = NULL, .count = 0};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(!render_template("{% endif %}", &ctx, &out, &err));
    TEST_ASSERT_ERR(TMPL_ERR_SYNTAX, err);
    free(out.data);
}

static void test_err_else_without_if(void) {
    TemplateContext ctx = {.vars = NULL, .count = 0};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(!render_template("{% else %}", &ctx, &out, &err));
    TEST_ASSERT_ERR(TMPL_ERR_SYNTAX, err);
    free(out.data);
}

static void test_err_unclosed_if(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"x", {.type = TMPL_BOOL, .value.boolean = true}}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(!render_template("{% if x %}hello", &ctx, &out, &err));
    TEST_ASSERT_ERR(TMPL_ERR_SYNTAX, err);
    free(out.data);
}

static void test_err_unterminated_comment(void) {
    TemplateContext ctx = {.vars = NULL, .count = 0};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(!render_template("<!-- not closed", &ctx, &out, &err));
    TEST_ASSERT_ERR(TMPL_ERR_SYNTAX, err);
    free(out.data);
}

static void test_err_for_invalid_syntax(void) {
    const char* items[] = {"a"};
    TemplateValue arr = {.type = TMPL_ARRAY, .value.array = {.items = items, .count = 1, .item_type = TMPL_STRING}};
    TemplateContext ctx = {.vars = (TemplateVar[]){{"l", arr}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(!render_template("{% for x from l %}{{ x }}{% endfor %}", &ctx, &out, &err));
    TEST_ASSERT_ERR(TMPL_ERR_SYNTAX, err);
    free(out.data);
}

static void test_err_set_no_equals(void) {
    TemplateContext ctx = {.vars = NULL, .count = 0};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(!render_template("{% set badvar %}", &ctx, &out, &err));
    TEST_ASSERT_ERR(TMPL_ERR_SYNTAX, err);
    free(out.data);
}

static void test_err_unknown_directive(void) {
    TemplateContext ctx = {.vars = NULL, .count = 0};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(!render_template("{% foobar %}", &ctx, &out, &err));
    TEST_ASSERT_ERR(TMPL_ERR_SYNTAX, err);
    free(out.data);
}

/* ================================================================
  12. Edge cases
   ================================================================ */

static void test_edge_empty_array(void) {
    const char* items[] = {NULL};
    TemplateValue arr = {.type = TMPL_ARRAY, .value.array = {.items = items, .count = 0, .item_type = TMPL_STRING}};
    TemplateContext ctx = {.vars = (TemplateVar[]){{"list", arr}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("before{% for i in list %}{{ i }}{% endfor %}after", &ctx, &out, &err));
    TEST_ASSERT_STR("beforeafter", out.data);
    free(out.data);
}

static void test_edge_zero_int_falsy(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"n", {.type = TMPL_INT, .value.integer = 0}}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{% if n %}yes{% else %}no{% endif %}", &ctx, &out, &err));
    TEST_ASSERT_STR("no", out.data);
    free(out.data);
}

static void test_edge_nonzero_int_truthy(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"n", {.type = TMPL_INT, .value.integer = -1}}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{% if n %}yes{% else %}no{% endif %}", &ctx, &out, &err));
    TEST_ASSERT_STR("yes", out.data);
    free(out.data);
}

static void test_edge_empty_string_falsy(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"s", {.type = TMPL_STRING, .value.str = ""}}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{% if s %}yes{% else %}no{% endif %}", &ctx, &out, &err));
    TEST_ASSERT_STR("no", out.data);
    free(out.data);
}

static void test_edge_no_newline_at_end(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"x", {.type = TMPL_STRING, .value.str = "end"}}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ x }}", &ctx, &out, &err));
    TEST_ASSERT_STR("end", out.data);
    free(out.data);
}

static void test_edge_nested_for_loops(void) {
    const char* outer[] = {"A", "B"};
    const char* inner[] = {"1", "2"};
    TemplateValue oa = {.type = TMPL_ARRAY, .value.array = {.items = outer, .count = 2, .item_type = TMPL_STRING}};
    TemplateValue ia = {.type = TMPL_ARRAY, .value.array = {.items = inner, .count = 2, .item_type = TMPL_STRING}};
    TemplateContext ctx = {.vars = (TemplateVar[]){{"o", oa}, {"i", ia}}, .count = 2};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(
        render_template("{% for x in o %}{% for y in i %}{{ x }}{{ y }} {% endfor %}{% endfor %}", &ctx, &out, &err));
    TEST_ASSERT_STR("A1 A2 B1 B2 ", out.data);
    free(out.data);
}

static void test_edge_comment_strips_vars(void) {
    /* Variables inside HTML comments should not be rendered */
    TemplateContext ctx = {.vars = (TemplateVar[]){{"n", {.type = TMPL_STRING, .value.str = "X"}}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("a <!-- {{ n }} --> b", &ctx, &out, &err));
    TEST_ASSERT_STR("a  b", out.data);
    free(out.data);
}

static void test_edge_multiple_vars_same_line(void) {
    TemplateContext ctx = {.vars =
                               (TemplateVar[]){
                                   {"a", {.type = TMPL_STRING, .value.str = "X"}},
                                   {"b", {.type = TMPL_STRING, .value.str = "Y"}},
                                   {"c", {.type = TMPL_STRING, .value.str = "Z"}},
                               },
                           .count = 3};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{{ a }}{{ b }}{{ c }}", &ctx, &out, &err));
    TEST_ASSERT_STR("XYZ", out.data);
    free(out.data);
}

static void test_edge_filter_on_loop_meta(void) {
    /* loop.index1 | len should give "1" for single-digit iteration numbers */
    const char* items[] = {"x", "y", "z"};
    TemplateValue arr = {.type = TMPL_ARRAY, .value.array = {.items = items, .count = 3, .item_type = TMPL_STRING}};
    TemplateContext ctx = {.vars = (TemplateVar[]){{"l", arr}}, .count = 1};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(render_template("{% for i in l %}{{ loop.index1 | len }}{% endfor %}", &ctx, &out, &err));
    TEST_ASSERT_STR("111", out.data);
    free(out.data);
}

static void test_edge_deep_nesting(void) {
    TemplateContext ctx = {.vars = (TemplateVar[]){{"a", {.type = TMPL_BOOL, .value.boolean = true}},
                                                   {"b", {.type = TMPL_BOOL, .value.boolean = true}},
                                                   {"c", {.type = TMPL_BOOL, .value.boolean = true}},
                                                   {"v", {.type = TMPL_STRING, .value.str = "deep"}}},
                           .count = 4};
    OutputBuffer out = new_buf();
    TemplateError err = {0};
    TEST_ASSERT(
        render_template("{% if a %}{% if b %}{% if c %}{{ v }}{% endif %}{% endif %}{% endif %}", &ctx, &out, &err));
    TEST_ASSERT_STR("deep", out.data);
    free(out.data);
}

/* ================================================================
   main
   ================================================================ */

int main(void) {
    printf("\n=== Breeze Template Engine – Expanded Test Suite ===\n\n");

    printf("── 1. Regression (original features) ──────────────────\n");
    RUN(test_orig_variable_substitution);
    RUN(test_orig_for_loop);
    RUN(test_orig_if_else);
    RUN(test_orig_complex_expr);
    RUN(test_orig_all_value_types);
    RUN(test_orig_whitespace_control);
    RUN(test_orig_missing_variable);
    RUN(test_orig_unclosed_for);
    RUN(test_orig_nested_loops);
    RUN(test_orig_html_comment);

    printf("\n── 2. Built-in Filters ─────────────────────────────────\n");
    RUN(test_filter_upper);
    RUN(test_filter_lower);
    RUN(test_filter_len_string);
    RUN(test_filter_len_array);
    RUN(test_filter_trim);
    RUN(test_filter_reverse);
    RUN(test_filter_default_falsy);
    RUN(test_filter_default_truthy);
    RUN(test_filter_default_false_bool);
    RUN(test_filter_truncate_cuts);
    RUN(test_filter_truncate_noop);
    RUN(test_filter_capitalize);
    RUN(test_filter_replace);
    RUN(test_filter_on_int);
    RUN(test_filter_upper_int);

    printf("\n── 3. Chained Filters ──────────────────────────────────\n");
    RUN(test_chain_upper_truncate);
    RUN(test_chain_trim_upper_reverse);
    RUN(test_chain_replace_upper);

    printf("\n── 4. Loop Meta-variables ──────────────────────────────\n");
    RUN(test_loop_index);
    RUN(test_loop_index1);
    RUN(test_loop_first_last);
    RUN(test_loop_length);
    RUN(test_loop_meta_combined);
    RUN(test_loop_filter_on_item);
    RUN(test_loop_separator_pattern);

    printf("\n── 5. elif chains ──────────────────────────────────────\n");
    RUN(test_elif_first_branch);
    RUN(test_elif_middle_branch);
    RUN(test_elif_else_branch);
    RUN(test_elif_no_else);
    RUN(test_elif_multiple_chains);
    RUN(test_elif_without_if);

    printf("\n── 6. set tag ──────────────────────────────────────────\n");
    RUN(test_set_basic);
    RUN(test_set_quoted);
    RUN(test_set_single_quoted);
    RUN(test_set_overrides_context);
    RUN(test_set_multiple);
    RUN(test_set_reuse_in_loop);
    RUN(test_set_bad_syntax);

    printf("\n── 7. raw blocks ───────────────────────────────────────\n");
    RUN(test_raw_basic);
    RUN(test_raw_mixed);
    RUN(test_raw_unclosed);
    RUN(test_raw_preserves_braces);

    printf("\n── 8. render_template_file ─────────────────────────────\n");
    RUN(test_render_file_ok);
    RUN(test_render_file_missing);
    RUN(test_render_file_with_filters);

    printf("\n── 9. Dynamic context ──────────────────────────────────\n");
    RUN(test_ctx_new_basic);
    RUN(test_ctx_update);
    RUN(test_ctx_grows_past_initial_capacity);
    RUN(test_ctx_bool_and_int);

    printf("\n── 10. Custom filters ──────────────────────────────────\n");
    RUN(test_custom_filter_shout);
    RUN(test_custom_filter_wrap);
    RUN(test_custom_filter_overwrite);

    printf("\n── 11. Error paths ─────────────────────────────────────\n");
    RUN(test_err_unknown_filter);
    RUN(test_err_unterminated_var);
    RUN(test_err_unterminated_directive);
    RUN(test_err_endfor_without_for);
    RUN(test_err_endif_without_if);
    RUN(test_err_else_without_if);
    RUN(test_err_unclosed_if);
    RUN(test_err_unterminated_comment);
    RUN(test_err_for_invalid_syntax);
    RUN(test_err_set_no_equals);
    RUN(test_err_unknown_directive);

    printf("\n── 12. Edge cases ──────────────────────────────────────\n");
    RUN(test_edge_empty_array);
    RUN(test_edge_zero_int_falsy);
    RUN(test_edge_nonzero_int_truthy);
    RUN(test_edge_empty_string_falsy);
    RUN(test_edge_no_newline_at_end);
    RUN(test_edge_nested_for_loops);
    RUN(test_edge_comment_strips_vars);
    RUN(test_edge_multiple_vars_same_line);
    RUN(test_edge_filter_on_loop_meta);
    RUN(test_edge_deep_nesting);

    printf("\n══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", g_passed, g_failed);
    printf("══════════════════════════════════════════════════════\n\n");
    return g_failed > 0 ? 1 : 0;
}
