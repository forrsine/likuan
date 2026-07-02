#ifndef RX_AST_H
#define RX_AST_H

#include "charset.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef enum {
    AST_EMPTY,
    AST_CLASS,
    AST_DOT,
    AST_CONCAT,
    AST_ALT,
    AST_STAR,
    AST_PLUS,
    AST_QUESTION,
    AST_REPEAT,
    AST_GROUP,
    AST_ANCHOR_BEGIN,
    AST_ANCHOR_END
} ast_type_t;

typedef struct ast_node {
    ast_type_t type;
    struct ast_node *left;
    struct ast_node *right;
    unsigned char cls[RX_CHARSET_BYTES];
    size_t repeat_min;
    size_t repeat_max;
    bool repeat_unbounded;
    size_t group_id;
} ast_node_t;

ast_node_t *ast_new(ast_type_t type);
ast_node_t *ast_wrap(ast_type_t type, ast_node_t *child);
ast_node_t *ast_binary(ast_type_t type, ast_node_t *left, ast_node_t *right);
void ast_free(ast_node_t *node);
int ast_dump(const ast_node_t *node, FILE *out);

#endif
