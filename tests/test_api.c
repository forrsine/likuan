#include "regex_engine.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void expect_match(const char *pattern, const char *text, int expected_so, int expected_eo)
{
    rx_regex_t *re = NULL;
    int rc = regex_compile(&re, pattern, RX_FLAG_NONE);
    if (rc != RX_OK) {
        printf("FAIL compile /%s/: rc=%d\n", pattern, rc);
        failures++;
        return;
    }

    rx_match_t m = {-1, -1};
    rc = regex_match(re, text, &m, 1);
    if (rc != RX_OK || m.rm_so != expected_so || m.rm_eo != expected_eo) {
        printf("FAIL match /%s/ text='%s': rc=%d span=[%d,%d], expected [%d,%d]\n",
               pattern, text, rc, m.rm_so, m.rm_eo, expected_so, expected_eo);
        failures++;
    }
    regex_free(re);
}

static void expect_no_match(const char *pattern, const char *text)
{
    rx_regex_t *re = NULL;
    int rc = regex_compile(&re, pattern, RX_FLAG_NONE);
    if (rc != RX_OK) {
        printf("FAIL compile /%s/: rc=%d\n", pattern, rc);
        failures++;
        return;
    }

    rc = regex_match(re, text, NULL, 0);
    if (rc != RX_NOMATCH) {
        printf("FAIL no-match /%s/ text='%s': rc=%d\n", pattern, text, rc);
        failures++;
    }
    regex_free(re);
}

static void expect_search(const char *pattern, const char *text, int expected_so, int expected_eo)
{
    rx_regex_t *re = NULL;
    int rc = regex_compile(&re, pattern, RX_FLAG_NONE);
    if (rc != RX_OK) {
        printf("FAIL compile /%s/: rc=%d\n", pattern, rc);
        failures++;
        return;
    }

    rx_match_t m = {-1, -1};
    rc = regex_search(re, text, &m, 1);
    if (rc != RX_OK || m.rm_so != expected_so || m.rm_eo != expected_eo) {
        printf("FAIL search /%s/ text='%s': rc=%d span=[%d,%d], expected [%d,%d]\n",
               pattern, text, rc, m.rm_so, m.rm_eo, expected_so, expected_eo);
        failures++;
    }
    regex_free(re);
}

static void expect_dfa_result(const char *pattern,
                              const char *text,
                              int search,
                              int expected_so,
                              int expected_eo)
{
    rx_regex_t *re = NULL;
    char error[256];
    int rc = regex_compile_ex(&re, pattern, RX_FLAG_DFA, error, sizeof(error));
    if (rc != RX_OK) {
        printf("FAIL DFA compile /%s/: rc=%d error='%s'\n", pattern, rc, error);
        failures++;
        return;
    }

    rx_match_t match = {-1, -1};
    rc = search ? regex_search(re, text, &match, 1) : regex_match(re, text, &match, 1);
    if (rc != RX_OK || match.rm_so != expected_so || match.rm_eo != expected_eo) {
        printf("FAIL DFA %s /%s/ text='%s': rc=%d span=[%d,%d]\n",
               search ? "search" : "match", pattern, text, rc, match.rm_so, match.rm_eo);
        failures++;
    }
    regex_free(re);
}

static void expect_dfa_no_result(const char *pattern, const char *text, int search)
{
    rx_regex_t *re = NULL;
    int rc = regex_compile(&re, pattern, RX_FLAG_DFA);
    if (rc != RX_OK) {
        printf("FAIL DFA compile /%s/: rc=%d\n", pattern, rc);
        failures++;
        return;
    }

    rc = search ? regex_search(re, text, NULL, 0) : regex_match(re, text, NULL, 0);
    if (rc != RX_NOMATCH) {
        printf("FAIL DFA expected no %s /%s/ text='%s': rc=%d\n",
               search ? "search" : "match", pattern, text, rc);
        failures++;
    }
    regex_free(re);
}

