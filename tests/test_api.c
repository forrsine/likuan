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
                           size_t expected_count)
{
    rx_regex_t *re = NULL;
    int rc = regex_compile(&re, pattern, RX_FLAG_NONE);
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
    {
        const rx_match_t expected[] = {{0, 2}, {3, 5}};
        expect_findall("a{2}", "aabaa", expected, 2);
    }
    {
        const rx_match_t expected[] = {{0, 0}, {1, 1}, {2, 2}, {3, 3}};
        expect_findall("a{0}", "bbb", expected, 4);
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

    if (failures != 0) {
        printf("%d test(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
