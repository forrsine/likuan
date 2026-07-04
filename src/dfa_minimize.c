#include "dfa.h"

#include "regex_engine.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t block;
    size_t symbol;
} work_item_t;

typedef struct {
    work_item_t *items;
    size_t len;
    size_t cap;
    size_t head;
    size_t symbol_count;
    unsigned char *present;
} worklist_t;

static void worklist_free(worklist_t *work)
{
    free(work->items);
    free(work->present);
    memset(work, 0, sizeof(*work));
}

static bool worklist_init(worklist_t *work, size_t max_blocks, size_t symbol_count)
{
    memset(work, 0, sizeof(*work));
    work->symbol_count = symbol_count;
    work->present = (unsigned char *)calloc(max_blocks * symbol_count, 1);
    return work->present != NULL;
}

static bool worklist_push(worklist_t *work, size_t block, size_t symbol)
{
    size_t present_index = block * work->symbol_count + symbol;
    if (work->present[present_index]) {
        return true;
    }
    if (work->len == work->cap) {
        size_t next_cap = work->cap == 0 ? 32 : work->cap * 2;
        work_item_t *next =
            (work_item_t *)realloc(work->items, next_cap * sizeof(*next));
        if (next == NULL) {
            return false;
        }
        work->items = next;
        work->cap = next_cap;
    }
    work->items[work->len].block = block;
    work->items[work->len].symbol = symbol;
    ++work->len;
    work->present[present_index] = 1;
    return true;
}

static bool worklist_pop(worklist_t *work, work_item_t *item)
{
    if (work->head >= work->len) {
        return false;
    }
    *item = work->items[work->head++];
    work->present[item->block * work->symbol_count + item->symbol] = 0;
    return true;
}

static size_t transition_target(const dfa_t *dfa,
                                size_t state,
                                size_t symbol,
                                size_t dead_state)
{
    if (state == dead_state) {
        return dead_state;
    }

    bool final_context = symbol >= dfa->class_count;
    size_t cls = symbol % dfa->class_count;
    unsigned char byte = dfa->class_representative[cls];
    int target = final_context
                     ? dfa->states[state].final_transitions[byte]
                     : dfa->states[state].transitions[byte];
    return target < 0 ? dead_state : (size_t)target;
}

static int initialize_partitions(const dfa_t *dfa,
                                 int *block_of,
                                 size_t *block_size,
                                 size_t dead_state,
                                 size_t *block_count_out)
{
    size_t accepting = 0;
    for (size_t state = 0; state < dfa->len; ++state) {
        if (dfa->states[state].is_accept) {
            ++accepting;
        }
    }

    size_t total_states = dfa->len + 1;
    size_t block_count = accepting == 0 ? 1 : 2;
    for (size_t state = 0; state < total_states; ++state) {
        bool is_accept = state != dead_state && dfa->states[state].is_accept;
        int block = accepting == 0 ? 0 : (is_accept ? 0 : 1);
        block_of[state] = block;
        ++block_size[block];
    }
    *block_count_out = block_count;
    return RX_OK;
}

static int refine_partitions(const dfa_t *dfa,
                             int *block_of,
                             size_t *block_size,
                             size_t *block_count,
                             size_t dead_state)
{
    size_t total_states = dfa->len + 1;
    size_t symbol_count = dfa->class_count * 2;
    unsigned char *in_preimage = (unsigned char *)malloc(total_states);
    size_t *hit_count = (size_t *)calloc(total_states, sizeof(*hit_count));
    worklist_t work;
    if (in_preimage == NULL || hit_count == NULL ||
        !worklist_init(&work, total_states, symbol_count)) {
        free(in_preimage);
        free(hit_count);
        return RX_ESPACE;
    }

    size_t initial = 0;
    if (*block_count == 2 && block_size[1] < block_size[0]) {
        initial = 1;
    }
    for (size_t symbol = 0; symbol < symbol_count; ++symbol) {
        if (!worklist_push(&work, initial, symbol)) {
            worklist_free(&work);
            free(in_preimage);
            free(hit_count);
            return RX_ESPACE;
        }
    }

    work_item_t item;
    while (worklist_pop(&work, &item)) {
        memset(in_preimage, 0, total_states);
        memset(hit_count, 0, total_states * sizeof(*hit_count));

        for (size_t state = 0; state < total_states; ++state) {
            size_t target = transition_target(dfa, state, item.symbol, dead_state);
            if (block_of[target] == (int)item.block) {
                in_preimage[state] = 1;
                ++hit_count[block_of[state]];
            }
        }

        size_t blocks_before_split = *block_count;
        for (size_t block = 0; block < blocks_before_split; ++block) {
            size_t hits = hit_count[block];
            if (hits == 0 || hits == block_size[block]) {
                continue;
            }

            size_t new_block = (*block_count)++;
            for (size_t state = 0; state < total_states; ++state) {
                if (block_of[state] == (int)block && in_preimage[state]) {
                    block_of[state] = (int)new_block;
                }
            }
            block_size[new_block] = hits;
            block_size[block] -= hits;

            for (size_t symbol = 0; symbol < symbol_count; ++symbol) {
                size_t index = block * symbol_count + symbol;
                size_t next_block;
                if (work.present[index]) {
                    next_block = new_block;
                } else {
                    next_block = block_size[new_block] < block_size[block]
                                     ? new_block
                                     : block;
                }
                if (!worklist_push(&work, next_block, symbol)) {
                    worklist_free(&work);
                    free(in_preimage);
                    free(hit_count);
                    return RX_ESPACE;
                }
            }
        }
    }

    worklist_free(&work);
    free(in_preimage);
    free(hit_count);
    return RX_OK;
}

