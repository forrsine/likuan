#include "matcher.h"

#include "charset.h"
#include "regex_engine.h"

#include <stdlib.h>
#include <stdint.h>
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
<<<<<<< Updated upstream
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
=======
                can_take = true;
            } else if (tr->type == TR_CAPTURE_BEGIN || tr->type == TR_CAPTURE_END) {
                can_take = true;
            } else if (tr->type == TR_ANCHOR_BEGIN) {
                can_take = pos == 0;
            } else if (tr->type == TR_ANCHOR_END) {
                can_take = pos == text_len;
>>>>>>> Stashed changes
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
        memcpy(next_captures,
               set->captures,
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
           captures,
           set->slot_count * sizeof(*captures));
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
        memcpy(base,
               set->captures + pending * set->slot_count,
               set->slot_count * sizeof(*base));
        const transition_vec_t *vec = &nfa->states[state].trans;
        for (size_t i = 0; i < vec->len; ++i) {
            const transition_t *tr = &vec->items[i];
            bool can_take = tr->type == TR_EPS ||
                            (tr->type == TR_ANCHOR_BEGIN && pos == 0) ||
                            (tr->type == TR_ANCHOR_END && pos == text_len) ||
                            tr->type == TR_CAPTURE_BEGIN ||
                            tr->type == TR_CAPTURE_END;
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
    if (nfa == NULL || text == NULL || matches == NULL || nmatch < 2 || capture_count == 0) {
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
              tagged_expand_closure(nfa,
                                    &current,
                                    start,
                                    text_len,
                                    capture_count,
                                    base,
                                    candidate);
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
            ok = tagged_expand_closure(nfa,
                                       &next,
                                       pos + 1,
                                       text_len,
                                       capture_count,
                                       base,
                                       candidate);
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
