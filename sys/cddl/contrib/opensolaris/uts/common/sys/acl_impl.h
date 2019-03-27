/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_ACL_IMPL_H
#define	_SYS_ACL_IMPL_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * acl flags
 *
 * ACL_AUTO_INHERIT, ACL_PROTECTED and ACL_DEFAULTED
 * flags can also be stored in this field.
 */
#define	ACL_IS_TRIVIAL	0x10000
#define	ACL_IS_DIR	0x20000

typedef enum acl_type {
	ACLENT_T = 0,
	ACE_T = 1
} zfs_acl_type_t;

struct acl_info {
	zfs_acl_type_t acl_type;	/* style of acl */
	int acl_cnt;			/* number of acl entries */
	int acl_entry_size;		/* sizeof acl entry */
	int acl_flags;			/* special flags about acl */
	void *acl_aclp;			/* the acl */
};

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_ACL_IMPL_H */
