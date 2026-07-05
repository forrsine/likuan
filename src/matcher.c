#include "matcher.h"

#include "charset.h"
#include "regex_engine.h"

#include <stdlib.h>
#include <string.h>

#define RX_MAX_CAPTURES 32

typedef struct {
    int *states;
    int *caps;
    size_t len;
    size_t cap;
    size_t num_slots;
    unsigned char *seen;
    size_t seen_len;
    int *queue;
    size_t q_head;
    size_t q_tail;
    size_t q_cap;
    int *state_to_idx;
} closure_t;

static bool closure_init(closure_t *c, size_t max_states, size_t num_slots)
{
    memset(c, 0, sizeof(*c));
    c->num_slots = num_slots;
    c->seen = (unsigned char *)calloc(max_states, 1);
    if (c->seen == NULL) {
        return false;
    }
    c->seen_len = max_states;
    c->queue = (int *)malloc(max_states * sizeof(int));
    if (c->queue == NULL) {
        free(c->seen);
        c->seen = NULL;
        return false;
    }
    c->q_cap = max_states;
    c->state_to_idx = (int *)malloc(max_states * sizeof(int));
    if (c->state_to_idx == NULL) {
        free(c->seen);
        c->seen = NULL;
        free(c->queue);
        c->queue = NULL;
        return false;
    }
    for (size_t i = 0; i < max_states; ++i) {
        c->state_to_idx[i] = -1;
    }
    return true;
}

static void closure_free(closure_t *c)
{
    free(c->states);
    free(c->caps);
    free(c->seen);
    free(c->queue);
    free(c->state_to_idx);
    memset(c, 0, sizeof(*c));
}

static void closure_clear(closure_t *c)
{
    for (size_t i = 0; i < c->len; ++i) {
        c->state_to_idx[c->states[i]] = -1;
    }
    c->len = 0;
    c->q_head = 0;
    c->q_tail = 0;
    memset(c->seen, 0, c->seen_len);
}

static int closure_find(const closure_t *c, int state)
{
    if (state < 0 || (size_t)state >= c->seen_len) {
        return -1;
    }
    return c->state_to_idx[state];
}

static int *closure_caps(const closure_t *c, size_t idx)
{
    return c->caps + idx * c->num_slots;
}

static bool closure_reserve(closure_t *c)
{
    if (c->len < c->cap) {
        return true;
    }
    size_t new_cap = c->cap == 0 ? 16 : c->cap * 2;
    int *new_states = (int *)realloc(c->states, new_cap * sizeof(int));
    if (new_states == NULL) {
        return false;
    }
    c->states = new_states;
    int *new_caps = (int *)realloc(c->caps, new_cap * c->num_slots * sizeof(int));
    if (new_caps == NULL) {
        return false;
    }
    c->caps = new_caps;
    c->cap = new_cap;
    return true;
}

static void copy_caps(int *dst, const int *src, size_t num_slots)
{
    memcpy(dst, src, num_slots * sizeof(int));
}

static bool merge_caps(int *dst, const int *src, size_t num_slots)
{
    bool changed = false;
    for (size_t i = 0; i < num_slots; ++i) {
        if (src[i] == -1) {
            continue;
        }
        if (dst[i] < src[i]) {
            dst[i] = src[i];
            changed = true;
        }
    }
    return changed;
}

static int closure_add_entry(closure_t *c, int state, const int *in_caps)
{
    int existing = closure_find(c, state);
    if (existing >= 0) {
        if (merge_caps(closure_caps(c, existing), in_caps, c->num_slots)) {
            if (c->q_tail < c->q_cap) {
                c->queue[c->q_tail++] = existing;
            }
        }
        return existing;
    }

    if (!closure_reserve(c)) {
        return -1;
    }

    int idx = (int)c->len;
    c->states[idx] = state;
    copy_caps(closure_caps(c, idx), in_caps, c->num_slots);
    c->seen[state] = 1;
    c->state_to_idx[state] = idx;
    c->len++;

    if (c->q_tail < c->q_cap) {
        c->queue[c->q_tail++] = idx;
    }
    return idx;
}

