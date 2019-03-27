/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2005-2009 Apple Inc.
 * Copyright (c) 2016 Robert N. M. Watson
 * All rights reserved.
 *
 * Portions of this software were developed by BAE Systems, the University of
 * Cambridge Computer Laboratory, and Memorial University under DARPA/AFRL
 * contract FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent
 * Computing (TC) research program.
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

#ifndef	_BSM_AUDIT_H
#define	_BSM_AUDIT_H

#include <sys/param.h>
#include <sys/types.h>

#define	AUDIT_RECORD_MAGIC	0x828a0f1b
#define	MAX_AUDIT_RECORDS	20
#define	MAXAUDITDATA		(0x8000 - 1)
#define	MAX_AUDIT_RECORD_SIZE	MAXAUDITDATA
#define	MIN_AUDIT_FILE_SIZE	(512 * 1024)

/*
 * Minimum noumber of free blocks on the filesystem containing the audit
 * log necessary to avoid a hard log rotation. DO NOT SET THIS VALUE TO 0
 * as the kernel does an unsigned compare, plus we want to leave a few blocks
 * free so userspace can terminate the log, etc.
 */
#define	AUDIT_HARD_LIMIT_FREE_BLOCKS	4

/*
 * Triggers for the audit daemon.
 */
#define	AUDIT_TRIGGER_MIN		1
#define	AUDIT_TRIGGER_LOW_SPACE		1	/* Below low watermark. */
#define	AUDIT_TRIGGER_ROTATE_KERNEL	2	/* Kernel requests rotate. */
#define	AUDIT_TRIGGER_READ_FILE		3	/* Re-read config file. */
#define	AUDIT_TRIGGER_CLOSE_AND_DIE	4	/* Terminate audit. */
#define	AUDIT_TRIGGER_NO_SPACE		5	/* Below min free space. */
#define	AUDIT_TRIGGER_ROTATE_USER	6	/* User requests rotate. */
#define	AUDIT_TRIGGER_INITIALIZE	7	/* User initialize of auditd. */
#define	AUDIT_TRIGGER_EXPIRE_TRAILS	8	/* User expiration of trails. */
#define	AUDIT_TRIGGER_MAX		8

/*
 * The special device filename (FreeBSD).
 */
#define	AUDITDEV_FILENAME	"audit"
#define	AUDIT_TRIGGER_FILE	("/dev/" AUDITDEV_FILENAME)

/*
 * Pre-defined audit IDs
 */
#define	AU_DEFAUDITID	(uid_t)(-1)
#define	AU_DEFAUDITSID	 0
#define	AU_ASSIGN_ASID	-1

/*
 * IPC types.
 */
#define	AT_IPC_MSG	((u_char)1)	/* Message IPC id. */
#define	AT_IPC_SEM	((u_char)2)	/* Semaphore IPC id. */
#define	AT_IPC_SHM	((u_char)3)	/* Shared mem IPC id. */

/*
 * Audit conditions.
 */
#define	AUC_UNSET		0
#define	AUC_AUDITING		1
#define	AUC_NOAUDIT		2
#define	AUC_DISABLED		-1

/*
 * auditon(2) commands.
 */
#define	A_OLDGETPOLICY	2
#define	A_OLDSETPOLICY	3
#define	A_GETKMASK	4
#define	A_SETKMASK	5
#define	A_OLDGETQCTRL	6
#define	A_OLDSETQCTRL	7
#define	A_GETCWD	8
#define	A_GETCAR	9
#define	A_GETSTAT	12
#define	A_SETSTAT	13
#define	A_SETUMASK	14
#define	A_SETSMASK	15
#define	A_OLDGETCOND	20
#define	A_OLDSETCOND	21
#define	A_GETCLASS	22
#define	A_SETCLASS	23
#define	A_GETPINFO	24
#define	A_SETPMASK	25
#define	A_SETFSIZE	26
#define	A_GETFSIZE	27
#define	A_GETPINFO_ADDR	28
#define	A_GETKAUDIT	29
#define	A_SETKAUDIT	30
#define	A_SENDTRIGGER	31
#define	A_GETSINFO_ADDR	32
#define	A_GETPOLICY	33
#define	A_SETPOLICY	34
#define	A_GETQCTRL	35
#define	A_SETQCTRL	36
#define	A_GETCOND	37
#define	A_SETCOND	38
#define	A_GETEVENT	39	/* Get audit event-to-name mapping. */
#define	A_SETEVENT	40	/* Set audit event-to-name mapping. */

