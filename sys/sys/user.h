/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1989, 1991, 1993
 *	The Regents of the University of California.
 * Copyright (c) 2007 Robert N. M. Watson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)user.h	8.2 (Berkeley) 9/23/93
 * $FreeBSD$
 */

#ifndef _SYS_USER_H_
#define _SYS_USER_H_

#include <machine/pcb.h>
#ifndef _KERNEL
/* stuff that *used* to be included by user.h, or is now needed */
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/ucred.h>
#include <sys/uio.h>
#include <sys/queue.h>
#include <sys/_lock.h>
#include <sys/_mutex.h>
#include <sys/proc.h>
#include <vm/vm.h>		/* XXX */
#include <vm/vm_param.h>	/* XXX */
#include <vm/pmap.h>		/* XXX */
#include <vm/vm_map.h>		/* XXX */
#endif /* !_KERNEL */
#ifndef _SYS_RESOURCEVAR_H_
#include <sys/resourcevar.h>
#endif
#ifndef _SYS_SIGNALVAR_H_
#include <sys/signalvar.h>
#endif
#ifndef _SYS_SOCKET_VAR_H_
#include <sys/socket.h>
#endif
#include <sys/caprights.h>

/*
 * KERN_PROC subtype ops return arrays of selected proc structure entries:
 *
 * This struct includes several arrays of spare space, with different arrays
 * for different standard C-types.  When adding new variables to this struct,
 * the space for byte-aligned data should be taken from the ki_sparestring,
 * pointers from ki_spareptrs, word-aligned data from ki_spareints, and
 * doubleword-aligned data from ki_sparelongs.  Make sure the space for new
 * variables come from the array which matches the size and alignment of
 * those variables on ALL hardware platforms, and then adjust the appropriate
 * KI_NSPARE_* value(s) to match.
 *
 * Always verify that sizeof(struct kinfo_proc) == KINFO_PROC_SIZE on all
 * platforms after you have added new variables.  Note that if you change
 * the value of KINFO_PROC_SIZE, then many userland programs will stop
 * working until they are recompiled!
 *
 * Once you have added the new field, you will need to add code to initialize
 * it in two places: function fill_kinfo_proc in sys/kern/kern_proc.c and
 * function kvm_proclist in lib/libkvm/kvm_proc.c .
 */
#define	KI_NSPARE_INT	2
#define	KI_NSPARE_LONG	12
#define	KI_NSPARE_PTR	6

#ifndef _KERNEL
#ifndef KINFO_PROC_SIZE
#error "Unknown architecture"
#endif
#endif /* !_KERNEL */

#define	WMESGLEN	8		/* size of returned wchan message */
#define	LOCKNAMELEN	8		/* size of returned lock name */
#define	TDNAMLEN	16		/* size of returned thread name */
#define	COMMLEN		19		/* size of returned ki_comm name */
#define	KI_EMULNAMELEN	16		/* size of returned ki_emul */
#define	KI_NGROUPS	16		/* number of groups in ki_groups */
#define	LOGNAMELEN	17		/* size of returned ki_login */
#define	LOGINCLASSLEN	17		/* size of returned ki_loginclass */

#ifndef BURN_BRIDGES
#define	OCOMMLEN	TDNAMLEN	
#define	ki_ocomm	ki_tdname
#endif

/* Flags for the process credential. */
#define	KI_CRF_CAPABILITY_MODE	0x00000001
/*
 * Steal a bit from ki_cr_flags to indicate that the cred had more than
 * KI_NGROUPS groups.
 */
#define KI_CRF_GRP_OVERFLOW	0x80000000

