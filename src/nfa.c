#include "nfa.h"

#include "ast.h"
#include "charset.h"
#include "regex_engine.h"

#include <stdlib.h>
#include <string.h>

void nfa_init(nfa_t *nfa)
{
    memset(nfa, 0, sizeof(*nfa));
    nfa->start = -1;
    nfa->accept = -1;
}

void nfa_free(nfa_t *nfa)
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

int nfa_add_state(nfa_t *nfa)
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

int nfa_add_transition(nfa_t *nfa, int from, int to, trans_type_t type,
                       const unsigned char cls[RX_CHARSET_BYTES])
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

int nfa_add_eps(nfa_t *nfa, int from, int to)
{
    return nfa_add_transition(nfa, from, to, TR_EPS, NULL);
}

int nfa_compile_ast(nfa_t *nfa, const ast_node_t *node, frag_t *out)
{
    if (node == NULL) {
        return RX_BADPAT;
    }

    if (node->type == AST_GROUP) {
        return nfa_compile_ast(nfa, node->left, out);
    }

    if (node->type == AST_CONCAT || node->type == AST_ALT) {
        frag_t a, b;
        int rc = nfa_compile_ast(nfa, node->left, &a);
        if (rc != RX_OK) {
            return rc;
        }
        rc = nfa_compile_ast(nfa, node->right, &b);
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
        int rc = nfa_compile_ast(nfa, node->left, &inner);
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
            rc = nfa_compile_ast(nfa, node->left, &part);
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
            rc = nfa_compile_ast(nfa, &optional, &part);
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
            rc = nfa_compile_ast(nfa, &tail, &part);
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
