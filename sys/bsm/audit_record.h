/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2005-2009 Apple Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _BSM_AUDIT_RECORD_H_
#define _BSM_AUDIT_RECORD_H_

#include <sys/types.h>
#include <sys/time.h>			/* struct timeval */

/*
 * Token type identifiers.
 */
#define	AUT_INVALID		0x00
#define	AUT_OTHER_FILE32	0x11
#define	AUT_OHEADER		0x12
#define	AUT_TRAILER		0x13
#define	AUT_HEADER32		0x14
#define	AUT_HEADER32_EX		0x15
#define	AUT_DATA		0x21
#define	AUT_IPC			0x22
#define	AUT_PATH		0x23
#define	AUT_SUBJECT32		0x24
#define	AUT_XATPATH		0x25
#define	AUT_PROCESS32		0x26
#define	AUT_RETURN32		0x27
#define	AUT_TEXT		0x28
#define	AUT_OPAQUE		0x29
#define	AUT_IN_ADDR		0x2a
#define	AUT_IP			0x2b
#define	AUT_IPORT		0x2c
#define	AUT_ARG32		0x2d
#define	AUT_SOCKET		0x2e
#define	AUT_SEQ			0x2f
#define	AUT_ACL			0x30
#define	AUT_ATTR		0x31
#define	AUT_IPC_PERM		0x32
#define	AUT_LABEL		0x33
#define	AUT_GROUPS		0x34
#define	AUT_ACE			0x35
#define	AUT_PRIV		0x38
#define	AUT_UPRIV		0x39
#define	AUT_LIAISON		0x3a
#define	AUT_NEWGROUPS		0x3b
#define	AUT_EXEC_ARGS		0x3c
#define	AUT_EXEC_ENV		0x3d
#define	AUT_ATTR32		0x3e
#define	AUT_UNAUTH		0x3f
#define	AUT_XATOM		0x40
#define	AUT_XOBJ		0x41
#define	AUT_XPROTO		0x42
#define	AUT_XSELECT		0x43
#define	AUT_XCOLORMAP		0x44
#define	AUT_XCURSOR		0x45
#define	AUT_XFONT		0x46
#define	AUT_XGC			0x47
#define	AUT_XPIXMAP		0x48
#define	AUT_XPROPERTY		0x49
#define	AUT_XWINDOW		0x4a
#define	AUT_XCLIENT		0x4b
#define	AUT_CMD			0x51
#define	AUT_EXIT		0x52
#define	AUT_ZONENAME		0x60
#define	AUT_HOST		0x70
#define	AUT_ARG64		0x71
#define	AUT_RETURN64		0x72
#define	AUT_ATTR64		0x73
#define	AUT_HEADER64		0x74
#define	AUT_SUBJECT64		0x75
#define	AUT_PROCESS64		0x77
#define	AUT_OTHER_FILE64	0x78
#define	AUT_HEADER64_EX		0x79
#define	AUT_SUBJECT32_EX	0x7a
#define	AUT_PROCESS32_EX	0x7b
#define	AUT_SUBJECT64_EX	0x7c
#define	AUT_PROCESS64_EX	0x7d
#define	AUT_IN_ADDR_EX		0x7e
#define	AUT_SOCKET_EX		0x7f

/*
 * Pre-64-bit BSM, 32-bit tokens weren't explicitly named as '32'.  We have
 * compatibility defines.
 */
#define	AUT_HEADER		AUT_HEADER32
#define	AUT_ARG			AUT_ARG32
#define	AUT_RETURN		AUT_RETURN32
#define	AUT_SUBJECT		AUT_SUBJECT32
#define	AUT_PROCESS		AUT_PROCESS32
#define	AUT_OTHER_FILE		AUT_OTHER_FILE32

/*
 * The values for the following token ids are not defined by BSM.
 *
 * XXXRW: Not sure how to handle these in OpenBSM yet, but I'll give them
 * names more consistent with Sun's BSM.  These originally came from Apple's
 * BSM.
 */
#define	AUT_SOCKINET32		0x80		/* XXX */
#define	AUT_SOCKINET128		0x81		/* XXX */
#define	AUT_SOCKUNIX		0x82		/* XXX */

#define	AUT_RIGHTS		0x83

/* print values for the arbitrary token */
#define AUP_BINARY      0
#define AUP_OCTAL       1
#define AUP_DECIMAL     2
#define AUP_HEX         3
#define AUP_STRING      4

/* data-types for the arbitrary token */
#define AUR_BYTE        0
#define AUR_CHAR        AUR_BYTE
#define AUR_SHORT       1
#define AUR_INT32       2
#define AUR_INT         AUR_INT32
#define AUR_INT64       3

