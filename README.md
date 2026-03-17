# Breeze Template Engine

A lightweight template engine for C with Django/Jinja-style syntax.

Breeze is designed for projects that want expressive templates without bringing in heavy dependencies. It supports typed variables, loops, conditionals, filters, template files, custom filters, and dynamic contexts.

## Highlights

- C99, zero third-party dependencies
- Cross-platform build support (Linux, macOS, Windows)
- Typed values: string, int, float, double, bool, long, unsigned int, array
- Template control flow:
  - if / elif / else / endif
  - for / endfor
- Loop metadata:
  - loop.index
  - loop.index1
  - loop.first
  - loop.last
  - loop.length
- Filters:
  - Built-in: upper, lower, len, trim, reverse, default, capitalize, truncate, replace
  - Chained filters
  - User-defined custom filters
- Raw blocks and template-level variable assignment
- Detailed error reporting with line numbers

## Build and Test

### Build

```sh
git clone https://github.com/abiiranathan/breeze.git
cd breeze
make
```

### Run tests

```sh
make test
```

### Install

```sh
sudo make install
```

Custom prefix:

```sh
make install PREFIX=/opt/breeze
```

## Quick Start

```c
#include "breeze.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    const char* fruits[] = {"Apple", "Banana", "Cherry"};

    TemplateVar vars[] = {
        VAR_STRING("name", "Ava"),
        VAR_ARRAY_STR("fruits", fruits),
        VAR_BOOL("show_count", true),
    };

    TemplateContext ctx = {
        .vars = vars,
        .count = sizeof(vars) / sizeof(vars[0]),
    };

    const char* tpl =
        "Hello {{ name | capitalize }}!\n"
        "{% if show_count %}You have {{ fruits | len }} fruits.\n{% endif %}"
        "{% for f in fruits %}- {{ loop.index1 }}. {{ f }}\n{% endfor %}";

    OutputBuffer out;
    TemplateError err = {0};

    if (!buffer_init(&out, 128)) return 1;

    if (!render_template(tpl, &ctx, &out, &err)) {
        fprintf(stderr, "Render failed at line %zu: %s\n", err.line, err.message);
        free(out.data);
        return 1;
    }

    puts(out.data);
    free(out.data);
    return 0;
}
```

## Template Syntax

### Variables

```txt
{{ name }}
{{ price }}
{{ title | upper }}
{{ title | trim | capitalize }}
```

### Conditionals

```txt
{% if is_admin %}
  Admin panel
{% elif is_editor %}
  Editor panel
{% else %}
  User panel
{% endif %}
```

Supported boolean operators:

- and
- or
- not
- Parentheses for grouping

Example:

```txt
{% if is_active and not is_banned %}Allowed{% endif %}
```

### Loops

```txt
{% for item in items %}
  {{ item }}
{% endfor %}
```

Loop metadata inside for blocks:

- loop.index: 0-based index
- loop.index1: 1-based index
- loop.first: true for first element
- loop.last: true for last element
- loop.length: total number of items

Example:

```txt
{% for item in items %}
  {{ loop.index1 }}/{{ loop.length }}: {{ item }}
{% endfor %}
```

### Set variables in template

```txt
{% set greeting = "Hello" %}
{{ greeting }} {{ name }}
```

### Raw blocks

Everything inside raw/endraw is output literally:

```txt
{% raw %}
{{ this_will_not_render }}
{% if x %}also literal{% endif %}
{% endraw %}
```

### HTML comments

HTML comments are ignored by the template renderer:

```txt
Visible <!-- {{ hidden_value }} --> text
```

## Built-in Filters

All filters use this form:

```txt
{{ value | filter }}
{{ value | filter:arg }}
{{ value | filter1 | filter2:arg }}
```

### upper

Converts text to uppercase.

```txt
{{ name | upper }}
```

### lower

Converts text to lowercase.

```txt
{{ name | lower }}
```

### len

Returns length.

- strings: character count
- arrays: item count
- other values: length of string representation

```txt
{{ title | len }}
{{ users | len }}
```

### trim

Trims leading and trailing whitespace.

```txt
{{ note | trim }}
```

### reverse

Reverses the string representation.

```txt
{{ code | reverse }}
```

### default

Uses fallback value when input is falsy.

Falsy values include:

- false
- 0
- empty string
- empty array

```txt
{{ nickname | default:Anonymous }}
```

### capitalize

Uppercases first character and lowercases the rest.

```txt
{{ city | capitalize }}
```

### truncate

Cuts string representation to max length.

```txt
{{ bio | truncate:80 }}
```

