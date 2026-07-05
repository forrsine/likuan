#include "regex_engine.h"

#include <regex.h>
#include <stdio.h>

static int failures = 0;

static void compare_search(const char *pattern, const char *text)
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
    for (unsigned flags = RX_FLAG_NONE; flags <= RX_FLAG_DFA; ++flags) {
        rx_regex_t *re = NULL;
        int compile_rc = regex_compile(&re, pattern, flags);
        rx_match_t actual = {-1, -1};
        int actual_rc = compile_rc == RX_OK
                            ? regex_search(re, text, &actual, 1)
                            : compile_rc;
        int expected_rc = posix_rc == 0 ? RX_OK : RX_NOMATCH;
        if (actual_rc != expected_rc ||
            (expected_rc == RX_OK &&
             (actual.rm_so != (int)posix_match.rm_so ||
              actual.rm_eo != (int)posix_match.rm_eo))) {
            printf("FAIL POSIX compare mode=%s /%s/ text='%s': "
                   "ours=%d[%d,%d] posix=%d[%ld,%ld]\n",
                   flags == RX_FLAG_DFA ? "DFA" : "NFA",
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
        regex_free(re);
    }
    regfree(&posix);
}

int main(void)
{
    const char *patterns[] = {
        "a", "a|b", "ab*", "(ab|c)+", "[a-c]{1,3}",
        "a*", "a|aa", "[^x]+", "^a$", "a$", "^$",
        "([a-z]+)([0-9]+)"
    };
    const char *texts[] = {
        "", "a", "b", "aa", "abbb", "cc", "123a",
        "xyz", "cab", "abab", "--abc123!"
    };
    for (size_t p = 0; p < sizeof(patterns) / sizeof(patterns[0]); ++p) {
        for (size_t t = 0; t < sizeof(texts) / sizeof(texts[0]); ++t) {
            compare_search(patterns[p], texts[t]);
        }
    }
    if (failures != 0) {
        printf("%d POSIX comparison test(s) failed\n", failures);
        return 1;
    }
    printf("POSIX comparison tests passed\n");
    return 0;
}
