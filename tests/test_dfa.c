#include "dfa.h"
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

static int build_dfa(const char *pattern, nfa_t *nfa, dfa_t *dfa)
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
    dfa_init(dfa);
    frag_t fragment;
    int rc = nfa_compile_ast(nfa, ast, &fragment);
    if (rc == RX_OK) {
        nfa->start = fragment.start;
        nfa->accept = fragment.accept;
        rc = dfa_build(dfa, nfa);
    }
    if (rc != RX_OK) {
        printf("FAIL DFA build /%s/: rc=%d\n", pattern, rc);
        ++failures;
    }
    ast_free(ast);
    return rc;
}

static void test_subset_shapes(void)
{
    nfa_t nfa;
    dfa_t dfa;
    if (build_dfa("a|b", &nfa, &dfa) == RX_OK) {
        check(dfa.len == 3, "alternate DFA state count");
        check(dfa_transition_count(&dfa) == 2, "alternate DFA transition count");
        check(dfa.class_count == 3, "alternate character class count");
        check(dfa_final_override_count(&dfa) == 0, "alternate has no final overrides");
        check(!dfa.states[dfa.start].is_accept, "alternate start is not accepting");
        dfa_free(&dfa);
        nfa_free(&nfa);
    }

    if (build_dfa("a*", &nfa, &dfa) == RX_OK) {
        check(dfa.len == 2, "star DFA state count before minimization");
        check(dfa_transition_count(&dfa) == 2, "star DFA transition count");
        check(dfa.class_count == 2, "star character class count");
        check(dfa.states[dfa.start].is_accept, "star start is accepting");
        dfa_free(&dfa);
        nfa_free(&nfa);
    }

    if (build_dfa("[a-c]+", &nfa, &dfa) == RX_OK) {
        check(dfa.len == 2, "class plus DFA state count");
        check(dfa_transition_count(&dfa) == 6, "class plus byte transition count");
        check(dfa.class_count == 2, "range character class count");
        dfa_free(&dfa);
        nfa_free(&nfa);
    }

    if (build_dfa("([a-z]+)\\d{2,4}", &nfa, &dfa) == RX_OK) {
        check(dfa.class_count == 3, "letters and digits character class count");
        dfa_free(&dfa);
        nfa_free(&nfa);
    }
}

static void test_dfa_execution(void)
{
    nfa_t nfa;
    dfa_t dfa;
    if (build_dfa("a|aa", &nfa, &dfa) == RX_OK) {
        size_t end = 0;
        check(dfa_run_from(&dfa, "aa", 0, &end) == RX_OK && end == 2,
              "DFA chooses longest match at same start");
        check(dfa_run_from(&dfa, "ba", 0, &end) == RX_NOMATCH,
              "DFA reports no match");
        dfa_free(&dfa);
        nfa_free(&nfa);
    }
}

static void test_hopcroft_minimization(void)
{
    nfa_t nfa;
    dfa_t dfa;
    size_t end = 0;

    if (build_dfa("a|b", &nfa, &dfa) == RX_OK) {
        check(dfa.len == 3, "alternate state count before minimization");
        check(dfa_minimize(&dfa) == RX_OK, "alternate minimization succeeds");
        check(dfa.len == 2, "alternate equivalent accept states merge");
        check(dfa.subset_state_count == 3, "alternate subset state count retained");
        check(dfa_run_from(&dfa, "a", 0, &end) == RX_OK && end == 1,
              "minimized alternate matches a");
        check(dfa_run_from(&dfa, "b", 0, &end) == RX_OK && end == 1,
              "minimized alternate matches b");
        check(dfa_minimize(&dfa) == RX_OK && dfa.len == 2,
              "repeated minimization is stable");
        dfa_free(&dfa);
        nfa_free(&nfa);
    }

    if (build_dfa("a*", &nfa, &dfa) == RX_OK) {
        check(dfa_minimize(&dfa) == RX_OK, "star minimization succeeds");
        check(dfa.len == 1, "star accepting states merge");
        check(dfa_run_from(&dfa, "aaaa", 0, &end) == RX_OK && end == 4,
              "minimized star keeps longest match");
        dfa_free(&dfa);
        nfa_free(&nfa);
    }

    if (build_dfa("^a$", &nfa, &dfa) == RX_OK) {
        size_t before = dfa.len;
        check(dfa_minimize(&dfa) == RX_OK, "anchored minimization succeeds");
        check(dfa.len < before, "anchored dead states merge");
        check(dfa_run_from(&dfa, "a", 0, &end) == RX_OK && end == 1,
              "minimized anchors match complete text");
        check(dfa_run_from(&dfa, "za", 1, &end) == RX_NOMATCH,
              "minimized begin anchor rejects offset");
        check(dfa_run_from(&dfa, "ab", 0, &end) == RX_NOMATCH,
              "minimized end anchor rejects early match");
        dfa_free(&dfa);
        nfa_free(&nfa);
    }
}