/*
 * Audit policy controls.
 */
#define	AUDIT_CNT	0x0001
#define	AUDIT_AHLT	0x0002
#define	AUDIT_ARGV	0x0004
#define	AUDIT_ARGE	0x0008
#define	AUDIT_SEQ	0x0010
#define	AUDIT_WINDATA	0x0020
#define	AUDIT_USER	0x0040
#define	AUDIT_GROUP	0x0080
#define	AUDIT_TRAIL	0x0100
#define	AUDIT_PATH	0x0200
#define	AUDIT_SCNT	0x0400
#define	AUDIT_PUBLIC	0x0800
#define	AUDIT_ZONENAME	0x1000
#define	AUDIT_PERZONE	0x2000

/*
 * Default audit queue control parameters.
 */
#define	AQ_HIWATER	100
#define	AQ_MAXHIGH	10000
#define	AQ_LOWATER	10
#define	AQ_BUFSZ	MAXAUDITDATA
#define	AQ_MAXBUFSZ	1048576

/*
 * Default minimum percentage free space on file system.
 */
#define	AU_FS_MINFREE	20

/*
 * Type definitions used indicating the length of variable length addresses
 * in tokens containing addresses, such as header fields.
 */
#define	AU_IPv4		4
#define	AU_IPv6		16

__BEGIN_DECLS

typedef	uid_t		au_id_t;
typedef	pid_t		au_asid_t;
typedef	u_int16_t	au_event_t;
typedef	u_int16_t	au_emod_t;
typedef	u_int32_t	au_class_t;
typedef	u_int64_t	au_asflgs_t __attribute__ ((aligned (8)));

struct au_tid {
	u_int32_t	port;		/* XXX dev_t compatibility */
	u_int32_t	machine;
};
typedef	struct au_tid	au_tid_t;

struct au_tid_addr {
	u_int32_t	at_port;	/* XXX dev_t compatibility */
	u_int32_t	at_type;
	u_int32_t	at_addr[4];
};
typedef	struct au_tid_addr	au_tid_addr_t;

struct au_mask {
	unsigned int    am_success;     /* Success bits. */
	unsigned int    am_failure;     /* Failure bits. */
};
typedef	struct au_mask	au_mask_t;

struct auditinfo {
	au_id_t		ai_auid;	/* Audit user ID. */
	au_mask_t	ai_mask;	/* Audit masks. */
	au_tid_t	ai_termid;	/* Terminal ID. */
	au_asid_t	ai_asid;	/* Audit session ID. */
};
typedef	struct auditinfo	auditinfo_t;

struct auditinfo_addr {
	au_id_t		ai_auid;	/* Audit user ID. */
	au_mask_t	ai_mask;	/* Audit masks. */
	au_tid_addr_t	ai_termid;	/* Terminal ID. */
	au_asid_t	ai_asid;	/* Audit session ID. */
	au_asflgs_t	ai_flags;	/* Audit session flags. */
};
typedef	struct auditinfo_addr	auditinfo_addr_t;

struct auditpinfo {
	pid_t		ap_pid;		/* ID of target process. */
	au_id_t		ap_auid;	/* Audit user ID. */
	au_mask_t	ap_mask;	/* Audit masks. */
	au_tid_t	ap_termid;	/* Terminal ID. */
	au_asid_t	ap_asid;	/* Audit session ID. */
};
typedef	struct auditpinfo	auditpinfo_t;

