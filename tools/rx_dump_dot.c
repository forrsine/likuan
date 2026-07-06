#include "dot.h"
#include "nfa.h"
#include "parser.h"

#include "regex_engine.h"

#include <stdio.h>
#include <string.h>

typedef enum {
    MODE_NONE,
    MODE_NFA,
    MODE_DFA
} dump_mode_t;

static void usage(const char *program)
{
    fprintf(stderr,
            "usage: %s (--nfa|--dfa) [--output FILE] PATTERN\n",
            program);
}

int main(int argc, char **argv)
{
    dump_mode_t mode = MODE_NONE;
    const char *pattern = NULL;
    const char *output_path = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--nfa") == 0) {
            mode = MODE_NFA;
        } else if (strcmp(argv[i], "--dfa") == 0 ||
                   strcmp(argv[i], "--mindfa") == 0) {
            mode = MODE_DFA;
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (pattern == NULL) {
            pattern = argv[i];
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (mode == MODE_NONE || pattern == NULL) {
        usage(argv[0]);
        return 2;
    }

    FILE *out = stdout;
    if (output_path != NULL) {
#ifdef _MSC_VER
        if (fopen_s(&out, output_path, "w") != 0) {
            out = NULL;
        }
#else
        out = fopen(output_path, "w");
#endif
        if (out == NULL) {
            fprintf(stderr, "cannot open output file: %s\n", output_path);
            return 1;
        }
    }

    char error[RX_ERROR_SIZE];
    int status = RX_BADPAT;
    ast_node_t *ast = rx_parse_pattern(pattern, error, &status, NULL);
    if (ast == NULL) {
        fprintf(stderr, "parse failed: %s (rc=%d: %s)\n",
                error, status, regex_status_string(status));
        if (out != stdout) {
            fclose(out);
        }
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
    if (rc == RX_OK && mode == MODE_DFA) {
        rc = dfa_build(&dfa, &nfa);
        if (rc == RX_OK) {
            rc = dfa_minimize(&dfa);
        }
    }
    if (rc == RX_OK) {
        int dump_rc = mode == MODE_NFA
                          ? nfa_dump_dot(&nfa, out)
                          : dfa_dump_dot(&dfa, out);
        rc = dump_rc == 0 ? RX_OK : RX_BADPAT;
    }
    if (rc != RX_OK) {
        fprintf(stderr, "DOT generation failed (rc=%d: %s)\n",
                rc, regex_status_string(rc));
    }

    if (out != stdout) {
        fclose(out);
    }
    dfa_free(&dfa);
    nfa_free(&nfa);
    ast_free(ast);
    return rc == RX_OK ? 0 : 1;
}
