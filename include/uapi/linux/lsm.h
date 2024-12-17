/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Linux Security Modules (LSM) - User space API
 *
 * Copyright (C) 2022 Casey Schaufler <casey@schaufler-ca.com>
 * Copyright (C) 2022 Intel Corporation
 */

#ifndef _UAPI_LINUX_LSM_H
#define _UAPI_LINUX_LSM_H

#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/unistd.h>

/**
 * struct lsm_ctx - LSM context information
 * @id: the LSM id number, see LSM_ID_XXX
 * @flags: LSM specific flags
 * @len: length of the lsm_ctx struct, @ctx and any other data or padding
 * @ctx_len: the size of @ctx
 * @ctx: the LSM context value
 *
 * The @len field MUST be equal to the size of the lsm_ctx struct
 * plus any additional padding and/or data placed after @ctx.
 *
 * In all cases @ctx_len MUST be equal to the length of @ctx.
 * If @ctx is a string value it should be nul terminated with
 * @ctx_len equal to `strlen(@ctx) + 1`.  Binary values are
 * supported.
 *
 * The @flags and @ctx fields SHOULD only be interpreted by the
 * LSM specified by @id; they MUST be set to zero/0 when not used.
 */
struct lsm_ctx {
	__u64 id;
	__u64 flags;
	__u64 len;
	__u64 ctx_len;
	__u8 ctx[] __counted_by(ctx_len);
};

/*
 * ID tokens to identify Linux Security Modules (LSMs)
 *
 * These token values are used to uniquely identify specific LSMs
 * in the kernel as well as in the kernel's LSM userspace API.
 *
 * A value of zero/0 is considered undefined and should not be used
 * outside the kernel. Values 1-99 are reserved for potential
 * future use.
 */
#define LSM_ID_UNDEF		0
#define LSM_ID_CAPABILITY	100
#define LSM_ID_SELINUX		101
#define LSM_ID_SMACK		102
#define LSM_ID_TOMOYO		103
#define LSM_ID_APPARMOR		104
#define LSM_ID_YAMA		105
#define LSM_ID_LOADPIN		106
#define LSM_ID_SAFESETID	107
#define LSM_ID_LOCKDOWN		108
#define LSM_ID_BPF		109
#define LSM_ID_LANDLOCK		110
#define LSM_ID_IMA		111
#define LSM_ID_EVM		112
#define LSM_ID_IPE		113

/*
 * LSM_ATTR_XXX definitions identify different LSM attributes
 * which are used in the kernel's LSM userspace API. Support
 * for these attributes vary across the different LSMs. None
 * are required.
 *
 * A value of zero/0 is considered undefined and should not be used
 * outside the kernel. Values 1-99 are reserved for potential
 * future use.
 */
#define LSM_ATTR_UNDEF		0
#define LSM_ATTR_CURRENT	100
#define LSM_ATTR_EXEC		101
#define LSM_ATTR_FSCREATE	102
#define LSM_ATTR_KEYCREATE	103
#define LSM_ATTR_PREV		104
#define LSM_ATTR_SOCKCREATE	105

/*
 * LSM_FLAG_XXX definitions identify special handling instructions
 * for the API.
 */
#define LSM_FLAG_SINGLE	0x0001

#endif /* _UAPI_LINUX_LSM_H */
