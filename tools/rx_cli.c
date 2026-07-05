#include "regex_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    unsigned flags = RX_FLAG_NONE;
    int pattern_arg = 1;
    if (argc == 4 && strcmp(argv[1], "--dfa") == 0) {
        flags = RX_FLAG_DFA;
        pattern_arg = 2;
    } else if (argc != 3) {
        fprintf(stderr, "usage: %s [--dfa] PATTERN TEXT\n", argv[0]);
        return 2;
    }

    rx_regex_t *re = NULL;
    char error[256];
    int rc = regex_compile_ex(&re, argv[pattern_arg], flags, error, sizeof(error));
    if (rc != RX_OK) {
        fprintf(stderr, "compile failed: %s (rc=%d: %s)\n",
                error, rc, regex_status_string(rc));
        return 1;
    }

    size_t match_count = regex_capture_count(re) + 1;
    rx_match_t *matches = (rx_match_t *)malloc(match_count * sizeof(*matches));
    if (matches == NULL) {
        fprintf(stderr, "out of memory\n");
        regex_free(re);
        return 1;
    }
    const char *text = argv[pattern_arg + 1];
    if (flags == RX_FLAG_DFA) {
        printf("mode: DFA\n");
    }
    rc = regex_search(re, text, matches, match_count);
    if (rc == RX_OK) {
        printf("match [%d,%d): ", matches[0].rm_so, matches[0].rm_eo);
        for (int i = matches[0].rm_so; i < matches[0].rm_eo; ++i) {
            putchar(text[i]);
        }
        putchar('\n');
        for (size_t group = 1; group < match_count; ++group) {
            printf("group %zu [%d,%d): ",
                   group, matches[group].rm_so, matches[group].rm_eo);
            if (matches[group].rm_so >= 0) {
                for (int i = matches[group].rm_so; i < matches[group].rm_eo; ++i) {
                    putchar(text[i]);
                }
            } else {
                fputs("<unmatched>", stdout);
            }
            putchar('\n');
        }
    } else if (rc == RX_NOMATCH) {
        printf("no match\n");
    } else {
        printf("search failed: rc=%d\n", rc);
    }

    free(matches);
    regex_free(re);
    return rc == RX_OK || rc == RX_NOMATCH ? 0 : 1;
}
