#include "parser.h"

#include "regex_engine.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int condition, const char *message)
{
    if (!condition) {
        printf("FAIL %s\n", message);
        ++failures;
    }
}

static ast_node_t *parse_ok(const char *pattern, size_t expected_captures)
{
    char error[RX_ERROR_SIZE];
    int status = RX_BADPAT;
    size_t captures = 0;
    ast_node_t *ast = rx_parse_pattern(pattern, error, &status, &captures);
    if (ast == NULL || status != RX_OK || captures != expected_captures) {
        printf("FAIL parse /%s/: status=%d captures=%zu error='%s'\n",
               pattern, status, captures, error);
        ++failures;
    }
    return ast;
}

static void test_precedence_and_groups(void)
{
    ast_node_t *ast = parse_ok("(ab|c){2,4}", 1);
    if (ast != NULL) {
        check(ast->type == AST_REPEAT && ast->repeat_min == 2 && ast->repeat_max == 4,
              "repeat is AST root");
        check(ast->left != NULL && ast->left->type == AST_GROUP && ast->left->group_id == 1,
              "group id is retained");
        check(ast->left != NULL && ast->left->left != NULL && ast->left->left->type == AST_ALT,
              "alternation precedence inside group");
    }
    ast_free(ast);

    ast = parse_ok("ab|c*", 0);
    if (ast != NULL) {
        check(ast->type == AST_ALT, "alternation has lowest precedence");
        check(ast->left != NULL && ast->left->type == AST_CONCAT, "concatenation precedence");
        check(ast->right != NULL && ast->right->type == AST_STAR, "repeat has highest precedence");
    }
    ast_free(ast);

    ast = parse_ok("((a)b)", 2);
    if (ast != NULL) {
        check(ast->type == AST_GROUP && ast->group_id == 1, "outer group numbered first");
        check(ast->left != NULL && ast->left->type == AST_CONCAT &&
                  ast->left->left != NULL && ast->left->left->type == AST_GROUP &&
                  ast->left->left->group_id == 2,
              "nested group numbering");
    }
    ast_free(ast);
}

static void test_ast_dump(void)
{
    ast_node_t *ast = parse_ok("(a|b){2}", 1);
    if (ast == NULL) {
        return;
    }

    FILE *file = NULL;
#ifdef _MSC_VER
    if (tmpfile_s(&file) != 0) {
        file = NULL;
    }
#else
    file = tmpfile();
#endif
    if (file == NULL || ast_dump(ast, file) != 0) {
        printf("FAIL AST dump setup\n");
        ++failures;
    } else {
        char buffer[512];
        rewind(file);
        size_t size = fread(buffer, 1, sizeof(buffer) - 1, file);
        buffer[size] = '\0';
        check(strstr(buffer, "REPEAT {2}") != NULL, "AST dump repeat");
        check(strstr(buffer, "GROUP #1") != NULL, "AST dump group");
        check(strstr(buffer, "ALT") != NULL, "AST dump alternation");
    }
    if (file != NULL) {
        fclose(file);
    }
    ast_free(ast);
}

static void expect_error(const char *pattern, int expected_status, const char *expected_text)
{
    char error[RX_ERROR_SIZE];
    int status = RX_OK;
    ast_node_t *ast = rx_parse_pattern(pattern, error, &status, NULL);
    if (ast != NULL || status != expected_status || strstr(error, expected_text) == NULL) {
        printf("FAIL parse error /%s/: status=%d error='%s'\n", pattern, status, error);
        ++failures;
    }
    ast_free(ast);
}

int main(void)
{
    test_precedence_and_groups();
    test_ast_dump();
    expect_error("(abc", RX_EPAREN, "byte 0");
    expect_error("a**", RX_BADRPT, "byte 2");
    expect_error("[abc", RX_EBRACK, "byte 0");
    if (failures != 0) {
        printf("%d parser test(s) failed\n", failures);
        return 1;
    }
    printf("parser tests passed\n");
    return 0;
}