static void expect_compile_error(const char *pattern)
{
    rx_regex_t *re = NULL;
    int rc = regex_compile(&re, pattern, RX_FLAG_NONE);
    if (rc == RX_OK) {
        printf("FAIL compile-error /%s/: unexpectedly compiled\n", pattern);
        failures++;
        regex_free(re);
    }
}

static void expect_compile_error_detail(const char *pattern,
                                        unsigned flags,
                                        int expected_status,
                                        const char *expected_text)
{
    rx_regex_t *re = NULL;
    char error[256];
    int rc = regex_compile_ex(&re, pattern, flags, error, sizeof(error));
    if (rc != expected_status || re != NULL || strstr(error, expected_text) == NULL) {
        printf("FAIL compile detail /%s/: rc=%d error='%s'\n", pattern, rc, error);
        failures++;
    }
    regex_free(re);
}

typedef struct {
    rx_match_t matches[16];
    size_t count;
} match_list_t;

static int collect_match(const rx_match_t *matches, size_t nmatch, void *userdata)
{
    match_list_t *list = (match_list_t *)userdata;
    if (nmatch > 0 && list->count < sizeof(list->matches) / sizeof(list->matches[0])) {
        list->matches[list->count++] = matches[0];
    }
    return 1;
}

static void expect_findall(const char *pattern,
                           const char *text,
                           const rx_match_t *expected,
                           size_t expected_count,
                           unsigned flags)
{
    rx_regex_t *re = NULL;
    int rc = regex_compile(&re, pattern, flags);
    if (rc != RX_OK) {
        printf("FAIL compile /%s/: rc=%d\n", pattern, rc);
        failures++;
        return;
    }

    match_list_t actual = {{{0, 0}}, 0};
    rc = regex_findall(re, text, collect_match, &actual);
    if (rc != RX_OK || actual.count != expected_count) {
        printf("FAIL findall /%s/ text='%s': rc=%d count=%zu, expected %zu\n",
               pattern, text, rc, actual.count, expected_count);
        failures++;
    } else {
        for (size_t i = 0; i < expected_count; ++i) {
            if (actual.matches[i].rm_so != expected[i].rm_so ||
                actual.matches[i].rm_eo != expected[i].rm_eo) {
                printf("FAIL findall /%s/ item %zu: span=[%d,%d], expected [%d,%d]\n",
                       pattern, i, actual.matches[i].rm_so, actual.matches[i].rm_eo,
                       expected[i].rm_so, expected[i].rm_eo);
                failures++;
            }
        }
    }
    regex_free(re);
}

