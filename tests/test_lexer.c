#include "lexer.h"

#include "regex_engine.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int condition, const char *message)
{
    if (!condition) {
        printf("FAIL %s\n", message);
        ++failures;
    }
}

static rx_token_t next_token(rx_lexer_t *lexer, rx_token_type_t expected, size_t expected_pos)
{
    rx_token_t token;
    int rc = rx_lexer_next(lexer, &token);
    if (rc != RX_OK || token.type != expected || token.pos != expected_pos) {
        printf("FAIL token: rc=%d type=%s pos=%zu, expected %s at %zu\n",
               rc, rc == RX_OK ? rx_token_type_name(token.type) : "ERROR", token.pos,
               rx_token_type_name(expected), expected_pos);
        ++failures;
    }
    return token;
}

static void test_token_stream(void)
{
    rx_lexer_t lexer;
    rx_lexer_init(&lexer, "([a-c]+)\\d{2,4}");

    next_token(&lexer, TOK_LPAREN, 0);
    rx_token_t cls = next_token(&lexer, TOK_CLASS, 1);
    check(rx_charset_has(cls.cls, 'a') && rx_charset_has(cls.cls, 'b') &&
              rx_charset_has(cls.cls, 'c') && !rx_charset_has(cls.cls, 'd'),
          "character range token");
    next_token(&lexer, TOK_PLUS, 6);
    next_token(&lexer, TOK_RPAREN, 7);
    cls = next_token(&lexer, TOK_CLASS, 8);
    check(rx_charset_has(cls.cls, '0') && rx_charset_has(cls.cls, '9') &&
              !rx_charset_has(cls.cls, 'a'),
          "digit escape token");
    rx_token_t repeat = next_token(&lexer, TOK_REPEAT, 10);
    check(repeat.repeat_min == 2 && repeat.repeat_max == 4 && !repeat.repeat_unbounded,
          "bounded repeat token");
    next_token(&lexer, TOK_EOF, 15);
}

static void test_unbounded_repeat(void)
{
    rx_lexer_t lexer;
    rx_lexer_init(&lexer, "a{2,}");
    next_token(&lexer, TOK_CLASS, 0);
    rx_token_t repeat = next_token(&lexer, TOK_REPEAT, 1);
    check(repeat.repeat_min == 2 && repeat.repeat_unbounded, "unbounded repeat token");
}

static void test_lexer_errors(void)
{
    const char *patterns[] = {"[z-a]", "[abc", "a{3,2}", "a{10001}"};
    const int expected[] = {RX_BADPAT, RX_EBRACK, RX_BADRPT, RX_BADRPT};

    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i) {
        rx_lexer_t lexer;
        rx_token_t token;
        int rc = RX_OK;
        rx_lexer_init(&lexer, patterns[i]);
        while (rc == RX_OK) {
            rc = rx_lexer_next(&lexer, &token);
            if (rc == RX_OK && token.type == TOK_EOF) {
                break;
            }
        }
        if (rc != expected[i] || strstr(lexer.error, "byte") == NULL) {
            printf("FAIL lexer error /%s/: rc=%d message='%s'\n", patterns[i], rc, lexer.error);
            ++failures;
        }
    }
}

int main(void)
{
    test_token_stream();
    test_unbounded_repeat();
    test_lexer_errors();
    if (failures != 0) {
        printf("%d lexer test(s) failed\n", failures);
        return 1;
    }
    printf("lexer tests passed\n");
    return 0;
}
