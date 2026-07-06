#include "regex_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef RX_HAVE_POSIX_REGEX
#define RX_HAVE_POSIX_REGEX 0
#endif

#if RX_HAVE_POSIX_REGEX
#include <regex.h>
#endif

typedef struct {
    const char *name;
    const char *pattern;
    const char *text;
} bench_case_t;

typedef struct {
    double compile_us;
    double match_us;
    int status;
    rx_regex_stats_t stats;
} engine_result_t;

static double elapsed_us(clock_t begin, clock_t end, size_t iterations)
{
    return ((double)(end - begin) * 1000000.0 / (double)CLOCKS_PER_SEC) /
           (double)iterations;
}

static int run_engine(const bench_case_t *bench,
                      unsigned flags,
                      size_t iterations,
                      engine_result_t *result)
{
    size_t compile_iterations = iterations / 10;
    if (compile_iterations < 200) {
        compile_iterations = 200;
    }
    if (compile_iterations > 2000) {
        compile_iterations = 2000;
    }

    clock_t begin = clock();
    for (size_t i = 0; i < compile_iterations; ++i) {
        rx_regex_t *compiled = NULL;
        int rc = regex_compile(&compiled, bench->pattern, flags);
        regex_free(compiled);
        if (rc != RX_OK) {
            return rc;
        }
    }
    result->compile_us = elapsed_us(begin, clock(), compile_iterations);

    rx_regex_t *re = NULL;
    int rc = regex_compile(&re, bench->pattern, flags);
    if (rc != RX_OK) {
        return rc;
    }
    regex_get_stats(re, &result->stats);

    rx_match_t match;
    int checksum = 0;
    begin = clock();
    for (size_t i = 0; i < iterations; ++i) {
        rc = regex_search(re, bench->text, &match, 1);
        checksum += rc == RX_OK ? match.rm_eo : rc;
    }
    result->match_us = elapsed_us(begin, clock(), iterations);
    result->status = rc;
    regex_free(re);
    if (checksum == -1) {
        fputs("", stderr);
    }
    return RX_OK;
}

#if RX_HAVE_POSIX_REGEX
static int run_posix(const bench_case_t *bench,
                     size_t iterations,
                     double *compile_us,
                     double *match_us,
                     int *status)
{
    size_t compile_iterations = iterations / 10;
    if (compile_iterations < 200) {
        compile_iterations = 200;
    }
    if (compile_iterations > 2000) {
        compile_iterations = 2000;
    }

    clock_t begin = clock();
    for (size_t i = 0; i < compile_iterations; ++i) {
        regex_t re;
        int rc = regcomp(&re, bench->pattern, REG_EXTENDED);
        if (rc != 0) {
            return rc;
        }
        regfree(&re);
    }
    *compile_us = elapsed_us(begin, clock(), compile_iterations);

    regex_t re;
    int rc = regcomp(&re, bench->pattern, REG_EXTENDED);
    if (rc != 0) {
        return rc;
    }
    regmatch_t match;
    int checksum = 0;
    begin = clock();
    for (size_t i = 0; i < iterations; ++i) {
        rc = regexec(&re, bench->text, 1, &match, 0);
        checksum += rc == 0 ? (int)match.rm_eo : rc;
    }
    *match_us = elapsed_us(begin, clock(), iterations);
    *status = rc;
    regfree(&re);
    if (checksum == -1) {
        fputs("", stderr);
    }
    return 0;
}
#endif

static void print_engine_result(const char *name,
                                const char *mode,
                                const engine_result_t *result)
{
    printf("%-14s %-6s %10.3f %10.3f %7zu %7zu %7zu %7zu\n",
           name,
           mode,
           result->compile_us,
           result->match_us,
           result->stats.nfa_states,
           result->stats.dfa_subset_states,
           result->stats.dfa_states,
           result->stats.dfa_character_classes);
}

static void write_engine_csv(FILE *csv,
                             const char *name,
                             const char *mode,
                             size_t iterations,
                             double compile_us,
                             double match_us,
                             const rx_regex_stats_t *stats,
                             double speed_vs_nfa)
{
    if (csv == NULL) {
        return;
    }
    fprintf(csv, "%s,%s,%zu,%.6f,%.6f,%zu,%zu,%zu,%zu,%.2f\n",
            name,
            mode,
            iterations,
            compile_us,
            match_us,
            stats != NULL ? stats->nfa_states : 0,
            stats != NULL ? stats->dfa_subset_states : 0,
            stats != NULL ? stats->dfa_states : 0,
            stats != NULL ? stats->dfa_character_classes : 0,
            speed_vs_nfa);
}