int main(void)
{
    expect_match("a", "a", 0, 1);
    expect_no_match("a", "b");
    expect_match("ab", "ab", 0, 2);
    expect_match("a|b", "a", 0, 1);
    expect_match("a|b", "b", 0, 1);
    expect_match("a*", "", 0, 0);
    expect_match("a*", "aaa", 0, 3);
    expect_match("a+", "aaa", 0, 3);
    expect_no_match("a+", "");
    expect_match("ab?c", "ac", 0, 2);
    expect_match("ab?c", "abc", 0, 3);
    expect_match("(ab)+", "abab", 0, 4);
    expect_match("a{3}", "aaa", 0, 3);
    expect_no_match("a{3}", "aa");
    expect_match("a{0}", "", 0, 0);
    expect_no_match("a{0}", "a");
    expect_match("a{2,4}", "aaa", 0, 3);
    expect_match("a{2,4}a", "aaaaa", 0, 5);
    expect_no_match("a{2,4}", "a");
    expect_match("a{2,}", "aaaaa", 0, 5);
    expect_match("a{0,}", "aaaa", 0, 4);
    expect_match("a{0,10000}", "", 0, 0);
    expect_match("(ab){2,3}", "ababab", 0, 6);
    expect_match("[0-9]{2}", "42", 0, 2);
    expect_match("\\{2\\}", "{2}", 0, 3);
    expect_match(".", "x", 0, 1);
    expect_match("[a-c]+", "abc", 0, 3);
    expect_no_match("[^a]+", "aaa");
    expect_match("\\d+", "123", 0, 3);
    expect_match("\\w+", "abc_123", 0, 7);
    expect_match("^abc$", "abc", 0, 3);
    expect_no_match("^abc$", "zabc");
    expect_search("abc", "zzabcxx", 2, 5);
    expect_search("a{2,3}", "zaaax", 1, 4);
    expect_search("a*", "bbb", 0, 0);
    expect_dfa_result("a{2,4}", "aaa", 0, 0, 3);
    expect_dfa_result("a+", "zzaaa", 1, 2, 5);
    expect_dfa_result("^abc$", "abc", 0, 0, 3);
    expect_dfa_result("abc$", "zzabc", 1, 2, 5);
    expect_dfa_result("^$", "", 0, 0, 0);
    expect_dfa_no_result("^abc$", "zabc", 0);
    expect_dfa_no_result("^abc", "zabc", 1);
    expect_dfa_no_result("abc$", "abcx", 1);
    {
        const rx_match_t expected[] = {{0, 2}, {3, 5}};
        expect_findall("a{2}", "aabaa", expected, 2, RX_FLAG_NONE);
    }
    {
        const rx_match_t expected[] = {{0, 0}, {1, 1}, {2, 2}, {3, 3}};
        expect_findall("a{0}", "bbb", expected, 4, RX_FLAG_NONE);
    }
    {
        const rx_match_t expected[] = {{0, 2}, {4, 5}};
        expect_findall("a+", "aa_ba", expected, 2, RX_FLAG_DFA);
    }
    expect_compile_error("(");
    expect_compile_error("[abc");
    expect_compile_error("{1,2}");
    expect_compile_error("a{}");
    expect_compile_error("a{,2}");
    expect_compile_error("a{2");
    expect_compile_error("a{3,2}");
    expect_compile_error("a{2,x}");
    expect_compile_error("a{10001}");
    expect_compile_error("a**");
    expect_compile_error("a{2}?");
    expect_compile_error("[z-a]");
    expect_compile_error("[a-\\d]");
    expect_compile_error_detail("(abc", RX_FLAG_NONE, RX_EPAREN, "byte 0");
    expect_compile_error_detail("[abc", RX_FLAG_NONE, RX_EBRACK, "byte 0");
    expect_compile_error_detail("a{2", RX_FLAG_NONE, RX_EBRACE, "byte 1");
    expect_compile_error_detail("a{3,2}", RX_FLAG_NONE, RX_BADRPT, "byte 1");
    expect_compile_error_detail("a**", RX_FLAG_NONE, RX_BADRPT, "byte 2");
    expect_compile_error_detail("abc", 2u, RX_EUNSUPPORTED, "flags");
    if (strcmp(regex_status_string(RX_BADRPT), "invalid repetition operator") != 0) {
        printf("FAIL status string\n");
        failures++;
    }

    /* --- Capture group tests --- */
    {
        rx_regex_t *re = NULL;
        int rc;

        /* Simple single group */
        rc = regex_compile(&re, "(a)", RX_FLAG_NONE);
        if (rc != RX_OK) {
            printf("FAIL capture compile /(a)/: rc=%d\n", rc);
            failures++;
        } else {
            rx_match_t m[2] = {{-1, -1}, {-1, -1}};
            rc = regex_match(re, "a", m, 2);
            if (rc != RX_OK || m[0].rm_so != 0 || m[0].rm_eo != 1 ||
                m[1].rm_so != 0 || m[1].rm_eo != 1) {
                printf("FAIL capture /(a)/ 'a': rc=%d full=[%d,%d] g1=[%d,%d]\n",
                       rc, m[0].rm_so, m[0].rm_eo, m[1].rm_so, m[1].rm_eo);
                failures++;
            }
        }
        regex_free(re);

        /* Multiple groups */
        rc = regex_compile(&re, "(a)(b)", RX_FLAG_NONE);
        if (rc != RX_OK) {
            printf("FAIL capture compile: rc=%d\n", rc);
            failures++;
        } else {
            rx_match_t m[3] = {{-1, -1}, {-1, -1}, {-1, -1}};
            rc = regex_match(re, "ab", m, 3);
            if (rc != RX_OK || m[0].rm_so != 0 || m[0].rm_eo != 2 ||
                m[1].rm_so != 0 || m[1].rm_eo != 1 ||
                m[2].rm_so != 1 || m[2].rm_eo != 2) {
                printf("FAIL capture /(a)(b)/ 'ab': rc=%d full=[%d,%d] g1=[%d,%d] g2=[%d,%d]\n",
                       rc, m[0].rm_so, m[0].rm_eo, m[1].rm_so, m[1].rm_eo, m[2].rm_so, m[2].rm_eo);
                failures++;
            }
        }
        regex_free(re);

        /* Nested groups */
        rc = regex_compile(&re, "(a(b)c)", RX_FLAG_NONE);
        if (rc != RX_OK) {
            printf("FAIL capture compile /(a(b)c)/: rc=%d\n", rc);
            failures++;
        } else {
            rx_match_t m[3] = {{-1, -1}, {-1, -1}, {-1, -1}};
            rc = regex_match(re, "abc", m, 3);
            if (rc != RX_OK || m[0].rm_so != 0 || m[0].rm_eo != 3 ||
                m[1].rm_so != 0 || m[1].rm_eo != 3 ||
                m[2].rm_so != 1 || m[2].rm_eo != 2) {
                printf("FAIL capture /(a(b)c)/ 'abc': rc=%d full=[%d,%d] g1=[%d,%d] g2=[%d,%d]\n",
                       rc, m[0].rm_so, m[0].rm_eo, m[1].rm_so, m[1].rm_eo, m[2].rm_so, m[2].rm_eo);
                failures++;
            }
        }
        regex_free(re);

        /* Group with + repetition (last iteration wins) */
        rc = regex_compile(&re, "(a)+", RX_FLAG_NONE);
        if (rc != RX_OK) {
            printf("FAIL capture compile /(a)+/: rc=%d\n", rc);
            failures++;
        } else {
            rx_match_t m[2] = {{-1, -1}, {-1, -1}};
            rc = regex_match(re, "aaa", m, 2);
            if (rc != RX_OK || m[0].rm_so != 0 || m[0].rm_eo != 3 ||
                m[1].rm_so != 2 || m[1].rm_eo != 3) {
                printf("FAIL capture /(a)+/ 'aaa': rc=%d full=[%d,%d] g1=[%d,%d]\n",
                       rc, m[0].rm_so, m[0].rm_eo, m[1].rm_so, m[1].rm_eo);
                failures++;
            }
        }
        regex_free(re);

        /* Alternation - first branch matches */
        rc = regex_compile(&re, "(a)|(b)", RX_FLAG_NONE);
        if (rc != RX_OK) {
            printf("FAIL capture compile /(a)|(b)/: rc=%d\n", rc);
            failures++;
        } else {
            rx_match_t m[3] = {{-1, -1}, {-1, -1}, {-1, -1}};
            rc = regex_match(re, "a", m, 3);
            if (rc != RX_OK || m[0].rm_so != 0 || m[0].rm_eo != 1 ||
                m[1].rm_so != 0 || m[1].rm_eo != 1 ||
                m[2].rm_so != -1 || m[2].rm_eo != -1) {
                printf("FAIL capture /(a)|(b)/ 'a': rc=%d full=[%d,%d] g1=[%d,%d] g2=[%d,%d]\n",
                       rc, m[0].rm_so, m[0].rm_eo, m[1].rm_so, m[1].rm_eo, m[2].rm_so, m[2].rm_eo);
                failures++;
            }
            /* Second branch matches */
            m[0].rm_so = m[0].rm_eo = m[1].rm_so = m[1].rm_eo = m[2].rm_so = m[2].rm_eo = -1;
            rc = regex_match(re, "b", m, 3);
            if (rc != RX_OK || m[0].rm_so != 0 || m[0].rm_eo != 1 ||
                m[1].rm_so != -1 || m[1].rm_eo != -1 ||
                m[2].rm_so != 0 || m[2].rm_eo != 1) {
                printf("FAIL capture /(a)|(b)/ 'b': rc=%d full=[%d,%d] g1=[%d,%d] g2=[%d,%d]\n",
                       rc, m[0].rm_so, m[0].rm_eo, m[1].rm_so, m[1].rm_eo, m[2].rm_so, m[2].rm_eo);
                failures++;
            }
        }
        regex_free(re);

        /* Search with groups */
        rc = regex_compile(&re, "(a+)(b+)", RX_FLAG_NONE);
        if (rc != RX_OK) {
            printf("FAIL capture compile /(a+)(b+)/: rc=%d\n", rc);
            failures++;
        } else {
            rx_match_t m[3] = {{-1, -1}, {-1, -1}, {-1, -1}};
            rc = regex_search(re, "xaabb", m, 3);
            if (rc != RX_OK || m[0].rm_so != 1 || m[0].rm_eo != 5 ||
                m[1].rm_so != 1 || m[1].rm_eo != 3 ||
                m[2].rm_so != 3 || m[2].rm_eo != 5) {
                printf("FAIL capture search /(a+)(b+)/ 'xaabb': rc=%d full=[%d,%d] g1=[%d,%d] g2=[%d,%d]\n",
                       rc, m[0].rm_so, m[0].rm_eo, m[1].rm_so, m[1].rm_eo, m[2].rm_so, m[2].rm_eo);
                failures++;
            }
        }
        regex_free(re);

        /* Group with star - captures last iteration */
        rc = regex_compile(&re, "(ab)*", RX_FLAG_NONE);
        if (rc != RX_OK) {
            printf("FAIL capture compile /(ab)*/: rc=%d\n", rc);
            failures++;
        } else {
            rx_match_t m[2] = {{-1, -1}, {-1, -1}};
            rc = regex_match(re, "abab", m, 2);
            if (rc != RX_OK || m[0].rm_so != 0 || m[0].rm_eo != 4 ||
                m[1].rm_so != 2 || m[1].rm_eo != 4) {
                printf("FAIL capture /(ab)*/ 'abab': rc=%d full=[%d,%d] g1=[%d,%d]\n",
                       rc, m[0].rm_so, m[0].rm_eo, m[1].rm_so, m[1].rm_eo);
                failures++;
            }
        }
        regex_free(re);

        /* Group with quantifier - no match should return NOMATCH */
        rc = regex_compile(&re, "(a)", RX_FLAG_NONE);
        if (rc != RX_OK) {
            printf("FAIL capture compile: rc=%d\n", rc);
            failures++;
        } else {
            rx_match_t m[2] = {{-1, -1}, {-1, -1}};
            rc = regex_match(re, "b", m, 2);
            if (rc != RX_NOMATCH) {
                printf("FAIL capture no-match: rc=%d\n", rc);
                failures++;
            }
        }
        regex_free(re);

        /* DFA mode with groups (captures not supported in DFA, only full match) */
        rc = regex_compile(&re, "(ab)+", RX_FLAG_DFA);
        if (rc != RX_OK) {
            printf("FAIL DFA capture compile: rc=%d\n", rc);
            failures++;
        } else {
            rx_match_t m[1] = {{-1, -1}};
            rc = regex_match(re, "abab", m, 1);
            if (rc != RX_OK || m[0].rm_so != 0 || m[0].rm_eo != 4) {
                printf("FAIL DFA capture /(ab)+/ 'abab': rc=%d full=[%d,%d]\n",
                       rc, m[0].rm_so, m[0].rm_eo);
                failures++;
            }
        }
        regex_free(re);

        /* \D \W \S escape sequences */
        expect_match("\\D+", "abc", 0, 3);
        expect_no_match("\\D+", "123");
        expect_match("\\W+", "!@#", 0, 3);
        expect_no_match("\\W+", "abc");
        expect_match("\\S+", "abc", 0, 3);
        expect_no_match("\\S+", " \t");
    }

    if (failures != 0) {
        printf("%d test(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