static void test_anchor_support(void)
{
    nfa_t nfa;
    dfa_t dfa;
    size_t end = 0;

    if (build_dfa("^a$", &nfa, &dfa) == RX_OK) {
        check(dfa_can_build(&nfa), "DFA accepts anchored NFA");
        check(dfa_run_from(&dfa, "a", 0, &end) == RX_OK && end == 1,
              "begin and end anchors match complete text");
        check(dfa_run_from(&dfa, "za", 1, &end) == RX_NOMATCH,
              "begin anchor rejects nonzero start");
        check(dfa_run_from(&dfa, "ab", 0, &end) == RX_NOMATCH,
              "end anchor rejects non-final position");
        check(dfa_final_override_count(&dfa) > 0,
              "end anchor creates final-context transitions");
        dfa_free(&dfa);
        nfa_free(&nfa);
    }

    if (build_dfa("^$", &nfa, &dfa) == RX_OK) {
        check(dfa_run_from(&dfa, "", 0, &end) == RX_OK && end == 0,
              "anchors match empty text");
        check(dfa_run_from(&dfa, "x", 0, &end) == RX_NOMATCH,
              "empty anchored pattern rejects nonempty text");
        dfa_free(&dfa);
        nfa_free(&nfa);
    }

    if (build_dfa("a$", &nfa, &dfa) == RX_OK) {
        check(dfa_run_from(&dfa, "za", 1, &end) == RX_OK && end == 2,
              "end anchor works from search offset");
        check(dfa_run_from(&dfa, "zab", 1, &end) == RX_NOMATCH,
              "end anchor requires text end");
        dfa_free(&dfa);
        nfa_free(&nfa);
    }
}

static void test_dfa_table(void)
{
    nfa_t nfa;
    dfa_t dfa;
    if (build_dfa("a|b", &nfa, &dfa) != RX_OK) {
        return;
    }
    if (dfa_minimize(&dfa) != RX_OK) {
        printf("FAIL DFA table minimization\n");
        ++failures;
        dfa_free(&dfa);
        nfa_free(&nfa);
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
    if (file == NULL || dfa_dump_table(&dfa, file) != 0) {
        printf("FAIL DFA table setup\n");
        ++failures;
    } else {
        char buffer[2048];
        rewind(file);
        size_t size = fread(buffer, 1, sizeof(buffer) - 1, file);
        buffer[size] = '\0';
        check(strstr(buffer, "states=2 subset_states=3 byte_transitions=2") != NULL,
              "DFA table minimization summary");
        check(strstr(buffer, "classes=3 final_overrides=0") != NULL,
              "DFA table compression summary");
        check(strstr(buffer, "start") != NULL && strstr(buffer, "accept") != NULL,
              "DFA table roles");
        check(strstr(buffer, "[ab]") != NULL, "DFA table merged inputs");
    }
    if (file != NULL) {
        fclose(file);
    }
    dfa_free(&dfa);
    nfa_free(&nfa);
}

static void test_nfa_dfa_equivalence(void)
{
    const char *patterns[] = {
        "a", "a|b", "ab*", "(ab|c)+", "[a-c]{1,3}",
        "\\d+\\w?", "a*", "a|aa", "[^x]+", "^a$", "a$", "^$", "(^a|b$)"
    };
    const char *texts[] = {
        "", "a", "b", "aa", "abbb", "cc", "123a", "xyz", "cab", "abab"
    };

    for (size_t p = 0; p < sizeof(patterns) / sizeof(patterns[0]); ++p) {
        rx_regex_t *nfa_re = NULL;
        rx_regex_t *dfa_re = NULL;
        int nfa_compile = regex_compile(&nfa_re, patterns[p], RX_FLAG_NONE);
        int dfa_compile = regex_compile(&dfa_re, patterns[p], RX_FLAG_DFA);
        if (nfa_compile != RX_OK || dfa_compile != RX_OK) {
            printf("FAIL cross-mode compile /%s/: nfa=%d dfa=%d\n",
                   patterns[p], nfa_compile, dfa_compile);
            ++failures;
            regex_free(nfa_re);
            regex_free(dfa_re);
            continue;
        }

        for (size_t t = 0; t < sizeof(texts) / sizeof(texts[0]); ++t) {
            rx_match_t nfa_match = {-1, -1};
            rx_match_t dfa_match = {-1, -1};
            int nfa_rc = regex_match(nfa_re, texts[t], &nfa_match, 1);
            int dfa_rc = regex_match(dfa_re, texts[t], &dfa_match, 1);
            if (nfa_rc != dfa_rc ||
                (nfa_rc == RX_OK && (nfa_match.rm_so != dfa_match.rm_so ||
                                     nfa_match.rm_eo != dfa_match.rm_eo))) {
                printf("FAIL cross-mode match /%s/ text='%s': nfa=%d[%d,%d] dfa=%d[%d,%d]\n",
                       patterns[p], texts[t], nfa_rc, nfa_match.rm_so, nfa_match.rm_eo,
                       dfa_rc, dfa_match.rm_so, dfa_match.rm_eo);
                ++failures;
            }

            nfa_match.rm_so = nfa_match.rm_eo = -1;
            dfa_match.rm_so = dfa_match.rm_eo = -1;
            nfa_rc = regex_search(nfa_re, texts[t], &nfa_match, 1);
            dfa_rc = regex_search(dfa_re, texts[t], &dfa_match, 1);
            if (nfa_rc != dfa_rc ||
                (nfa_rc == RX_OK && (nfa_match.rm_so != dfa_match.rm_so ||
                                     nfa_match.rm_eo != dfa_match.rm_eo))) {
                printf("FAIL cross-mode search /%s/ text='%s': nfa=%d[%d,%d] dfa=%d[%d,%d]\n",
                       patterns[p], texts[t], nfa_rc, nfa_match.rm_so, nfa_match.rm_eo,
                       dfa_rc, dfa_match.rm_so, dfa_match.rm_eo);
                ++failures;
            }
        }
        regex_free(nfa_re);
        regex_free(dfa_re);
    }
}

int main(void)
{
    test_subset_shapes();
    test_dfa_execution();
    test_hopcroft_minimization();
    test_anchor_support();
    test_dfa_table();
    test_nfa_dfa_equivalence();
    if (failures != 0) {
        printf("%d DFA test(s) failed\n", failures);
        return 1;
    }
    printf("DFA tests passed\n");
    return 0;
}