struct auditpinfo_addr {
	pid_t		ap_pid;		/* ID of target process. */
	au_id_t		ap_auid;	/* Audit user ID. */
	au_mask_t	ap_mask;	/* Audit masks. */
	au_tid_addr_t	ap_termid;	/* Terminal ID. */
	au_asid_t	ap_asid;	/* Audit session ID. */
	au_asflgs_t	ap_flags;	/* Audit session flags. */
};
typedef	struct auditpinfo_addr	auditpinfo_addr_t;

struct au_session {
	auditinfo_addr_t	*as_aia_p;	/* Ptr to full audit info. */
	au_mask_t		 as_mask;	/* Process Audit Masks. */
};
typedef struct au_session       au_session_t;

/*
 * Contents of token_t are opaque outside of libbsm.
 */
typedef	struct au_token	token_t;

/*
 * Kernel audit queue control parameters:
 * 			Default:		Maximum:
 * 	aq_hiwater:	AQ_HIWATER (100)	AQ_MAXHIGH (10000) 
 * 	aq_lowater:	AQ_LOWATER (10)		<aq_hiwater
 * 	aq_bufsz:	AQ_BUFSZ (32767)	AQ_MAXBUFSZ (1048576)
 * 	aq_delay:	20			20000 (not used) 
 */
struct au_qctrl {
	int	aq_hiwater;	/* Max # of audit recs in queue when */
				/* threads with new ARs get blocked. */ 

	int	aq_lowater;	/* # of audit recs in queue when */
				/* blocked threads get unblocked. */

	int	aq_bufsz;	/* Max size of audit record for audit(2). */
	int	aq_delay;	/* Queue delay (not used). */
	int	aq_minfree;	/* Minimum filesystem percent free space. */
};
typedef	struct au_qctrl	au_qctrl_t;

/*
 * Structure for the audit statistics.
 */
struct audit_stat {
	unsigned int	as_version;
	unsigned int	as_numevent;
	int		as_generated;
	int		as_nonattrib;
	int		as_kernel;
	int		as_audit;
	int		as_auditctl;
	int		as_enqueue;
	int		as_written;
	int		as_wblocked;
	int		as_rblocked;
	int		as_dropped;
	int		as_totalsize;
	unsigned int	as_memused;
};
typedef	struct audit_stat	au_stat_t;

/*
 * Structure for the audit file statistics.
 */
struct audit_fstat {
	u_int64_t	af_filesz;
	u_int64_t	af_currsz;
};
typedef	struct audit_fstat	au_fstat_t;

/*
 * Audit to event class mapping.
 */
struct au_evclass_map {
	au_event_t	ec_number;
	au_class_t	ec_class;
};
typedef	struct au_evclass_map	au_evclass_map_t;

/*
 * Event-to-name mapping.
 */
#define	EVNAMEMAP_NAME_SIZE	64
struct au_evname_map {
	au_event_t	en_number;
	char		en_name[EVNAMEMAP_NAME_SIZE];
};
typedef struct au_evname_map	au_evname_map_t;

/*
 * Audit system calls.
 */
#if !defined(_KERNEL) && !defined(KERNEL)
int	audit(const void *, int);
int	auditon(int, void *, int);
int	auditctl(const char *);
int	getauid(au_id_t *);
int	setauid(const au_id_t *);
int	getaudit(struct auditinfo *);
int	setaudit(const struct auditinfo *);
int	getaudit_addr(struct auditinfo_addr *, int);
int	setaudit_addr(const struct auditinfo_addr *, int);

#ifdef __APPLE_API_PRIVATE
#include <mach/port.h>
mach_port_name_t audit_session_self(void);
au_asid_t	 audit_session_join(mach_port_name_t port);
#endif /* __APPLE_API_PRIVATE */

#endif /* defined(_KERNEL) || defined(KERNEL) */

__END_DECLS

#endif /* !_BSM_AUDIT_H */
