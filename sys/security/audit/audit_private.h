/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1999-2009 Apple Inc.
 * Copyright (c) 2016, 2018 Robert N. M. Watson
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
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * This include file contains function prototypes and type definitions used
 * within the audit implementation.
 */

#ifndef _SECURITY_AUDIT_PRIVATE_H_
#define	_SECURITY_AUDIT_PRIVATE_H_

#ifndef _KERNEL
#error "no user-serviceable parts inside"
#endif

#include <sys/caprights.h>
#include <sys/ipc.h>
#include <sys/socket.h>
#include <sys/ucred.h>

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_AUDITBSM);
MALLOC_DECLARE(M_AUDITDATA);
MALLOC_DECLARE(M_AUDITPATH);
MALLOC_DECLARE(M_AUDITTEXT);
MALLOC_DECLARE(M_AUDITGIDSET);
#endif

/*
 * Audit control variables that are usually set/read via system calls and
 * used to control various aspects of auditing.
 */
extern struct au_qctrl		audit_qctrl;
extern struct audit_fstat	audit_fstat;
extern struct au_mask		audit_nae_mask;
extern int			audit_panic_on_write_fail;
extern int			audit_fail_stop;
extern int			audit_argv;
extern int			audit_arge;

/*
 * Success/failure conditions for the conversion of a kernel audit record to
 * BSM format.
 */
#define	BSM_SUCCESS	0
#define	BSM_FAILURE	1
#define	BSM_NOAUDIT	2

/*
 * Defines for the kernel audit record k_ar_commit field.  Flags are set to
 * indicate what sort of record it is, and which preselection mechanism
 * selected it.
 */
#define	AR_COMMIT_KERNEL	0x00000001U
#define	AR_COMMIT_USER		0x00000010U

#define	AR_PRESELECT_TRAIL	0x00001000U
#define	AR_PRESELECT_PIPE	0x00002000U

#define	AR_PRESELECT_USER_TRAIL	0x00004000U
#define	AR_PRESELECT_USER_PIPE	0x00008000U

#define	AR_PRESELECT_DTRACE	0x00010000U

/*
 * Audit data is generated as a stream of struct audit_record structures,
 * linked by struct kaudit_record, and contain storage for possible audit so
 * that it will not need to be allocated during the processing of a system
 * call, both improving efficiency and avoiding sleeping at untimely moments.
 * This structure is converted to BSM format before being written to disk.
 */
struct vnode_au_info {
	mode_t	vn_mode;
	uid_t	vn_uid;
	gid_t	vn_gid;
	u_int32_t vn_dev;		/* XXX dev_t compatibility */
	long	vn_fsid;		/* XXX uint64_t compatibility */
	long	vn_fileid;		/* XXX ino_t compatibility */
	long	vn_gen;
};

struct groupset {
	gid_t	*gidset;
	u_int	 gidset_size;
};

struct socket_au_info {
	int		so_domain;
	int		so_type;
	int		so_protocol;
	in_addr_t	so_raddr;	/* Remote address if INET socket. */
	in_addr_t	so_laddr;	/* Local address if INET socket. */
	u_short		so_rport;	/* Remote port. */
	u_short		so_lport;	/* Local port. */
};

/*
 * The following is used for A_OLDSETQCTRL and AU_OLDGETQCTRL and a 64-bit
 * userland.
 */
struct au_qctrl64 {
	u_int64_t	aq64_hiwater;
	u_int64_t	aq64_lowater;
	u_int64_t	aq64_bufsz;
	u_int64_t	aq64_delay;
	u_int64_t	aq64_minfree;
};
typedef	struct au_qctrl64	au_qctrl64_t;