struct kinfo_proc {
	int	ki_structsize;		/* size of this structure */
	int	ki_layout;		/* reserved: layout identifier */
	struct	pargs *ki_args;		/* address of command arguments */
	struct	proc *ki_paddr;		/* address of proc */
	struct	user *ki_addr;		/* kernel virtual addr of u-area */
	struct	vnode *ki_tracep;	/* pointer to trace file */
	struct	vnode *ki_textvp;	/* pointer to executable file */
	struct	filedesc *ki_fd;	/* pointer to open file info */
	struct	vmspace *ki_vmspace;	/* pointer to kernel vmspace struct */
	void	*ki_wchan;		/* sleep address */
	pid_t	ki_pid;			/* Process identifier */
	pid_t	ki_ppid;		/* parent process id */
	pid_t	ki_pgid;		/* process group id */
	pid_t	ki_tpgid;		/* tty process group id */
	pid_t	ki_sid;			/* Process session ID */
	pid_t	ki_tsid;		/* Terminal session ID */
	short	ki_jobc;		/* job control counter */
	short	ki_spare_short1;	/* unused (just here for alignment) */
	uint32_t ki_tdev_freebsd11;	/* controlling tty dev */
	sigset_t ki_siglist;		/* Signals arrived but not delivered */
	sigset_t ki_sigmask;		/* Current signal mask */
	sigset_t ki_sigignore;		/* Signals being ignored */
	sigset_t ki_sigcatch;		/* Signals being caught by user */
	uid_t	ki_uid;			/* effective user id */
	uid_t	ki_ruid;		/* Real user id */
	uid_t	ki_svuid;		/* Saved effective user id */
	gid_t	ki_rgid;		/* Real group id */
	gid_t	ki_svgid;		/* Saved effective group id */
	short	ki_ngroups;		/* number of groups */
	short	ki_spare_short2;	/* unused (just here for alignment) */
	gid_t	ki_groups[KI_NGROUPS];	/* groups */
	vm_size_t ki_size;		/* virtual size */
	segsz_t ki_rssize;		/* current resident set size in pages */
	segsz_t ki_swrss;		/* resident set size before last swap */
	segsz_t ki_tsize;		/* text size (pages) XXX */
	segsz_t ki_dsize;		/* data size (pages) XXX */
	segsz_t ki_ssize;		/* stack size (pages) */
	u_short	ki_xstat;		/* Exit status for wait & stop signal */
	u_short	ki_acflag;		/* Accounting flags */
	fixpt_t	ki_pctcpu;	 	/* %cpu for process during ki_swtime */
	u_int	ki_estcpu;	 	/* Time averaged value of ki_cpticks */
	u_int	ki_slptime;	 	/* Time since last blocked */
	u_int	ki_swtime;	 	/* Time swapped in or out */
	u_int	ki_cow;			/* number of copy-on-write faults */
	u_int64_t ki_runtime;		/* Real time in microsec */
	struct	timeval ki_start;	/* starting time */
	struct	timeval ki_childtime;	/* time used by process children */
	long	ki_flag;		/* P_* flags */
	long	ki_kiflag;		/* KI_* flags (below) */
	int	ki_traceflag;		/* Kernel trace points */
	char	ki_stat;		/* S* process status */
	signed char ki_nice;		/* Process "nice" value */
	char	ki_lock;		/* Process lock (prevent swap) count */
	char	ki_rqindex;		/* Run queue index */
	u_char	ki_oncpu_old;		/* Which cpu we are on (legacy) */
	u_char	ki_lastcpu_old;		/* Last cpu we were on (legacy) */
	char	ki_tdname[TDNAMLEN+1];	/* thread name */
	char	ki_wmesg[WMESGLEN+1];	/* wchan message */
	char	ki_login[LOGNAMELEN+1];	/* setlogin name */
	char	ki_lockname[LOCKNAMELEN+1]; /* lock name */
	char	ki_comm[COMMLEN+1];	/* command name */
	char	ki_emul[KI_EMULNAMELEN+1];  /* emulation name */
	char	ki_loginclass[LOGINCLASSLEN+1]; /* login class */
	char	ki_moretdname[MAXCOMLEN-TDNAMLEN+1];	/* more thread name */
	/*
	 * When adding new variables, take space for char-strings from the
	 * front of ki_sparestrings, and ints from the end of ki_spareints.
	 * That way the spare room from both arrays will remain contiguous.
	 */
	char	ki_sparestrings[46];	/* spare string space */
	int	ki_spareints[KI_NSPARE_INT];	/* spare room for growth */
	uint64_t ki_tdev;		/* controlling tty dev */
	int	ki_oncpu;		/* Which cpu we are on */
	int	ki_lastcpu;		/* Last cpu we were on */
	int	ki_tracer;		/* Pid of tracing process */
	int	ki_flag2;		/* P2_* flags */
	int	ki_fibnum;		/* Default FIB number */
	u_int	ki_cr_flags;		/* Credential flags */
	int	ki_jid;			/* Process jail ID */
	int	ki_numthreads;		/* XXXKSE number of threads in total */
	lwpid_t	ki_tid;			/* XXXKSE thread id */
	struct	priority ki_pri;	/* process priority */
	struct	rusage ki_rusage;	/* process rusage statistics */
	/* XXX - most fields in ki_rusage_ch are not (yet) filled in */
	struct	rusage ki_rusage_ch;	/* rusage of children processes */
	struct	pcb *ki_pcb;		/* kernel virtual addr of pcb */
	void	*ki_kstack;		/* kernel virtual addr of stack */
	void	*ki_udata;		/* User convenience pointer */
	struct	thread *ki_tdaddr;	/* address of thread */
	/*
	 * When adding new variables, take space for pointers from the
	 * front of ki_spareptrs, and longs from the end of ki_sparelongs.
	 * That way the spare room from both arrays will remain contiguous.
	 */
	void	*ki_spareptrs[KI_NSPARE_PTR];	/* spare room for growth */
	long	ki_sparelongs[KI_NSPARE_LONG];	/* spare room for growth */
	long	ki_sflag;		/* PS_* flags */
	long	ki_tdflags;		/* XXXKSE kthread flag */
};
void fill_kinfo_proc(struct proc *, struct kinfo_proc *);
/* XXX - the following two defines are temporary */
#define	ki_childstime	ki_rusage_ch.ru_stime
#define	ki_childutime	ki_rusage_ch.ru_utime

