/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * android_kabi.h - Android kernel abi abstraction header
 *
 * Copyright (C) 2020 Google, Inc.
 *
 * Heavily influenced by rh_kabi.h which came from the RHEL/CENTOS kernel and
 * was:
 *	Copyright (c) 2014 Don Zickus
 *	Copyright (c) 2015-2018 Jiri Benc
 *	Copyright (c) 2015 Sabrina Dubroca, Hannes Frederic Sowa
 *	Copyright (c) 2016-2018 Prarit Bhargava
 *	Copyright (c) 2017 Paolo Abeni, Larry Woodman
 *
 * These macros are to be used to try to help alleviate future kernel abi
 * changes that will occur as LTS and other kernel patches are merged into the
 * tree during a period in which the kernel abi is wishing to not be disturbed.
 *
 * There are two times these macros should be used:
 *  - Before the kernel abi is "frozen"
 *    Padding can be added to various kernel structures that have in the past
 *    been known to change over time.  That will give "room" in the structure
 *    that can then be used when fields are added so that the structure size
 *    will not change.
 *
 *  - After the kernel abi is "frozen"
 *    If a structure's field is changed to a type that is identical in size to
 *    the previous type, it can be changed with a union macro
 *    If a field is added to a structure, the padding fields can be used to add
 *    the new field in a "safe" way.
 */
#ifndef _ANDROID_KABI_H
#define _ANDROID_KABI_H

#include <linux/compiler.h>

/*
 * Worker macros, don't use these, use the ones without a leading '_'
 */

#define __ANDROID_KABI_CHECK_SIZE_ALIGN(_orig, _new)				\
	union {									\
		_Static_assert(sizeof(struct{_new;}) <= sizeof(struct{_orig;}),	\
			       __FILE__ ":" __stringify(__LINE__) ": "		\
			       __stringify(_new)				\
			       " is larger than "				\
			       __stringify(_orig) );				\
		_Static_assert(__alignof__(struct{_new;}) <= __alignof__(struct{_orig;}),	\
			       __FILE__ ":" __stringify(__LINE__) ": "		\
			       __stringify(_orig)				\
			       " is not aligned the same as "			\
			       __stringify(_new) );				\
	}

#ifdef __GENKSYMS__

#define _ANDROID_KABI_REPLACE(_orig, _new)		_orig

#else

#define _ANDROID_KABI_REPLACE(_orig, _new)			\
	union {							\
		_new;						\
		struct {					\
			_orig;					\
		};						\
		__ANDROID_KABI_CHECK_SIZE_ALIGN(_orig, _new);	\
	}

#endif /* __GENKSYMS__ */

#define _ANDROID_KABI_RESERVE(n)		u64 android_kabi_reserved##n


/*
 * Macros to use _before_ the ABI is frozen
 */

/*
 * ANDROID_KABI_RESERVE
 *   Reserve some "padding" in a structure for potential future use.
 *   This normally placed at the end of a structure.
 *   number: the "number" of the padding variable in the structure.  Start with
 *   1 and go up.
 */
#ifdef CONFIG_ANDROID_KABI_RESERVE
#define ANDROID_KABI_RESERVE(number)	_ANDROID_KABI_RESERVE(number)
#else
#define ANDROID_KABI_RESERVE(number)
#endif


/*
 * Macros to use _after_ the ABI is frozen
 */

/*
 * ANDROID_KABI_USE(number, _new)
 *   Use a previous padding entry that was defined with ANDROID_KABI_RESERVE
 *   number: the previous "number" of the padding variable
 *   _new: the variable to use now instead of the padding variable
 */
#define ANDROID_KABI_USE(number, _new)		\
	_ANDROID_KABI_REPLACE(_ANDROID_KABI_RESERVE(number), _new)

/*
 * ANDROID_KABI_USE2(number, _new1, _new2)
 *   Use a previous padding entry that was defined with ANDROID_KABI_RESERVE for
 *   two new variables that fit into 64 bits.  This is good for when you do not
 *   want to "burn" a 64bit padding variable for a smaller variable size if not
 *   needed.
 */
#define ANDROID_KABI_USE2(number, _new1, _new2)			\
	_ANDROID_KABI_REPLACE(_ANDROID_KABI_RESERVE(number), struct{ _new1; _new2; })


#endif /* _ANDROID_KABI_H */
