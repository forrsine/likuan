#ifndef RX_DFA_H
#define RX_DFA_H

#include "nfa.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define RX_DFA_ALPHABET_SIZE 256
#define RX_DFA_STATE_LIMIT 4096u

typedef struct {
    unsigned char *nfa_set;
    int transitions[RX_DFA_ALPHABET_SIZE];
    bool is_accept;
} dfa_state_t;

typedef struct {
    dfa_state_t *states;
    size_t len;
    size_t cap;
    size_t set_bytes;
    int start;
} dfa_t;

void dfa_init(dfa_t *dfa);
void dfa_free(dfa_t *dfa);
bool dfa_can_build(const nfa_t *nfa);
int dfa_build(dfa_t *dfa, const nfa_t *nfa);
int dfa_run_from(const dfa_t *dfa, const char *text, size_t start, size_t *end_out);
size_t dfa_transition_count(const dfa_t *dfa);
int dfa_dump_table(const dfa_t *dfa, FILE *out);

#endif
