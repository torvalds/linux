/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _DUMMY_STDDEF_H
#define _DUMMY_STDDEF_H

#define offsetof(TYPE, MEMBER)	__builtin_offsetof(TYPE, MEMBER)
#define NULL ((void *)0)

#endif /* _DUMMY_STDDEF_H */
