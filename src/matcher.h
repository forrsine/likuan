#ifndef RX_MATCHER_H
#define RX_MATCHER_H

#include "nfa.h"
#include "regex_engine.h"

#include <stddef.h>

<<<<<<< Updated upstream
int nfa_run_from(const nfa_t *nfa, const char *text, size_t start, size_t *end_out,
                 int *captures, size_t total_groups);
=======
int nfa_run_from(const nfa_t *nfa, const char *text, size_t start, size_t *end_out);
int nfa_capture_span(const nfa_t *nfa,
                     const char *text,
                     size_t start,
                     size_t end,
                     rx_match_t *matches,
                     size_t nmatch,
                     size_t capture_count);
>>>>>>> Stashed changes

#endif