### replace

Replaces substrings.

Argument format:

- from:to

```txt
{{ text | replace:world:earth }}
```

## Context and Values

### Static context

Use stack arrays with TemplateVar.

```c
TemplateVar vars[] = {
    VAR_STRING("user", "Nabi"),
    VAR_INT("age", 21),
    VAR_BOOL("premium", true),
};

TemplateContext ctx = {
    .vars = vars,
    .count = sizeof(vars) / sizeof(vars[0]),
};
```

### Arrays

For string arrays:

```c
const char* tags[] = {"c", "templates", "engine"};
TemplateVar v = VAR_ARRAY_STR("tags", tags);
```

For numeric arrays, create pointer arrays:

```c
int nums[] = {10, 20, 30};
MAKE_PTR_ARRAY(nums, int*, num_ptrs);

TemplateVar vars[] = {
    VAR_ARRAY("nums", num_ptrs, TMPL_INT),
};
```

### Dynamic context

When values are assembled at runtime:

```c
TemplateContext* ctx = context_new(8);
if (!ctx) return 1;

context_set(ctx, "name", (TemplateValue){.type = TMPL_STRING, .value.str = "Ava"});
context_set(ctx, "visits", (TemplateValue){.type = TMPL_UINT, .value.uint = 42});

OutputBuffer out;
TemplateError err = {0};
buffer_init(&out, 128);

if (render_template("{{ name }} has {{ visits }} visits", ctx, &out, &err)) {
    puts(out.data);
}

free(out.data);
context_free(ctx);
```

## Rendering from File

```c
OutputBuffer out;
TemplateError err = {0};

buffer_init(&out, 256);

if (!render_template_file("template.html", &ctx, &out, &err)) {
    fprintf(stderr, "Render failed at line %zu: %s\n", err.line, err.message);
    free(out.data);
    return 1;
}

puts(out.data);
free(out.data);
```

## Custom Filters

Register your own filter function:

```c
#include "breeze.h"
#include <stdio.h>

static bool shout_filter(const TemplateValue* val, const char* arg, OutputBuffer* out) {
    (void)arg;

    char buf[256] = {0};
    if (val->type == TMPL_STRING && val->value.str) {
        snprintf(buf, sizeof(buf), "%s!!!", val->value.str);
    } else {
        snprintf(buf, sizeof(buf), "!!!");
    }

    size_t len = strlen(buf);

    /* Ensure output buffer has enough space */
    if (out->size + len + 1 > out->capacity) {
        size_t new_cap = (out->capacity + len + 1) * 2;
        char* new_data = realloc(out->data, new_cap);
        if (!new_data) return false;
        out->data = new_data;
        out->capacity = new_cap;
    }

    memcpy(out->data + out->size, buf, len);
    out->size += len;
    out->data[out->size] = '\0';
    return true;
}

int main(void) {
    breeze_register_filter("shout", shout_filter);
    return 0;
}
```

Notes:

- Registering with an existing name replaces that filter.
- breeze_clear_filters clears the filter registry.
- Built-ins are re-registered automatically when rendering.

## Error Handling

All render functions return bool.

- true: success
- false: failure (see TemplateError)

TemplateError fields:

- type: error category
- message: human-readable description
- line: approximate line number

Error types:

- TMPL_ERR_NONE
- TMPL_ERR_PARSE
- TMPL_ERR_SYNTAX
- TMPL_ERR_RENDER
- TMPL_ERR_MEMORY
- TMPL_ERR_IO

## Memory Management

- Call buffer_init before rendering.
- Always free out.data after use.
- Free dynamic contexts with context_free.
- If you allocate pointer arrays or custom data, free them according to your own ownership model.

## Full API Reference

Public API is declared in breeze.h.

Core types:

- TemplateValue
- TemplateVar
- TemplateContext
- TemplateError
- OutputBuffer

Core functions:

- buffer_init
- render_template
- render_template_file
- context_new
- context_set
- context_free
- breeze_register_filter
- breeze_clear_filters

Convenience macros:

- VAR_STRING
- VAR_INT
- VAR_FLOAT
- VAR_DOUBLE
- VAR_BOOL
- VAR_LONG
- VAR_UINT
- VAR_ARRAY
- VAR_ARRAY_STR
- MAKE_PTR_ARRAY

## Common Pitfalls

- Passing numeric arrays directly to VAR_ARRAY without creating pointer arrays.
- Forgetting to free out.data.
- Using undefined variables in templates.
- Using malformed directives, such as missing endif or endfor.

## License

MIT. See LICENSE.