/* ... and their sizes */
#define AUR_BYTE_SIZE       sizeof(u_char)
#define AUR_CHAR_SIZE       AUR_BYTE_SIZE
#define AUR_SHORT_SIZE      sizeof(uint16_t)
#define AUR_INT32_SIZE      sizeof(uint32_t)
#define AUR_INT_SIZE        AUR_INT32_SIZE
#define AUR_INT64_SIZE      sizeof(uint64_t)

/* Modifiers for the header token */
#define PAD_NOTATTR  0x4000   /* nonattributable event */
#define PAD_FAILURE  0x8000   /* fail audit event */

#define AUDIT_MAX_GROUPS      16

/*
 * A number of BSM versions are floating around and defined.  Here are
 * constants for them.  OpenBSM uses the same token types, etc, used in the
 * Solaris BSM version, but has a separate version number in order to
 * identify a potentially different event identifier name space.
 */
#define	AUDIT_HEADER_VERSION_OLDDARWIN	1	/* In retrospect, a mistake. */
#define	AUDIT_HEADER_VERSION_SOLARIS	2
#define	AUDIT_HEADER_VERSION_TSOL25	3
#define	AUDIT_HEADER_VERSION_TSOL	4
#define	AUDIT_HEADER_VERSION_OPENBSM10	10
#define	AUDIT_HEADER_VERSION_OPENBSM11	11
#define	AUDIT_HEADER_VERSION_OPENBSM	AUDIT_HEADER_VERSION_OPENBSM11

#define	AUT_TRAILER_MAGIC	0xb105

/* BSM library calls */

__BEGIN_DECLS

struct in_addr;
struct in6_addr;
struct ip;
struct ipc_perm;
struct kevent;
struct sockaddr;
struct sockaddr_in;
struct sockaddr_in6;
struct sockaddr_un;
#if defined(_KERNEL) || defined(KERNEL)
struct vnode_au_info;
#endif

#ifndef	_CAP_RIGHTS_T_DECLARED
#define	_CAP_RIGHTS_T_DECLARED
struct cap_rights;

typedef	struct cap_rights	cap_rights_t;
#endif

int	 au_open(void);
int	 au_write(int d, token_t *m);
int	 au_close(int d, int keep, short event);
int	 au_close_buffer(int d, short event, u_char *buffer, size_t *buflen);
int	 au_close_token(token_t *tok, u_char *buffer, size_t *buflen);

token_t	*au_to_file(const char *file, struct timeval tm);

token_t	*au_to_header32_tm(int rec_size, au_event_t e_type, au_emod_t e_mod,
	    struct timeval tm);
token_t	*au_to_header32_ex_tm(int rec_size, au_event_t e_type, au_emod_t e_mod,
	    struct timeval tm, struct auditinfo_addr *aia);
token_t	*au_to_header64_tm(int rec_size, au_event_t e_type, au_emod_t e_mod,
	    struct timeval tm);
#if !defined(KERNEL) && !defined(_KERNEL)
token_t	*au_to_header(int rec_size, au_event_t e_type, au_emod_t e_mod);
token_t	*au_to_header_ex(int rec_size, au_event_t e_type, au_emod_t e_mod);
token_t	*au_to_header32(int rec_size, au_event_t e_type, au_emod_t e_mod);
token_t	*au_to_header64(int rec_size, au_event_t e_type, au_emod_t e_mod);
token_t	*au_to_header32_ex(int rec_size, au_event_t e_type, au_emod_t e_mod);
#endif

token_t	*au_to_me(void);
token_t	*au_to_arg(char n, const char *text, uint32_t v);
token_t	*au_to_arg32(char n, const char *text, uint32_t v);
token_t	*au_to_arg64(char n, const char *text, uint64_t v);

#if defined(_KERNEL) || defined(KERNEL)
token_t	*au_to_attr(struct vnode_au_info *vni);
token_t	*au_to_attr32(struct vnode_au_info *vni);
token_t	*au_to_attr64(struct vnode_au_info *vni);
#endif

token_t	*au_to_data(char unit_print, char unit_type, char unit_count,
	    const char *p);
token_t	*au_to_exit(int retval, int err);
token_t	*au_to_groups(int *groups);
token_t	*au_to_newgroups(uint16_t n, gid_t *groups);
token_t	*au_to_in_addr(struct in_addr *internet_addr);
token_t	*au_to_in_addr_ex(struct in6_addr *internet_addr);
token_t	*au_to_ip(struct ip *ip);
token_t	*au_to_ipc(char type, int id);
token_t	*au_to_ipc_perm(struct ipc_perm *perm);
token_t	*au_to_iport(uint16_t iport);
token_t	*au_to_opaque(const char *data, uint16_t bytes);
token_t	*au_to_path(const char *path);
token_t	*au_to_privset(char *privtypestr, char *privstr);
token_t	*au_to_process(au_id_t auid, uid_t euid, gid_t egid, uid_t ruid,
	    gid_t rgid, pid_t pid, au_asid_t sid, au_tid_t *tid);
