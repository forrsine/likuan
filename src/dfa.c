#include "dfa.h"

#include "charset.h"
#include "regex_engine.h"

#include <stdlib.h>
#include <string.h>

static void bitset_add(unsigned char *set, size_t state)
{
    set[state >> 3] |= (unsigned char)(1u << (state & 7u));
}

static bool bitset_has(const unsigned char *set, size_t state)
{
    return (set[state >> 3] & (unsigned char)(1u << (state & 7u))) != 0;
}

static bool bitset_empty(const unsigned char *set, size_t bytes)
{
    for (size_t i = 0; i < bytes; ++i) {
        if (set[i] != 0) {
            return false;
        }
    }
    return true;
}

void dfa_init(dfa_t *dfa)
{
    memset(dfa, 0, sizeof(*dfa));
    dfa->start = -1;
    for (size_t i = 0; i < RX_DFA_CONTEXT_COUNT; ++i) {
        dfa->start_states[i] = -1;
    }
}

void dfa_free(dfa_t *dfa)
{
    if (dfa == NULL) {
        return;
    }
    for (size_t i = 0; i < dfa->len; ++i) {
        free(dfa->states[i].nfa_set);
    }
    free(dfa->states);
    free(dfa->hash_buckets);
    dfa_init(dfa);
}

bool dfa_can_build(const nfa_t *nfa)
{
    if (nfa == NULL || nfa->len == 0 || nfa->start < 0 || nfa->accept < 0 ||
        (size_t)nfa->start >= nfa->len || (size_t)nfa->accept >= nfa->len) {
        return false;
    }
    for (size_t state = 0; state < nfa->len; ++state) {
        const transition_vec_t *transitions = &nfa->states[state].trans;
        for (size_t i = 0; i < transitions->len; ++i) {
            const transition_t *transition = &transitions->items[i];
            if (transition->to < 0 || (size_t)transition->to >= nfa->len ||
                transition->type < TR_EPS || transition->type > TR_CAPTURE_END) {
                return false;
            }
        }
    }
    return true;
}

static int epsilon_closure(const nfa_t *nfa,
                           unsigned char *set,
                           bool at_begin,
                           bool at_end)
{
    int *queue = (int *)malloc(nfa->len * sizeof(*queue));
    if (queue == NULL) {
        return RX_ESPACE;
    }

    size_t head = 0;
    size_t tail = 0;
    for (size_t state = 0; state < nfa->len; ++state) {
        if (bitset_has(set, state)) {
            queue[tail++] = (int)state;
        }
    }

    while (head < tail) {
        int state = queue[head++];
        const transition_vec_t *transitions = &nfa->states[state].trans;
        for (size_t i = 0; i < transitions->len; ++i) {
            const transition_t *transition = &transitions->items[i];
            bool can_take = transition->type == TR_EPS ||
                            transition->type == TR_CAPTURE_BEGIN ||
                            transition->type == TR_CAPTURE_END ||
                            (transition->type == TR_ANCHOR_BEGIN && at_begin) ||
                            (transition->type == TR_ANCHOR_END && at_end);
            if (can_take && !bitset_has(set, (size_t)transition->to)) {
                bitset_add(set, (size_t)transition->to);
                queue[tail++] = transition->to;
            }
        }
    }

    free(queue);
    return RX_OK;
}

static int move_and_close(const nfa_t *nfa,
                          const unsigned char *source,
                          unsigned char byte,
                          bool at_end,
                          unsigned char *target,
                          size_t set_bytes)
{
    memset(target, 0, set_bytes);
    for (size_t state = 0; state < nfa->len; ++state) {
        if (!bitset_has(source, state)) {
            continue;
        }
        const transition_vec_t *transitions = &nfa->states[state].trans;
        for (size_t i = 0; i < transitions->len; ++i) {
            const transition_t *transition = &transitions->items[i];
            if (transition->type == TR_ANY ||
                (transition->type == TR_CLASS && rx_charset_has(transition->cls, byte))) {
                bitset_add(target, (size_t)transition->to);
            }
        }
    }
    if (bitset_empty(target, set_bytes)) {
        return RX_OK;
    }
    return epsilon_closure(nfa, target, false, at_end);
}

static bool bytes_equivalent(const nfa_t *nfa, unsigned char left, unsigned char right)
{
    for (size_t state = 0; state < nfa->len; ++state) {
        const transition_vec_t *transitions = &nfa->states[state].trans;
        for (size_t i = 0; i < transitions->len; ++i) {
            const transition_t *transition = &transitions->items[i];
            if (transition->type == TR_CLASS &&
                rx_charset_has(transition->cls, left) !=
                    rx_charset_has(transition->cls, right)) {
                return false;
            }
        }
    }
    return true;
}

