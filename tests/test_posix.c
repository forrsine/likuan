#include "regex_engine.h"

#include <regex.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
static size_t comparisons = 0;

static void compare_operation(const char *pattern, const char *text, int full_match)
{
    regex_t posix;
    int posix_compile = regcomp(&posix, pattern, REG_EXTENDED);
    if (posix_compile != 0) {
        printf("FAIL POSIX compile /%s/: rc=%d\n", pattern, posix_compile);
        ++failures;
        return;
    }

    regmatch_t posix_match = {-1, -1};
    int posix_rc = regexec(&posix, text, 1, &posix_match, 0);
    int expected_rc = posix_rc == 0 ? RX_OK : RX_NOMATCH;
    if (full_match &&
        (posix_rc != 0 || posix_match.rm_so != 0 ||
         posix_match.rm_eo != (regoff_t)strlen(text))) {
        expected_rc = RX_NOMATCH;
    }
    for (unsigned flags = RX_FLAG_NONE; flags <= RX_FLAG_DFA; ++flags) {
        rx_regex_t *re = NULL;
        int compile_rc = regex_compile(&re, pattern, flags);
        rx_match_t actual = {-1, -1};
        int actual_rc = compile_rc == RX_OK
                            ? (full_match
                                   ? regex_match(re, text, &actual, 1)
                                   : regex_search(re, text, &actual, 1))
                            : compile_rc;
        if (actual_rc != expected_rc ||
            (expected_rc == RX_OK &&
             (actual.rm_so != (int)posix_match.rm_so ||
              actual.rm_eo != (int)posix_match.rm_eo))) {
            printf("FAIL POSIX compare mode=%s op=%s /%s/ text='%s': "
                   "ours=%d[%d,%d] posix=%d[%ld,%ld]\n",
                   flags == RX_FLAG_DFA ? "DFA" : "NFA",
                   full_match ? "match" : "search",
                   pattern,
                   text,
                   actual_rc,
                   actual.rm_so,
                   actual.rm_eo,
                   posix_rc,
                   (long)posix_match.rm_so,
                   (long)posix_match.rm_eo);
            ++failures;
        }
        ++comparisons;
        regex_free(re);
    }
    regfree(&posix);
}

int main(void)
{
    const char *patterns[] = {
        "a", "a|b", "ab*", "(ab|c)+", "[a-c]{1,3}",
        "a*", "a|aa", "[^x]+", "^a$", "a$", "^$",
        "([a-z]+)([0-9]+)", ".", "a?", "ab?c", "a{2}",
        "a{2,}", "colou?r", "[A-Za-z_][A-Za-z0-9_]*", "(a|ab)b"
    };
    const char *texts[] = {
        "", "a", "b", "aa", "abbb", "cc", "123a",
        "xyz", "cab", "abab", "--abc123!", "ac", "abc",
        "aaab", "color", "colour", "value_123"
    };
    for (size_t p = 0; p < sizeof(patterns) / sizeof(patterns[0]); ++p) {
        for (size_t t = 0; t < sizeof(texts) / sizeof(texts[0]); ++t) {
            compare_operation(patterns[p], texts[t], 0);
            compare_operation(patterns[p], texts[t], 1);
        }
    }
    if (failures != 0) {
        printf("%d POSIX comparison test(s) failed\n", failures);
        return 1;
    }
    printf("POSIX comparison tests passed (%zu/%zu, 100%%)\n",
           comparisons, comparisons);
    return 0;
}
