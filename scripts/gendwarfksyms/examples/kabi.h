/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Google LLC
 *
 * Example macros for maintaining kABI stability.
 *
 * This file is based on android_kabi.h, which has the following notice:
 *
 * Heavily influenced by rh_kabi.h which came from the RHEL/CENTOS kernel
 * and was:
 *	Copyright (c) 2014 Don Zickus
 *	Copyright (c) 2015-2018 Jiri Benc
 *	Copyright (c) 2015 Sabrina Dubroca, Hannes Frederic Sowa
 *	Copyright (c) 2016-2018 Prarit Bhargava
 *	Copyright (c) 2017 Paolo Abeni, Larry Woodman
 */

#ifndef __KABI_H__
#define __KABI_H__

/* Kernel macros for userspace testing. */
#ifndef __aligned
#define __aligned(x) __attribute__((__aligned__(x)))
#endif
#ifndef __used
#define __used __attribute__((__used__))
#endif
#ifndef __section
#define __section(section) __attribute__((__section__(section)))
#endif
#ifndef __PASTE
#define ___PASTE(a, b) a##b
#define __PASTE(a, b) ___PASTE(a, b)
#endif
#ifndef __stringify
#define __stringify_1(x...) #x
#define __stringify(x...) __stringify_1(x)
#endif

#define __KABI_RULE(hint, target, value)                             \
	static const char __PASTE(__gendwarfksyms_rule_,             \
				  __COUNTER__)[] __used __aligned(1) \
		__section(".discard.gendwarfksyms.kabi_rules") =     \
			"1\0" #hint "\0" #target "\0" #value

#define __KABI_NORMAL_SIZE_ALIGN(_orig, _new)                                             \
	union {                                                                           \
		_Static_assert(                                                           \
			sizeof(struct { _new; }) <= sizeof(struct { _orig; }),            \
			__FILE__ ":" __stringify(__LINE__) ": " __stringify(              \
				_new) " is larger than " __stringify(_orig));             \
		_Static_assert(                                                           \
			__alignof__(struct { _new; }) <=                                  \
				__alignof__(struct { _orig; }),                           \
			__FILE__ ":" __stringify(__LINE__) ": " __stringify(              \
				_orig) " is not aligned the same as " __stringify(_new)); \
	}

#define __KABI_REPLACE(_orig, _new)                    \
	union {                                        \
		_new;                                  \
		struct {                               \
			_orig;                         \
		};                                     \
		__KABI_NORMAL_SIZE_ALIGN(_orig, _new); \
	}

/*
 * KABI_DECLONLY(fqn)
 *   Treat the struct/union/enum fqn as a declaration, i.e. even if
 *   a definition is available, don't expand the contents.
 */
#define KABI_DECLONLY(fqn) __KABI_RULE(declonly, fqn, )

/*
 * KABI_ENUMERATOR_IGNORE(fqn, field)
 *   When expanding enum fqn, skip the provided field. This makes it
 *   possible to hide added enum fields from versioning.
 */
#define KABI_ENUMERATOR_IGNORE(fqn, field) \
	__KABI_RULE(enumerator_ignore, fqn field, )

/*
 * KABI_ENUMERATOR_VALUE(fqn, field, value)
 *   When expanding enum fqn, use the provided value for the
 *   specified field. This makes it possible to override enumerator
 *   values when calculating versions.
 */
#define KABI_ENUMERATOR_VALUE(fqn, field, value) \
	__KABI_RULE(enumerator_value, fqn field, value)

/*
 * KABI_RESERVE
 *   Reserve some "padding" in a structure for use by LTS backports.
 *   This is normally placed at the end of a structure.
 *   number: the "number" of the padding variable in the structure.  Start with
 *   1 and go up.
 */
#define KABI_RESERVE(n) unsigned long __kabi_reserved##n

/*
 * KABI_RESERVE_ARRAY
 *   Same as _BACKPORT_RESERVE but allocates an array with the specified
 *   size in bytes.
 */
#define KABI_RESERVE_ARRAY(n, s) \
	unsigned char __aligned(8) __kabi_reserved##n[s]

/*
 * KABI_IGNORE
 *   Add a new field that's ignored in versioning.
 */
#define KABI_IGNORE(n, _new)                     \
	union {                                  \
		_new;                            \
		unsigned char __kabi_ignored##n; \
	}

/*
 * KABI_REPLACE
 *   Replace a field with a compatible new field.
 */
#define KABI_REPLACE(_oldtype, _oldname, _new) \
	__KABI_REPLACE(_oldtype __kabi_renamed##_oldname, struct { _new; })

/*
 * KABI_USE(number, _new)
 *   Use a previous padding entry that was defined with KABI_RESERVE
 *   number: the previous "number" of the padding variable
 *   _new: the variable to use now instead of the padding variable
 */
#define KABI_USE(number, _new) __KABI_REPLACE(KABI_RESERVE(number), _new)

/*
 * KABI_USE2(number, _new1, _new2)
 *   Use a previous padding entry that was defined with KABI_RESERVE for
 *   two new variables that fit into 64 bits.  This is good for when you do not
 *   want to "burn" a 64bit padding variable for a smaller variable size if not
 *   needed.
 */
#define KABI_USE2(number, _new1, _new2)        \
	__KABI_REPLACE(                        \
		KABI_RESERVE(number), struct { \
			_new1;                 \
			_new2;                 \
		})
/*
 * KABI_USE_ARRAY(number, bytes, _new)
 *   Use a previous padding entry that was defined with KABI_RESERVE_ARRAY
 *   number: the previous "number" of the padding variable
 *   bytes: the size in bytes reserved for the array
 *   _new: the variable to use now instead of the padding variable
 */
#define KABI_USE_ARRAY(number, bytes, _new) \
	__KABI_REPLACE(KABI_RESERVE_ARRAY(number, bytes), _new)

#endif /* __KABI_H__ */
