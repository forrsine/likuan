#include "charset.h"

#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>

void rx_charset_clear(unsigned char cls[RX_CHARSET_BYTES])
{
    memset(cls, 0, RX_CHARSET_BYTES);
}

void rx_charset_add(unsigned char cls[RX_CHARSET_BYTES], unsigned char c)
{
    cls[c >> 3] |= (unsigned char)(1u << (c & 7u));
}

bool rx_charset_has(const unsigned char cls[RX_CHARSET_BYTES], unsigned char c)
{
    return (cls[c >> 3] & (unsigned char)(1u << (c & 7u))) != 0;
}

void rx_charset_add_range(unsigned char cls[RX_CHARSET_BYTES], unsigned char lo, unsigned char hi)
{
    for (unsigned int c = lo; c <= hi; ++c) {
        rx_charset_add(cls, (unsigned char)c);
        if (c == UCHAR_MAX) {
            break;
        }
    }
}

void rx_charset_invert(unsigned char cls[RX_CHARSET_BYTES])
{
    for (size_t i = 0; i < RX_CHARSET_BYTES; ++i) {
        cls[i] = (unsigned char)~cls[i];
    }
}

void rx_charset_add_digit(unsigned char cls[RX_CHARSET_BYTES])
{
    rx_charset_add_range(cls, '0', '9');
}

void rx_charset_add_word(unsigned char cls[RX_CHARSET_BYTES])
{
    rx_charset_add_range(cls, 'a', 'z');
    rx_charset_add_range(cls, 'A', 'Z');
    rx_charset_add_range(cls, '0', '9');
    rx_charset_add(cls, '_');
}

void rx_charset_add_space(unsigned char cls[RX_CHARSET_BYTES])
{
    rx_charset_add(cls, ' ');
    rx_charset_add(cls, '\t');
    rx_charset_add(cls, '\n');
    rx_charset_add(cls, '\r');
    rx_charset_add(cls, '\f');
    rx_charset_add(cls, '\v');
}

static void dump_char(FILE *out, unsigned char c)
{
    switch (c) {
    case '\n': fputs("\\n", out); return;
    case '\r': fputs("\\r", out); return;
    case '\t': fputs("\\t", out); return;
    case '\\': fputs("\\\\", out); return;
    case ']': fputs("\\]", out); return;
    case '-': fputs("\\-", out); return;
    default:
        if (isprint(c)) {
            fputc(c, out);
        } else {
            fprintf(out, "\\x%02X", (unsigned)c);
        }
    }
}

int rx_charset_dump(const unsigned char cls[RX_CHARSET_BYTES], FILE *out)
{
    if (cls == NULL || out == NULL) {
        return -1;
    }

    fputc('[', out);
    for (unsigned int i = 0; i < 256;) {
        if (!rx_charset_has(cls, (unsigned char)i)) {
            ++i;
            continue;
        }
        unsigned int end = i;
        while (end + 1 < 256 && rx_charset_has(cls, (unsigned char)(end + 1))) {
            ++end;
        }
        dump_char(out, (unsigned char)i);
        if (end >= i + 2) {
            fputc('-', out);
            dump_char(out, (unsigned char)end);
        } else if (end == i + 1) {
            dump_char(out, (unsigned char)end);
        }
        i = end + 1;
    }
    fputc(']', out);
    return ferror(out) ? -1 : 0;
}
