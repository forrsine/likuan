#include "regex_engine.h"

#include "ast.h"
#include "charset.h"
#include "parser.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    TR_EPS,
    TR_CLASS,
    TR_ANY,
    TR_ANCHOR_BEGIN,
    TR_ANCHOR_END
} trans_type_t;

typedef struct {
    int to;
    trans_type_t type;
    unsigned char cls[32];
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

struct rx_regex {
    char *pattern;
    unsigned flags;
    char error[256];
    ast_node_t *ast;
    size_t capture_count;
    nfa_t nfa;
};

typedef struct {
    int start;
    int accept;
} frag_t;

typedef struct {
    int *items;
    size_t len;
    size_t cap;
    unsigned char *seen;
    size_t seen_len;
} state_set_t;

static void set_error(char *dst, size_t dst_len, const char *fmt, ...)
{
    if (dst_len == 0) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dst, dst_len, fmt, ap);
    va_end(ap);
    dst[dst_len - 1] = '\0';
}

static char *rx_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *copy = (char *)malloc(n);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, s, n);
    return copy;
}

static void nfa_free(nfa_t *nfa)
{
    if (nfa == NULL) {
        return;
    }
    for (size_t i = 0; i < nfa->len; ++i) {
        free(nfa->states[i].trans.items);
    }
    free(nfa->states);
    memset(nfa, 0, sizeof(*nfa));
    nfa->start = -1;
    nfa->accept = -1;
}