static void build_character_classes(dfa_t *dfa, const nfa_t *nfa)
{
    dfa->class_count = 0;
    for (unsigned int byte = 0; byte < RX_DFA_ALPHABET_SIZE; ++byte) {
        size_t cls = 0;
        while (cls < dfa->class_count &&
               !bytes_equivalent(nfa,
                                 (unsigned char)byte,
                                 dfa->class_representative[cls])) {
            ++cls;
        }
        if (cls == dfa->class_count) {
            dfa->class_representative[cls] = (unsigned char)byte;
            ++dfa->class_count;
        }
        dfa->class_of[byte] = (unsigned short)cls;
    }
}

static size_t dfa_set_hash(const unsigned char *set, size_t bytes)
{
    size_t hash = (size_t)2166136261u;
    for (size_t i = 0; i < bytes; ++i) {
        hash ^= set[i];
        hash *= (size_t)16777619u;
    }
    return hash;
}

static int dfa_prepare_hash(dfa_t *dfa)
{
    dfa->hash_bucket_count = 8192;
    dfa->hash_buckets =
        (int *)malloc(dfa->hash_bucket_count * sizeof(*dfa->hash_buckets));
    if (dfa->hash_buckets == NULL) {
        dfa->hash_bucket_count = 0;
        return RX_ESPACE;
    }
    for (size_t i = 0; i < dfa->hash_bucket_count; ++i) {
        dfa->hash_buckets[i] = -1;
    }
    return RX_OK;
}

static int dfa_find_state(const dfa_t *dfa, const unsigned char *set)
{
    size_t bucket = dfa_set_hash(set, dfa->set_bytes) % dfa->hash_bucket_count;
    for (int state = dfa->hash_buckets[bucket];
         state >= 0;
         state = dfa->states[state].hash_next) {
        if (memcmp(dfa->states[state].nfa_set, set, dfa->set_bytes) == 0) {
            return state;
        }
    }
    return -1;
}

