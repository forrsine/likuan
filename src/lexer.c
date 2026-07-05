#include "lexer.h"

#include "regex_engine.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void lexer_error(rx_lexer_t *lexer, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(lexer->error, sizeof(lexer->error), fmt, ap);
    va_end(ap);
    lexer->error[sizeof(lexer->error) - 1] = '\0';
}

static int lexer_peek(const rx_lexer_t *lexer)
{
    return (unsigned char)lexer->pattern[lexer->pos];
}

static int lexer_next_char(rx_lexer_t *lexer)
{
    unsigned char c = (unsigned char)lexer->pattern[lexer->pos];
    if (c != '\0') {
        ++lexer->pos;
    }
    return c;
}

static int lex_escape(rx_lexer_t *lexer, unsigned char cls[RX_CHARSET_BYTES])
{
    int c = lexer_next_char(lexer);
    if (c == '\0') {
        lexer_error(lexer, "pattern ends after escape at byte %zu", lexer->pos);
        return RX_BADPAT;
    }

    rx_charset_clear(cls);
    switch (c) {
    case 'd': rx_charset_add_digit(cls); break;
    case 'D': rx_charset_add_digit(cls); rx_charset_invert(cls); break;
    case 'w': rx_charset_add_word(cls); break;
    case 'W': rx_charset_add_word(cls); rx_charset_invert(cls); break;
    case 's': rx_charset_add_space(cls); break;
    case 'S': rx_charset_add_space(cls); rx_charset_invert(cls); break;
    case 'n': rx_charset_add(cls, '\n'); break;
    case 't': rx_charset_add(cls, '\t'); break;
    case 'r': rx_charset_add(cls, '\r'); break;
    default: rx_charset_add(cls, (unsigned char)c); break;
    }
    return RX_OK;
}

static void charset_union(unsigned char dst[RX_CHARSET_BYTES],
                          const unsigned char src[RX_CHARSET_BYTES])
{
    for (size_t i = 0; i < RX_CHARSET_BYTES; ++i) {
        dst[i] |= src[i];
    }
}

static bool charset_single(const unsigned char cls[RX_CHARSET_BYTES], unsigned char *value)
{
    bool found = false;
    for (unsigned int i = 0; i < 256; ++i) {
        if (!rx_charset_has(cls, (unsigned char)i)) {
            continue;
        }
        if (found) {
            return false;
        }
        *value = (unsigned char)i;
        found = true;
    }
    return found;
}

static int lex_class_element(rx_lexer_t *lexer,
                             unsigned char cls[RX_CHARSET_BYTES],
                             bool *single,
                             unsigned char *value)
{
    int c = lexer_next_char(lexer);
    if (c == '\0') {
        lexer_error(lexer, "unclosed character class at byte %zu", lexer->pos);
        return RX_EBRACK;
    }

    if (c == '\\') {
        int rc = lex_escape(lexer, cls);
        if (rc != RX_OK) {
            return rc;
        }
        *single = charset_single(cls, value);
        return RX_OK;
    }

    rx_charset_clear(cls);
    *value = (unsigned char)c;
    *single = true;
    rx_charset_add(cls, *value);
    return RX_OK;
}

static int lex_class(rx_lexer_t *lexer, rx_token_t *token)
{
    bool negate = false;
    bool saw_any = false;
    rx_charset_clear(token->cls);

    if (lexer_peek(lexer) == '^') {
        lexer_next_char(lexer);
        negate = true;
    }

    while (lexer_peek(lexer) != '\0' && lexer_peek(lexer) != ']') {
        unsigned char first_cls[RX_CHARSET_BYTES];
        unsigned char first = 0;
        bool first_single = false;
        int rc = lex_class_element(lexer, first_cls, &first_single, &first);
        if (rc != RX_OK) {
            return rc;
        }

        if (lexer_peek(lexer) == '-' && lexer->pattern[lexer->pos + 1] != ']' &&
            lexer->pattern[lexer->pos + 1] != '\0') {
            size_t range_pos = lexer->pos;
            lexer_next_char(lexer);
            unsigned char last_cls[RX_CHARSET_BYTES];
            unsigned char last = 0;
            bool last_single = false;
            rc = lex_class_element(lexer, last_cls, &last_single, &last);
            if (rc != RX_OK) {
                return rc;
            }
            if (!first_single || !last_single) {
                lexer_error(lexer, "character class escape cannot be used as a range endpoint at byte %zu",
                            range_pos);
                return RX_BADPAT;
            }
            if (first > last) {
                lexer_error(lexer, "invalid character range at byte %zu", range_pos);
                return RX_BADPAT;
            }
            rx_charset_add_range(token->cls, first, last);
        } else {
            charset_union(token->cls, first_cls);
        }
        saw_any = true;
    }

    if (lexer_peek(lexer) != ']') {
        lexer_error(lexer, "unclosed character class at byte %zu", token->pos);
        return RX_EBRACK;
    }
    lexer_next_char(lexer);
    if (!saw_any) {
        lexer_error(lexer, "empty character class at byte %zu is unsupported", token->pos);
        return RX_EBRACK;
    }
    if (negate) {
        rx_charset_invert(token->cls);
    }
    token->type = TOK_CLASS;
    return RX_OK;
}