union auditon_udata {
	char			*au_path;
	int			au_cond;
	int			au_flags;
	int			au_policy;
	int			au_trigger;
	int64_t			au_cond64;
	int64_t			au_policy64;
	au_evclass_map_t	au_evclass;
	au_mask_t		au_mask;
	auditinfo_t		au_auinfo;
	auditpinfo_t		au_aupinfo;
	auditpinfo_addr_t	au_aupinfo_addr;
	au_qctrl_t		au_qctrl;
	au_qctrl64_t		au_qctrl64;
	au_stat_t		au_stat;
	au_fstat_t		au_fstat;
	auditinfo_addr_t	au_kau_info;
	au_evname_map_t		au_evname;
};

struct posix_ipc_perm {
	uid_t	pipc_uid;
	gid_t	pipc_gid;
	mode_t	pipc_mode;
};

struct audit_record {
	/* Audit record header. */
	u_int32_t		ar_magic;
	int			ar_event;
	int			ar_retval; /* value returned to the process */
	int			ar_errno;  /* return status of system call */
	struct timespec		ar_starttime;
	struct timespec		ar_endtime;
	u_int64_t		ar_valid_arg;  /* Bitmask of valid arguments */

	/* Audit subject information. */
	struct xucred		ar_subj_cred;
	uid_t			ar_subj_ruid;
	gid_t			ar_subj_rgid;
	gid_t			ar_subj_egid;
	uid_t			ar_subj_auid; /* Audit user ID */
	pid_t			ar_subj_asid; /* Audit session ID */
	pid_t			ar_subj_pid;
	struct au_tid		ar_subj_term;
	struct au_tid_addr	ar_subj_term_addr;
	struct au_mask		ar_subj_amask;

	/* Operation arguments. */
	uid_t			ar_arg_euid;
	uid_t			ar_arg_ruid;
	uid_t			ar_arg_suid;
	gid_t			ar_arg_egid;
	gid_t			ar_arg_rgid;
	gid_t			ar_arg_sgid;
	pid_t			ar_arg_pid;
	pid_t			ar_arg_asid;
	struct au_tid		ar_arg_termid;
	struct au_tid_addr	ar_arg_termid_addr;
	uid_t			ar_arg_uid;
	uid_t			ar_arg_auid;
	gid_t			ar_arg_gid;
	struct groupset		ar_arg_groups;
	int			ar_arg_fd;
	int			ar_arg_atfd1;
	int			ar_arg_atfd2;
	int			ar_arg_fflags;
	mode_t			ar_arg_mode;
	int			ar_arg_dev;	/* XXX dev_t compatibility */
	long			ar_arg_value;
	void			*ar_arg_addr;
	int			ar_arg_len;
	int			ar_arg_mask;
	u_int			ar_arg_signum;
	char			ar_arg_login[MAXLOGNAME];
	int			ar_arg_ctlname[CTL_MAXNAME];
	struct socket_au_info	ar_arg_sockinfo;
	char			*ar_arg_upath1;
	char			*ar_arg_upath2;
	char			*ar_arg_text;
	struct au_mask		ar_arg_amask;
	struct vnode_au_info	ar_arg_vnode1;
	struct vnode_au_info	ar_arg_vnode2;
	int			ar_arg_cmd;
	int			ar_arg_svipc_which;
	int			ar_arg_svipc_cmd;
	struct ipc_perm		ar_arg_svipc_perm;
	int			ar_arg_svipc_id;
	void			*ar_arg_svipc_addr;
	struct posix_ipc_perm	ar_arg_pipc_perm;
	union auditon_udata	ar_arg_auditon;
	char			*ar_arg_argv;
	int			ar_arg_argc;
	char			*ar_arg_envv;
	int			ar_arg_envc;
	int			ar_arg_exitstatus;
	int			ar_arg_exitretval;
	struct sockaddr_storage ar_arg_sockaddr;
	cap_rights_t		ar_arg_rights;
	uint32_t		ar_arg_fcntl_rights;
	char			ar_jailname[MAXHOSTNAMELEN];
};

