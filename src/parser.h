#ifndef RX_PARSER_H
#define RX_PARSER_H

#include "ast.h"
#include "lexer.h"

ast_node_t *rx_parse_pattern(const char *pattern,
                             char error[RX_ERROR_SIZE],
                             int *status_out,
                             size_t *capture_count_out);

#endif
