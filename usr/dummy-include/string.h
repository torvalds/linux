/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _DUMMY_STRING_H
#define _DUMMY_STRING_H

#include <stddef.h>

#define memset(_s, _c, _n) __builtin_memset(_s, _c, _n)
#define memcpy(_dest, _src, _n) __builtin_memcpy(_dest, _src, _n)

#define strlen(_s) __builtin_strlen(_s)

#endif /* _DUMMY_STRING_H */
