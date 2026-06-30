#include "regex_engine.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s PATTERN TEXT\n", argv[0]);
        return 2;
    }

    rx_regex_t *re = NULL;
    int rc = regex_compile(&re, argv[1], RX_FLAG_NONE);
    if (rc != RX_OK) {
        fprintf(stderr, "compile failed: rc=%d\n", rc);
        return 1;
    }

    rx_match_t m = {-1, -1};
    rc = regex_search(re, argv[2], &m, 1);
    if (rc == RX_OK) {
        printf("match [%d,%d): ", m.rm_so, m.rm_eo);
        for (int i = m.rm_so; i < m.rm_eo; ++i) {
            putchar(argv[2][i]);
        }
        putchar('\n');
    } else if (rc == RX_NOMATCH) {
        printf("no match\n");
    } else {
        printf("search failed: rc=%d\n", rc);
    }

    regex_free(re);
    return rc == RX_OK || rc == RX_NOMATCH ? 0 : 1;
}

