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

#ifndef _SYS_SECURITY_MAC_BSDEXTENDED_H
#define	_SYS_SECURITY_MAC_BSDEXTENDED_H

#define MB_VERSION 2 /* Used to check library and kernel are the same. */

/*
 * Rights that can be represented in mbr_mode.  These have the same values as
 * the V* rights in vnode.h, but in order to avoid sharing user and kernel
 * constants, we define them here.  That will also improve ABI stability if
 * the in-kernel values change.
 */
#define	MBI_EXEC	000100
#define	MBI_WRITE	000200
#define	MBI_READ	000400
#define	MBI_ADMIN	010000
#define	MBI_STAT	020000
#define	MBI_APPEND	040000
#define	MBI_ALLPERM	(MBI_EXEC | MBI_WRITE | MBI_READ | MBI_ADMIN | \
			    MBI_STAT | MBI_APPEND)

#define	MBS_UID_DEFINED	0x00000001	/* uid field should be matched */
#define	MBS_GID_DEFINED	0x00000002	/* gid field should be matched */
#define	MBS_PRISON_DEFINED 0x00000004	/* prison field should be matched */

#define MBS_ALL_FLAGS (MBS_UID_DEFINED | MBS_GID_DEFINED | MBS_PRISON_DEFINED)

struct mac_bsdextended_subject {
	int	mbs_flags;
	int	mbs_neg;
	uid_t	mbs_uid_min;
	uid_t	mbs_uid_max;
	gid_t	mbs_gid_min;
	gid_t	mbs_gid_max;
	int	mbs_prison;
};

#define	MBO_UID_DEFINED	0x00000001	/* uid field should be matched */
#define	MBO_GID_DEFINED	0x00000002	/* gid field should be matched */
#define	MBO_FSID_DEFINED 0x00000004	/* fsid field should be matched */
#define	MBO_SUID	0x00000008	/* object must be suid */
#define	MBO_SGID	0x00000010	/* object must be sgid */
#define	MBO_UID_SUBJECT	0x00000020	/* uid must match subject */
#define	MBO_GID_SUBJECT	0x00000040	/* gid must match subject */
#define	MBO_TYPE_DEFINED 0x00000080	/* object type should be matched */

#define MBO_ALL_FLAGS (MBO_UID_DEFINED | MBO_GID_DEFINED | MBO_FSID_DEFINED | \
	    MBO_SUID | MBO_SGID | MBO_UID_SUBJECT | MBO_GID_SUBJECT | \
	    MBO_TYPE_DEFINED)

#define MBO_TYPE_REG	0x00000001
#define MBO_TYPE_DIR	0x00000002
#define MBO_TYPE_BLK	0x00000004
#define MBO_TYPE_CHR	0x00000008
#define MBO_TYPE_LNK	0x00000010
#define MBO_TYPE_SOCK	0x00000020
#define MBO_TYPE_FIFO	0x00000040

#define MBO_ALL_TYPE	(MBO_TYPE_REG | MBO_TYPE_DIR | MBO_TYPE_BLK | \
	    MBO_TYPE_CHR | MBO_TYPE_LNK | MBO_TYPE_SOCK | MBO_TYPE_FIFO)

struct mac_bsdextended_object {
	int	mbo_flags;
	int	mbo_neg;
	uid_t	mbo_uid_min;
	uid_t	mbo_uid_max;
	gid_t	mbo_gid_min;
	gid_t	mbo_gid_max;
	struct fsid mbo_fsid;
	int	mbo_type;
};

struct mac_bsdextended_rule {
	struct mac_bsdextended_subject	mbr_subject;
	struct mac_bsdextended_object	mbr_object;
	mode_t				mbr_mode;	/* maximum access */
};

#endif /* _SYS_SECURITY_MAC_BSDEXTENDED_H */
