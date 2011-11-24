/*
 *  Copyright (c) 2008 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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
#ifndef _PROJECT_H_
#define _PROJECT_H_
#include "queue.h"

#define OS_TIMER_FUNC(fn) void fn(A_HANDLE hdl, void *context)
#define OS_TIMER_FUNC_PTR(fn) void (* fn)(A_HANDLE hdl, void *context)
#define OS_CANCEL_TIMER(timer_hdl) A_UNTIMEOUT(timer_hdl)
#define OS_SET_TIMER(timer_hdl, period, repeat) A_TIMEOUT_MS(timer_hdl, period, repeat)
#define OS_INIT_TIMER(timer_hdl, fn, arg) A_INIT_TIMER(timer_hdl, fn, arg)

typedef A_TIMER os_timer_t;

/* Memory related */
#define OS_MEMZERO(ptr, size) A_MEMZERO(ptr, size)
#define OS_MEMCPY(dst, src, len) A_MEMCPY(dst, src, len)
#define OS_MALLOC(nbytes) A_MALLOC(nbytes)
#define OS_FREE(ptr) A_FREE(ptr)

#endif


