#include "dfa.h"

#include "charset.h"
#include "regex_engine.h"

#include <stdlib.h>
#include <string.h>

static void bitset_add(unsigned char *set, size_t state)
{
    set[state >> 3] |= (unsigned char)(1u << (state & 7u));
}

static bool bitset_has(const unsigned char *set, size_t state)
{
    return (set[state >> 3] & (unsigned char)(1u << (state & 7u))) != 0;
}

static bool bitset_empty(const unsigned char *set, size_t bytes)
{
    for (size_t i = 0; i < bytes; ++i) {
        if (set[i] != 0) {
            return false;
        }
    }
    return true;
}

void dfa_init(dfa_t *dfa)
{
    memset(dfa, 0, sizeof(*dfa));
    dfa->start = -1;
}

void dfa_free(dfa_t *dfa)
{
    if (dfa == NULL) {
        return;
    }
    for (size_t i = 0; i < dfa->len; ++i) {
        free(dfa->states[i].nfa_set);
    }
    free(dfa->states);
    dfa_init(dfa);
}

bool dfa_can_build(const nfa_t *nfa)
{
    if (nfa == NULL) {
        return false;
    }
    for (size_t state = 0; state < nfa->len; ++state) {
        const transition_vec_t *transitions = &nfa->states[state].trans;
        for (size_t i = 0; i < transitions->len; ++i) {
            trans_type_t type = transitions->items[i].type;
            if (type == TR_ANCHOR_BEGIN || type == TR_ANCHOR_END) {
                return false;
            }
        }
    }
    return true;
}

static int epsilon_closure(const nfa_t *nfa, unsigned char *set)
{
    int *queue = (int *)malloc(nfa->len * sizeof(*queue));
    if (queue == NULL) {
        return RX_ESPACE;
    }

    size_t head = 0;
    size_t tail = 0;
    for (size_t state = 0; state < nfa->len; ++state) {
        if (bitset_has(set, state)) {
            queue[tail++] = (int)state;
        }
    }

    while (head < tail) {
        int state = queue[head++];
        const transition_vec_t *transitions = &nfa->states[state].trans;
        for (size_t i = 0; i < transitions->len; ++i) {
            const transition_t *transition = &transitions->items[i];
            if (transition->type == TR_EPS && !bitset_has(set, (size_t)transition->to)) {
                bitset_add(set, (size_t)transition->to);
                queue[tail++] = transition->to;
            }
        }
    }

    free(queue);
    return RX_OK;
}

static int move_and_close(const nfa_t *nfa,
                          const unsigned char *source,
                          unsigned char byte,
                          unsigned char *target,
                          size_t set_bytes)
{
    memset(target, 0, set_bytes);
    for (size_t state = 0; state < nfa->len; ++state) {
        if (!bitset_has(source, state)) {
            continue;
        }
        const transition_vec_t *transitions = &nfa->states[state].trans;
        for (size_t i = 0; i < transitions->len; ++i) {
            const transition_t *transition = &transitions->items[i];
            if (transition->type == TR_ANY ||
                (transition->type == TR_CLASS && rx_charset_has(transition->cls, byte))) {
                bitset_add(target, (size_t)transition->to);
            }
        }
    }
    if (bitset_empty(target, set_bytes)) {
        return RX_OK;
    }
    return epsilon_closure(nfa, target);
}

