/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _STATIC_CALL_TYPES_H
#define _STATIC_CALL_TYPES_H

#include <linux/types.h>
#include <linux/stringify.h>

#define STATIC_CALL_KEY_PREFIX		__SCK__
#define STATIC_CALL_KEY_PREFIX_STR	__stringify(STATIC_CALL_KEY_PREFIX)
#define STATIC_CALL_KEY_PREFIX_LEN	(sizeof(STATIC_CALL_KEY_PREFIX_STR) - 1)
#define STATIC_CALL_KEY(name)		__PASTE(STATIC_CALL_KEY_PREFIX, name)

#define STATIC_CALL_TRAMP_PREFIX	__SCT__
#define STATIC_CALL_TRAMP_PREFIX_STR	__stringify(STATIC_CALL_TRAMP_PREFIX)
#define STATIC_CALL_TRAMP_PREFIX_LEN	(sizeof(STATIC_CALL_TRAMP_PREFIX_STR) - 1)
#define STATIC_CALL_TRAMP(name)		__PASTE(STATIC_CALL_TRAMP_PREFIX, name)
#define STATIC_CALL_TRAMP_STR(name)	__stringify(STATIC_CALL_TRAMP(name))

/*
 * The static call site table needs to be created by external tooling (objtool
 * or a compiler plugin).
 */
struct static_call_site {
	s32 addr;
	s32 key;
};

#endif /* _STATIC_CALL_TYPES_H */