/*
 *  Legacy PS_ flag.  This moved to p_flag but is maintained for
 *  compatibility.
 */
#define	PS_INMEM	0x00001		/* Loaded into memory. */

/* ki_sessflag values */
#define	KI_CTTY		0x00000001	/* controlling tty vnode active */
#define	KI_SLEADER	0x00000002	/* session leader */
#define	KI_LOCKBLOCK	0x00000004	/* proc blocked on lock ki_lockname */

/*
 * This used to be the per-process structure containing data that
 * isn't needed in core when the process is swapped out, but now it
 * remains only for the benefit of a.out core dumps.
 */
struct user {
	struct	pstats u_stats;		/* *p_stats */
	struct	kinfo_proc u_kproc;	/* eproc */
};

/*
 * The KERN_PROC_FILE sysctl allows a process to dump the file descriptor
 * array of another process.
 */
#define	KF_ATTR_VALID	0x0001

#define	KF_TYPE_NONE	0
#define	KF_TYPE_VNODE	1
#define	KF_TYPE_SOCKET	2
#define	KF_TYPE_PIPE	3
#define	KF_TYPE_FIFO	4
#define	KF_TYPE_KQUEUE	5
#define	KF_TYPE_CRYPTO	6
#define	KF_TYPE_MQUEUE	7
#define	KF_TYPE_SHM	8
#define	KF_TYPE_SEM	9
#define	KF_TYPE_PTS	10
#define	KF_TYPE_PROCDESC	11
#define	KF_TYPE_DEV	12
#define	KF_TYPE_UNKNOWN	255

#define	KF_VTYPE_VNON	0
#define	KF_VTYPE_VREG	1
#define	KF_VTYPE_VDIR	2
#define	KF_VTYPE_VBLK	3
#define	KF_VTYPE_VCHR	4
#define	KF_VTYPE_VLNK	5
#define	KF_VTYPE_VSOCK	6
#define	KF_VTYPE_VFIFO	7
#define	KF_VTYPE_VBAD	8
#define	KF_VTYPE_UNKNOWN	255

#define	KF_FD_TYPE_CWD	-1	/* Current working directory */
#define	KF_FD_TYPE_ROOT	-2	/* Root directory */
#define	KF_FD_TYPE_JAIL	-3	/* Jail directory */
#define	KF_FD_TYPE_TRACE	-4	/* Ktrace vnode */
#define	KF_FD_TYPE_TEXT	-5	/* Text vnode */
#define	KF_FD_TYPE_CTTY	-6	/* Controlling terminal */

#define	KF_FLAG_READ		0x00000001
#define	KF_FLAG_WRITE		0x00000002
#define	KF_FLAG_APPEND		0x00000004
#define	KF_FLAG_ASYNC		0x00000008
#define	KF_FLAG_FSYNC		0x00000010
#define	KF_FLAG_NONBLOCK	0x00000020
#define	KF_FLAG_DIRECT		0x00000040
#define	KF_FLAG_HASLOCK		0x00000080
#define	KF_FLAG_SHLOCK		0x00000100
#define	KF_FLAG_EXLOCK		0x00000200
#define	KF_FLAG_NOFOLLOW	0x00000400
#define	KF_FLAG_CREAT		0x00000800
#define	KF_FLAG_TRUNC		0x00001000
#define	KF_FLAG_EXCL		0x00002000
#define	KF_FLAG_EXEC		0x00004000

