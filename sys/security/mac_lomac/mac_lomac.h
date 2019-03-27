/*-
 * Copyright (c) 1999-2002 Robert N. M. Watson
 * Copyright (c) 2001-2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
 *
 * This software was developed for the FreeBSD Project in part by NAI Labs,
 * the Security Research Division of Network Associates, Inc. under
 * DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA
 * CHATS research program.
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
 * Definitions for the TrustedBSD LOMAC integrity policy module.
 */
#ifndef _SYS_SECURITY_MAC_LOMAC_H
#define	_SYS_SECURITY_MAC_LOMAC_H

#define	MAC_LOMAC_EXTATTR_NAMESPACE	EXTATTR_NAMESPACE_SYSTEM
#define	MAC_LOMAC_EXTATTR_NAME		"mac_lomac"

#define	MAC_LOMAC_LABEL_NAME		"lomac"

#define	MAC_LOMAC_FLAG_SINGLE	0x00000001	/* ml_single initialized */
#define	MAC_LOMAC_FLAG_RANGE	0x00000002	/* ml_range* initialized */
#define	MAC_LOMAC_FLAG_AUX	0x00000004	/* ml_auxsingle initialized */
#define	MAC_LOMAC_FLAGS_BOTH	(MAC_LOMAC_FLAG_SINGLE | MAC_LOMAC_FLAG_RANGE)
#define	MAC_LOMAC_FLAG_UPDATE	0x00000008	/* must demote this process */

#define	MAC_LOMAC_TYPE_UNDEF	0	/* Undefined */
#define	MAC_LOMAC_TYPE_GRADE	1	/* Hierarchal grade with mb_grade. */
#define	MAC_LOMAC_TYPE_LOW	2	/* Dominated by any
					 * MAC_LOMAC_TYPE_LABEL. */
#define	MAC_LOMAC_TYPE_HIGH	3	/* Dominates any
					 * MAC_LOMAC_TYPE_LABEL. */
#define	MAC_LOMAC_TYPE_EQUAL	4	/* Equivalent to any
					 * MAC_LOMAC_TYPE_LABEL. */

/*
 * Structures and constants associated with a LOMAC Integrity policy.
 * mac_lomac represents a LOMAC label, with mb_type determining its properties,
 * and mb_grade represents the hierarchal grade if valid for the current
 * mb_type.
 */

struct mac_lomac_element {
	u_short	mle_type;
	u_short	mle_grade;
};

/*
 * LOMAC labels start with two components: a single label, and a label
 * range.  Depending on the context, one or both may be used; the ml_flags
 * field permits the provider to indicate what fields are intended for
 * use.  The auxiliary label works the same way, but is only valid on
 * filesystem objects to provide inheritance semantics on directories
 * and "non-demoting" execution on executable files.
 */
struct mac_lomac {
	int				ml_flags;
	struct mac_lomac_element	ml_single;
	struct mac_lomac_element	ml_rangelow, ml_rangehigh;
	struct mac_lomac_element	ml_auxsingle;
};

#endif /* !_SYS_SECURITY_MAC_LOMAC_H */