static int nfa_add_state(nfa_t *nfa)
{
    if (nfa->len == nfa->cap) {
        size_t next_cap = nfa->cap == 0 ? 16 : nfa->cap * 2;
        nfa_state_t *next = (nfa_state_t *)realloc(nfa->states, next_cap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        memset(next + nfa->cap, 0, (next_cap - nfa->cap) * sizeof(*next));
        nfa->states = next;
        nfa->cap = next_cap;
    }
    return (int)nfa->len++;
}

static int nfa_add_transition(nfa_t *nfa, int from, int to, trans_type_t type, const unsigned char cls[32])
{
    transition_vec_t *vec = &nfa->states[from].trans;
    if (vec->len == vec->cap) {
        size_t next_cap = vec->cap == 0 ? 4 : vec->cap * 2;
        transition_t *next = (transition_t *)realloc(vec->items, next_cap * sizeof(*next));
        if (next == NULL) {
            return RX_ESPACE;
        }
        vec->items = next;
        vec->cap = next_cap;
    }
    transition_t *tr = &vec->items[vec->len++];
    memset(tr, 0, sizeof(*tr));
    tr->to = to;
    tr->type = type;
    if (cls != NULL) {
        memcpy(tr->cls, cls, sizeof(tr->cls));
    }
    return RX_OK;
}

static int nfa_add_eps(nfa_t *nfa, int from, int to)
{
    return nfa_add_transition(nfa, from, to, TR_EPS, NULL);
}

static int compile_ast(nfa_t *nfa, const ast_node_t *node, frag_t *out)
{
    if (node == NULL) {
        return RX_BADPAT;
    }

    if (node->type == AST_GROUP) {
        return compile_ast(nfa, node->left, out);
    }

    if (node->type == AST_CONCAT || node->type == AST_ALT) {
        frag_t a, b;
        int rc = compile_ast(nfa, node->left, &a);
        if (rc != RX_OK) {
            return rc;
        }
        rc = compile_ast(nfa, node->right, &b);
        if (rc != RX_OK) {
            return rc;
        }
        if (node->type == AST_CONCAT) {
            rc = nfa_add_eps(nfa, a.accept, b.start);
            if (rc != RX_OK) {
                return rc;
            }
            out->start = a.start;
            out->accept = b.accept;
            return RX_OK;
        }

        int s = nfa_add_state(nfa);
        int e = nfa_add_state(nfa);
        if (s < 0 || e < 0) {
            return RX_ESPACE;
        }
        rc = nfa_add_eps(nfa, s, a.start);
        if (rc == RX_OK) {
            rc = nfa_add_eps(nfa, s, b.start);
        }
        if (rc == RX_OK) {
            rc = nfa_add_eps(nfa, a.accept, e);
        }
        if (rc == RX_OK) {
            rc = nfa_add_eps(nfa, b.accept, e);
        }
        if (rc != RX_OK) {
            return rc;
        }
        out->start = s;
        out->accept = e;
        return RX_OK;
    }

    if (node->type == AST_STAR || node->type == AST_PLUS || node->type == AST_QUESTION) {
        frag_t inner;
        int rc = compile_ast(nfa, node->left, &inner);
        if (rc != RX_OK) {
            return rc;
        }
        int s = nfa_add_state(nfa);
        int e = nfa_add_state(nfa);
        if (s < 0 || e < 0) {
            return RX_ESPACE;
        }

        if (node->type == AST_STAR) {
            rc = nfa_add_eps(nfa, s, inner.start);
            if (rc == RX_OK) {
                rc = nfa_add_eps(nfa, s, e);
            }
            if (rc == RX_OK) {
                rc = nfa_add_eps(nfa, inner.accept, inner.start);
            }
            if (rc == RX_OK) {
                rc = nfa_add_eps(nfa, inner.accept, e);
            }
        } else if (node->type == AST_PLUS) {
            rc = nfa_add_eps(nfa, s, inner.start);
            if (rc == RX_OK) {
                rc = nfa_add_eps(nfa, inner.accept, inner.start);
            }
            if (rc == RX_OK) {
                rc = nfa_add_eps(nfa, inner.accept, e);
            }
        } else {
            rc = nfa_add_eps(nfa, s, inner.start);
            if (rc == RX_OK) {
                rc = nfa_add_eps(nfa, s, e);
            }
            if (rc == RX_OK) {
                rc = nfa_add_eps(nfa, inner.accept, e);
            }
        }
        if (rc != RX_OK) {
            return rc;
        }
        out->start = s;
        out->accept = e;
        return RX_OK;
    }

    if (node->type == AST_REPEAT) {
        frag_t result = {0, 0};
        bool have_result = false;
        int rc = RX_OK;

        for (size_t i = 0; i < node->repeat_min; ++i) {
            frag_t part;
            rc = compile_ast(nfa, node->left, &part);
            if (rc != RX_OK) {
                return rc;
            }
            if (!have_result) {
                result = part;
                have_result = true;
            } else {
                rc = nfa_add_eps(nfa, result.accept, part.start);
                if (rc != RX_OK) {
                    return rc;
                }
                result.accept = part.accept;
            }
        }

        size_t optional_count = node->repeat_unbounded ? 0 : node->repeat_max - node->repeat_min;
        for (size_t i = 0; i < optional_count; ++i) {
            ast_node_t optional = {0};
            optional.type = AST_QUESTION;
            optional.left = node->left;
            frag_t part;
            rc = compile_ast(nfa, &optional, &part);
            if (rc != RX_OK) {
                return rc;
            }
            if (!have_result) {
                result = part;
                have_result = true;
            } else {
                rc = nfa_add_eps(nfa, result.accept, part.start);
                if (rc != RX_OK) {
                    return rc;
                }
                result.accept = part.accept;
            }
        }

        if (node->repeat_unbounded) {
            ast_node_t tail = {0};
            tail.type = AST_STAR;
            tail.left = node->left;
            frag_t part;
            rc = compile_ast(nfa, &tail, &part);
            if (rc != RX_OK) {
                return rc;
            }
            if (!have_result) {
                result = part;
                have_result = true;
            } else {
                rc = nfa_add_eps(nfa, result.accept, part.start);
                if (rc != RX_OK) {
                    return rc;
                }
                result.accept = part.accept;
            }
        }

        if (!have_result) {
            int s = nfa_add_state(nfa);
            int e = nfa_add_state(nfa);
            if (s < 0 || e < 0) {
                return RX_ESPACE;
            }
            rc = nfa_add_eps(nfa, s, e);
            if (rc != RX_OK) {
                return rc;
            }
            result.start = s;
            result.accept = e;
        }

        *out = result;
        return RX_OK;
    }

    int s = nfa_add_state(nfa);
    int e = nfa_add_state(nfa);
    if (s < 0 || e < 0) {
        return RX_ESPACE;
    }

    int rc = RX_OK;
    switch (node->type) {
    case AST_EMPTY:
        rc = nfa_add_eps(nfa, s, e);
        break;
    case AST_CLASS:
        rc = nfa_add_transition(nfa, s, e, TR_CLASS, node->cls);
        break;
    case AST_DOT:
        rc = nfa_add_transition(nfa, s, e, TR_ANY, NULL);
        break;
    case AST_ANCHOR_BEGIN:
        rc = nfa_add_transition(nfa, s, e, TR_ANCHOR_BEGIN, NULL);
        break;
    case AST_ANCHOR_END:
        rc = nfa_add_transition(nfa, s, e, TR_ANCHOR_END, NULL);
        break;
    default:
        rc = RX_BADPAT;
        break;
    }
    if (rc != RX_OK) {
        return rc;
    }
    out->start = s;
    out->accept = e;
    return RX_OK;
}

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

static bool add_closure(const nfa_t *nfa, state_set_t *set, int state, size_t pos, size_t text_len)
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
            bool can_take = false;
            if (tr->type == TR_EPS) {
                can_take = true;
            } else if (tr->type == TR_ANCHOR_BEGIN) {
                can_take = pos == 0;
            } else if (tr->type == TR_ANCHOR_END) {
                can_take = pos == text_len;
            }
            if (can_take && !set_push(set, tr->to)) {
                return false;
            }
        }
    }
    return true;
}

