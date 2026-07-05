#ifndef RX_NFA_H
#define RX_NFA_H

#include "charset.h"

#include <stddef.h>
#include <stdio.h>

struct ast_node;

typedef enum {
    TR_EPS,
    TR_CLASS,
    TR_ANY,
    TR_ANCHOR_BEGIN,
    TR_ANCHOR_END,
<<<<<<< Updated upstream
    TR_SAVE_START,
    TR_SAVE_END
=======
    TR_CAPTURE_BEGIN,
    TR_CAPTURE_END
>>>>>>> Stashed changes
} trans_type_t;

typedef struct {
    int to;
    trans_type_t type;
    size_t group_id;
    unsigned char cls[RX_CHARSET_BYTES];
    int save_slot;
} transition_t;

typedef struct {
    transition_t *items;
    size_t len;
    size_t cap;
} transition_vec_t;

typedef struct {
    transition_vec_t trans;
} nfa_state_t;

typedef struct {
    nfa_state_t *states;
    size_t len;
    size_t cap;
    int start;
    int accept;
} nfa_t;

typedef struct {
    int start;
    int accept;
} frag_t;

void nfa_init(nfa_t *nfa);
void nfa_free(nfa_t *nfa);
int nfa_add_state(nfa_t *nfa);
int nfa_add_transition(nfa_t *nfa, int from, int to, trans_type_t type,
                       const unsigned char cls[RX_CHARSET_BYTES]);
int nfa_add_eps(nfa_t *nfa, int from, int to);
<<<<<<< Updated upstream
int nfa_add_save(nfa_t *nfa, int from, int to, trans_type_t type, int save_slot);
=======
int nfa_add_capture(nfa_t *nfa, int from, int to, trans_type_t type, size_t group_id);
>>>>>>> Stashed changes
int nfa_compile_ast(nfa_t *nfa, const struct ast_node *node, frag_t *out);
size_t nfa_transition_count(const nfa_t *nfa);
const char *nfa_transition_type_name(trans_type_t type);
int nfa_dump_table(const nfa_t *nfa, FILE *out);

#endif