static int dfa_add_state(dfa_t *dfa, const nfa_t *nfa, const unsigned char *set)
{
    if (dfa->len >= RX_DFA_STATE_LIMIT) {
        return -2;
    }
    if (dfa->len == dfa->cap) {
        size_t next_cap = dfa->cap == 0 ? 16 : dfa->cap * 2;
        dfa_state_t *next = (dfa_state_t *)realloc(dfa->states, next_cap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        memset(next + dfa->cap, 0, (next_cap - dfa->cap) * sizeof(*next));
        dfa->states = next;
        dfa->cap = next_cap;
    }

    dfa_state_t *state = &dfa->states[dfa->len];
    state->nfa_set = (unsigned char *)malloc(dfa->set_bytes);
    if (state->nfa_set == NULL) {
        return -1;
    }
    memcpy(state->nfa_set, set, dfa->set_bytes);
    for (size_t i = 0; i < RX_DFA_ALPHABET_SIZE; ++i) {
        state->transitions[i] = -1;
        state->final_transitions[i] = -1;
    }
    state->is_accept = bitset_has(set, (size_t)nfa->accept);
    int index = (int)dfa->len++;
    size_t bucket = dfa_set_hash(set, dfa->set_bytes) % dfa->hash_bucket_count;
    state->hash_next = dfa->hash_buckets[bucket];
    dfa->hash_buckets[bucket] = index;
    return index;
}

static int dfa_resolve_state(dfa_t *dfa,
                             const nfa_t *nfa,
                             const unsigned char *set,
                             int *state_out)
{
    if (bitset_empty(set, dfa->set_bytes)) {
        *state_out = -1;
        return RX_OK;
    }

    int state = dfa_find_state(dfa, set);
    if (state < 0) {
        state = dfa_add_state(dfa, nfa, set);
        if (state < 0) {
            return state == -2 ? RX_EUNSUPPORTED : RX_ESPACE;
        }
    }
    *state_out = state;
    return RX_OK;
}

static void set_class_transition(dfa_t *dfa,
                                 size_t state,
                                 size_t cls,
                                 int target,
                                 bool at_end)
{
    for (size_t byte = 0; byte < RX_DFA_ALPHABET_SIZE; ++byte) {
        if (dfa->class_of[byte] == cls) {
            if (at_end) {
                dfa->states[state].final_transitions[byte] = target;
            } else {
                dfa->states[state].transitions[byte] = target;
            }
        }
    }
}

int dfa_build(dfa_t *dfa, const nfa_t *nfa)
{
    if (dfa == NULL || !dfa_can_build(nfa)) {
        return RX_BADPAT;
    }

    dfa->set_bytes = (nfa->len + 7u) / 8u;
    build_character_classes(dfa, nfa);

    int rc = dfa_prepare_hash(dfa);
    if (rc != RX_OK) {
        return rc;
    }

    unsigned char *start_set = (unsigned char *)calloc(dfa->set_bytes, 1);
    unsigned char *target_set = (unsigned char *)malloc(dfa->set_bytes);
    if (start_set == NULL || target_set == NULL) {
        free(start_set);
        free(target_set);
        free(dfa->hash_buckets);
        dfa->hash_buckets = NULL;
        dfa->hash_bucket_count = 0;
        return RX_ESPACE;
    }

    rc = RX_OK;
    for (size_t context = 0; context < RX_DFA_CONTEXT_COUNT; ++context) {
        memset(start_set, 0, dfa->set_bytes);
        bitset_add(start_set, (size_t)nfa->start);
        rc = epsilon_closure(nfa,
                             start_set,
                             (context & DFA_CONTEXT_BEGIN) != 0,
                             (context & DFA_CONTEXT_END) != 0);
        if (rc != RX_OK) {
            goto done;
        }
        rc = dfa_resolve_state(dfa, nfa, start_set, &dfa->start_states[context]);
        if (rc != RX_OK) {
            goto done;
        }
    }
    dfa->start = dfa->start_states[DFA_CONTEXT_BEGIN];

    for (size_t current = 0; current < dfa->len; ++current) {
        for (size_t cls = 0; cls < dfa->class_count; ++cls) {
            unsigned char byte = dfa->class_representative[cls];
            int target = -1;

            rc = move_and_close(nfa,
                                dfa->states[current].nfa_set,
                                byte,
                                false,
                                target_set,
                                dfa->set_bytes);
            if (rc != RX_OK) {
                goto done;
            }
            rc = dfa_resolve_state(dfa, nfa, target_set, &target);
            if (rc != RX_OK) {
                goto done;
            }
            set_class_transition(dfa, current, cls, target, false);

            rc = move_and_close(nfa,
                                dfa->states[current].nfa_set,
                                byte,
                                true,
                                target_set,
                                dfa->set_bytes);
            if (rc != RX_OK) {
                goto done;
            }
            rc = dfa_resolve_state(dfa, nfa, target_set, &target);
            if (rc != RX_OK) {
                goto done;
            }
            set_class_transition(dfa, current, cls, target, true);
        }
    }

done:
    free(start_set);
    free(target_set);
    free(dfa->hash_buckets);
    dfa->hash_buckets = NULL;
    dfa->hash_bucket_count = 0;
    if (rc == RX_OK) {
        dfa->subset_state_count = dfa->len;
    }
    return rc;
}

int dfa_run_from(const dfa_t *dfa, const char *text, size_t start, size_t *end_out)
{
    if (dfa == NULL || text == NULL || end_out == NULL || dfa->start < 0) {
        return RX_BADPAT;
    }

    size_t text_len = strlen(text);
    if (start > text_len) {
        return RX_NOMATCH;
    }
    size_t context = (start == 0 ? DFA_CONTEXT_BEGIN : DFA_CONTEXT_MID) |
                     (start == text_len ? DFA_CONTEXT_END : DFA_CONTEXT_MID);
    int state = dfa->start_states[context];
    bool found = dfa->states[state].is_accept;
    size_t best = start;

    for (size_t pos = start; pos < text_len; ++pos) {
        unsigned char byte = (unsigned char)text[pos];
        int next = pos + 1 == text_len
                       ? dfa->states[state].final_transitions[byte]
                       : dfa->states[state].transitions[byte];
        if (next < 0) {
            break;
        }
        state = next;
        if (dfa->states[state].is_accept) {
            found = true;
            best = pos + 1;
        }
    }
    if (!found) {
        return RX_NOMATCH;
    }
    *end_out = best;
    return RX_OK;
}

size_t dfa_transition_count(const dfa_t *dfa)
{
    if (dfa == NULL) {
        return 0;
    }
    size_t count = 0;
    for (size_t state = 0; state < dfa->len; ++state) {
        for (size_t byte = 0; byte < RX_DFA_ALPHABET_SIZE; ++byte) {
            if (dfa->states[state].transitions[byte] >= 0) {
                ++count;
            }
        }
    }
    return count;
}

size_t dfa_final_override_count(const dfa_t *dfa)
{
    if (dfa == NULL) {
        return 0;
    }
    size_t count = 0;
    for (size_t state = 0; state < dfa->len; ++state) {
        for (size_t byte = 0; byte < RX_DFA_ALPHABET_SIZE; ++byte) {
            if (dfa->states[state].final_transitions[byte] !=
                dfa->states[state].transitions[byte]) {
                ++count;
            }
        }
    }
    return count;
}

static bool dfa_is_start_state(const dfa_t *dfa, size_t state)
{
    for (size_t context = 0; context < RX_DFA_CONTEXT_COUNT; ++context) {
        if (dfa->start_states[context] == (int)state) {
            return true;
        }
    }
    return false;
}

static const char *dfa_state_role(const dfa_t *dfa, size_t state)
{
    if (dfa_is_start_state(dfa, state) && dfa->states[state].is_accept) {
        return "start+accept";
    }
    if (dfa_is_start_state(dfa, state)) {
        return "start";
    }
    if (dfa->states[state].is_accept) {
        return "accept";
    }
    return "-";
}

static bool target_has_bytes(const dfa_state_t *state,
                             size_t target,
                             bool final_only,
                             unsigned char cls[RX_CHARSET_BYTES])
{
    rx_charset_clear(cls);
    bool found = false;
    for (size_t byte = 0; byte < RX_DFA_ALPHABET_SIZE; ++byte) {
        int actual = final_only ? state->final_transitions[byte] : state->transitions[byte];
        if (actual == (int)target &&
            (!final_only || actual != state->transitions[byte])) {
            rx_charset_add(cls, (unsigned char)byte);
            found = true;
        }
    }
    return found;
}

int dfa_dump_table(const dfa_t *dfa, FILE *out)
{
    if (dfa == NULL || out == NULL || dfa->start < 0) {
        return -1;
    }

    fprintf(out,
            "DFA states=%zu subset_states=%zu byte_transitions=%zu classes=%zu "
            "final_overrides=%zu start=%d\n",
            dfa->len,
            dfa->subset_state_count,
            dfa_transition_count(dfa),
            dfa->class_count,
            dfa_final_override_count(dfa),
            dfa->start);
    fprintf(out,
            "START contexts: mid=%d begin=%d end=%d begin+end=%d\n",
            dfa->start_states[DFA_CONTEXT_MID],
            dfa->start_states[DFA_CONTEXT_BEGIN],
            dfa->start_states[DFA_CONTEXT_END],
            dfa->start_states[DFA_CONTEXT_BEGIN_END]);
    fputs("STATE  ROLE          CONTEXT  INPUT               TO\n", out);
    fputs("-----  ------------  -------  ------------------  -----\n", out);
    for (size_t state = 0; state < dfa->len; ++state) {
        bool wrote = false;
        unsigned char cls[RX_CHARSET_BYTES];
        for (size_t target = 0; target < dfa->len; ++target) {
            if (!target_has_bytes(&dfa->states[state], target, false, cls)) {
                continue;
            }
            if (!wrote) {
                fprintf(out, "%-5zu  %-12s  %-7s  ", state, dfa_state_role(dfa, state), "NORMAL");
            } else {
                fprintf(out, "%-5s  %-12s  %-7s  ", "", "", "NORMAL");
            }
            rx_charset_dump(cls, out);
            fprintf(out, "%*s%zu\n", 20, "", target);
            wrote = true;
        }
        for (size_t target = 0; target < dfa->len; ++target) {
            if (!target_has_bytes(&dfa->states[state], target, true, cls)) {
                continue;
            }
            if (!wrote) {
                fprintf(out, "%-5zu  %-12s  %-7s  ", state, dfa_state_role(dfa, state), "FINAL");
            } else {
                fprintf(out, "%-5s  %-12s  %-7s  ", "", "", "FINAL");
            }
            rx_charset_dump(cls, out);
            fprintf(out, "%*s%zu\n", 20, "", target);
            wrote = true;
        }
        if (!wrote) {
            fprintf(out, "%-5zu  %-12s  %-7s  %-18s  %s\n",
                    state, dfa_state_role(dfa, state), "-", "-", "-");
        }
    }
    return ferror(out) ? -1 : 0;
}
