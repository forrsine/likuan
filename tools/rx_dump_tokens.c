#include "lexer.h"

#include "regex_engine.h"

#include <ctype.h>
#include <stdio.h>

static void print_char(unsigned char c)
{
    switch (c) {
    case '\n': fputs("\\n", stdout); return;
    case '\r': fputs("\\r", stdout); return;
    case '\t': fputs("\\t", stdout); return;
    case '\\': fputs("\\\\", stdout); return;
    case ']': fputs("\\]", stdout); return;
    case '-': fputs("\\-", stdout); return;
    default:
        if (isprint(c)) {
            putchar(c);
        } else {
            printf("\\x%02X", (unsigned)c);
        }
    }
}

static void print_class(const unsigned char cls[RX_CHARSET_BYTES])
{
    putchar('[');
    for (unsigned int i = 0; i < 256;) {
        if (!rx_charset_has(cls, (unsigned char)i)) {
            ++i;
            continue;
        }
        unsigned int end = i;
        while (end + 1 < 256 && rx_charset_has(cls, (unsigned char)(end + 1))) {
            ++end;
        }
        print_char((unsigned char)i);
        if (end >= i + 2) {
            putchar('-');
            print_char((unsigned char)end);
        } else if (end == i + 1) {
            print_char((unsigned char)end);
        }
        i = end + 1;
    }
    putchar(']');
}

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
            print_class(token.cls);
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
