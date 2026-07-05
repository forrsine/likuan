#ifndef RX_MATCHER_H
#define RX_MATCHER_H

#include "nfa.h"

#include <stddef.h>

int nfa_run_from(const nfa_t *nfa, const char *text, size_t start, size_t *end_out,
                 int *captures, size_t total_groups);

#endif
