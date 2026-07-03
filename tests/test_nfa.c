#include "matcher.h"
#include "nfa.h"
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

static int build_nfa(const char *pattern, nfa_t *nfa)
{
    char error[RX_ERROR_SIZE];
    int status = RX_BADPAT;
    ast_node_t *ast = rx_parse_pattern(pattern, error, &status, NULL);
    if (ast == NULL) {
        printf("FAIL parse /%s/: %s\n", pattern, error);
        ++failures;
        return status;
    }

    nfa_init(nfa);
    frag_t fragment;
    int rc = nfa_compile_ast(nfa, ast, &fragment);
    if (rc == RX_OK) {
        nfa->start = fragment.start;
        nfa->accept = fragment.accept;
    } else {
        printf("FAIL NFA build /%s/: rc=%d\n", pattern, rc);
        ++failures;
    }
    ast_free(ast);
    return rc;
}

static void test_literal(void)
{
    nfa_t nfa;
    if (build_nfa("a", &nfa) != RX_OK) {
        return;
    }
    check(nfa.len == 2, "literal state count");
    check(nfa_transition_count(&nfa) == 1, "literal transition count");
    check(nfa.start == 0 && nfa.accept == 1, "literal endpoints");
    check(nfa.states[0].trans.len == 1 && nfa.states[0].trans.items[0].type == TR_CLASS &&
              nfa.states[0].trans.items[0].to == 1 &&
              rx_charset_has(nfa.states[0].trans.items[0].cls, 'a'),
          "literal character transition");
    nfa_free(&nfa);
}

static void test_thompson_shapes(void)
{
    nfa_t nfa;
    if (build_nfa("a|b", &nfa) == RX_OK) {
        check(nfa.len == 6, "alternate state count");
        check(nfa_transition_count(&nfa) == 6, "alternate transition count");
        check(nfa.start == 4 && nfa.accept == 5, "alternate endpoints");
        nfa_free(&nfa);
    }

    if (build_nfa("a*", &nfa) == RX_OK) {
        check(nfa.len == 4, "star state count");
        check(nfa_transition_count(&nfa) == 5, "star transition count");
        check(nfa.start == 2 && nfa.accept == 3, "star endpoints");
        nfa_free(&nfa);
    }
}

static void test_nfa_execution(void)
{
    nfa_t nfa;
    if (build_nfa("a|aa", &nfa) == RX_OK) {
        size_t end = 0;
        int rc = nfa_run_from(&nfa, "aa", 0, &end);
        check(rc == RX_OK && end == 2, "NFA chooses longest match at same start");
        nfa_free(&nfa);
    }

    if (build_nfa("^a$", &nfa) == RX_OK) {
        size_t end = 0;
        check(nfa_run_from(&nfa, "a", 0, &end) == RX_OK && end == 1,
              "NFA anchor match");
        check(nfa_run_from(&nfa, "za", 1, &end) == RX_NOMATCH,
              "begin anchor rejects nonzero start");
        nfa_free(&nfa);
    }
}

static void test_state_table(void)
{
    nfa_t nfa;
    if (build_nfa("a|b", &nfa) != RX_OK) {
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
    if (file == NULL || nfa_dump_table(&nfa, file) != 0) {
        printf("FAIL NFA table setup\n");
        ++failures;
    } else {
        char buffer[2048];
        rewind(file);
        size_t size = fread(buffer, 1, sizeof(buffer) - 1, file);
        buffer[size] = '\0';
        check(strstr(buffer, "states=6 transitions=6") != NULL, "NFA table summary");
        check(strstr(buffer, "start") != NULL && strstr(buffer, "accept") != NULL,
              "NFA table roles");
        check(strstr(buffer, "CLASS") != NULL && strstr(buffer, "[a]") != NULL,
              "NFA table class transition");
        check(strstr(buffer, "EPS") != NULL, "NFA table epsilon transition");
    }
    if (file != NULL) {
        fclose(file);
    }
    nfa_free(&nfa);
}

int main(void)
{
    test_literal();
    test_thompson_shapes();
    test_nfa_execution();
    test_state_table();
    if (failures != 0) {
        printf("%d NFA test(s) failed\n", failures);
        return 1;
    }
    printf("NFA tests passed\n");
    return 0;
}
