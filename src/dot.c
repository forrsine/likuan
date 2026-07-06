#include "dot.h"

#include "charset.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>

static void dot_dump_char(FILE *out, unsigned char c)
{
    switch (c) {
    case '"': fputs("\\\"", out); return;
    case '\\': fputs("\\\\", out); return;
    case '\n': fputs("\\\\n", out); return;
    case '\r': fputs("\\\\r", out); return;
    case '\t': fputs("\\\\t", out); return;
    case ']': fputs("\\]", out); return;
    case '-': fputs("\\-", out); return;
    default:
        if (isprint(c)) {
            fputc(c, out);
        } else {
            fprintf(out, "\\\\x%02X", (unsigned)c);
        }
    }
}

static void dot_dump_charset(FILE *out,
                             const unsigned char cls[RX_CHARSET_BYTES])
{
    fputc('[', out);
    for (unsigned int i = 0; i < RX_DFA_ALPHABET_SIZE;) {
        if (!rx_charset_has(cls, (unsigned char)i)) {
            ++i;
            continue;
        }
        unsigned int end = i;
        while (end + 1 < RX_DFA_ALPHABET_SIZE &&
               rx_charset_has(cls, (unsigned char)(end + 1))) {
            ++end;
        }
        dot_dump_char(out, (unsigned char)i);
        if (end >= i + 2) {
            fputc('-', out);
            dot_dump_char(out, (unsigned char)end);
        } else if (end == i + 1) {
            dot_dump_char(out, (unsigned char)end);
        }
        i = end + 1;
    }
    fputc(']', out);
}

static void dot_dump_transition_label(const transition_t *transition, FILE *out)
{
    switch (transition->type) {
    case TR_EPS:
        fputs("eps", out);
        break;
    case TR_CLASS:
        dot_dump_charset(out, transition->cls);
        break;
    case TR_ANY:
        fputc('.', out);
        break;
    case TR_ANCHOR_BEGIN:
        fputc('^', out);
        break;
    case TR_ANCHOR_END:
        fputc('$', out);
        break;
    case TR_CAPTURE_BEGIN:
        fprintf(out, "group %zu begin", transition->group_id);
        break;
    case TR_CAPTURE_END:
        fprintf(out, "group %zu end", transition->group_id);
        break;
    }
}

static void dot_header(FILE *out, const char *name)
{
    fprintf(out, "digraph %s {\n", name);
    fputs("  rankdir=LR;\n", out);
    fputs("  graph [bgcolor=\"white\", pad=\"0.2\"];\n", out);
    fputs("  node [shape=circle, fontname=\"Consolas\", fontsize=10];\n", out);
    fputs("  edge [fontname=\"Consolas\", fontsize=9];\n", out);
}

int nfa_dump_dot(const nfa_t *nfa, FILE *out)
{
    if (nfa == NULL || out == NULL || nfa->start < 0 || nfa->accept < 0) {
        return -1;
    }

    dot_header(out, "NFA");
    fputs("  start [shape=point, label=\"\"];\n", out);
    fprintf(out, "  start -> q%d;\n", nfa->start);
    for (size_t state = 0; state < nfa->len; ++state) {
        fprintf(out, "  q%zu [label=\"%zu\"", state, state);
        if ((int)state == nfa->accept) {
            fputs(", shape=doublecircle", out);
        }
        fputs("];\n", out);
    }

    for (size_t state = 0; state < nfa->len; ++state) {
        const transition_vec_t *vec = &nfa->states[state].trans;
        for (size_t i = 0; i < vec->len; ++i) {
            const transition_t *transition = &vec->items[i];
            fprintf(out, "  q%zu -> q%d [label=\"", state, transition->to);
            dot_dump_transition_label(transition, out);
            fputs("\"];\n", out);
        }
    }
    fputs("}\n", out);
    return ferror(out) ? -1 : 0;
}

static bool collect_dfa_edge(const dfa_state_t *state,
                             int target,
                             bool final_only,
                             unsigned char cls[RX_CHARSET_BYTES])
{
    rx_charset_clear(cls);
    bool found = false;
    for (size_t byte = 0; byte < RX_DFA_ALPHABET_SIZE; ++byte) {
        int actual = final_only
                         ? state->final_transitions[byte]
                         : state->transitions[byte];
        if (actual == target &&
            (!final_only || actual != state->transitions[byte])) {
            rx_charset_add(cls, (unsigned char)byte);
            found = true;
        }
    }
    return found;
}

static void dump_dfa_start(FILE *out,
                           const char *id,
                           const char *label,
                           int target)
{
    fprintf(out, "  %s [shape=plaintext, label=\"%s\"];\n", id, label);
    fprintf(out, "  %s -> q%d;\n", id, target);
}

int dfa_dump_dot(const dfa_t *dfa, FILE *out)
{
    if (dfa == NULL || out == NULL || dfa->start < 0) {
        return -1;
    }

    dot_header(out, "MinDFA");
    dump_dfa_start(out, "start_mid", "mid", dfa->start_states[DFA_CONTEXT_MID]);
    dump_dfa_start(out, "start_begin", "begin", dfa->start_states[DFA_CONTEXT_BEGIN]);
    dump_dfa_start(out, "start_end", "end", dfa->start_states[DFA_CONTEXT_END]);
    dump_dfa_start(out, "start_empty", "begin+end",
                   dfa->start_states[DFA_CONTEXT_BEGIN_END]);

    for (size_t state = 0; state < dfa->len; ++state) {
        fprintf(out, "  q%zu [label=\"%zu\"", state, state);
        if (dfa->states[state].is_accept) {
            fputs(", shape=doublecircle", out);
        }
        fputs("];\n", out);
    }

    unsigned char cls[RX_CHARSET_BYTES];
    for (size_t state = 0; state < dfa->len; ++state) {
        for (size_t target = 0; target < dfa->len; ++target) {
            if (collect_dfa_edge(&dfa->states[state], (int)target, false, cls)) {
                fprintf(out, "  q%zu -> q%zu [label=\"", state, target);
                dot_dump_charset(out, cls);
                fputs("\"];\n", out);
            }
        }
        for (size_t target = 0; target < dfa->len; ++target) {
            if (collect_dfa_edge(&dfa->states[state], (int)target, true, cls)) {
                fprintf(out, "  q%zu -> q%zu [label=\"final ", state, target);
                dot_dump_charset(out, cls);
                fputs("\", style=dashed, color=\"#2563eb\"];\n", out);
            }
        }
    }
    fputs("}\n", out);
    return ferror(out) ? -1 : 0;
}
