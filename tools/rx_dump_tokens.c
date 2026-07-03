#include "lexer.h"

#include "regex_engine.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s PATTERN\n", argv[0]);
        return 2;
    }

    rx_lexer_t lexer;
    rx_token_t token;
    rx_lexer_init(&lexer, argv[1]);
    for (;;) {
        int rc = rx_lexer_next(&lexer, &token);
        if (rc != RX_OK) {
            fprintf(stderr, "lexer failed: %s (rc=%d: %s)\n",
                    lexer.error, rc, regex_status_string(rc));
            return 1;
        }

        printf("%zu\t%s", token.pos, rx_token_type_name(token.type));
        if (token.type == TOK_CLASS) {
            putchar(' ');
            rx_charset_dump(token.cls, stdout);
        } else if (token.type == TOK_REPEAT) {
            if (token.repeat_unbounded) {
                printf(" {%zu,}", token.repeat_min);
            } else if (token.repeat_min == token.repeat_max) {
                printf(" {%zu}", token.repeat_min);
            } else {
                printf(" {%zu,%zu}", token.repeat_min, token.repeat_max);
            }
        }
        putchar('\n');
        if (token.type == TOK_EOF) {
            return 0;
        }
    }
}
