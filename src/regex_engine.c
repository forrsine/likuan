#include "regex_engine.h"

#include "ast.h"
#include "dfa.h"
#include "matcher.h"
#include "nfa.h"
#include "parser.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RX_MAX_CAPTURES 32

struct rx_regex {
    char *pattern;
    unsigned flags;
    char error[256];
    ast_node_t *ast;
    size_t capture_count;
    nfa_t nfa;
    dfa_t dfa;
};

static void set_error(char *dst, size_t dst_len, const char *fmt, ...)
{
    if (dst == NULL || dst_len == 0) {
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

int regex_compile_ex(rx_regex_t **out,
                     const char *pattern,
                     unsigned flags,
                     char *error,
                     size_t error_size)
{
    if (error != NULL && error_size > 0) {
        error[0] = '\0';
    }
    if (out == NULL || pattern == NULL) {
        set_error(error, error_size, "output pointer and pattern must not be null");
        return RX_BADPAT;
    }
    *out = NULL;
    if ((flags & ~RX_FLAG_DFA) != 0) {
        set_error(error, error_size, "unsupported compile flags: 0x%X", flags);
        return RX_EUNSUPPORTED;
    }

    rx_regex_t *re = (rx_regex_t *)calloc(1, sizeof(*re));
    if (re == NULL) {
        set_error(error, error_size, "out of memory");
        return RX_ESPACE;
    }
    re->flags = flags;
    nfa_init(&re->nfa);
    dfa_init(&re->dfa);
    re->pattern = rx_strdup(pattern);
    if (re->pattern == NULL) {
        set_error(error, error_size, "out of memory");
        regex_free(re);
        return RX_ESPACE;
    }

    int parse_status = RX_BADPAT;
    re->ast = rx_parse_pattern(pattern, re->error, &parse_status, &re->capture_count);
    if (re->ast == NULL) {
        set_error(error, error_size, "%s", re->error);
        regex_free(re);
        return parse_status;
    }

    frag_t frag;
    int rc = nfa_compile_ast(&re->nfa, re->ast, &frag);
    if (rc != RX_OK) {
        set_error(re->error, sizeof(re->error), "failed to build NFA");
        set_error(error, error_size, "%s", re->error);
        regex_free(re);
        return rc;
    }
    re->nfa.start = frag.start;
    re->nfa.accept = frag.accept;

    if ((flags & RX_FLAG_DFA) != 0) {
        rc = dfa_build(&re->dfa, &re->nfa);
        if (rc == RX_OK) {
            rc = dfa_minimize(&re->dfa);
        }
        if (rc != RX_OK) {
            if (rc == RX_EUNSUPPORTED) {
                set_error(re->error, sizeof(re->error),
                          "failed to build DFA (state limit %u)", RX_DFA_STATE_LIMIT);
            } else {
                set_error(re->error, sizeof(re->error),
                          "out of memory while building or minimizing DFA");
            }
            set_error(error, error_size, "%s", re->error);
            regex_free(re);
            return rc;
        }
    }

    *out = re;
    return RX_OK;
}

int regex_compile(rx_regex_t **out, const char *pattern, unsigned flags)
{
    return regex_compile_ex(out, pattern, flags, NULL, 0);
}

static int regex_run_from(const rx_regex_t *re, const char *text, size_t start,
                           size_t *end_out, int *captures, size_t total_groups)
{
    if ((re->flags & RX_FLAG_DFA) != 0) {
        int rc = dfa_run_from(&re->dfa, text, start, end_out);
        if (rc == RX_OK && captures != NULL && total_groups > 0) {
            captures[0] = (int)start;
            captures[1] = (int)(*end_out);
        }
        return rc;
    }
    return nfa_run_from(&re->nfa, text, start, end_out, captures, total_groups);
}

static void populate_matches(rx_match_t *matches, size_t nmatch,
                              const int *captures, size_t total_groups)
{
    if (matches == NULL || nmatch == 0 || captures == NULL) {
        return;
    }
    size_t n = nmatch < total_groups ? nmatch : total_groups;
    for (size_t i = 0; i < n; ++i) {
        matches[i].rm_so = captures[i * 2];
        matches[i].rm_eo = captures[i * 2 + 1];
    }
}

int regex_match(const rx_regex_t *re, const char *text, rx_match_t *matches, size_t nmatch)
{
    if (re == NULL || text == NULL) {
        return RX_BADPAT;
    }
    size_t total_groups = re->capture_count + 1;
    int captures[RX_MAX_CAPTURES * 2];
    memset(captures, -1, sizeof(captures));

    size_t end = 0;
    int rc = regex_run_from(re, text, 0, &end, captures, total_groups);
    if (rc != RX_OK) {
        return rc;
    }
    if (end != strlen(text)) {
        return RX_NOMATCH;
    }
    populate_matches(matches, nmatch, captures, total_groups);
    return RX_OK;
}

int regex_search(const rx_regex_t *re, const char *text, rx_match_t *matches, size_t nmatch)
{
    if (re == NULL || text == NULL) {
        return RX_BADPAT;
    }
    size_t total_groups = re->capture_count + 1;
    int captures[RX_MAX_CAPTURES * 2];

    size_t len = strlen(text);
    for (size_t start = 0; start <= len; ++start) {
        size_t end = 0;
        memset(captures, -1, sizeof(captures));
        int rc = regex_run_from(re, text, start, &end, captures, total_groups);
        if (rc == RX_OK) {
            populate_matches(matches, nmatch, captures, total_groups);
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

    size_t total_groups = re->capture_count + 1;
    int captures[RX_MAX_CAPTURES * 2];
    size_t len = strlen(text);
    size_t start = 0;

    while (start <= len) {
        rx_match_t m_buf[16];
        size_t n = total_groups < 16 ? total_groups : 16;
        bool found = false;

        for (size_t probe = start; probe <= len; ++probe) {
            size_t end = 0;
            memset(captures, -1, sizeof(captures));
            int rc = regex_run_from(re, text, probe, &end, captures, total_groups);
            if (rc == RX_OK) {
                populate_matches(m_buf, n, captures, total_groups);
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
        int keep_going = on_match(m_buf, n, userdata);
        if (!keep_going) {
            return RX_OK;
        }
        if (m_buf[0].rm_eo > m_buf[0].rm_so) {
            start = (size_t)m_buf[0].rm_eo;
        } else {
            start = (size_t)m_buf[0].rm_eo + 1;
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

const char *regex_status_string(int status)
{
    switch (status) {
    case RX_OK: return "success";
    case RX_NOMATCH: return "no match";
    case RX_BADPAT: return "invalid pattern";
    case RX_EPAREN: return "unbalanced parentheses";
    case RX_EBRACK: return "invalid character class";
    case RX_EBRACE: return "invalid repetition braces";
    case RX_BADRPT: return "invalid repetition operator";
    case RX_ESPACE: return "out of memory";
    case RX_EUNSUPPORTED: return "unsupported feature or limit exceeded";
    default: return "unknown regex status";
    }
}

void regex_free(rx_regex_t *re)
{
    if (re == NULL) {
        return;
    }
    free(re->pattern);
    ast_free(re->ast);
    nfa_free(&re->nfa);
    dfa_free(&re->dfa);
    free(re);
}
