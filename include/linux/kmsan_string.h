/* SPDX-License-Identifier: GPL-2.0 */
/*
 * KMSAN string functions API used in other headers.
 *
 * Copyright (C) 2022 Google LLC
 * Author: Alexander Potapenko <glider@google.com>
 *
 */
#ifndef _LINUX_KMSAN_STRING_H
#define _LINUX_KMSAN_STRING_H

/*
 * KMSAN overrides the default memcpy/memset/memmove implementations in the
 * kernel, which requires having __msan_XXX function prototypes in several other
 * headers. Keep them in one place instead of open-coding.
 */
void *__msan_memcpy(void *dst, const void *src, size_t size);
void *__msan_memset(void *s, int c, size_t n);
void *__msan_memmove(void *dest, const void *src, size_t len);

#endif /* _LINUX_KMSAN_STRING_H */
