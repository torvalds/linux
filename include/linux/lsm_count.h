/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (C) 2023 Google LLC.
 */

#ifndef __LINUX_LSM_COUNT_H
#define __LINUX_LSM_COUNT_H

#include <linux/args.h>

#ifdef CONFIG_SECURITY

/*
 * Macros to count the number of LSMs enabled in the kernel at compile time.
 */

/*
 * Capabilities is enabled when CONFIG_SECURITY is enabled.
 */
#if IS_ENABLED(CONFIG_SECURITY)
#define CAPABILITIES_ENABLED 1,
#else
#define CAPABILITIES_ENABLED
#endif

#if IS_ENABLED(CONFIG_SECURITY_SELINUX)
#define SELINUX_ENABLED 1,
#else
#define SELINUX_ENABLED
#endif

#if IS_ENABLED(CONFIG_SECURITY_SMACK)
#define SMACK_ENABLED 1,
#else
#define SMACK_ENABLED
#endif

#if IS_ENABLED(CONFIG_SECURITY_APPARMOR)
#define APPARMOR_ENABLED 1,
#else
#define APPARMOR_ENABLED
#endif

#if IS_ENABLED(CONFIG_SECURITY_TOMOYO)
#define TOMOYO_ENABLED 1,
#else
#define TOMOYO_ENABLED
#endif

#if IS_ENABLED(CONFIG_SECURITY_YAMA)
#define YAMA_ENABLED 1,
#else
#define YAMA_ENABLED
#endif

#if IS_ENABLED(CONFIG_SECURITY_LOADPIN)
#define LOADPIN_ENABLED 1,
#else
#define LOADPIN_ENABLED
#endif

#if IS_ENABLED(CONFIG_SECURITY_LOCKDOWN_LSM)
#define LOCKDOWN_ENABLED 1,
#else
#define LOCKDOWN_ENABLED
#endif

#if IS_ENABLED(CONFIG_SECURITY_SAFESETID)
#define SAFESETID_ENABLED 1,
#else
#define SAFESETID_ENABLED
#endif

#if IS_ENABLED(CONFIG_BPF_LSM)
#define BPF_LSM_ENABLED 1,
#else
#define BPF_LSM_ENABLED
#endif

#if IS_ENABLED(CONFIG_SECURITY_LANDLOCK)
#define LANDLOCK_ENABLED 1,
#else
#define LANDLOCK_ENABLED
#endif

#if IS_ENABLED(CONFIG_IMA)
#define IMA_ENABLED 1,
#else
#define IMA_ENABLED
#endif

#if IS_ENABLED(CONFIG_EVM)
#define EVM_ENABLED 1,
#else
#define EVM_ENABLED
#endif

#if IS_ENABLED(CONFIG_SECURITY_IPE)
#define IPE_ENABLED 1,
#else
#define IPE_ENABLED
#endif

/*
 *  There is a trailing comma that we need to be accounted for. This is done by
 *  using a skipped argument in __COUNT_LSMS
 */
#define __COUNT_LSMS(skipped_arg, args...) COUNT_ARGS(args...)
#define COUNT_LSMS(args...) __COUNT_LSMS(args)

#define MAX_LSM_COUNT			\
	COUNT_LSMS(			\
		CAPABILITIES_ENABLED	\
		SELINUX_ENABLED		\
		SMACK_ENABLED		\
		APPARMOR_ENABLED	\
		TOMOYO_ENABLED		\
		YAMA_ENABLED		\
		LOADPIN_ENABLED		\
		LOCKDOWN_ENABLED	\
		SAFESETID_ENABLED	\
		BPF_LSM_ENABLED		\
		LANDLOCK_ENABLED	\
		IMA_ENABLED		\
		EVM_ENABLED		\
		IPE_ENABLED)

#else

#define MAX_LSM_COUNT 0

#endif /* CONFIG_SECURITY */

#endif  /* __LINUX_LSM_COUNT_H */
