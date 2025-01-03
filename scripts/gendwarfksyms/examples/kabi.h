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

#endif /* __KABI_H__ */