/*
 * Old format.  Has variable hidden padding due to alignment.
 * This is a compatibility hack for pre-build 7.1 packages.
 */
#if defined(__amd64__)
#define	KINFO_OFILE_SIZE	1328
#endif
#if defined(__i386__)
#define	KINFO_OFILE_SIZE	1324
#endif

struct kinfo_ofile {
	int	kf_structsize;			/* Size of kinfo_file. */
	int	kf_type;			/* Descriptor type. */
	int	kf_fd;				/* Array index. */
	int	kf_ref_count;			/* Reference count. */
	int	kf_flags;			/* Flags. */
	/* XXX Hidden alignment padding here on amd64 */
	off_t	kf_offset;			/* Seek location. */
	int	kf_vnode_type;			/* Vnode type. */
	int	kf_sock_domain;			/* Socket domain. */
	int	kf_sock_type;			/* Socket type. */
	int	kf_sock_protocol;		/* Socket protocol. */
	char	kf_path[PATH_MAX];	/* Path to file, if any. */
	struct sockaddr_storage kf_sa_local;	/* Socket address. */
	struct sockaddr_storage	kf_sa_peer;	/* Peer address. */
};

#if defined(__amd64__) || defined(__i386__)
/*
 * This size should never be changed. If you really need to, you must provide
 * backward ABI compatibility by allocating a new sysctl MIB that will return
 * the new structure. The current structure has to be returned by the current
 * sysctl MIB. See how it is done for the kinfo_ofile structure.
 */
#define	KINFO_FILE_SIZE	1392
#endif

struct kinfo_file {
	int		kf_structsize;		/* Variable size of record. */
	int		kf_type;		/* Descriptor type. */
	int		kf_fd;			/* Array index. */
	int		kf_ref_count;		/* Reference count. */
	int		kf_flags;		/* Flags. */
	int		kf_pad0;		/* Round to 64 bit alignment. */
	int64_t		kf_offset;		/* Seek location. */
	union {
		struct {
			/* API compatiblity with FreeBSD < 12. */
			int		kf_vnode_type;
			int		kf_sock_domain;
			int		kf_sock_type;
			int		kf_sock_protocol;
			struct sockaddr_storage kf_sa_local;
			struct sockaddr_storage	kf_sa_peer;
		};
		union {
			struct {
				/* Sendq size */
				uint32_t	kf_sock_sendq;
				/* Socket domain. */
				int		kf_sock_domain0;
				/* Socket type. */
				int		kf_sock_type0;
				/* Socket protocol. */
				int		kf_sock_protocol0;
				/* Socket address. */
				struct sockaddr_storage kf_sa_local;
				/* Peer address. */
				struct sockaddr_storage	kf_sa_peer;
				/* Address of so_pcb. */
				uint64_t	kf_sock_pcb;
				/* Address of inp_ppcb. */
				uint64_t	kf_sock_inpcb;
				/* Address of unp_conn. */
				uint64_t	kf_sock_unpconn;
				/* Send buffer state. */
				uint16_t	kf_sock_snd_sb_state;
				/* Receive buffer state. */
				uint16_t	kf_sock_rcv_sb_state;
				/* Recvq size. */
				uint32_t	kf_sock_recvq;
			} kf_sock;
			struct {
				/* Vnode type. */
				int		kf_file_type;
				/* Space for future use */
				int		kf_spareint[3];
				uint64_t	kf_spareint64[30];
				/* Vnode filesystem id. */
				uint64_t	kf_file_fsid;
				/* File device. */
				uint64_t	kf_file_rdev;
				/* Global file id. */
				uint64_t	kf_file_fileid;
				/* File size. */
				uint64_t	kf_file_size;
				/* Vnode filesystem id, FreeBSD 11 compat. */
				uint32_t	kf_file_fsid_freebsd11;
				/* File device, FreeBSD 11 compat. */
				uint32_t	kf_file_rdev_freebsd11;
				/* File mode. */
				uint16_t	kf_file_mode;
				/* Round to 64 bit alignment. */
				uint16_t	kf_file_pad0;
				uint32_t	kf_file_pad1;
			} kf_file;
			struct {
				uint32_t	kf_spareint[4];
				uint64_t	kf_spareint64[32];
				uint32_t	kf_sem_value;
				uint16_t	kf_sem_mode;
			} kf_sem;
			struct {
				uint32_t	kf_spareint[4];
				uint64_t	kf_spareint64[32];
				uint64_t	kf_pipe_addr;
				uint64_t	kf_pipe_peer;
				uint32_t	kf_pipe_buffer_cnt;
				/* Round to 64 bit alignment. */
				uint32_t	kf_pipe_pad0[3];
			} kf_pipe;
			struct {
				uint32_t	kf_spareint[4];
				uint64_t	kf_spareint64[32];
				uint32_t	kf_pts_dev_freebsd11;
				uint32_t	kf_pts_pad0;
				uint64_t	kf_pts_dev;
				/* Round to 64 bit alignment. */
				uint32_t	kf_pts_pad1[4];
			} kf_pts;
			struct {
				uint32_t	kf_spareint[4];
				uint64_t	kf_spareint64[32];
				pid_t		kf_pid;
			} kf_proc;
		} kf_un;
	};
	uint16_t	kf_status;		/* Status flags. */
	uint16_t	kf_pad1;		/* Round to 32 bit alignment. */
	int		_kf_ispare0;		/* Space for more stuff. */
	cap_rights_t	kf_cap_rights;		/* Capability rights. */
	uint64_t	_kf_cap_spare;		/* Space for future cap_rights_t. */
	/* Truncated before copyout in sysctl */
	char		kf_path[PATH_MAX];	/* Path to file, if any. */
};