static int run_case(const bench_case_t *bench,
                    size_t iterations,
                    int compare_posix,
                    FILE *csv)
{
    engine_result_t nfa = {0};
    engine_result_t dfa = {0};
    int rc = run_engine(bench, RX_FLAG_NONE, iterations, &nfa);
    if (rc != RX_OK) {
        fprintf(stderr, "NFA benchmark compile failed for /%s/: rc=%d\n",
                bench->pattern, rc);
        return 1;
    }
    rc = run_engine(bench, RX_FLAG_DFA, iterations, &dfa);
    if (rc != RX_OK) {
        fprintf(stderr, "DFA benchmark compile failed for /%s/: rc=%d\n",
                bench->pattern, rc);
        return 1;
    }
    print_engine_result(bench->name, "NFA", &nfa);
    print_engine_result(bench->name, "DFA", &dfa);
    double dfa_speed_vs_nfa =
        dfa.match_us > 0.0 ? nfa.match_us / dfa.match_us * 100.0 : 0.0;
    if (nfa.match_us > 0.0 && dfa.match_us > 0.0) {
        printf("  DFA/NFA matching speed: %.1f%%\n", dfa_speed_vs_nfa);
    } else {
        puts("  Matching interval is below timer resolution; increase --iterations.");
    }
    write_engine_csv(csv, bench->name, "NFA", iterations,
                     nfa.compile_us, nfa.match_us, &nfa.stats, 100.0);
    write_engine_csv(csv, bench->name, "DFA", iterations,
                     dfa.compile_us, dfa.match_us, &dfa.stats, dfa_speed_vs_nfa);

#if RX_HAVE_POSIX_REGEX
    if (compare_posix) {
        double compile_us = 0.0;
        double match_us = 0.0;
        int status = 0;
        rc = run_posix(bench, iterations, &compile_us, &match_us, &status);
        if (rc != 0) {
            fprintf(stderr, "POSIX benchmark compile failed for /%s/: rc=%d\n",
                    bench->pattern, rc);
            return 1;
        }
        printf("%-14s %-6s %10.3f %10.3f %7s %7s %7s %7s\n",
               bench->name, "POSIX", compile_us, match_us,
               "-", "-", "-", "-");
        write_engine_csv(csv, bench->name, "POSIX", iterations,
                         compile_us, match_us, NULL,
                         match_us > 0.0 ? nfa.match_us / match_us * 100.0 : 0.0);
        if (dfa.match_us > 0.0) {
            printf("  DFA/POSIX matching speed: %.1f%%\n",
                   match_us / dfa.match_us * 100.0);
        }
        (void)status;
    }
#else
    if (compare_posix) {
        printf("  POSIX regex.h unavailable in this build; run in WSL/Linux for comparison.\n");
    }
#endif
    return 0;
}

static void usage(const char *program)
{
    fprintf(stderr,
            "usage: %s [--iterations N] [--compare-posix] [--csv FILE] "
            "[PATTERN TEXT]\n",
            program);
}

int main(int argc, char **argv)
{
    size_t iterations = 20000;
    int compare_posix = 0;
    const char *csv_path = NULL;
    const char *pattern = NULL;
    const char *text = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--compare-posix") == 0) {
            compare_posix = 1;
        } else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
            csv_path = argv[++i];
        } else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            char *end = NULL;
            unsigned long value = strtoul(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || value == 0) {
                usage(argv[0]);
                return 2;
            }
            iterations = (size_t)value;
        } else if (pattern == NULL) {
            pattern = argv[i];
        } else if (text == NULL) {
            text = argv[i];
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if ((pattern == NULL) != (text == NULL)) {
        usage(argv[0]);
        return 2;
    }

    const bench_case_t builtins[] = {
        {"word-digits", "[a-z]+[0-9]{2,4}", "prefix abcdefghijklmnop1234 suffix"},
        {"alternation", "(error|warning|info):[ ]+[a-z]+", "2026 warning: network ready"},
        {"nested-plus", "^(a+)+$", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
        {"identifier", "[A-Za-z_][A-Za-z0-9_]*", "value_12345"}
    };

    FILE *csv = NULL;
    if (csv_path != NULL) {
#ifdef _MSC_VER
        if (fopen_s(&csv, csv_path, "w") != 0) {
            csv = NULL;
        }
#else
        csv = fopen(csv_path, "w");
#endif
        if (csv == NULL) {
            fprintf(stderr, "cannot open CSV output: %s\n", csv_path);
            return 1;
        }
        fputs("case,mode,iterations,compile_us,match_us,nfa_states,"
              "subset_states,mindfa_states,classes,speed_vs_nfa_percent\n",
              csv);
    }

    printf("iterations=%zu\n", iterations);
    printf("%-14s %-6s %10s %10s %7s %7s %7s %7s\n",
           "case", "mode", "compile_us", "match_us",
           "NFA", "subset", "minDFA", "classes");
    if (pattern != NULL) {
        bench_case_t custom;
        custom.name = "custom";
        custom.pattern = pattern;
        custom.text = text;
        int rc = run_case(&custom, iterations, compare_posix, csv);
        if (csv != NULL) {
            fclose(csv);
        }
        return rc;
    }
    int result = 0;
    for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); ++i) {
        if (run_case(&builtins[i], iterations, compare_posix, csv) != 0) {
            result = 1;
            break;
        }
    }
    if (csv != NULL) {
        fclose(csv);
    }
    return result;
}