static bool set_has_state(const state_set_t *set, int state)
{
    return state >= 0 && (size_t)state < set->seen_len && set->seen[state] != 0;
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

static int nfa_run_from(const rx_regex_t *re, const char *text, size_t start, size_t *end_out)
{
    const nfa_t *nfa = &re->nfa;
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
        best = start;
    }

    for (size_t pos = start; pos < text_len; ++pos) {
        unsigned char c = (unsigned char)text[pos];
        set_clear(&next);

        for (size_t i = 0; i < cur.len; ++i) {
            int state = cur.items[i];
            const transition_vec_t *vec = &nfa->states[state].trans;
            for (size_t j = 0; j < vec->len; ++j) {
                const transition_t *tr = &vec->items[j];
                if (trans_matches(tr, c)) {
                    if (!add_closure(nfa, &next, tr->to, pos + 1, text_len)) {
                        set_free(&cur);
                        set_free(&next);
                        return RX_ESPACE;
                    }
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

int regex_compile(rx_regex_t **out, const char *pattern, unsigned flags)
{
    if (out == NULL || pattern == NULL) {
        return RX_BADPAT;
    }
    *out = NULL;

    rx_regex_t *re = (rx_regex_t *)calloc(1, sizeof(*re));
    if (re == NULL) {
        return RX_ESPACE;
    }
    re->flags = flags;
    re->nfa.start = -1;
    re->nfa.accept = -1;
    re->pattern = rx_strdup(pattern);
    if (re->pattern == NULL) {
        regex_free(re);
        return RX_ESPACE;
    }

    int parse_status = RX_BADPAT;
    re->ast = rx_parse_pattern(pattern, re->error, &parse_status, &re->capture_count);
    if (re->ast == NULL) {
        regex_free(re);
        return parse_status;
    }

    frag_t frag;
    int rc = compile_ast(&re->nfa, re->ast, &frag);
    if (rc != RX_OK) {
        set_error(re->error, sizeof(re->error), "failed to build NFA");
        regex_free(re);
        return rc;
    }
    re->nfa.start = frag.start;
    re->nfa.accept = frag.accept;

    *out = re;
    return RX_OK;
}

int regex_match(const rx_regex_t *re, const char *text, rx_match_t *matches, size_t nmatch)
{
    if (re == NULL || text == NULL) {
        return RX_BADPAT;
    }
    size_t end = 0;
    int rc = nfa_run_from(re, text, 0, &end);
    if (rc != RX_OK) {
        return rc;
    }
    if (end != strlen(text)) {
        return RX_NOMATCH;
    }
    if (matches != NULL && nmatch > 0) {
        matches[0].rm_so = 0;
        matches[0].rm_eo = (int)end;
    }
    return RX_OK;
}

int regex_search(const rx_regex_t *re, const char *text, rx_match_t *matches, size_t nmatch)
{
    if (re == NULL || text == NULL) {
        return RX_BADPAT;
    }
    size_t len = strlen(text);
    for (size_t start = 0; start <= len; ++start) {
        size_t end = 0;
        int rc = nfa_run_from(re, text, start, &end);
        if (rc == RX_OK) {
            if (matches != NULL && nmatch > 0) {
                matches[0].rm_so = (int)start;
                matches[0].rm_eo = (int)end;
            }
            return RX_OK;
        }
        if (rc != RX_NOMATCH) {
            return rc;
        }
    }
    return RX_NOMATCH;
}

int regex_findall(const rx_regex_t *re,
                  const char *text,
                  int (*on_match)(const rx_match_t *matches, size_t nmatch, void *userdata),
                  void *userdata)
{
    if (re == NULL || text == NULL || on_match == NULL) {
        return RX_BADPAT;
    }

    size_t len = strlen(text);
    size_t start = 0;
    while (start <= len) {
        rx_match_t m = {-1, -1};
        bool found = false;

        for (size_t probe = start; probe <= len; ++probe) {
            size_t end = 0;
            int rc = nfa_run_from(re, text, probe, &end);
            if (rc == RX_OK) {
                m.rm_so = (int)probe;
                m.rm_eo = (int)end;
                found = true;
                break;
            }
            if (rc != RX_NOMATCH) {
                return rc;
            }
        }

        if (!found) {
            return RX_OK;
        }
        int keep_going = on_match(&m, 1, userdata);
        if (!keep_going) {
            return RX_OK;
        }
        if (m.rm_eo > m.rm_so) {
            start = (size_t)m.rm_eo;
        } else {
            start = (size_t)m.rm_eo + 1;
        }
    }
    return RX_OK;
}

const char *regex_error(const rx_regex_t *re)
{
    if (re == NULL) {
        return "no regex object";
    }
    return re->error[0] ? re->error : "no error";
}

void regex_free(rx_regex_t *re)
{
    if (re == NULL) {
        return;
    }
    free(re->pattern);
    ast_free(re->ast);
    nfa_free(&re->nfa);
    free(re);
}
