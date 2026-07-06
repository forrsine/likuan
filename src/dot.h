#ifndef RX_DOT_H
#define RX_DOT_H

#include "dfa.h"
#include "nfa.h"

#include <stdio.h>

int nfa_dump_dot(const nfa_t *nfa, FILE *out);
int dfa_dump_dot(const dfa_t *dfa, FILE *out);

#endif
