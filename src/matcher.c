#include "matcher.h"

#include "charset.h"
#include "regex_engine.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    int *items;
    size_t len;
    size_t cap;
    unsigned char *seen;
    size_t seen_len;
} state_set_t;

static bool set_init(state_set_t *set, size_t state_count)
{
    memset(set, 0, sizeof(*set));
    set->seen = (unsigned char *)calloc(state_count, 1);
    if (set->seen == NULL) {
        return false;
    }
    set->seen_len = state_count;
    return true;
}

static void set_free(state_set_t *set)
{
    free(set->items);
    free(set->seen);
    memset(set, 0, sizeof(*set));
}

static bool set_push(state_set_t *set, int state)
{
    if (state < 0 || (size_t)state >= set->seen_len) {
        return false;
    }
    if (set->seen[state]) {
        return true;
    }
    if (set->len == set->cap) {
        size_t next_cap = set->cap == 0 ? 16 : set->cap * 2;
        int *next = (int *)realloc(set->items, next_cap * sizeof(*next));
        if (next == NULL) {
            return false;
        }
        set->items = next;
        set->cap = next_cap;
    }
    set->seen[state] = 1;
    set->items[set->len++] = state;
    return true;
}

static void set_clear(state_set_t *set)
{
    for (size_t i = 0; i < set->len; ++i) {
        set->seen[set->items[i]] = 0;
    }
    set->len = 0;
}

static bool set_has_state(const state_set_t *set, int state)
{
    return state >= 0 && (size_t)state < set->seen_len && set->seen[state] != 0;
}

static bool add_closure(const nfa_t *nfa, state_set_t *set, int state,
                        size_t pos, size_t text_len)
{
    size_t first_new = set->len;
    if (!set_push(set, state)) {
        return false;
    }
    if (set->len == first_new) {
        return true;
    }

    for (size_t pending = first_new; pending < set->len; ++pending) {
        int current = set->items[pending];
        const transition_vec_t *vec = &nfa->states[current].trans;
        for (size_t i = 0; i < vec->len; ++i) {
            const transition_t *tr = &vec->items[i];
            bool can_take = tr->type == TR_EPS ||
                            tr->type == TR_CAPTURE_BEGIN ||
                            tr->type == TR_CAPTURE_END ||
                            (tr->type == TR_ANCHOR_BEGIN && pos == 0) ||
                            (tr->type == TR_ANCHOR_END && pos == text_len);
            if (can_take && !set_push(set, tr->to)) {
                return false;
            }
        }
    }
    return true;
}

static bool trans_matches(const transition_t *tr, unsigned char c)
{
    if (tr->type == TR_ANY) {
        return true;
    }
    if (tr->type == TR_CLASS) {
        return rx_charset_has(tr->cls, c);
    }
    return false;
}

int nfa_run_from(const nfa_t *nfa, const char *text, size_t start, size_t *end_out)
{
    size_t text_len = strlen(text);
    state_set_t cur = {0};
    state_set_t next = {0};

    if (!set_init(&cur, nfa->len) || !set_init(&next, nfa->len)) {
        set_free(&cur);
        set_free(&next);
        return RX_ESPACE;
    }

    if (!add_closure(nfa, &cur, nfa->start, start, text_len)) {
        set_free(&cur);
        set_free(&next);
        return RX_ESPACE;
    }

    bool found = false;
    size_t best = start;
    if (set_has_state(&cur, nfa->accept)) {
        found = true;
    }

    for (size_t pos = start; pos < text_len; ++pos) {
        unsigned char c = (unsigned char)text[pos];
        set_clear(&next);

        for (size_t i = 0; i < cur.len; ++i) {
            int state = cur.items[i];
            const transition_vec_t *vec = &nfa->states[state].trans;
            for (size_t j = 0; j < vec->len; ++j) {
                const transition_t *tr = &vec->items[j];
                if (trans_matches(tr, c) &&
                    !add_closure(nfa, &next, tr->to, pos + 1, text_len)) {
                    set_free(&cur);
                    set_free(&next);
                    return RX_ESPACE;
                }
            }
        }

        if (next.len == 0) {
            break;
        }
        state_set_t tmp = cur;
        cur = next;
        next = tmp;

        if (set_has_state(&cur, nfa->accept)) {
            found = true;
            best = pos + 1;
        }
    }

    set_free(&cur);
    set_free(&next);
    if (!found) {
        return RX_NOMATCH;
    }
    *end_out = best;
    return RX_OK;
}