/*
 * Arguments in the audit record are initially not defined; flags are set to
 * indicate if they are present so they can be included in the audit log
 * stream only if defined.
 */
#define	ARG_EUID		0x0000000000000001ULL
#define	ARG_RUID		0x0000000000000002ULL
#define	ARG_SUID		0x0000000000000004ULL
#define	ARG_EGID		0x0000000000000008ULL
#define	ARG_RGID		0x0000000000000010ULL
#define	ARG_SGID		0x0000000000000020ULL
#define	ARG_PID			0x0000000000000040ULL
#define	ARG_UID			0x0000000000000080ULL
#define	ARG_AUID		0x0000000000000100ULL
#define	ARG_GID			0x0000000000000200ULL
#define	ARG_FD			0x0000000000000400ULL
#define	ARG_POSIX_IPC_PERM	0x0000000000000800ULL
#define	ARG_FFLAGS		0x0000000000001000ULL
#define	ARG_MODE		0x0000000000002000ULL
#define	ARG_DEV			0x0000000000004000ULL
#define	ARG_ADDR		0x0000000000008000ULL
#define	ARG_LEN			0x0000000000010000ULL
#define	ARG_MASK		0x0000000000020000ULL
#define	ARG_SIGNUM		0x0000000000040000ULL
#define	ARG_LOGIN		0x0000000000080000ULL
#define	ARG_SADDRINET		0x0000000000100000ULL
#define	ARG_SADDRINET6		0x0000000000200000ULL
#define	ARG_SADDRUNIX		0x0000000000400000ULL
#define	ARG_TERMID_ADDR		0x0000000000800000ULL
#define	ARG_UNUSED2		0x0000000001000000ULL
#define	ARG_UPATH1		0x0000000002000000ULL
#define	ARG_UPATH2		0x0000000004000000ULL
#define	ARG_TEXT		0x0000000008000000ULL
#define	ARG_VNODE1		0x0000000010000000ULL
#define	ARG_VNODE2		0x0000000020000000ULL
#define	ARG_SVIPC_CMD		0x0000000040000000ULL
#define	ARG_SVIPC_PERM		0x0000000080000000ULL
#define	ARG_SVIPC_ID		0x0000000100000000ULL
#define	ARG_SVIPC_ADDR		0x0000000200000000ULL
#define	ARG_GROUPSET		0x0000000400000000ULL
#define	ARG_CMD			0x0000000800000000ULL
#define	ARG_SOCKINFO		0x0000001000000000ULL
#define	ARG_ASID		0x0000002000000000ULL
#define	ARG_TERMID		0x0000004000000000ULL
#define	ARG_AUDITON		0x0000008000000000ULL
#define	ARG_VALUE		0x0000010000000000ULL
#define	ARG_AMASK		0x0000020000000000ULL
#define	ARG_CTLNAME		0x0000040000000000ULL
#define	ARG_PROCESS		0x0000080000000000ULL
#define	ARG_MACHPORT1		0x0000100000000000ULL
#define	ARG_MACHPORT2		0x0000200000000000ULL
#define	ARG_EXIT		0x0000400000000000ULL
#define	ARG_IOVECSTR		0x0000800000000000ULL
#define	ARG_ARGV		0x0001000000000000ULL
#define	ARG_ENVV		0x0002000000000000ULL
#define	ARG_ATFD1		0x0004000000000000ULL
#define	ARG_ATFD2		0x0008000000000000ULL
#define	ARG_RIGHTS		0x0010000000000000ULL
#define	ARG_FCNTL_RIGHTS	0x0020000000000000ULL
#define	ARG_SVIPC_WHICH		0x0200000000000000ULL
#define	ARG_NONE		0x0000000000000000ULL
#define	ARG_ALL			0xFFFFFFFFFFFFFFFFULL

