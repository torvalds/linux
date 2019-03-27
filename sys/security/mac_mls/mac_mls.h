/*-
 * Copyright (c) 1999-2002 Robert N. M. Watson
 * Copyright (c) 2001-2004 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
 *
 * This software was developed for the FreeBSD Project in part by Network
 * Associates Laboratories, the Security Research Division of Network
 * Associates, Inc. under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"),
 * as part of the DARPA CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
/*
 * Definitions for the TrustedBSD MLS confidentiality policy module.
 */
#ifndef _SYS_SECURITY_MAC_MLS_H
#define	_SYS_SECURITY_MAC_MLS_H

#define	MAC_MLS_EXTATTR_NAMESPACE	EXTATTR_NAMESPACE_SYSTEM
#define	MAC_MLS_EXTATTR_NAME		"mac_mls"

#define	MAC_MLS_LABEL_NAME		"mls"

#define	MAC_MLS_FLAG_EFFECTIVE	0x00000001	/* mm_effective initialized */
#define	MAC_MLS_FLAG_RANGE	0x00000002	/* mm_range* initialized */
#define	MAC_MLS_FLAGS_BOTH	(MAC_MLS_FLAG_EFFECTIVE | MAC_MLS_FLAG_RANGE)

#define	MAC_MLS_TYPE_UNDEF	0	/* Undefined */
#define	MAC_MLS_TYPE_LEVEL	1	/* Hierarchal level with mm_level. */
#define	MAC_MLS_TYPE_LOW	2	/* Dominated by any
					 * MAC_MLS_TYPE_LABEL. */
#define	MAC_MLS_TYPE_HIGH	3	/* Dominates any
					 * MAC_MLS_TYPE_LABEL. */
#define	MAC_MLS_TYPE_EQUAL	4	/* Equivalent to any
					 * MAC_MLS_TYPE_LABEL. */

/*
 * Structures and constants associated with a Multi-Level Security policy.
 * mac_mls represents an MLS label, with mm_type determining its properties,
 * and mm_level represents the hierarchal sensitivity level if valid for the
 * current mm_type.  If compartments are used, the same semantics apply as
 * long as the suject is in every compartment the object is in.  LOW, EQUAL
 * and HIGH cannot be in compartments.
 */

/*
 * MLS compartments bit set size (in bits).
 */
#define	MAC_MLS_MAX_COMPARTMENTS	256

struct mac_mls_element {
	u_short	mme_type;
	u_short	mme_level;
	u_char	mme_compartments[MAC_MLS_MAX_COMPARTMENTS >> 3];
};

/*
 * MLS labels consist of two components: an effective label, and a label
 * range.  Depending on the context, one or both may be used; the mb_flags
 * field permits the provider to indicate what fields are intended for
 * use.
 */
struct mac_mls {
	int			mm_flags;
	struct mac_mls_element	mm_effective;
	struct mac_mls_element	mm_rangelow, mm_rangehigh;
};

/*
 * MLS compartments bit test/set macros.
 * The range is 1 to MAC_MLS_MAX_COMPARTMENTS.
 */
#define	MAC_MLS_BIT_TEST(b, w) \
	((w)[(((b) - 1) >> 3)] & (1 << (((b) - 1) & 7)))
#define	MAC_MLS_BIT_SET(b, w) \
	((w)[(((b) - 1) >> 3)] |= (1 << (((b) - 1) & 7)))
#define	MAC_MLS_BIT_SET_EMPTY(set)	mls_bit_set_empty(set)

#endif /* !_SYS_SECURITY_MAC_MLS_H */