static int rebuild_dfa(dfa_t *dfa, const int *block_of, size_t block_count)
{
    int *block_to_new = (int *)malloc(block_count * sizeof(*block_to_new));
    int *representative = (int *)malloc(block_count * sizeof(*representative));
    if (block_to_new == NULL || representative == NULL) {
        free(block_to_new);
        free(representative);
        return RX_ESPACE;
    }
    for (size_t block = 0; block < block_count; ++block) {
        block_to_new[block] = -1;
        representative[block] = -1;
    }

    size_t new_len = 0;
    for (size_t state = 0; state < dfa->len; ++state) {
        size_t block = (size_t)block_of[state];
        if (block_to_new[block] < 0) {
            block_to_new[block] = (int)new_len++;
            representative[block] = (int)state;
        }
    }
    if (new_len == dfa->len) {
        free(block_to_new);
        free(representative);
        return RX_OK;
    }

    dfa_state_t *new_states =
        (dfa_state_t *)calloc(new_len, sizeof(*new_states));
    if (new_states == NULL) {
        free(block_to_new);
        free(representative);
        return RX_ESPACE;
    }

    for (size_t state = 0; state < new_len; ++state) {
        new_states[state].nfa_set = (unsigned char *)calloc(dfa->set_bytes, 1);
        if (new_states[state].nfa_set == NULL) {
            for (size_t i = 0; i < state; ++i) {
                free(new_states[i].nfa_set);
            }
            free(new_states);
            free(block_to_new);
            free(representative);
            return RX_ESPACE;
        }
        for (size_t byte = 0; byte < RX_DFA_ALPHABET_SIZE; ++byte) {
            new_states[state].transitions[byte] = -1;
            new_states[state].final_transitions[byte] = -1;
        }
    }

    for (size_t old_state = 0; old_state < dfa->len; ++old_state) {
        int new_state = block_to_new[block_of[old_state]];
        new_states[new_state].is_accept = dfa->states[old_state].is_accept;
        for (size_t byte = 0; byte < dfa->set_bytes; ++byte) {
            new_states[new_state].nfa_set[byte] |= dfa->states[old_state].nfa_set[byte];
        }
    }

    for (size_t block = 0; block < block_count; ++block) {
        if (representative[block] < 0) {
            continue;
        }
        int new_state = block_to_new[block];
        const dfa_state_t *old = &dfa->states[representative[block]];
        for (size_t byte = 0; byte < RX_DFA_ALPHABET_SIZE; ++byte) {
            int target = old->transitions[byte];
            int final_target = old->final_transitions[byte];
            if (target >= 0) {
                new_states[new_state].transitions[byte] =
                    block_to_new[block_of[target]];
            }
            if (final_target >= 0) {
                new_states[new_state].final_transitions[byte] =
                    block_to_new[block_of[final_target]];
            }
        }
    }

    for (size_t context = 0; context < RX_DFA_CONTEXT_COUNT; ++context) {
        dfa->start_states[context] =
            block_to_new[block_of[dfa->start_states[context]]];
    }
    dfa->start = dfa->start_states[DFA_CONTEXT_BEGIN];

    for (size_t state = 0; state < dfa->len; ++state) {
        free(dfa->states[state].nfa_set);
    }
    free(dfa->states);
    dfa->states = new_states;
    dfa->len = new_len;
    dfa->cap = new_len;

    free(block_to_new);
    free(representative);
    return RX_OK;
}

int dfa_minimize(dfa_t *dfa)
{
    if (dfa == NULL || dfa->len == 0 || dfa->class_count == 0 ||
        dfa->start < 0) {
        return RX_BADPAT;
    }

    size_t total_states = dfa->len + 1;
    int *block_of = (int *)malloc(total_states * sizeof(*block_of));
    size_t *block_size = (size_t *)calloc(total_states, sizeof(*block_size));
    if (block_of == NULL || block_size == NULL) {
        free(block_of);
        free(block_size);
        return RX_ESPACE;
    }

    size_t block_count = 0;
    int rc = initialize_partitions(dfa,
                                   block_of,
                                   block_size,
                                   dfa->len,
                                   &block_count);
    if (rc == RX_OK) {
        rc = refine_partitions(dfa,
                               block_of,
                               block_size,
                               &block_count,
                               dfa->len);
    }
    if (rc == RX_OK) {
        rc = rebuild_dfa(dfa, block_of, block_count);
    }

    free(block_of);
    free(block_size);
    return rc;
}
