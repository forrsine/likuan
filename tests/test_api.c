#include "regex_engine.h"

#include <stdio.h>

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
    expect_match(".", "x", 0, 1);
    expect_match("[a-c]+", "abc", 0, 3);
    expect_no_match("[^a]+", "aaa");
    expect_match("\\d+", "123", 0, 3);
    expect_match("\\w+", "abc_123", 0, 7);
    expect_match("^abc$", "abc", 0, 3);
    expect_no_match("^abc$", "zabc");
    expect_search("abc", "zzabcxx", 2, 5);
    expect_search("a*", "bbb", 0, 0);
    expect_compile_error("(");
    expect_compile_error("[abc");
    expect_compile_error("{1,2}");

    if (failures != 0) {
        printf("%d test(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}

