
# Breeze Template Engine 🌬️

[![Build Status](https://github.com/abiiranathan/breeze/actions/workflows/ci.yml/badge.svg)](https://github.com/abiiranathan/breeze/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)

A lightweight, cross-platform template engine for C with a focus on simplicity and performance. Inspired by Django/Jinja2 syntax but designed for embedded systems and high-performance applications.

## Features ✨

- **Clean syntax** with `{{ variables }}` and `{% tags %}`
- **Cross-platform** (Linux, macOS, Windows)
- **Zero dependencies** (just C 99 standard library)
- **Type-safe** value system (strings, numbers, arrays, booleans)
- **Control structures**:
  - Conditionals (`if/else/endif`)
  - Loops (`for x in list`)
- **Memory efficient** with buffer reuse
- **Error reporting** for syntax error, memory allocation failure and missing variables.

## Installation 📦

### Unix-like Systems (Linux/macOS)

```sh
git clone https://github.com/abiiranathan/breeze.git
cd breeze
make
sudo make install
```

### Windows (MinGW/MSYS2)

```sh
git clone https://github.com/abiiranathan/breeze.git
cd breeze
make
make install PREFIX="C:\Program Files\Breeze"
```

## Quick Start 🚀

```c
#include <breeze.h>

int main() {
    TemplateContext ctx = {
        .vars = {
            {"name", {TMPL_STRING, .value.str = "World"}},
            {"items", {TMPL_ARRAY, .value.array = {.items = fruits, .count = 3, .item_type = TMPL_STRING}}}
        },
        .count = 2
    };

    const char* template = "Hello {{ name }}!\n"
                          "{% for item in items %}"
                          "- {{ item }}\n"
                          "{% endfor %}";

    OutputBuffer out;
    TemplateError err;
    
    if (render_template(template, &ctx, &out, &err)) {
        printf("%s", out.data);
        free(out.data);
    } else {
        fprintf(stderr, "Error: %s (line %zu)\n", err.message, err.line);
    }
    
    return 0;
}
```

## Documentation 📚

### Template Syntax

| Syntax             | Description           |
| ------------------ | --------------------- |
| `{{ var }}`        | Variable substitution |
| `{% if x %}`       | Conditional block     |
| `{% for x in y %}` | Loop over array       |
| `<!-- comment -->` | Template comments     |

### API Reference

```c
#include <breeze.h>

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

    char template[2048];
    FILE* fp = fopen("template.html", "r");
    if (!fp) {
        return -1;
    }

    ssize_t n = fread(template, 1, sizeof(template) - 1, fp);
    if (n <= 0) {
        fclose(fp);
        return -2;
    }
    template[n] = '\0';

    // --- Render ---
    OutputBuffer out;
    TemplateError err = {0};
    if (!buffer_init(&out, 2048)) {
        exit(1);
    };

    if (render_template(template, &ctx, &out, &err)) {
        printf("%s", out.data);
    } else {
        fprintf(stderr, "\n--- Render Failed ---\n");
        fprintf(stderr, "Error on line %zu: %s\n", err.line, err.message);
    }

    free(out.data);
    return 0;
}

```


## Contributing 🤝

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing`)
5. Open a Pull Request

## License 📜

MIT License - See [LICENSE](LICENSE) for details.