static int lex_count(rx_lexer_t *lexer, size_t brace_pos, size_t *value)
{
    if (lexer_peek(lexer) < '0' || lexer_peek(lexer) > '9') {
        lexer_error(lexer, "expected repetition bound at byte %zu", lexer->pos);
        return RX_EBRACE;
    }

    *value = 0;
    while (lexer_peek(lexer) >= '0' && lexer_peek(lexer) <= '9') {
        unsigned digit = (unsigned)(lexer_next_char(lexer) - '0');
        if (*value > (RX_REPEAT_LIMIT - digit) / 10u) {
            lexer_error(lexer, "repetition at byte %zu exceeds limit %u", brace_pos, RX_REPEAT_LIMIT);
            return RX_BADRPT;
        }
        *value = *value * 10u + digit;
    }
    return RX_OK;
}

static int lex_repeat(rx_lexer_t *lexer, rx_token_t *token)
{
    int rc = lex_count(lexer, token->pos, &token->repeat_min);
    if (rc != RX_OK) {
        return rc;
    }
    token->repeat_max = token->repeat_min;

    if (lexer_peek(lexer) == ',') {
        lexer_next_char(lexer);
        if (lexer_peek(lexer) == '}') {
            token->repeat_unbounded = true;
        } else {
            rc = lex_count(lexer, token->pos, &token->repeat_max);
            if (rc != RX_OK) {
                return rc;
            }
        }
    }

    if (lexer_peek(lexer) != '}') {
        lexer_error(lexer, "missing '}' for repetition at byte %zu", token->pos);
        return RX_EBRACE;
    }
    lexer_next_char(lexer);
    if (!token->repeat_unbounded && token->repeat_max < token->repeat_min) {
        lexer_error(lexer, "repetition upper bound is smaller than lower bound at byte %zu", token->pos);
        return RX_BADRPT;
    }
    token->type = TOK_REPEAT;
    return RX_OK;
}

void rx_lexer_init(rx_lexer_t *lexer, const char *pattern)
{
    memset(lexer, 0, sizeof(*lexer));
    lexer->pattern = pattern != NULL ? pattern : "";
}

int rx_lexer_next(rx_lexer_t *lexer, rx_token_t *token)
{
    if (lexer == NULL || token == NULL || lexer->pattern == NULL) {
        return RX_BADPAT;
    }

    memset(token, 0, sizeof(*token));
    token->pos = lexer->pos;
    int c = lexer_next_char(lexer);
    switch (c) {
    case '\0': token->type = TOK_EOF; return RX_OK;
    case '.': token->type = TOK_DOT; return RX_OK;
    case '|': token->type = TOK_ALT; return RX_OK;
    case '*': token->type = TOK_STAR; return RX_OK;
    case '+': token->type = TOK_PLUS; return RX_OK;
    case '?': token->type = TOK_QUESTION; return RX_OK;
    case '(': token->type = TOK_LPAREN; return RX_OK;
    case ')': token->type = TOK_RPAREN; return RX_OK;
    case '^': token->type = TOK_ANCHOR_BEGIN; return RX_OK;
    case '$': token->type = TOK_ANCHOR_END; return RX_OK;
    case '[': return lex_class(lexer, token);
    case '{': return lex_repeat(lexer, token);
    case '\\':
        token->type = TOK_CLASS;
        return lex_escape(lexer, token->cls);
    default:
        token->type = TOK_CLASS;
        rx_charset_clear(token->cls);
        rx_charset_add(token->cls, (unsigned char)c);
        return RX_OK;
    }
}

const char *rx_token_type_name(rx_token_type_t type)
{
    switch (type) {
    case TOK_EOF: return "EOF";
    case TOK_CLASS: return "CLASS";
    case TOK_DOT: return "DOT";
    case TOK_ALT: return "ALT";
    case TOK_STAR: return "STAR";
    case TOK_PLUS: return "PLUS";
    case TOK_QUESTION: return "QUESTION";
    case TOK_REPEAT: return "REPEAT";
    case TOK_LPAREN: return "LPAREN";
    case TOK_RPAREN: return "RPAREN";
    case TOK_ANCHOR_BEGIN: return "ANCHOR_BEGIN";
    case TOK_ANCHOR_END: return "ANCHOR_END";
    }
    return "UNKNOWN";
}