static bool closure_expand(closure_t *c, const nfa_t *nfa, size_t pos, size_t text_len)
{
    int local_caps[RX_MAX_CAPTURES * 2];

    while (c->q_head < c->q_tail) {
        int idx = c->queue[c->q_head++];
        int state = c->states[idx];

        copy_caps(local_caps, closure_caps(c, idx), c->num_slots);

        const transition_vec_t *vec = &nfa->states[state].trans;
        for (size_t i = 0; i < vec->len; ++i) {
            const transition_t *tr = &vec->items[i];
            bool take = false;
            int new_caps[RX_MAX_CAPTURES * 2];

            copy_caps(new_caps, local_caps, c->num_slots);

            if (tr->type == TR_EPS) {
                take = true;
            } else if (tr->type == TR_SAVE_START) {
                int slot = tr->save_slot * 2;
                if (slot >= 0 && (size_t)slot < c->num_slots) {
                    new_caps[slot] = (int)pos;
                }
                take = true;
            } else if (tr->type == TR_SAVE_END) {
                int slot = tr->save_slot * 2 + 1;
                if (slot >= 0 && (size_t)slot < c->num_slots) {
                    new_caps[slot] = (int)pos;
                }
                take = true;
            } else if (tr->type == TR_ANCHOR_BEGIN && pos == 0) {
                take = true;
            } else if (tr->type == TR_ANCHOR_END && pos == text_len) {
                take = true;
            }

            if (take && closure_add_entry(c, tr->to, new_caps) < 0) {
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

int nfa_run_from(const nfa_t *nfa, const char *text, size_t start, size_t *end_out,
                 int *captures, size_t total_groups)
{
    size_t text_len = strlen(text);
    size_t num_slots = total_groups * 2;
    closure_t cur, next;

    if (!closure_init(&cur, nfa->len, num_slots) ||
        !closure_init(&next, nfa->len, num_slots)) {
        closure_free(&cur);
        closure_free(&next);
        return RX_ESPACE;
    }

    int empty_caps[RX_MAX_CAPTURES * 2];
    memset(empty_caps, -1, (size_t)((int)num_slots * (int)sizeof(int)));
    if (closure_add_entry(&cur, nfa->start, empty_caps) < 0 ||
        !closure_expand(&cur, nfa, start, text_len)) {
        closure_free(&cur);
        closure_free(&next);
        return RX_ESPACE;
    }

    bool found = false;
    size_t best = start;
    int best_caps[RX_MAX_CAPTURES * 2];
    memset(best_caps, -1, sizeof(best_caps));

    int accept_idx = closure_find(&cur, nfa->accept);
    if (accept_idx >= 0) {
        found = true;
        best = start;
        copy_caps(best_caps, closure_caps(&cur, accept_idx), num_slots);
    }

    for (size_t pos = start; pos < text_len; ++pos) {
        unsigned char c = (unsigned char)text[pos];
        closure_clear(&next);

        for (size_t i = 0; i < cur.len; ++i) {
            int state = cur.states[i];
            const int *cur_caps_state = closure_caps(&cur, i);
            const transition_vec_t *vec = &nfa->states[state].trans;
            for (size_t j = 0; j < vec->len; ++j) {
                const transition_t *tr = &vec->items[j];
                if (trans_matches(tr, c)) {
                    if (closure_add_entry(&next, tr->to, cur_caps_state) < 0) {
                        closure_free(&cur);
                        closure_free(&next);
                        return RX_ESPACE;
                    }
                }
            }
        }

        if (next.len == 0) {
            break;
        }
        if (!closure_expand(&next, nfa, pos + 1, text_len)) {
            closure_free(&cur);
            closure_free(&next);
            return RX_ESPACE;
        }

        closure_t tmp = cur;
        cur = next;
        next = tmp;

        accept_idx = closure_find(&cur, nfa->accept);
        if (accept_idx >= 0) {
            found = true;
            best = pos + 1;
            copy_caps(best_caps, closure_caps(&cur, accept_idx), num_slots);
        }
    }

    closure_free(&cur);
    closure_free(&next);

    if (!found) {
        return RX_NOMATCH;
    }
    *end_out = best;
    if (captures != NULL && total_groups > 0) {
        memcpy(captures, best_caps, num_slots * sizeof(int));
        captures[0] = (int)start;
        captures[1] = (int)best;
    }
    return RX_OK;
}
