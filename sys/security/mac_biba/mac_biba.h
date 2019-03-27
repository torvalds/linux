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
 * Definitions for the TrustedBSD Biba integrity policy module.
 */
#ifndef _SYS_SECURITY_MAC_BIBA_H
#define	_SYS_SECURITY_MAC_BIBA_H

#define	MAC_BIBA_EXTATTR_NAMESPACE	EXTATTR_NAMESPACE_SYSTEM
#define	MAC_BIBA_EXTATTR_NAME		"mac_biba"

#define	MAC_BIBA_LABEL_NAME		"biba"

#define	MAC_BIBA_FLAG_EFFECTIVE	0x00000001	/* mb_effective initialized */
#define	MAC_BIBA_FLAG_RANGE	0x00000002	/* mb_range* initialized */
#define	MAC_BIBA_FLAGS_BOTH	(MAC_BIBA_FLAG_EFFECTIVE | MAC_BIBA_FLAG_RANGE)

#define	MAC_BIBA_TYPE_UNDEF	0	/* Undefined */
#define	MAC_BIBA_TYPE_GRADE	1	/* Hierarchal grade with mb_grade. */
#define	MAC_BIBA_TYPE_LOW	2	/* Dominated by any
					 * MAC_BIBA_TYPE_LABEL. */
#define	MAC_BIBA_TYPE_HIGH	3	/* Dominates any
					 * MAC_BIBA_TYPE_LABEL. */
#define	MAC_BIBA_TYPE_EQUAL	4	/* Equivalent to any
					 * MAC_BIBA_TYPE_LABEL. */

/*
 * Structures and constants associated with a Biba Integrity policy.
 * mac_biba represents a Biba label, with mb_type determining its properties,
 * and mb_grade represents the hierarchal grade if valid for the current
 * mb_type.
 */

#define	MAC_BIBA_MAX_COMPARTMENTS	256

struct mac_biba_element {
	u_short	mbe_type;
	u_short	mbe_grade;
	u_char	mbe_compartments[MAC_BIBA_MAX_COMPARTMENTS >> 3];
};

/*
 * Biba labels consist of two components: an effective label, and a label
 * range.  Depending on the context, one or both may be used; the mb_flags
 * field permits the provider to indicate what fields are intended for
 * use.
 */
struct mac_biba {
	int			mb_flags;
	struct mac_biba_element	mb_effective;
	struct mac_biba_element	mb_rangelow, mb_rangehigh;
};

/*
 * Biba compartments bit test/set macros.
 * The range is 1 to MAC_BIBA_MAX_COMPARTMENTS.
 */
#define	MAC_BIBA_BIT_TEST(b, w) \
	((w)[(((b) - 1) >> 3)] & (1 << (((b) - 1) & 7)))
#define	MAC_BIBA_BIT_SET(b, w) \
	((w)[(((b) - 1) >> 3)] |= (1 << (((b) - 1) & 7)))
#define	MAC_BIBA_BIT_SET_EMPTY(set)	biba_bit_set_empty(set)

#endif /* !_SYS_SECURITY_MAC_BIBA_H */
