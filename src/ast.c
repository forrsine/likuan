#include "ast.h"

#include <stdlib.h>

ast_node_t *ast_new(ast_type_t type)
{
    ast_node_t *node = (ast_node_t *)calloc(1, sizeof(*node));
    if (node != NULL) {
        node->type = type;
    }
    return node;
}

ast_node_t *ast_wrap(ast_type_t type, ast_node_t *child)
{
    ast_node_t *node = ast_new(type);
    if (node != NULL) {
        node->left = child;
    }
    return node;
}

ast_node_t *ast_binary(ast_type_t type, ast_node_t *left, ast_node_t *right)
{
    ast_node_t *node = ast_new(type);
    if (node != NULL) {
        node->left = left;
        node->right = right;
    }
    return node;
}

void ast_free(ast_node_t *node)
{
    if (node == NULL) {
        return;
    }
    ast_free(node->left);
    ast_free(node->right);
    free(node);
}

static void dump_indent(FILE *out, size_t depth)
{
    for (size_t i = 0; i < depth; ++i) {
        fputs("  ", out);
    }
}

static void dump_node(const ast_node_t *node, FILE *out, size_t depth)
{
    dump_indent(out, depth);
    switch (node->type) {
    case AST_EMPTY: fputs("EMPTY\n", out); return;
    case AST_CLASS:
        fputs("CLASS ", out);
        rx_charset_dump(node->cls, out);
        fputc('\n', out);
        return;
    case AST_DOT: fputs("DOT\n", out); return;
    case AST_ANCHOR_BEGIN: fputs("ANCHOR_BEGIN\n", out); return;
    case AST_ANCHOR_END: fputs("ANCHOR_END\n", out); return;
    case AST_CONCAT: fputs("CONCAT\n", out); break;
    case AST_ALT: fputs("ALT\n", out); break;
    case AST_STAR: fputs("STAR\n", out); break;
    case AST_PLUS: fputs("PLUS\n", out); break;
    case AST_QUESTION: fputs("QUESTION\n", out); break;
    case AST_GROUP: fprintf(out, "GROUP #%zu\n", node->group_id); break;
    case AST_REPEAT:
        if (node->repeat_unbounded) {
            fprintf(out, "REPEAT {%zu,}\n", node->repeat_min);
        } else if (node->repeat_min == node->repeat_max) {
            fprintf(out, "REPEAT {%zu}\n", node->repeat_min);
        } else {
            fprintf(out, "REPEAT {%zu,%zu}\n", node->repeat_min, node->repeat_max);
        }
        break;
    }

    if (node->left != NULL) {
        dump_node(node->left, out, depth + 1);
    }
    if (node->right != NULL) {
        dump_node(node->right, out, depth + 1);
    }
}

int ast_dump(const ast_node_t *node, FILE *out)
{
    if (node == NULL || out == NULL) {
        return -1;
    }
    dump_node(node, out, 0);
    return ferror(out) ? -1 : 0;
}
