#include "parser.h"

#include "regex_engine.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    rx_lexer_t lexer;
    rx_token_t current;
    char error[RX_ERROR_SIZE];
    int status;
    size_t next_group;
    size_t group_depth;
} parser_t;

static void parser_error(parser_t *parser, int status, const char *fmt, ...)
{
    if (parser->status != RX_OK) {
        return;
    }
    parser->status = status;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(parser->error, sizeof(parser->error), fmt, ap);
    va_end(ap);
    parser->error[sizeof(parser->error) - 1] = '\0';
}

static bool parser_advance(parser_t *parser)
{
    int rc = rx_lexer_next(&parser->lexer, &parser->current);
    if (rc == RX_OK) {
        return true;
    }
    parser->status = rc;
    snprintf(parser->error, sizeof(parser->error), "%s",
             parser->lexer.error[0] ? parser->lexer.error : "lexer failed");
    return false;
}

static bool token_ends_concat(rx_token_type_t type)
{
    return type == TOK_EOF || type == TOK_RPAREN || type == TOK_ALT;
}

static bool token_is_repeat(rx_token_type_t type)
{
    return type == TOK_STAR || type == TOK_PLUS || type == TOK_QUESTION || type == TOK_REPEAT;
}

static ast_node_t *parse_expr(parser_t *parser);

static ast_node_t *parse_atom(parser_t *parser)
{
    rx_token_t token = parser->current;
    ast_node_t *node = NULL;

    if (token_ends_concat(token.type)) {
        node = ast_new(AST_EMPTY);
        if (node == NULL) {
            parser_error(parser, RX_ESPACE, "out of memory");
        }
        return node;
    }

    if (token.type == TOK_LPAREN) {
        if (parser->group_depth >= RX_MAX_GROUP_DEPTH) {
            parser_error(parser, RX_EUNSUPPORTED,
                         "group nesting exceeds limit %u at byte %zu",
                         RX_MAX_GROUP_DEPTH, token.pos);
            return NULL;
        }
        size_t group_id = parser->next_group++;
        ++parser->group_depth;
        if (!parser_advance(parser)) {
            --parser->group_depth;
            return NULL;
        }
        ast_node_t *inside = parse_expr(parser);
        if (inside == NULL) {
            --parser->group_depth;
            return NULL;
        }
        if (parser->current.type != TOK_RPAREN) {
            --parser->group_depth;
            ast_free(inside);
            parser_error(parser, RX_EPAREN, "missing ')' for group opened at byte %zu", token.pos);
            return NULL;
        }
        --parser->group_depth;
        if (!parser_advance(parser)) {
            ast_free(inside);
            return NULL;
        }
        node = ast_wrap(AST_GROUP, inside);
        if (node != NULL) {
            node->group_id = group_id;
        }
    } else if (token.type == TOK_CLASS) {
        node = ast_new(AST_CLASS);
        if (node != NULL) {
            memcpy(node->cls, token.cls, sizeof(node->cls));
        }
        parser_advance(parser);
    } else if (token.type == TOK_DOT) {
        node = ast_new(AST_DOT);
        parser_advance(parser);
    } else if (token.type == TOK_ANCHOR_BEGIN) {
        node = ast_new(AST_ANCHOR_BEGIN);
        parser_advance(parser);
    } else if (token.type == TOK_ANCHOR_END) {
        node = ast_new(AST_ANCHOR_END);
        parser_advance(parser);
    } else if (token_is_repeat(token.type)) {
        parser_error(parser, RX_BADRPT, "repetition operator has no operand at byte %zu", token.pos);
        return NULL;
    } else {
        parser_error(parser, RX_BADPAT, "unexpected token %s at byte %zu",
                     rx_token_type_name(token.type), token.pos);
        return NULL;
    }

    if (node == NULL && parser->status == RX_OK) {
        parser_error(parser, RX_ESPACE, "out of memory");
    }
    if (parser->status != RX_OK) {
        ast_free(node);
        return NULL;
    }
    return node;
}