#define	ARG_IS_VALID(kar, arg)	((kar)->k_ar.ar_valid_arg & (arg))
#define	ARG_SET_VALID(kar, arg) do {					\
	(kar)->k_ar.ar_valid_arg |= (arg);				\
} while (0)
#define	ARG_CLEAR_VALID(kar, arg) do {					\
	(kar)->k_ar.ar_valid_arg &= ~(arg);				\
} while (0)

/*
 * In-kernel version of audit record; the basic record plus queue meta-data.
 * This record can also have a pointer set to some opaque data that will be
 * passed through to the audit writing mechanism.
 */
struct kaudit_record {
	struct audit_record		 k_ar;
	u_int32_t			 k_ar_commit;
	void				*k_udata;	/* User data. */
	u_int				 k_ulen;	/* User data length. */
	struct uthread			*k_uthread;	/* Audited thread. */
	void				*k_dtaudit_state;
	TAILQ_ENTRY(kaudit_record)	 k_q;
};
TAILQ_HEAD(kaudit_queue, kaudit_record);

/*
 * Functions to manage the allocation, release, and commit of kernel audit
 * records.
 */
void			 audit_abort(struct kaudit_record *ar);
void			 audit_commit(struct kaudit_record *ar, int error,
			    int retval);
struct kaudit_record	*audit_new(int event, struct thread *td);

/*
 * Function to update the audit_syscalls_enabled flag, whose value is affected
 * by configuration of the audit trail/pipe mechanism and DTrace.  Call this
 * function when any of the inputs to that policy change.
 */
void	audit_syscalls_enabled_update(void);

/*
 * Functions relating to the conversion of internal kernel audit records to
 * the BSM file format.
 */
struct au_record;
int	 kaudit_to_bsm(struct kaudit_record *kar, struct au_record **pau);
int	 bsm_rec_verify(void *rec);

/*
 * Kernel versions of the libbsm audit record functions.
 */
void	 kau_free(struct au_record *rec);
void	 kau_init(void);

/*
 * Return values for pre-selection and post-selection decisions.
 */
#define	AU_PRS_SUCCESS	1
#define	AU_PRS_FAILURE	2
#define	AU_PRS_BOTH	(AU_PRS_SUCCESS|AU_PRS_FAILURE)

/*
 * Data structures relating to the kernel audit queue.  Ideally, these might
 * be abstracted so that only accessor methods are exposed.
 */
extern struct mtx		audit_mtx;
extern struct cv		audit_watermark_cv;
extern struct cv		audit_worker_cv;
extern struct kaudit_queue	audit_q;
extern int			audit_q_len;
extern int			audit_pre_q_len;
extern int			audit_in_failure;

/*
 * Flags to use on audit files when opening and closing.
 */
#define	AUDIT_OPEN_FLAGS	(FWRITE | O_APPEND)
#define	AUDIT_CLOSE_FLAGS	(FWRITE | O_APPEND)

/*
 * Audit event-to-name mapping structure, maintained in audit_bsm_klib.c.  It
 * appears in this header so that the DTrace audit provider can dereference
 * instances passed back in the au_evname_foreach() callbacks.  Safe access to
 * its fields requires holding ene_lock (after it is visible in the global
 * table).
 *
 * Locking:
 * (c) - Constant after inserted in the global table
 * (l) - Protected by ene_lock
 * (m) - Protected by evnamemap_lock (audit_bsm_klib.c)
 * (M) - Writes protected by evnamemap_lock; reads unprotected.
 */
struct evname_elem {
	au_event_t		ene_event;			/* (c) */
	char			ene_name[EVNAMEMAP_NAME_SIZE];	/* (l) */
	LIST_ENTRY(evname_elem)	ene_entry;			/* (m) */
	struct mtx		ene_lock;

	/* DTrace probe IDs; 0 if not yet registered. */
	uint32_t		ene_commit_probe_id;		/* (M) */
	uint32_t		ene_bsm_probe_id;		/* (M) */

