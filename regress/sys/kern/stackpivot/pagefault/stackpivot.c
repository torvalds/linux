/*
 * Copyright (c) 2018 Todd Mortimer <mortimer@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>

#include "../pivot.h"

static size_t *realstack;
static char *scan;
static size_t scansize = UINT16_MAX;

/* scan some memory crossing a page boundary */
size_t dowork() {
    size_t b = 0;
    size_t i;
    for (i = 0; i < scansize; ++i)
        b += *scan++;

    // We should be killed before we get here
    pivot(realstack);
    return b;
}

void doexit() {
    _exit(0);
}

int main() {

    /* allocate some memory to scan */
    scan = mmap(NULL, scansize, PROT_READ, MAP_PRIVATE | MAP_ANON, -1, 0);

    /* set up a rop chain on the real stack for syscalls */
    size_t stack[10];
    stack[0] = (size_t)doexit;
    realstack = stack;

    /* set up a basic alt stack on the heap that does some work */
    size_t *newstack = calloc(10, sizeof(size_t));
    printf("non-MAP_STACK stack at %p\n", newstack);
    newstack[0] = (size_t)dowork;
    pivot(newstack);
    return 0;
}
