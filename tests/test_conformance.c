#include "regex_engine.h"

#include <stdio.h>

typedef enum {
    OP_MATCH,
    OP_SEARCH
} operation_t;

typedef struct {
    const char *pattern;
    const char *text;
    operation_t operation;
    int status;
    int start;
    int end;
} conformance_case_t;

static const conformance_case_t cases[] = {
    {"a", "a", OP_MATCH, RX_OK, 0, 1},
    {"a", "b", OP_MATCH, RX_NOMATCH, -1, -1},
    {".", "x", OP_MATCH, RX_OK, 0, 1},
    {".", "\n", OP_MATCH, RX_OK, 0, 1},
    {"a*", "", OP_MATCH, RX_OK, 0, 0},
    {"a*", "aaaa", OP_MATCH, RX_OK, 0, 4},
    {"a+", "", OP_MATCH, RX_NOMATCH, -1, -1},
    {"a+", "aaaa", OP_MATCH, RX_OK, 0, 4},
    {"ab?c", "ac", OP_MATCH, RX_OK, 0, 2},
    {"ab?c", "abc", OP_MATCH, RX_OK, 0, 3},
    {"a|bc", "bc", OP_MATCH, RX_OK, 0, 2},
    {"(ab)+", "abab", OP_MATCH, RX_OK, 0, 4},
    {"[a-c]+", "abc", OP_MATCH, RX_OK, 0, 3},
    {"[^a]+", "bbb", OP_MATCH, RX_OK, 0, 3},
    {"[^a]+", "aaa", OP_MATCH, RX_NOMATCH, -1, -1},
    {"a{2}", "aa", OP_MATCH, RX_OK, 0, 2},
    {"a{2,4}", "aaa", OP_MATCH, RX_OK, 0, 3},
    {"a{2,}", "aaaaa", OP_MATCH, RX_OK, 0, 5},
    {"a{0}", "", OP_MATCH, RX_OK, 0, 0},
    {"^abc$", "abc", OP_MATCH, RX_OK, 0, 3},
    {"^abc$", "zabc", OP_MATCH, RX_NOMATCH, -1, -1},
    {"\\d+", "123", OP_MATCH, RX_OK, 0, 3},
    {"\\D+", "abc", OP_MATCH, RX_OK, 0, 3},
    {"\\D+", "123", OP_MATCH, RX_NOMATCH, -1, -1},
    {"\\w+", "abc_123", OP_MATCH, RX_OK, 0, 7},
    {"\\W+", "!@#", OP_MATCH, RX_OK, 0, 3},
    {"\\s+", " \t\n", OP_MATCH, RX_OK, 0, 3},
    {"\\S+", "abc", OP_MATCH, RX_OK, 0, 3},
    {"\\n\\t\\r", "\n\t\r", OP_MATCH, RX_OK, 0, 3},
    {"\\*\\+\\?", "*+?", OP_MATCH, RX_OK, 0, 3},
    {"()", "", OP_MATCH, RX_OK, 0, 0},
    {"((a)b)", "ab", OP_MATCH, RX_OK, 0, 2},
    {"[a-zA-Z0-9_]+", "Az_09", OP_MATCH, RX_OK, 0, 5},
    {"abc", "zzabcxx", OP_SEARCH, RX_OK, 2, 5},
    {"a*", "bbb", OP_SEARCH, RX_OK, 0, 0},
    {"a+", "bbaaa", OP_SEARCH, RX_OK, 2, 5},
    {"a$", "zza", OP_SEARCH, RX_OK, 2, 3},
    {"^a", "za", OP_SEARCH, RX_NOMATCH, -1, -1},
    {"^a", "abc", OP_SEARCH, RX_OK, 0, 1},
    {"[0-9]{2,4}", "x12345", OP_SEARCH, RX_OK, 1, 5},
    {"(ab|c)+", "zzabccx", OP_SEARCH, RX_OK, 2, 6},
    {"[^x]+", "xxabcx", OP_SEARCH, RX_OK, 2, 5},
    {"\\d+\\w?", "x12a!", OP_SEARCH, RX_OK, 1, 4},
    {"\\s+", "ab \tcd", OP_SEARCH, RX_OK, 2, 4},
    {"z+", "abcdef", OP_SEARCH, RX_NOMATCH, -1, -1}
};

static int run_case(const conformance_case_t *test, unsigned flags)
{
    rx_regex_t *re = NULL;
    int rc = regex_compile(&re, test->pattern, flags);
    if (rc != RX_OK) {
        printf("FAIL compile mode=%s /%s/: rc=%d\n",
               flags == RX_FLAG_DFA ? "DFA" : "NFA", test->pattern, rc);
        return 1;
    }

    rx_match_t match = {-1, -1};
    rc = test->operation == OP_MATCH
             ? regex_match(re, test->text, &match, 1)
             : regex_search(re, test->text, &match, 1);
    int failed = rc != test->status;
    if (!failed && rc == RX_OK) {
        failed = match.rm_so != test->start || match.rm_eo != test->end;
    }
    if (failed) {
        printf("FAIL mode=%s op=%s /%s/ text='%s': "
               "got=%d[%d,%d] expected=%d[%d,%d]\n",
               flags == RX_FLAG_DFA ? "DFA" : "NFA",
               test->operation == OP_MATCH ? "match" : "search",
               test->pattern,
               test->text,
               rc,
               match.rm_so,
               match.rm_eo,
               test->status,
               test->start,
               test->end);
    }
    regex_free(re);
    return failed;
}

int main(void)
{
    int failures = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        failures += run_case(&cases[i], RX_FLAG_NONE);
        failures += run_case(&cases[i], RX_FLAG_DFA);
    }
    size_t executions = sizeof(cases) / sizeof(cases[0]) * 2;
    if (failures != 0) {
        printf("%d of %zu conformance executions failed\n", failures, executions);
        return 1;
    }
    printf("conformance tests passed (%zu executions)\n", executions);
    return 0;
}
