#include "dot.h"
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

static int build_automata(const char *pattern, nfa_t *nfa, dfa_t *dfa)
{
    char error[RX_ERROR_SIZE];
    int status = RX_BADPAT;
    ast_node_t *ast = rx_parse_pattern(pattern, error, &status, NULL);
    if (ast == NULL) {
        printf("FAIL DOT parse /%s/: %s\n", pattern, error);
        ++failures;
        return status;
    }

    nfa_init(nfa);
    dfa_init(dfa);
    frag_t fragment;
    int rc = nfa_compile_ast(nfa, ast, &fragment);
    if (rc == RX_OK) {
        nfa->start = fragment.start;
        nfa->accept = fragment.accept;
        rc = dfa_build(dfa, nfa);
    }
    if (rc == RX_OK) {
        rc = dfa_minimize(dfa);
    }
    if (rc != RX_OK) {
        printf("FAIL DOT automata /%s/: rc=%d\n", pattern, rc);
        ++failures;
    }
    ast_free(ast);
    return rc;
}

static int read_stream(FILE *file, char *buffer, size_t capacity)
{
    rewind(file);
    size_t size = fread(buffer, 1, capacity - 1, file);
    buffer[size] = '\0';
    return ferror(file) ? -1 : 0;
}

static FILE *open_temp(void)
{
    FILE *file = NULL;
#ifdef _MSC_VER
    if (tmpfile_s(&file) != 0) {
        return NULL;
    }
#else
    file = tmpfile();
#endif
    return file;
}

static void test_nfa_dot(void)
{
    nfa_t nfa;
    dfa_t dfa;
    if (build_automata("(a|b)*", &nfa, &dfa) != RX_OK) {
        return;
    }
    FILE *file = open_temp();
    char buffer[16384];
    if (file == NULL || nfa_dump_dot(&nfa, file) != 0 ||
        read_stream(file, buffer, sizeof(buffer)) != 0) {
        check(0, "NFA DOT stream");
    } else {
        check(strstr(buffer, "digraph NFA {") != NULL, "NFA DOT graph");
        check(strstr(buffer, "shape=doublecircle") != NULL, "NFA accept shape");
        check(strstr(buffer, "label=\"eps\"") != NULL, "NFA epsilon label");
        check(strstr(buffer, "group 1 begin") != NULL &&
                  strstr(buffer, "group 1 end") != NULL,
              "NFA capture labels");
        check(strstr(buffer, "start -> q") != NULL, "NFA start arrow");
    }
    if (file != NULL) {
        fclose(file);
    }
    dfa_free(&dfa);
    nfa_free(&nfa);
}

static void test_dfa_dot(void)
{
    nfa_t nfa;
    dfa_t dfa;
    if (build_automata("a|b", &nfa, &dfa) != RX_OK) {
        return;
    }
    FILE *file = open_temp();
    char buffer[16384];
    if (file == NULL || dfa_dump_dot(&dfa, file) != 0 ||
        read_stream(file, buffer, sizeof(buffer)) != 0) {
        check(0, "DFA DOT stream");
    } else {
        check(strstr(buffer, "digraph MinDFA {") != NULL, "DFA DOT graph");
        check(strstr(buffer, "label=\"begin+end\"") != NULL,
              "DFA boundary starts");
        check(strstr(buffer, "label=\"[ab]\"") != NULL, "DFA merged edge");
        check(strstr(buffer, "shape=doublecircle") != NULL, "DFA accept shape");
    }
    if (file != NULL) {
        fclose(file);
    }
    dfa_free(&dfa);
    nfa_free(&nfa);
}

static void test_dot_escaping(void)
{
    const char *patterns[] = {"\"", "\\\\", "\\n"};
    const char *needles[] = {"\\\"", "\\\\", "\\\\n"};
    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i) {
        nfa_t nfa;
        dfa_t dfa;
        if (build_automata(patterns[i], &nfa, &dfa) != RX_OK) {
            continue;
        }
        FILE *file = open_temp();
        char buffer[4096];
        if (file == NULL || nfa_dump_dot(&nfa, file) != 0 ||
            read_stream(file, buffer, sizeof(buffer)) != 0) {
            check(0, "escaped DOT stream");
        } else {
            check(strstr(buffer, needles[i]) != NULL, "escaped DOT label");
        }
        if (file != NULL) {
            fclose(file);
        }
        dfa_free(&dfa);
        nfa_free(&nfa);
    }
}

int main(void)
{
    test_nfa_dot();
    test_dfa_dot();
    test_dot_escaping();
    if (failures != 0) {
        printf("%d DOT test(s) failed\n", failures);
        return 1;
    }
    printf("DOT tests passed\n");
    return 0;
}
