#include "breeze.h"

int main() {
    const char* fruits[] = {"Apple", "Banana", "Cherry"};
    int numbers_data[] = {1, 2, 3, 4, 5};
    MAKE_PTR_ARRAY(numbers_data, int*, number_ptrs);

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
    fclose(fp);

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
