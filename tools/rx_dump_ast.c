#include "parser.h"

#include "regex_engine.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s PATTERN\n", argv[0]);
        return 2;
    }

    char error[RX_ERROR_SIZE];
    int status = RX_BADPAT;
    size_t captures = 0;
    ast_node_t *ast = rx_parse_pattern(argv[1], error, &status, &captures);
    if (ast == NULL) {
        fprintf(stderr, "parse failed: %s (rc=%d)\n", error, status);
        return 1;
    }

    printf("captures: %zu\n", captures);
    int rc = ast_dump(ast, stdout);
    ast_free(ast);
    return rc == 0 ? 0 : 1;
}