static int dfa_find_state(const dfa_t *dfa, const unsigned char *set)
{
    for (size_t i = 0; i < dfa->len; ++i) {
        if (memcmp(dfa->states[i].nfa_set, set, dfa->set_bytes) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int dfa_add_state(dfa_t *dfa, const nfa_t *nfa, const unsigned char *set)
{
    if (dfa->len >= RX_DFA_STATE_LIMIT) {
        return -2;
    }
    if (dfa->len == dfa->cap) {
        size_t next_cap = dfa->cap == 0 ? 16 : dfa->cap * 2;
        dfa_state_t *next = (dfa_state_t *)realloc(dfa->states, next_cap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        memset(next + dfa->cap, 0, (next_cap - dfa->cap) * sizeof(*next));
        dfa->states = next;
        dfa->cap = next_cap;
    }

    dfa_state_t *state = &dfa->states[dfa->len];
    state->nfa_set = (unsigned char *)malloc(dfa->set_bytes);
    if (state->nfa_set == NULL) {
        return -1;
    }
    memcpy(state->nfa_set, set, dfa->set_bytes);
    for (size_t i = 0; i < RX_DFA_ALPHABET_SIZE; ++i) {
        state->transitions[i] = -1;
    }
    state->is_accept = bitset_has(set, (size_t)nfa->accept);
    return (int)dfa->len++;
}

int dfa_build(dfa_t *dfa, const nfa_t *nfa)
{
    if (dfa == NULL || nfa == NULL || nfa->len == 0 || nfa->start < 0 || nfa->accept < 0) {
        return RX_BADPAT;
    }
    if (!dfa_can_build(nfa)) {
        return RX_EUNSUPPORTED;
    }

    dfa->set_bytes = (nfa->len + 7u) / 8u;
    unsigned char *start_set = (unsigned char *)calloc(dfa->set_bytes, 1);
    unsigned char *target_set = (unsigned char *)malloc(dfa->set_bytes);
    if (start_set == NULL || target_set == NULL) {
        free(start_set);
        free(target_set);
        return RX_ESPACE;
    }

    bitset_add(start_set, (size_t)nfa->start);
    int rc = epsilon_closure(nfa, start_set);
    if (rc != RX_OK) {
        free(start_set);
        free(target_set);
        return rc;
    }
    int start = dfa_add_state(dfa, nfa, start_set);
    if (start < 0) {
        free(start_set);
        free(target_set);
        return start == -2 ? RX_EUNSUPPORTED : RX_ESPACE;
    }
    dfa->start = start;

    for (size_t current = 0; current < dfa->len; ++current) {
        for (unsigned int byte = 0; byte < RX_DFA_ALPHABET_SIZE; ++byte) {
            const unsigned char *source = dfa->states[current].nfa_set;
            rc = move_and_close(nfa, source, (unsigned char)byte, target_set, dfa->set_bytes);
            if (rc != RX_OK) {
                free(start_set);
                free(target_set);
                return rc;
            }
            if (bitset_empty(target_set, dfa->set_bytes)) {
                continue;
            }
            int target = dfa_find_state(dfa, target_set);
            if (target < 0) {
                target = dfa_add_state(dfa, nfa, target_set);
                if (target < 0) {
                    free(start_set);
                    free(target_set);
                    return target == -2 ? RX_EUNSUPPORTED : RX_ESPACE;
                }
            }
            dfa->states[current].transitions[byte] = target;
        }
    }

    free(start_set);
    free(target_set);
    return RX_OK;
}

int dfa_run_from(const dfa_t *dfa, const char *text, size_t start, size_t *end_out)
{
    if (dfa == NULL || text == NULL || end_out == NULL || dfa->start < 0) {
        return RX_BADPAT;
    }

    size_t text_len = strlen(text);
    int state = dfa->start;
    bool found = dfa->states[state].is_accept;
    size_t best = start;

    for (size_t pos = start; pos < text_len; ++pos) {
        int next = dfa->states[state].transitions[(unsigned char)text[pos]];
        if (next < 0) {
            break;
        }
        state = next;
        if (dfa->states[state].is_accept) {
            found = true;
            best = pos + 1;
        }
    }
    if (!found) {
        return RX_NOMATCH;
    }
    *end_out = best;
    return RX_OK;
}

size_t dfa_transition_count(const dfa_t *dfa)
{
    if (dfa == NULL) {
        return 0;
    }
    size_t count = 0;
    for (size_t state = 0; state < dfa->len; ++state) {
        for (size_t byte = 0; byte < RX_DFA_ALPHABET_SIZE; ++byte) {
            if (dfa->states[state].transitions[byte] >= 0) {
                ++count;
            }
        }
    }
    return count;
}

static const char *dfa_state_role(const dfa_t *dfa, size_t state)
{
    if ((int)state == dfa->start && dfa->states[state].is_accept) {
        return "start+accept";
    }
    if ((int)state == dfa->start) {
        return "start";
    }
    if (dfa->states[state].is_accept) {
        return "accept";
    }
    return "-";
}

int dfa_dump_table(const dfa_t *dfa, FILE *out)
{
    if (dfa == NULL || out == NULL || dfa->start < 0) {
        return -1;
    }

    fprintf(out, "DFA states=%zu byte_transitions=%zu start=%d\n",
            dfa->len, dfa_transition_count(dfa), dfa->start);
    fputs("STATE  ROLE          INPUT               TO\n", out);
    fputs("-----  ------------  ------------------  -----\n", out);
    for (size_t state = 0; state < dfa->len; ++state) {
        bool wrote = false;
        for (size_t target = 0; target < dfa->len; ++target) {
            unsigned char cls[RX_CHARSET_BYTES];
            rx_charset_clear(cls);
            bool has_target = false;
            for (size_t byte = 0; byte < RX_DFA_ALPHABET_SIZE; ++byte) {
                if (dfa->states[state].transitions[byte] == (int)target) {
                    rx_charset_add(cls, (unsigned char)byte);
                    has_target = true;
                }
            }
            if (!has_target) {
                continue;
            }
            if (!wrote) {
                fprintf(out, "%-5zu  %-12s  ", state, dfa_state_role(dfa, state));
            } else {
                fprintf(out, "%-5s  %-12s  ", "", "");
            }
            rx_charset_dump(cls, out);
            fprintf(out, "%*s%zu\n", 20, "", target);
            wrote = true;
        }
        if (!wrote) {
            fprintf(out, "%-5zu  %-12s  %-18s  %s\n",
                    state, dfa_state_role(dfa, state), "-", "-");
        }
    }
    return ferror(out) ? -1 : 0;
}