	/* Flags indicating if the probes enabled or not. */
	int			ene_commit_probe_enabled;	/* (M) */
	int			ene_bsm_probe_enabled;		/* (M) */
};

#define	EVNAME_LOCK(ene)	mtx_lock(&(ene)->ene_lock)
#define	EVNAME_UNLOCK(ene)	mtx_unlock(&(ene)->ene_lock)

/*
 * Callback function typedef for the same.
 */
typedef	void	(*au_evnamemap_callback_t)(struct evname_elem *ene);

/*
 * DTrace audit provider (dtaudit) hooks -- to be set non-NULL when the audit
 * provider is loaded and ready to be called into.
 */
extern void	*(*dtaudit_hook_preselect)(au_id_t auid, au_event_t event,
		    au_class_t class);
extern int	(*dtaudit_hook_commit)(struct kaudit_record *kar,
		    au_id_t auid, au_event_t event, au_class_t class,
		    int sorf);
extern void	(*dtaudit_hook_bsm)(struct kaudit_record *kar, au_id_t auid,
		    au_event_t event, au_class_t class, int sorf,
		    void *bsm_data, size_t bsm_len);

#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/malloc.h>

/*
 * Some of the BSM tokenizer functions take different parameters in the
 * kernel implementations in order to save the copying of large kernel data
 * structures.  The prototypes of these functions are declared here.
 */
token_t		*kau_to_socket(struct socket_au_info *soi);

/*
 * audit_klib prototypes
 */
int		 au_preselect(au_event_t event, au_class_t class,
		    au_mask_t *mask_p, int sorf);
void		 au_evclassmap_init(void);
void		 au_evclassmap_insert(au_event_t event, au_class_t class);
au_class_t	 au_event_class(au_event_t event);
void		 au_evnamemap_init(void);
void		 au_evnamemap_insert(au_event_t event, const char *name);
void		 au_evnamemap_foreach(au_evnamemap_callback_t callback);
struct evname_elem	*au_evnamemap_lookup(au_event_t event);
int		 au_event_name(au_event_t event, char *name);
au_event_t	 audit_ctlname_to_sysctlevent(int name[], uint64_t valid_arg);
au_event_t	 audit_flags_and_error_to_openevent(int oflags, int error);
au_event_t	 audit_flags_and_error_to_openatevent(int oflags, int error);
au_event_t	 audit_msgctl_to_event(int cmd);
au_event_t	 audit_msgsys_to_event(int which);
au_event_t	 audit_semctl_to_event(int cmd);
au_event_t	 audit_semsys_to_event(int which);
au_event_t	 audit_shmsys_to_event(int which);
void		 audit_canon_path(struct thread *td, int dirfd, char *path,
		    char *cpath);
au_event_t	 auditon_command_event(int cmd);

/*
 * Audit trigger events notify user space of kernel audit conditions
 * asynchronously.
 */
void		 audit_trigger_init(void);
int		 audit_send_trigger(unsigned int trigger);

/*
 * Accessor functions to manage global audit state.
 */
void	 audit_set_kinfo(struct auditinfo_addr *);
void	 audit_get_kinfo(struct auditinfo_addr *);

/*
 * General audit related functions.
 */
struct kaudit_record	*currecord(void);
void			 audit_free(struct kaudit_record *ar);
void			 audit_shutdown(void *arg, int howto);
void			 audit_rotate_vnode(struct ucred *cred,
			    struct vnode *vp);
void			 audit_worker_init(void);

/*
 * Audit pipe functions.
 */
int	 audit_pipe_preselect(au_id_t auid, au_event_t event,
	    au_class_t class, int sorf, int trail_select);
void	 audit_pipe_submit(au_id_t auid, au_event_t event, au_class_t class,
	    int sorf, int trail_select, void *record, u_int record_len);
void	 audit_pipe_submit_user(void *record, u_int record_len);

#endif /* ! _SECURITY_AUDIT_PRIVATE_H_ */
