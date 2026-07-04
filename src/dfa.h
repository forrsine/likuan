#ifndef RX_DFA_H
#define RX_DFA_H

#include "nfa.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define RX_DFA_ALPHABET_SIZE 256
#define RX_DFA_STATE_LIMIT 4096u
#define RX_DFA_CONTEXT_COUNT 4

typedef enum {
    DFA_CONTEXT_MID = 0,
    DFA_CONTEXT_BEGIN = 1,
    DFA_CONTEXT_END = 2,
    DFA_CONTEXT_BEGIN_END = 3
} dfa_context_t;

typedef struct {
    unsigned char *nfa_set;
    int transitions[RX_DFA_ALPHABET_SIZE];
    int final_transitions[RX_DFA_ALPHABET_SIZE];
    bool is_accept;
} dfa_state_t;

typedef struct {
    dfa_state_t *states;
    size_t len;
    size_t cap;
    size_t set_bytes;
    size_t subset_state_count;
    int start;
    int start_states[RX_DFA_CONTEXT_COUNT];
    unsigned short class_of[RX_DFA_ALPHABET_SIZE];
    unsigned char class_representative[RX_DFA_ALPHABET_SIZE];
    size_t class_count;
} dfa_t;

void dfa_init(dfa_t *dfa);
void dfa_free(dfa_t *dfa);
bool dfa_can_build(const nfa_t *nfa);
int dfa_build(dfa_t *dfa, const nfa_t *nfa);
int dfa_minimize(dfa_t *dfa);
int dfa_run_from(const dfa_t *dfa, const char *text, size_t start, size_t *end_out);
size_t dfa_transition_count(const dfa_t *dfa);
size_t dfa_final_override_count(const dfa_t *dfa);
int dfa_dump_table(const dfa_t *dfa, FILE *out);

#endif
