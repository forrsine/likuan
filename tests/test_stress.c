#include "regex_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int condition, const char *message)
{
    if (!condition) {
        printf("FAIL %s\n", message);
        ++failures;
    }
}

static void compare_modes(const char *pattern,
                          const char *text,
                          int expected_status,
                          int expected_so,
                          int expected_eo)
{
    rx_regex_t *nfa = NULL;
    rx_regex_t *dfa = NULL;
    int nfa_compile = regex_compile(&nfa, pattern, RX_FLAG_NONE);
    int dfa_compile = regex_compile(&dfa, pattern, RX_FLAG_DFA);
    check(nfa_compile == RX_OK && dfa_compile == RX_OK, "stress modes compile");
    if (nfa_compile == RX_OK && dfa_compile == RX_OK) {
        rx_match_t nfa_match = {-1, -1};
        rx_match_t dfa_match = {-1, -1};
        int nfa_rc = regex_match(nfa, text, &nfa_match, 1);
        int dfa_rc = regex_match(dfa, text, &dfa_match, 1);
        check(nfa_rc == expected_status && dfa_rc == expected_status,
              "stress modes status");
        if (expected_status == RX_OK) {
            check(nfa_match.rm_so == expected_so && nfa_match.rm_eo == expected_eo,
                  "stress NFA span");
            check(dfa_match.rm_so == expected_so && dfa_match.rm_eo == expected_eo,
                  "stress DFA span");
        }
    }
    regex_free(nfa);
    regex_free(dfa);
}

static void test_catastrophic_backtracking_case(void)
{
    enum { TEXT_LENGTH = 4096 };
    char *text = (char *)malloc(TEXT_LENGTH + 2);
    if (text == NULL) {
        check(0, "stress text allocation");
        return;
    }
    memset(text, 'a', TEXT_LENGTH);
    text[TEXT_LENGTH] = '\0';
    compare_modes("^(a+)+$", text, RX_OK, 0, TEXT_LENGTH);

    text[TEXT_LENGTH] = '!';
    text[TEXT_LENGTH + 1] = '\0';
    compare_modes("^(a+)+$", text, RX_NOMATCH, -1, -1);
    free(text);
}

static void test_many_capture_groups(void)
{
    enum { GROUPS = 64 };
    char pattern[GROUPS * 3 + 1];
    char text[GROUPS + 1];
    rx_match_t matches[GROUPS + 1];
    for (size_t i = 0; i < GROUPS; ++i) {
        pattern[i * 3] = '(';
        pattern[i * 3 + 1] = 'a';
        pattern[i * 3 + 2] = ')';
        text[i] = 'a';
    }
    pattern[GROUPS * 3] = '\0';
    text[GROUPS] = '\0';

    rx_regex_t *re = NULL;
    int rc = regex_compile(&re, pattern, RX_FLAG_NONE);
    check(rc == RX_OK, "many captures compile");
    if (rc == RX_OK) {
        check(regex_capture_count(re) == GROUPS, "many captures count");
        rc = regex_match(re, text, matches, GROUPS + 1);
        check(rc == RX_OK, "many captures match");
        if (rc == RX_OK) {
            for (size_t i = 0; i < GROUPS; ++i) {
                if (matches[i + 1].rm_so != (int)i ||
                    matches[i + 1].rm_eo != (int)i + 1) {
                    check(0, "many captures spans");
                    break;
                }
            }
        }
    }
    regex_free(re);
}

static void test_compile_free_loop(void)
{
    for (size_t i = 0; i < 1000; ++i) {
        rx_regex_t *re = NULL;
        int rc = regex_compile(&re, "([a-z]+)(\\d{2,4})", i % 2 ? RX_FLAG_DFA : RX_FLAG_NONE);
        if (rc != RX_OK) {
            check(0, "compile/free loop");
            regex_free(re);
            return;
        }
        regex_free(re);
    }
}

static void test_stats(void)
{
    rx_regex_t *re = NULL;
    rx_regex_stats_t stats;
    int rc = regex_compile(&re, "a|b", RX_FLAG_DFA);
    check(rc == RX_OK, "stats compile");
    if (rc == RX_OK) {
        check(regex_get_stats(re, &stats) == RX_OK, "stats query");
        check(stats.nfa_states == 6 && stats.nfa_transitions == 6,
              "stats NFA values");
        check(stats.dfa_subset_states == 3 && stats.dfa_states == 2,
              "stats DFA values");
        check(stats.dfa_character_classes == 3, "stats character classes");
    }
    regex_free(re);
    check(regex_get_stats(NULL, &stats) == RX_BADPAT, "stats null regex");
}

int main(void)
{
    test_catastrophic_backtracking_case();
    test_many_capture_groups();
    test_compile_free_loop();
    test_stats();
    if (failures != 0) {
        printf("%d stress test(s) failed\n", failures);
        return 1;
    }
    printf("stress tests passed\n");
    return 0;
}
