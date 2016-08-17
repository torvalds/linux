/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2016, Datto, Inc. All rights reserved.
 */

#ifdef _KERNEL
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#else
#define	__exit
#define	__init
#endif

#include <sys/crypto/common.h>
#include <sys/crypto/api.h>
#include <sys/crypto/impl.h>
#include <sys/crypto/sched_impl.h>
#include <sys/modhash_impl.h>
#include <sys/crypto/icp.h>

/*
 * Changes made to the original Illumos Crypto Layer for the ICP:
 *
 * Several changes were needed to allow the Illumos Crypto Layer
 * to work in the Linux kernel. Almost all of the changes fall into
 * one of the following categories:
 *
 * 1) Moving the syntax to the C90: This was mostly a matter of
 * changing func() definitions to func(void). In a few cases,
 * initializations of structs with unions needed to have brackets
 * added.
 *
 * 2) Changes to allow userspace compilation: The ICP is meant to be
 * compiled and used in both userspace and kernel space (for ztest and
 * libzfs), so the _KERNEL macros did not make sense anymore. For the
 * same reason, many header includes were also changed to use
 * sys/zfs_context.h
 *
 * 3) Moving to a statically compiled architecture: At some point in
 * the future it may make sense to have encryption algorithms that are
 * loadable into the ICP at runtime via separate kernel modules.
 * However, considering that this code will probably not see much use
 * outside of zfs and zfs encryption only requires aes and sha256
 * algorithms it seemed like more trouble than it was worth to port over
 * Illumos's kernel module structure to a Linux kernel module. In
 * addition, The Illumos code related to keeping track of kernel modules
 * is very much tied to the Illumos OS and proved difficult to port to
 * Linux. Therefore, the structure of the ICP was simplified to work
 * statically and several pieces of code responsible for keeping track
 * of Illumos kernel modules were removed and simplified. All module
 * initialization and destruction is now called in this file during
 * Linux kernel module loading and unloading.
 *
 * 4) Adding destructors: The Illumos Crypto Layer is built into
 * the Illumos kernel and is not meant to be unloaded. Some destructors
 * were added to allow the ICP to be unloaded without leaking
 * structures.
 *
 * 5) Removing CRYPTO_DATA_MBLK related structures and code:
 * crypto_data_t can have 3 formats, CRYPTO_DATA_RAW, CRYPTO_DATA_UIO,
 * and CRYPTO_DATA_MBLK. ZFS only requires the first 2 formats, as the
 * last one is related to streamed data. To simplify the port, code
 * related to this format was removed.
 *
 * 6) Changes for architecture specific code: Some changes were needed
 * to make architecture specific assembly compile. The biggest change
 * here was to functions related to detecting CPU capabilities for amd64.
 * The Illumos Crypto Layer used called into the Illumos kernel's API
 * to discover these. They have been converted to instead use the
 * 'cpuid' instruction as per the Intel spec. In addition, references to
 * the sun4u' and sparc architectures have been removed so that these
 * will use the generic implementation.
 *
 * 7) Removing sha384 and sha512 code: The sha code was actually very
 * wasy to port. However, the generic sha384 and sha512 code actually
 * exceeds the stack size on arm and powerpc architectures. In an effort
 * to remove warnings, this code was removed.
 *
 * 8) Change large allocations from kmem_alloc() to vmem_alloc(): In
 * testing the ICP with the ZFS encryption code, a few allocations were
 * found that could potentially be very large. These caused the SPL to
 * throw warnings and so they were changed to use vmem_alloc().
 *
 * 9) Makefiles: Makefiles were added that would work with the existing
 * ZFS Makefiles.
 */

void __exit
icp_fini(void)
{
	skein_mod_fini();
	sha2_mod_fini();
	sha1_mod_fini();
	edonr_mod_fini();
	aes_mod_fini();
	kcf_sched_destroy();
	kcf_prov_tab_destroy();
	kcf_destroy_mech_tabs();
	mod_hash_fini();
}

/* roughly equivalent to kcf.c: _init() */
int __init
icp_init(void)
{
	/* initialize the mod hash module */
	mod_hash_init();

	/* initialize the mechanisms tables supported out-of-the-box */
	kcf_init_mech_tabs();

	/* initialize the providers tables */
	kcf_prov_tab_init();

	/*
	 * Initialize scheduling structures. Note that this does NOT
	 * start any threads since it might not be safe to do so.
	 */
	kcf_sched_init();

	/* initialize algorithms */
	aes_mod_init();
	edonr_mod_init();
	sha1_mod_init();
	sha2_mod_init();
	skein_mod_init();

	return (0);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
module_exit(icp_fini);
module_init(icp_init);
MODULE_LICENSE(ZFS_META_LICENSE);
#endif