token_t	*au_to_process32(au_id_t auid, uid_t euid, gid_t egid, uid_t ruid,
	    gid_t rgid, pid_t pid, au_asid_t sid, au_tid_t *tid);
token_t	*au_to_process64(au_id_t auid, uid_t euid, gid_t egid, uid_t ruid,
	    gid_t rgid, pid_t pid, au_asid_t sid, au_tid_t *tid);
token_t	*au_to_process_ex(au_id_t auid, uid_t euid, gid_t egid, uid_t ruid,
	    gid_t rgid, pid_t pid, au_asid_t sid, au_tid_addr_t *tid);
token_t	*au_to_process32_ex(au_id_t auid, uid_t euid, gid_t egid,
	    uid_t ruid, gid_t rgid, pid_t pid, au_asid_t sid,
	    au_tid_addr_t *tid);
token_t	*au_to_process64_ex(au_id_t auid, uid_t euid, gid_t egid, uid_t ruid,
	    gid_t rgid, pid_t pid, au_asid_t sid, au_tid_addr_t *tid);
token_t	*au_to_rights(cap_rights_t *rightsp);
token_t	*au_to_return(char status, uint32_t ret);
token_t	*au_to_return32(char status, uint32_t ret);
token_t	*au_to_return64(char status, uint64_t ret);
token_t	*au_to_seq(long audit_count);
token_t	*au_to_socket_ex(u_short so_domain, u_short so_type,
	    struct sockaddr *sa_local, struct sockaddr *sa_remote);
token_t	*au_to_sock_inet(struct sockaddr_in *so);
token_t	*au_to_sock_inet32(struct sockaddr_in *so);
token_t	*au_to_sock_inet128(struct sockaddr_in6 *so);
token_t	*au_to_sock_unix(struct sockaddr_un *so);
token_t	*au_to_subject(au_id_t auid, uid_t euid, gid_t egid, uid_t ruid,
	    gid_t rgid, pid_t pid, au_asid_t sid, au_tid_t *tid);
token_t	*au_to_subject32(au_id_t auid, uid_t euid, gid_t egid, uid_t ruid,
	    gid_t rgid, pid_t pid, au_asid_t sid, au_tid_t *tid);
token_t	*au_to_subject64(au_id_t auid, uid_t euid, gid_t egid, uid_t ruid,
	    gid_t rgid, pid_t pid, au_asid_t sid, au_tid_t *tid);
token_t	*au_to_subject_ex(au_id_t auid, uid_t euid, gid_t egid, uid_t ruid,
	    gid_t rgid, pid_t pid, au_asid_t sid, au_tid_addr_t *tid);
token_t	*au_to_subject32_ex(au_id_t auid, uid_t euid, gid_t egid, uid_t ruid,
	    gid_t rgid, pid_t pid, au_asid_t sid, au_tid_addr_t *tid);
token_t	*au_to_subject64_ex(au_id_t auid, uid_t euid, gid_t egid, uid_t ruid,
	    gid_t rgid, pid_t pid, au_asid_t sid, au_tid_addr_t *tid);
#if defined(_KERNEL) || defined(KERNEL)
token_t	*au_to_exec_args(char *args, int argc);
token_t	*au_to_exec_env(char *envs, int envc);
#else
token_t	*au_to_exec_args(char **argv);
token_t	*au_to_exec_env(char **envp);
#endif
token_t	*au_to_text(const char *text);
token_t	*au_to_kevent(struct kevent *kev);
token_t	*au_to_trailer(int rec_size);
token_t	*au_to_upriv(char sorf, char *priv);
token_t	*au_to_zonename(const char *zonename);

/*
 * BSM library routines for converting between local and BSM constant spaces.
 */
int	 au_bsm_to_domain(u_short bsm_domain, int *local_domainp);
int	 au_bsm_to_errno(u_char bsm_error, int *errorp);
int	 au_bsm_to_fcntl_cmd(u_short bsm_fcntl_cmd, int *local_fcntl_cmdp);
int	 au_bsm_to_socket_type(u_short bsm_socket_type,
	    int *local_socket_typep);
u_short	 au_domain_to_bsm(int local_domain);
u_char	 au_errno_to_bsm(int local_errno);
u_short	 au_fcntl_cmd_to_bsm(int local_fcntl_command);
u_short	 au_socket_type_to_bsm(int local_socket_type);

__END_DECLS

#endif /* ! _BSM_AUDIT_RECORD_H_ */