/*
 * The KERN_PROC_VMMAP sysctl allows a process to dump the VM layout of
 * another process as a series of entries.
 */
#define	KVME_TYPE_NONE		0
#define	KVME_TYPE_DEFAULT	1
#define	KVME_TYPE_VNODE		2
#define	KVME_TYPE_SWAP		3
#define	KVME_TYPE_DEVICE	4
#define	KVME_TYPE_PHYS		5
#define	KVME_TYPE_DEAD		6
#define	KVME_TYPE_SG		7
#define	KVME_TYPE_MGTDEVICE	8
#define	KVME_TYPE_UNKNOWN	255

#define	KVME_PROT_READ		0x00000001
#define	KVME_PROT_WRITE		0x00000002
#define	KVME_PROT_EXEC		0x00000004

#define	KVME_FLAG_COW		0x00000001
#define	KVME_FLAG_NEEDS_COPY	0x00000002
#define	KVME_FLAG_NOCOREDUMP	0x00000004
#define	KVME_FLAG_SUPER		0x00000008
#define	KVME_FLAG_GROWS_UP	0x00000010
#define	KVME_FLAG_GROWS_DOWN	0x00000020
#define	KVME_FLAG_USER_WIRED	0x00000040

#if defined(__amd64__)
#define	KINFO_OVMENTRY_SIZE	1168
#endif
#if defined(__i386__)
#define	KINFO_OVMENTRY_SIZE	1128
#endif

struct kinfo_ovmentry {
	int	 kve_structsize;		/* Size of kinfo_vmmapentry. */
	int	 kve_type;			/* Type of map entry. */
	void	*kve_start;			/* Starting address. */
	void	*kve_end;			/* Finishing address. */
	int	 kve_flags;			/* Flags on map entry. */
	int	 kve_resident;			/* Number of resident pages. */
	int	 kve_private_resident;		/* Number of private pages. */
	int	 kve_protection;		/* Protection bitmask. */
	int	 kve_ref_count;			/* VM obj ref count. */
	int	 kve_shadow_count;		/* VM obj shadow count. */
	char	 kve_path[PATH_MAX];		/* Path to VM obj, if any. */
	void	*_kve_pspare[8];		/* Space for more stuff. */
	off_t	 kve_offset;			/* Mapping offset in object */
	uint64_t kve_fileid;			/* inode number if vnode */
	uint32_t kve_fsid;			/* dev_t of vnode location */
	int	 _kve_ispare[3];		/* Space for more stuff. */
};

#if defined(__amd64__) || defined(__i386__)
#define	KINFO_VMENTRY_SIZE	1160
#endif

