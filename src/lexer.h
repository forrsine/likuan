#ifndef RX_LEXER_H
#define RX_LEXER_H

#include "charset.h"

#include <stdbool.h>
#include <stddef.h>

#define RX_REPEAT_LIMIT 10000u
#define RX_ERROR_SIZE 256

typedef enum {
    TOK_EOF,
    TOK_CLASS,
    TOK_DOT,
    TOK_ALT,
    TOK_STAR,
    TOK_PLUS,
    TOK_QUESTION,
    TOK_REPEAT,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_ANCHOR_BEGIN,
    TOK_ANCHOR_END
} rx_token_type_t;

typedef struct {
    rx_token_type_t type;
    size_t pos;
    unsigned char cls[RX_CHARSET_BYTES];
    size_t repeat_min;
    size_t repeat_max;
    bool repeat_unbounded;
} rx_token_t;

typedef struct {
    const char *pattern;
    size_t pos;
    char error[RX_ERROR_SIZE];
} rx_lexer_t;

void rx_lexer_init(rx_lexer_t *lexer, const char *pattern);
int rx_lexer_next(rx_lexer_t *lexer, rx_token_t *token);
const char *rx_token_type_name(rx_token_type_t type);

#endif