static ast_node_t *parse_repeat(parser_t *parser)
{
    ast_node_t *node = parse_atom(parser);
    if (node == NULL || !token_is_repeat(parser->current.type)) {
        return node;
    }

    rx_token_t repeat = parser->current;
    ast_type_t type = AST_REPEAT;
    if (repeat.type == TOK_STAR) {
        type = AST_STAR;
    } else if (repeat.type == TOK_PLUS) {
        type = AST_PLUS;
    } else if (repeat.type == TOK_QUESTION) {
        type = AST_QUESTION;
    }

    ast_node_t *wrapped = ast_wrap(type, node);
    if (wrapped == NULL) {
        ast_free(node);
        parser_error(parser, RX_ESPACE, "out of memory");
        return NULL;
    }
    if (type == AST_REPEAT) {
        wrapped->repeat_min = repeat.repeat_min;
        wrapped->repeat_max = repeat.repeat_max;
        wrapped->repeat_unbounded = repeat.repeat_unbounded;
    }
    node = wrapped;

    if (!parser_advance(parser)) {
        ast_free(node);
        return NULL;
    }
    if (token_is_repeat(parser->current.type)) {
        ast_free(node);
        parser_error(parser, RX_BADRPT, "multiple repetition operators at byte %zu", parser->current.pos);
        return NULL;
    }
    return node;
}

static ast_node_t *parse_concat(parser_t *parser)
{
    ast_node_t *left = NULL;
    while (!token_ends_concat(parser->current.type)) {
        ast_node_t *right = parse_repeat(parser);
        if (right == NULL) {
            ast_free(left);
            return NULL;
        }
        if (left == NULL) {
            left = right;
        } else {
            ast_node_t *concat = ast_binary(AST_CONCAT, left, right);
            if (concat == NULL) {
                ast_free(left);
                ast_free(right);
                parser_error(parser, RX_ESPACE, "out of memory");
                return NULL;
            }
            left = concat;
        }
    }

    if (left == NULL) {
        left = ast_new(AST_EMPTY);
        if (left == NULL) {
            parser_error(parser, RX_ESPACE, "out of memory");
        }
    }
    return left;
}

static ast_node_t *parse_expr(parser_t *parser)
{
    ast_node_t *left = parse_concat(parser);
    if (left == NULL) {
        return NULL;
    }

    while (parser->current.type == TOK_ALT) {
        if (!parser_advance(parser)) {
            ast_free(left);
            return NULL;
        }
        ast_node_t *right = parse_concat(parser);
        if (right == NULL) {
            ast_free(left);
            return NULL;
        }
        ast_node_t *alternate = ast_binary(AST_ALT, left, right);
        if (alternate == NULL) {
            ast_free(left);
            ast_free(right);
            parser_error(parser, RX_ESPACE, "out of memory");
            return NULL;
        }
        left = alternate;
    }
    return left;
}

ast_node_t *rx_parse_pattern(const char *pattern,
                             char error[RX_ERROR_SIZE],
                             int *status_out,
                             size_t *capture_count_out)
{
    parser_t parser;
    memset(&parser, 0, sizeof(parser));
    parser.status = RX_OK;
    parser.next_group = 1;
    rx_lexer_init(&parser.lexer, pattern);

    if (pattern == NULL) {
        parser_error(&parser, RX_BADPAT, "pattern is null");
    } else if (parser_advance(&parser)) {
        ast_node_t *ast = parse_expr(&parser);
        if (ast != NULL && parser.current.type == TOK_RPAREN) {
            ast_free(ast);
            ast = NULL;
            parser_error(&parser, RX_EPAREN, "unmatched ')' at byte %zu", parser.current.pos);
        } else if (ast != NULL && parser.current.type != TOK_EOF) {
            ast_free(ast);
            ast = NULL;
            parser_error(&parser, RX_BADPAT, "unexpected token %s at byte %zu",
                         rx_token_type_name(parser.current.type), parser.current.pos);
        }

        if (ast != NULL) {
            if (error != NULL) {
                error[0] = '\0';
            }
            if (status_out != NULL) {
                *status_out = RX_OK;
            }
            if (capture_count_out != NULL) {
                *capture_count_out = parser.next_group - 1;
            }
            return ast;
        }
    }

    if (error != NULL) {
        snprintf(error, RX_ERROR_SIZE, "%s", parser.error[0] ? parser.error : "parse failed");
    }
    if (status_out != NULL) {
        *status_out = parser.status != RX_OK ? parser.status : RX_BADPAT;
    }
    if (capture_count_out != NULL) {
        *capture_count_out = 0;
    }
    return NULL;
}
