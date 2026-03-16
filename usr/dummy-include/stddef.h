/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _DUMMY_STDDEF_H
#define _DUMMY_STDDEF_H

#define offsetof(TYPE, MEMBER)	__builtin_offsetof(TYPE, MEMBER)

#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void *)0)
#endif

#endif /* _DUMMY_STDDEF_H */
