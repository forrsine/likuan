#include "charset.h"

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