struct kinfo_vmentry {
	int	 kve_structsize;		/* Variable size of record. */
	int	 kve_type;			/* Type of map entry. */
	uint64_t kve_start;			/* Starting address. */
	uint64_t kve_end;			/* Finishing address. */
	uint64_t kve_offset;			/* Mapping offset in object */
	uint64_t kve_vn_fileid;			/* inode number if vnode */
	uint32_t kve_vn_fsid_freebsd11;		/* dev_t of vnode location */
	int	 kve_flags;			/* Flags on map entry. */
	int	 kve_resident;			/* Number of resident pages. */
	int	 kve_private_resident;		/* Number of private pages. */
	int	 kve_protection;		/* Protection bitmask. */
	int	 kve_ref_count;			/* VM obj ref count. */
	int	 kve_shadow_count;		/* VM obj shadow count. */
	int	 kve_vn_type;			/* Vnode type. */
	uint64_t kve_vn_size;			/* File size. */
	uint32_t kve_vn_rdev_freebsd11;		/* Device id if device. */
	uint16_t kve_vn_mode;			/* File mode. */
	uint16_t kve_status;			/* Status flags. */
	uint64_t kve_vn_fsid;			/* dev_t of vnode location */
	uint64_t kve_vn_rdev;			/* Device id if device. */
	int	 _kve_ispare[8];		/* Space for more stuff. */
	/* Truncated before copyout in sysctl */
	char	 kve_path[PATH_MAX];		/* Path to VM obj, if any. */
};

/*
 * The "vm.objects" sysctl provides a list of all VM objects in the system
 * via an array of these entries.
 */
struct kinfo_vmobject {
	int	kvo_structsize;			/* Variable size of record. */
	int	kvo_type;			/* Object type: KVME_TYPE_*. */
	uint64_t kvo_size;			/* Object size in pages. */
	uint64_t kvo_vn_fileid;			/* inode number if vnode. */
	uint32_t kvo_vn_fsid_freebsd11;		/* dev_t of vnode location. */
	int	kvo_ref_count;			/* Reference count. */
	int	kvo_shadow_count;		/* Shadow count. */
	int	kvo_memattr;			/* Memory attribute. */
	uint64_t kvo_resident;			/* Number of resident pages. */
	uint64_t kvo_active;			/* Number of active pages. */
	uint64_t kvo_inactive;			/* Number of inactive pages. */
	uint64_t kvo_vn_fsid;
	uint64_t _kvo_qspare[7];
	uint32_t _kvo_ispare[8];
	char	kvo_path[PATH_MAX];		/* Pathname, if any. */
};

/*
 * The KERN_PROC_KSTACK sysctl allows a process to dump the kernel stacks of
 * another process as a series of entries.  Each stack is represented by a
 * series of symbol names and offsets as generated by stack_sbuf_print(9).
 */
#define	KKST_MAXLEN	1024

#define	KKST_STATE_STACKOK	0		/* Stack is valid. */
#define	KKST_STATE_SWAPPED	1		/* Stack swapped out. */
#define	KKST_STATE_RUNNING	2		/* Stack ephemeral. */

#if defined(__amd64__) || defined(__i386__)
#define	KINFO_KSTACK_SIZE	1096
#endif

struct kinfo_kstack {
	lwpid_t	 kkst_tid;			/* ID of thread. */
	int	 kkst_state;			/* Validity of stack. */
	char	 kkst_trace[KKST_MAXLEN];	/* String representing stack. */
	int	 _kkst_ispare[16];		/* Space for more stuff. */
};

struct kinfo_sigtramp {
	void	*ksigtramp_start;
	void	*ksigtramp_end;
	void	*ksigtramp_spare[4];
};

#ifdef _KERNEL
/* Flags for kern_proc_out function. */
#define KERN_PROC_NOTHREADS	0x1
#define KERN_PROC_MASK32	0x2

/* Flags for kern_proc_filedesc_out. */
#define	KERN_FILEDESC_PACK_KINFO	0x00000001U

/* Flags for kern_proc_vmmap_out. */
#define	KERN_VMMAP_PACK_KINFO		0x00000001U
struct sbuf;

/*
 * The kern_proc out functions are helper functions to dump process
 * miscellaneous kinfo structures to sbuf.  The main consumers are KERN_PROC
 * sysctls but they may also be used by other kernel subsystems.
 *
 * The functions manipulate the process locking state and expect the process
 * to be locked on enter.  On return the process is unlocked.
 */

int	kern_proc_filedesc_out(struct proc *p, struct sbuf *sb, ssize_t maxlen,
	int flags);
int	kern_proc_cwd_out(struct proc *p, struct sbuf *sb, ssize_t maxlen);
int	kern_proc_out(struct proc *p, struct sbuf *sb, int flags);
int	kern_proc_vmmap_out(struct proc *p, struct sbuf *sb, ssize_t maxlen,
	int flags);

int	vntype_to_kinfo(int vtype);
#endif /* !_KERNEL */

#endif
