#include "dfa.h"
#include "nfa.h"
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
    ast_node_t *ast = rx_parse_pattern(argv[1], error, &status, NULL);
    if (ast == NULL) {
        fprintf(stderr, "parse failed: %s (rc=%d: %s)\n",
                error, status, regex_status_string(status));
        return 1;
    }

    nfa_t nfa;
    nfa_init(&nfa);
    frag_t fragment;
    int rc = nfa_compile_ast(&nfa, ast, &fragment);
    if (rc == RX_OK) {
        nfa.start = fragment.start;
        nfa.accept = fragment.accept;
    }

    dfa_t dfa;
    dfa_init(&dfa);
    if (rc == RX_OK) {
        rc = dfa_build(&dfa, &nfa);
    }
    if (rc == RX_OK) {
        rc = dfa_minimize(&dfa);
    }
    if (rc == RX_OK) {
        printf("pattern=%s\n", argv[1]);
        rc = dfa_dump_table(&dfa, stdout) == 0 ? RX_OK : RX_BADPAT;
    } else {
        fprintf(stderr, "DFA build failed (rc=%d: %s)\n", rc, regex_status_string(rc));
    }

    dfa_free(&dfa);
    nfa_free(&nfa);
    ast_free(ast);
    return rc == RX_OK ? 0 : 1;
}
