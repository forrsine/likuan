#ifndef REGEX_ENGINE_H
#define REGEX_ENGINE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rx_regex rx_regex_t;

typedef struct {
    int rm_so;
    int rm_eo;
} rx_match_t;

typedef enum {
    RX_OK = 0,
    RX_NOMATCH = 1,
    RX_BADPAT = 2,
    RX_EPAREN = 3,
    RX_EBRACK = 4,
    RX_EBRACE = 5,
    RX_BADRPT = 6,
    RX_ESPACE = 7,
    RX_EUNSUPPORTED = 8
} rx_status_t;

enum {
    RX_FLAG_NONE = 0u,
    RX_FLAG_DFA = 1u << 0
};

int regex_compile(rx_regex_t **out, const char *pattern, unsigned flags);
int regex_compile_ex(rx_regex_t **out,
                     const char *pattern,
                     unsigned flags,
                     char *error,
                     size_t error_size);
int regex_match(const rx_regex_t *re, const char *text, rx_match_t *matches, size_t nmatch);
int regex_search(const rx_regex_t *re, const char *text, rx_match_t *matches, size_t nmatch);
int regex_findall(const rx_regex_t *re,
                  const char *text,
                  int (*on_match)(const rx_match_t *matches, size_t nmatch, void *userdata),
                  void *userdata);
size_t regex_capture_count(const rx_regex_t *re);
const char *regex_error(const rx_regex_t *re);
const char *regex_status_string(int status);
void regex_free(rx_regex_t *re);

#ifdef __cplusplus
}
#endif

#endif
