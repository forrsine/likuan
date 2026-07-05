#include "matcher.h"

#include "charset.h"
#include "regex_engine.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int *states;
    int *captures;
    size_t len;
    size_t cap;
    size_t state_count;
    size_t slot_count;
    unsigned char *seen;
} tagged_set_t;

static bool tagged_set_init(tagged_set_t *set, size_t state_count, size_t slot_count)
{
    memset(set, 0, sizeof(*set));
    set->seen = (unsigned char *)calloc(state_count, 1);
    if (set->seen == NULL) {
        return false;
    }
    set->state_count = state_count;
    set->slot_count = slot_count;
    return true;
}

static void tagged_set_free(tagged_set_t *set)
{
    free(set->states);
    free(set->captures);
    free(set->seen);
    memset(set, 0, sizeof(*set));
}

static void tagged_set_clear(tagged_set_t *set)
{
    for (size_t i = 0; i < set->len; ++i) {
        set->seen[set->states[i]] = 0;
    }
    set->len = 0;
}

static bool tagged_set_grow(tagged_set_t *set)
{
    size_t next_cap = set->cap == 0 ? 16 : set->cap * 2;
    if (set->slot_count != 0 &&
        next_cap > SIZE_MAX / set->slot_count / sizeof(*set->captures)) {
        return false;
    }
    int *next_states = (int *)malloc(next_cap * sizeof(*next_states));
    int *next_captures =
        (int *)malloc(next_cap * set->slot_count * sizeof(*next_captures));
    if (next_states == NULL || next_captures == NULL) {
        free(next_states);
        free(next_captures);
        return false;
    }
    if (set->len != 0) {
        memcpy(next_states, set->states, set->len * sizeof(*next_states));
        memcpy(next_captures, set->captures,
               set->len * set->slot_count * sizeof(*next_captures));
    }
    free(set->states);
    free(set->captures);
    set->states = next_states;
    set->captures = next_captures;
    set->cap = next_cap;
    return true;
}

static bool tagged_set_push(tagged_set_t *set, int state, const int *captures)
{
    if (state < 0 || (size_t)state >= set->state_count) {
        return false;
    }
    if (set->seen[state]) {
        return true;
    }
    if (set->len == set->cap && !tagged_set_grow(set)) {
        return false;
    }
    set->states[set->len] = state;
    memcpy(set->captures + set->len * set->slot_count,
           captures, set->slot_count * sizeof(*captures));
    set->seen[state] = 1;
    ++set->len;
    return true;
}

static bool tagged_expand_closure(const nfa_t *nfa,
                                  tagged_set_t *set,
                                  size_t pos,
                                  size_t text_len,
                                  size_t capture_count,
                                  int *base,
                                  int *candidate)
{
    for (size_t pending = 0; pending < set->len; ++pending) {
        int state = set->states[pending];
        memcpy(base, set->captures + pending * set->slot_count,
               set->slot_count * sizeof(*base));
        const transition_vec_t *vec = &nfa->states[state].trans;
        for (size_t i = 0; i < vec->len; ++i) {
            const transition_t *tr = &vec->items[i];
            bool can_take = tr->type == TR_EPS ||
                            tr->type == TR_CAPTURE_BEGIN ||
                            tr->type == TR_CAPTURE_END ||
                            (tr->type == TR_ANCHOR_BEGIN && pos == 0) ||
                            (tr->type == TR_ANCHOR_END && pos == text_len);
            if (!can_take) {
                continue;
            }

            const int *next_captures = base;
            if (tr->type == TR_CAPTURE_BEGIN || tr->type == TR_CAPTURE_END) {
                if (tr->group_id == 0 || tr->group_id > capture_count) {
                    return false;
                }
                memcpy(candidate, base, set->slot_count * sizeof(*candidate));
                size_t slot = (tr->group_id - 1) * 2;
                if (tr->type == TR_CAPTURE_END) {
                    ++slot;
                }
                candidate[slot] = (int)pos;
                next_captures = candidate;
            }
            if (!tagged_set_push(set, tr->to, next_captures)) {
                return false;
            }
        }
    }
    return true;
}

static bool trans_matches(const transition_t *tr, unsigned char c)
{
    return tr->type == TR_ANY ||
           (tr->type == TR_CLASS && rx_charset_has(tr->cls, c));
}

static const int *tagged_find_captures(const tagged_set_t *set, int state)
{
    for (size_t i = 0; i < set->len; ++i) {
        if (set->states[i] == state) {
            return set->captures + i * set->slot_count;
        }
    }
    return NULL;
}

int nfa_capture_span(const nfa_t *nfa,
                     const char *text,
                     size_t start,
                     size_t end,
                     rx_match_t *matches,
                     size_t nmatch,
                     size_t capture_count)
{
    if (nfa == NULL || text == NULL || matches == NULL ||
        nmatch < 2 || capture_count == 0) {
        return RX_BADPAT;
    }
    size_t text_len = strlen(text);
    if (start > end || end > text_len || capture_count > SIZE_MAX / 2) {
        return RX_BADPAT;
    }

    size_t slot_count = capture_count * 2;
    int *initial = (int *)malloc(slot_count * sizeof(*initial));
    int *base = (int *)malloc(slot_count * sizeof(*base));
    int *candidate = (int *)malloc(slot_count * sizeof(*candidate));
    tagged_set_t current = {0};
    tagged_set_t next = {0};
    if (initial == NULL || base == NULL || candidate == NULL ||
        !tagged_set_init(&current, nfa->len, slot_count) ||
        !tagged_set_init(&next, nfa->len, slot_count)) {
        free(initial);
        free(base);
        free(candidate);
        tagged_set_free(&current);
        tagged_set_free(&next);
        return RX_ESPACE;
    }
    for (size_t slot = 0; slot < slot_count; ++slot) {
        initial[slot] = -1;
    }

    bool ok = tagged_set_push(&current, nfa->start, initial) &&
              tagged_expand_closure(nfa, &current, start, text_len,
                                    capture_count, base, candidate);
    for (size_t pos = start; ok && pos < end; ++pos) {
        tagged_set_clear(&next);
        unsigned char byte = (unsigned char)text[pos];
        for (size_t i = 0; ok && i < current.len; ++i) {
            int state = current.states[i];
            const int *captures = current.captures + i * slot_count;
            const transition_vec_t *vec = &nfa->states[state].trans;
            for (size_t j = 0; j < vec->len; ++j) {
                if (trans_matches(&vec->items[j], byte) &&
                    !tagged_set_push(&next, vec->items[j].to, captures)) {
                    ok = false;
                    break;
                }
            }
        }
        if (ok) {
            ok = tagged_expand_closure(nfa, &next, pos + 1, text_len,
                                       capture_count, base, candidate);
        }
        tagged_set_t temp = current;
        current = next;
        next = temp;
    }

    int rc = RX_NOMATCH;
    if (ok) {
        const int *captures = tagged_find_captures(&current, nfa->accept);
        if (captures != NULL) {
            size_t groups_to_copy = capture_count;
            if (groups_to_copy + 1 > nmatch) {
                groups_to_copy = nmatch - 1;
            }
            for (size_t group = 0; group < groups_to_copy; ++group) {
                matches[group + 1].rm_so = captures[group * 2];
                matches[group + 1].rm_eo = captures[group * 2 + 1];
            }
            rc = RX_OK;
        }
    } else {
        rc = RX_ESPACE;
    }

    free(initial);
    free(base);
    free(candidate);
    tagged_set_free(&current);
    tagged_set_free(&next);
    return rc;
}
