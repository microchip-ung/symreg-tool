// SPDX-License-Identifier: MIT
/* Copyright (c) 2026 Microchip Technology Inc. and its subsidiaries.
 * Microchip switch symbolic read tool
 *
 * License: MIT
 * Copyright (c) 2026 Microchip Technology Inc.
 */

#include "symreg.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>

/* sr_zalloc
 * malloc that returns zero initialized memory
 * and terminates program in case of no memory
 */
void *sr_zalloc(size_t size)
{
    void *p = calloc(1, size);
    if (p == NULL) {
        fprintf(stderr, "No memory for calloc!\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

/* sr_strdup
 * strdup that terminates program in case of no memory
 */
char *sr_strdup(const char *s)
{
    assert(s);
    char *c = strdup(s);
    if (c == NULL) {
        fprintf(stderr, "No memory for strdup(%s)!\n", s);
        exit(EXIT_FAILURE);
    }
    return c;
}

/* sr_strslice
 * Get a slice of a string
 * src:   Pointer to source string to slice.
 * dst:   Pointer to destination buffer.
 * start: Index to start from.
 * end:   Index to stop before (<- NOTE).
 */
void sr_strslice(const char *src, char *dst, size_t start, size_t end)
{
    size_t i, j = 0;

    assert(src && dst);

    for (i = start; i < end; i++) {
        dst[j++] = src[i];
    }
    dst[j] = 0;
}

/* sr_str2number
 *
 * Convert a string to number
 *
 * str
 *   A pointer to a string to convert to long long number.
 *   Prepend with 0 for octal or 0x for hex.
 *
 * val
 *   Pointer to converted value.
 *
 * Returns
 *   0 if conversion resulted in a valid number, where min <= number => max.
 *  -1 if str is not a number.
 *  -2 if number is out of range.
 *  -3 if str contains garbage after number.
 */
int sr_str2number(const char *str, long long *val, long long min, long long max)
{
    char *end;
    long long ll;

    assert(str && val);
    ll = strtoll(str, &end, 0);

    if (end == str) {
        return -1;
    } else if ((ll < min) || (ll > max)) {
        return -2;
    } else if ('\0' != *end) {
        return -3;
    } else {
        *val = ll;
        return 0;
    }
}

int sr_str2u32(const char *str, u32 *val)
{
    long long ll;
    int rc = sr_str2number(str, &ll, 0, 0xffffffff);
    if (rc == 0) {
        *val = (u32)ll;
    }
    return rc;
}

/* vim: set ts=4 sw=4 sts=4 tw=120 cc=80,120 et ft=c cino=(0,w1,Ws,t0,\:s,l1 :*/
