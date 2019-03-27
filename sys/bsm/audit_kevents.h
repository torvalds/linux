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

#ifndef _BSM_AUDIT_KEVENTS_H_
#define	_BSM_AUDIT_KEVENTS_H_

/*
 * The reserved event numbers for kernel events are 1...2047 and 43001..44999.
 */
#define	AUE_IS_A_KEVENT(e)	(((e) > 0 && (e) < 2048) ||	\
				 ((e) > 43000 && (e) < 45000))

/*
 * Values marked as AUE_NULL are not required to be audited as per CAPP.
 *
 * Some conflicts exist in the assignment of name to event number mappings
 * between BSM implementations.  In general, we prefer the OpenSolaris
 * definition as we consider Solaris BSM to be authoritative.  _DARWIN_ has
 * been inserted for the Darwin variants.  If necessary, other tags will be
 * added in the future.
 */
#define	AUE_NULL		0
#define	AUE_EXIT		1
#define	AUE_FORK		2
#define	AUE_FORKALL		AUE_FORK	/* Solaris-specific. */
#define	AUE_OPEN		3
#define	AUE_CREAT		4
#define	AUE_LINK		5
#define	AUE_UNLINK		6
#define	AUE_DELETE		AUE_UNLINK	/* Darwin-specific. */
#define	AUE_EXEC		7
#define	AUE_CHDIR		8
#define	AUE_MKNOD		9
#define	AUE_CHMOD		10
#define	AUE_CHOWN		11
#define	AUE_UMOUNT		12
#define	AUE_JUNK		13	/* Solaris-specific. */
#define	AUE_ACCESS		14
#define	AUE_KILL		15
#define	AUE_STAT		16
#define	AUE_LSTAT		17
#define	AUE_ACCT		18
#define	AUE_MCTL		19	/* Solaris-specific. */
#define	AUE_REBOOT		20	/* XXX: Darwin conflict. */
#define	AUE_SYMLINK		21
#define	AUE_READLINK		22
#define	AUE_EXECVE		23
#define	AUE_CHROOT		24
#define	AUE_VFORK		25
#define	AUE_SETGROUPS		26
#define	AUE_SETPGRP		27
#define	AUE_SWAPON		28
#define	AUE_SETHOSTNAME		29	/* XXX: Darwin conflict. */
#define	AUE_FCNTL		30
#define	AUE_SETPRIORITY		31	/* XXX: Darwin conflict. */
#define	AUE_CONNECT		32
#define	AUE_ACCEPT		33
#define	AUE_BIND		34
#define	AUE_SETSOCKOPT		35
#define	AUE_VTRACE		36	/* Solaris-specific. */
#define	AUE_SETTIMEOFDAY	37	/* XXX: Darwin conflict. */
#define	AUE_FCHOWN		38
#define	AUE_FCHMOD		39
#define	AUE_SETREUID		40
#define	AUE_SETREGID		41
#define	AUE_RENAME		42
#define	AUE_TRUNCATE		43	/* XXX: Darwin conflict. */
#define	AUE_FTRUNCATE		44	/* XXX: Darwin conflict. */
#define	AUE_FLOCK		45	/* XXX: Darwin conflict. */
#define	AUE_SHUTDOWN		46
#define	AUE_MKDIR		47
#define	AUE_RMDIR		48
#define	AUE_UTIMES		49
#define	AUE_ADJTIME		50
#define	AUE_SETRLIMIT		51
#define	AUE_KILLPG		52
#define	AUE_NFS_SVC		53	/* XXX: Darwin conflict. */
#define	AUE_STATFS		54
#define	AUE_FSTATFS		55
#define	AUE_UNMOUNT		56	/* XXX: Darwin conflict. */
#define	AUE_ASYNC_DAEMON	57
#define	AUE_NFS_GETFH		58	/* XXX: Darwin conflict. */
#define	AUE_SETDOMAINNAME	59
#define	AUE_QUOTACTL		60	/* XXX: Darwin conflict. */
#define	AUE_EXPORTFS		61
#define	AUE_MOUNT		62
#define	AUE_SEMSYS		63
#define	AUE_MSGSYS		64
#define	AUE_SHMSYS		65
#define	AUE_BSMSYS		66	/* Solaris-specific. */
#define	AUE_RFSSYS		67	/* Solaris-specific. */
#define	AUE_FCHDIR		68
#define	AUE_FCHROOT		69
#define	AUE_VPIXSYS		70	/* Solaris-specific. */
#define	AUE_PATHCONF		71
#define	AUE_OPEN_R		72
#define	AUE_OPEN_RC		73
#define	AUE_OPEN_RT		74
#define	AUE_OPEN_RTC		75
#define	AUE_OPEN_W		76
#define	AUE_OPEN_WC		77
#define	AUE_OPEN_WT		78
#define	AUE_OPEN_WTC		79
#define	AUE_OPEN_RW		80
#define	AUE_OPEN_RWC		81
#define	AUE_OPEN_RWT		82
#define	AUE_OPEN_RWTC		83
#define	AUE_MSGCTL		84
#define	AUE_MSGCTL_RMID		85
#define	AUE_MSGCTL_SET		86
#define	AUE_MSGCTL_STAT		87
#define	AUE_MSGGET		88
#define	AUE_MSGRCV		89
#define	AUE_MSGSND		90
#define	AUE_SHMCTL		91
#define	AUE_SHMCTL_RMID		92
#define	AUE_SHMCTL_SET		93
#define	AUE_SHMCTL_STAT		94
#define	AUE_SHMGET		95
#define	AUE_SHMAT		96
#define	AUE_SHMDT		97
#define	AUE_SEMCTL		98
#define	AUE_SEMCTL_RMID		99
#define	AUE_SEMCTL_SET		100
#define	AUE_SEMCTL_STAT		101
#define	AUE_SEMCTL_GETNCNT	102
#define	AUE_SEMCTL_GETPID	103
#define	AUE_SEMCTL_GETVAL	104
#define	AUE_SEMCTL_GETALL	105
#define	AUE_SEMCTL_GETZCNT	106
#define	AUE_SEMCTL_SETVAL	107
#define	AUE_SEMCTL_SETALL	108
#define	AUE_SEMGET		109
#define	AUE_SEMOP		110
#define	AUE_CORE		111	/* Solaris-specific, currently. */
#define	AUE_CLOSE		112
#define	AUE_SYSTEMBOOT		113	/* Solaris-specific. */
#define	AUE_ASYNC_DAEMON_EXIT	114	/* Solaris-specific. */
#define	AUE_NFSSVC_EXIT		115	/* Solaris-specific. */
#define	AUE_WRITEL		128	/* Solaris-specific. */
#define	AUE_WRITEVL		129	/* Solaris-specific. */
#define	AUE_GETAUID		130
#define	AUE_SETAUID		131
#define	AUE_GETAUDIT		132
#define	AUE_SETAUDIT		133
#define	AUE_GETUSERAUDIT	134	/* Solaris-specific. */
#define	AUE_SETUSERAUDIT	135	/* Solaris-specific. */
#define	AUE_AUDITSVC		136	/* Solaris-specific. */
#define	AUE_AUDITUSER		137	/* Solaris-specific. */
#define	AUE_AUDITON		138
#define	AUE_AUDITON_GTERMID	139	/* Solaris-specific. */
#define	AUE_AUDITON_STERMID	140	/* Solaris-specific. */
#define	AUE_AUDITON_GPOLICY	141
#define	AUE_AUDITON_SPOLICY	142
#define	AUE_AUDITON_GQCTRL	145
#define	AUE_AUDITON_SQCTRL	146
#define	AUE_GETKERNSTATE	147	/* Solaris-specific. */
#define	AUE_SETKERNSTATE	148	/* Solaris-specific. */
#define	AUE_GETPORTAUDIT	149	/* Solaris-specific. */
#define	AUE_AUDITSTAT		150	/* Solaris-specific. */
#define	AUE_REVOKE		151
#define	AUE_MAC			152	/* Solaris-specific. */
#define	AUE_ENTERPROM		153	/* Solaris-specific. */
#define	AUE_EXITPROM		154	/* Solaris-specific. */
#define	AUE_IFLOAT		155	/* Solaris-specific. */
#define	AUE_PFLOAT		156	/* Solaris-specific. */
#define	AUE_UPRIV		157	/* Solaris-specific. */
#define	AUE_IOCTL		158
#define	AUE_SOCKET		183
#define	AUE_SENDTO		184
#define	AUE_PIPE		185
#define	AUE_SOCKETPAIR		186	/* XXX: Darwin conflict. */
#define	AUE_SEND		187
#define	AUE_SENDMSG		188
#define	AUE_RECV		189
#define	AUE_RECVMSG		190
#define	AUE_RECVFROM		191
#define	AUE_READ		192
#define	AUE_GETDENTS		193
#define	AUE_LSEEK		194
#define	AUE_WRITE		195
#define	AUE_WRITEV		196
#define	AUE_NFS			197	/* Solaris-specific. */
#define	AUE_READV		198
#define	AUE_OSTAT		199	/* Solaris-specific. */
#define	AUE_SETUID		200	/* XXXRW: Solaris old setuid? */
#define	AUE_STIME		201	/* XXXRW: Solaris old stime? */
#define	AUE_UTIME		202	/* XXXRW: Solaris old utime? */
#define	AUE_NICE		203	/* XXXRW: Solaris old nice? */
#define	AUE_OSETPGRP		204	/* Solaris-specific. */
#define	AUE_SETGID		205
#define	AUE_READL		206	/* Solaris-specific. */
#define	AUE_READVL		207	/* Solaris-specific. */
#define	AUE_FSTAT		208
#define	AUE_DUP2		209
#define	AUE_MMAP		210
#define	AUE_AUDIT		211
#define	AUE_PRIOCNTLSYS		212	/* Solaris-specific. */
#define	AUE_MUNMAP		213
#define	AUE_SETEGID		214
#define	AUE_SETEUID		215
#define	AUE_PUTMSG		216	/* Solaris-specific. */
#define	AUE_GETMSG		217	/* Solaris-specific. */
#define	AUE_PUTPMSG		218	/* Solaris-specific. */
#define	AUE_GETPMSG		219	/* Solaris-specific. */
#define	AUE_AUDITSYS		220	/* Solaris-specific. */
#define	AUE_AUDITON_GETKMASK	221
#define	AUE_AUDITON_SETKMASK	222
#define	AUE_AUDITON_GETCWD	223
#define	AUE_AUDITON_GETCAR	224
#define	AUE_AUDITON_GETSTAT	225
#define	AUE_AUDITON_SETSTAT	226
#define	AUE_AUDITON_SETUMASK	227
#define	AUE_AUDITON_SETSMASK	228
#define	AUE_AUDITON_GETCOND	229
#define	AUE_AUDITON_SETCOND	230
#define	AUE_AUDITON_GETCLASS	231
#define	AUE_AUDITON_SETCLASS	232
#define	AUE_FUSERS		233	/* Solaris-specific; also UTSSYS? */
#define	AUE_STATVFS		234
#define	AUE_XSTAT		235	/* Solaris-specific. */
#define	AUE_LXSTAT		236	/* Solaris-specific. */
#define	AUE_LCHOWN		237
#define	AUE_MEMCNTL		238	/* Solaris-specific. */
#define	AUE_SYSINFO		239	/* Solaris-specific. */
#define	AUE_XMKNOD		240	/* Solaris-specific. */
#define	AUE_FORK1		241
#define	AUE_MODCTL		242	/* Solaris-specific. */
#define	AUE_MODLOAD		243
#define	AUE_MODUNLOAD		244
#define	AUE_MODCONFIG		245	/* Solaris-specific. */
#define	AUE_MODADDMAJ		246	/* Solaris-specific. */
#define	AUE_SOCKACCEPT		247	/* Solaris-specific. */
#define	AUE_SOCKCONNECT		248	/* Solaris-specific. */
#define	AUE_SOCKSEND		249	/* Solaris-specific. */
#define	AUE_SOCKRECEIVE		250	/* Solaris-specific. */
#define	AUE_ACLSET		251
#define	AUE_FACLSET		252
#define	AUE_DOORFS		253	/* Solaris-specific. */
#define	AUE_DOORFS_DOOR_CALL	254	/* Solaris-specific. */
#define	AUE_DOORFS_DOOR_RETURN	255	/* Solaris-specific. */
#define	AUE_DOORFS_DOOR_CREATE	256	/* Solaris-specific. */
#define	AUE_DOORFS_DOOR_REVOKE	257	/* Solaris-specific. */
#define	AUE_DOORFS_DOOR_INFO	258	/* Solaris-specific. */
#define	AUE_DOORFS_DOOR_CRED	259	/* Solaris-specific. */
#define	AUE_DOORFS_DOOR_BIND	260	/* Solaris-specific. */
#define	AUE_DOORFS_DOOR_UNBIND	261	/* Solaris-specific. */
#define	AUE_P_ONLINE		262	/* Solaris-specific. */
#define	AUE_PROCESSOR_BIND	263	/* Solaris-specific. */
#define	AUE_INST_SYNC		264	/* Solaris-specific. */
#define	AUE_SOCKCONFIG		265	/* Solaris-specific. */
#define	AUE_SETAUDIT_ADDR	266
#define	AUE_GETAUDIT_ADDR	267
#define	AUE_UMOUNT2		268	/* Solaris-specific. */
#define	AUE_FSAT		269	/* Solaris-specific. */
#define	AUE_OPENAT_R		270
#define	AUE_OPENAT_RC		271
#define	AUE_OPENAT_RT		272
#define	AUE_OPENAT_RTC		273
#define	AUE_OPENAT_W		274
#define	AUE_OPENAT_WC		275
#define	AUE_OPENAT_WT		276
#define	AUE_OPENAT_WTC		277
#define	AUE_OPENAT_RW		278
#define	AUE_OPENAT_RWC		279
#define	AUE_OPENAT_RWT		280
#define	AUE_OPENAT_RWTC		281
#define	AUE_RENAMEAT		282
#define	AUE_FSTATAT		283
#define	AUE_FCHOWNAT		284
#define	AUE_FUTIMESAT		285
#define	AUE_UNLINKAT		286
#define	AUE_CLOCK_SETTIME	287
#define	AUE_NTP_ADJTIME		288
#define	AUE_SETPPRIV		289	/* Solaris-specific. */
#define	AUE_MODDEVPLCY		290	/* Solaris-specific. */
#define	AUE_MODADDPRIV		291	/* Solaris-specific. */
#define	AUE_CRYPTOADM		292	/* Solaris-specific. */
#define	AUE_CONFIGKSSL		293	/* Solaris-specific. */
#define	AUE_BRANDSYS		294	/* Solaris-specific. */
#define	AUE_PF_POLICY_ADDRULE	295	/* Solaris-specific. */
#define	AUE_PF_POLICY_DELRULE	296	/* Solaris-specific. */
#define	AUE_PF_POLICY_CLONE	297	/* Solaris-specific. */
#define	AUE_PF_POLICY_FLIP	298	/* Solaris-specific. */
#define	AUE_PF_POLICY_FLUSH	299	/* Solaris-specific. */
#define	AUE_PF_POLICY_ALGS	300	/* Solaris-specific. */
#define	AUE_PORTFS		301	/* Solaris-specific. */

/*
 * Events added for Apple Darwin that potentially collide with future Solaris
 * BSM events.  These are assigned AUE_DARWIN prefixes, and are deprecated in
 * new trails.  Systems generating these events should switch to the new
 * identifiers that avoid colliding with the Solaris identifier space.
 */
#define	AUE_DARWIN_GETFSSTAT	301
#define	AUE_DARWIN_PTRACE	302
#define	AUE_DARWIN_CHFLAGS	303
#define	AUE_DARWIN_FCHFLAGS	304
#define	AUE_DARWIN_PROFILE	305
#define	AUE_DARWIN_KTRACE	306
#define	AUE_DARWIN_SETLOGIN	307
#define	AUE_DARWIN_REBOOT	308
#define	AUE_DARWIN_REVOKE	309
#define	AUE_DARWIN_UMASK	310
#define	AUE_DARWIN_MPROTECT	311
#define	AUE_DARWIN_SETPRIORITY	312
#define	AUE_DARWIN_SETTIMEOFDAY	313
#define	AUE_DARWIN_FLOCK	314
#define	AUE_DARWIN_MKFIFO	315
#define	AUE_DARWIN_POLL		316
#define	AUE_DARWIN_SOCKETPAIR	317
#define	AUE_DARWIN_FUTIMES	318
#define	AUE_DARWIN_SETSID	319
#define	AUE_DARWIN_SETPRIVEXEC	320	/* Darwin-specific. */
#define	AUE_DARWIN_NFSSVC	321
#define	AUE_DARWIN_GETFH	322
#define	AUE_DARWIN_QUOTACTL	323
#define	AUE_DARWIN_ADDPROFILE	324	/* Darwin-specific. */
#define	AUE_DARWIN_KDEBUGTRACE	325	/* Darwin-specific. */
#define	AUE_DARWIN_KDBUGTRACE	AUE_KDEBUGTRACE
#define	AUE_DARWIN_FSTAT	326
#define	AUE_DARWIN_FPATHCONF	327
#define	AUE_DARWIN_GETDIRENTRIES	328
#define	AUE_DARWIN_TRUNCATE	329
#define	AUE_DARWIN_FTRUNCATE	330
#define	AUE_DARWIN_SYSCTL	331
#define	AUE_DARWIN_MLOCK	332
#define	AUE_DARWIN_MUNLOCK	333
#define	AUE_DARWIN_UNDELETE	334
#define	AUE_DARWIN_GETATTRLIST	335	/* Darwin-specific. */
#define	AUE_DARWIN_SETATTRLIST	336	/* Darwin-specific. */
#define	AUE_DARWIN_GETDIRENTRIESATTR	337	/* Darwin-specific. */
#define	AUE_DARWIN_EXCHANGEDATA	338	/* Darwin-specific. */
#define	AUE_DARWIN_SEARCHFS	339	/* Darwin-specific. */
#define	AUE_DARWIN_MINHERIT	340
#define	AUE_DARWIN_SEMCONFIG	341
#define	AUE_DARWIN_SEMOPEN	342
#define	AUE_DARWIN_SEMCLOSE	343
#define	AUE_DARWIN_SEMUNLINK	344
#define	AUE_DARWIN_SHMOPEN	345
#define	AUE_DARWIN_SHMUNLINK	346
#define	AUE_DARWIN_LOADSHFILE	347	/* Darwin-specific. */
#define	AUE_DARWIN_RESETSHFILE	348	/* Darwin-specific. */
#define	AUE_DARWIN_NEWSYSTEMSHREG	349	/* Darwin-specific. */
#define	AUE_DARWIN_PTHREADKILL	350	/* Darwin-specific. */
#define	AUE_DARWIN_PTHREADSIGMASK	351	/* Darwin-specific. */
#define	AUE_DARWIN_AUDITCTL	352
#define	AUE_DARWIN_RFORK	353
#define	AUE_DARWIN_LCHMOD	354
#define	AUE_DARWIN_SWAPOFF	355
#define	AUE_DARWIN_INITPROCESS	356	/* Darwin-specific. */
#define	AUE_DARWIN_MAPFD	357	/* Darwin-specific. */
#define	AUE_DARWIN_TASKFORPID	358	/* Darwin-specific. */
#define	AUE_DARWIN_PIDFORTASK	359	/* Darwin-specific. */
#define	AUE_DARWIN_SYSCTL_NONADMIN	360
#define	AUE_DARWIN_COPYFILE	361	/* Darwin-specific. */

/*
 * Audit event identifiers added as part of OpenBSM, generally corresponding
 * to events in FreeBSD, Darwin, and Linux that were not present in Solaris.
 * These often duplicate events added to the Solaris set by Darwin, but use
 * event identifiers in a higher range in order to avoid colliding with
 * future Solaris additions.
 *
 * If an event in this section is later added to Solaris, we prefer the
 * Solaris event identifier, and add _OPENBSM_ to the OpenBSM-specific
 * identifier so that old trails can still be processed, but new trails use
 * the Solaris identifier.
 */
#define	AUE_GETFSSTAT		43001
#define	AUE_PTRACE		43002
#define	AUE_CHFLAGS		43003
#define	AUE_FCHFLAGS		43004
#define	AUE_PROFILE		43005
#define	AUE_KTRACE		43006
#define	AUE_SETLOGIN		43007
#define	AUE_OPENBSM_REVOKE	43008	/* Solaris event now preferred. */
#define	AUE_UMASK		43009
#define	AUE_MPROTECT		43010
#define	AUE_MKFIFO		43011
#define	AUE_POLL		43012
#define	AUE_FUTIMES		43013
#define	AUE_SETSID		43014
#define	AUE_SETPRIVEXEC		43015	/* Darwin-specific. */
#define	AUE_ADDPROFILE		43016	/* Darwin-specific. */
#define	AUE_KDEBUGTRACE		43017	/* Darwin-specific. */
#define	AUE_KDBUGTRACE		AUE_KDEBUGTRACE
#define	AUE_OPENBSM_FSTAT	43018	/* Solaris event now preferred. */
#define	AUE_FPATHCONF		43019
#define	AUE_GETDIRENTRIES	43020
#define	AUE_SYSCTL		43021
#define	AUE_MLOCK		43022
#define	AUE_MUNLOCK		43023
#define	AUE_UNDELETE		43024
#define	AUE_GETATTRLIST		43025	/* Darwin-specific. */
#define	AUE_SETATTRLIST		43026	/* Darwin-specific. */
#define	AUE_GETDIRENTRIESATTR	43027	/* Darwin-specific. */
#define	AUE_EXCHANGEDATA	43028	/* Darwin-specific. */
#define	AUE_SEARCHFS		43029	/* Darwin-specific. */
#define	AUE_MINHERIT		43030
#define	AUE_SEMCONFIG		43031
#define	AUE_SEMOPEN		43032
#define	AUE_SEMCLOSE		43033
#define	AUE_SEMUNLINK		43034
#define	AUE_SHMOPEN		43035
#define	AUE_SHMUNLINK		43036
#define	AUE_LOADSHFILE		43037	/* Darwin-specific. */
#define	AUE_RESETSHFILE		43038	/* Darwin-specific. */
#define	AUE_NEWSYSTEMSHREG	43039	/* Darwin-specific. */
#define	AUE_PTHREADKILL		43040	/* Darwin-specific. */
#define	AUE_PTHREADSIGMASK	43041	/* Darwin-specific. */
#define	AUE_AUDITCTL		43042
#define	AUE_RFORK		43043
#define	AUE_LCHMOD		43044
#define	AUE_SWAPOFF		43045
#define	AUE_INITPROCESS		43046	/* Darwin-specific. */
#define	AUE_MAPFD		43047	/* Darwin-specific. */
#define	AUE_TASKFORPID		43048	/* Darwin-specific. */
#define	AUE_PIDFORTASK		43049	/* Darwin-specific. */
#define	AUE_SYSCTL_NONADMIN	43050
#define	AUE_COPYFILE		43051	/* Darwin-specific. */

/*
 * Events added to OpenBSM for FreeBSD and Linux; may also be used by Darwin
 * in the future.
 */
#define	AUE_LUTIMES		43052
#define	AUE_LCHFLAGS		43053	/* FreeBSD-specific. */
#define	AUE_SENDFILE		43054	/* BSD/Linux-specific. */
#define	AUE_USELIB		43055	/* Linux-specific. */
#define	AUE_GETRESUID		43056
#define	AUE_SETRESUID		43057
#define	AUE_GETRESGID		43058
#define	AUE_SETRESGID		43059
#define	AUE_WAIT4		43060	/* FreeBSD-specific. */
#define	AUE_LGETFH		43061	/* FreeBSD-specific. */
#define	AUE_FHSTATFS		43062	/* FreeBSD-specific. */
#define	AUE_FHOPEN		43063	/* FreeBSD-specific. */
#define	AUE_FHSTAT		43064	/* FreeBSD-specific. */
#define	AUE_JAIL		43065	/* FreeBSD-specific. */
#define	AUE_EACCESS		43066	/* FreeBSD-specific. */
#define	AUE_KQUEUE		43067	/* FreeBSD-specific. */
#define	AUE_KEVENT		43068	/* FreeBSD-specific. */
#define	AUE_FSYNC		43069
#define	AUE_NMOUNT		43070	/* FreeBSD-specific. */
#define	AUE_BDFLUSH		43071	/* Linux-specific. */
#define	AUE_SETFSUID		43072	/* Linux-specific. */
#define	AUE_SETFSGID		43073	/* Linux-specific. */
#define	AUE_PERSONALITY		43074	/* Linux-specific. */
#define	AUE_SCHED_GETSCHEDULER	43075	/* POSIX.1b. */
#define	AUE_SCHED_SETSCHEDULER	43076	/* POSIX.1b. */
#define	AUE_PRCTL		43077	/* Linux-specific. */
#define	AUE_GETCWD		43078	/* FreeBSD/Linux-specific. */
#define	AUE_CAPGET		43079	/* Linux-specific. */
#define	AUE_CAPSET		43080	/* Linux-specific. */
#define	AUE_PIVOT_ROOT		43081	/* Linux-specific. */
#define	AUE_RTPRIO		43082	/* FreeBSD-specific. */
#define	AUE_SCHED_GETPARAM	43083	/* POSIX.1b. */
#define	AUE_SCHED_SETPARAM	43084	/* POSIX.1b. */
#define	AUE_SCHED_GET_PRIORITY_MAX	43085	/* POSIX.1b. */
#define	AUE_SCHED_GET_PRIORITY_MIN	43086	/* POSIX.1b. */
#define	AUE_SCHED_RR_GET_INTERVAL	43087	/* POSIX.1b. */
#define	AUE_ACL_GET_FILE	43088	/* FreeBSD. */
#define	AUE_ACL_SET_FILE	43089	/* FreeBSD. */
#define	AUE_ACL_GET_FD		43090	/* FreeBSD. */
#define	AUE_ACL_SET_FD		43091	/* FreeBSD. */
#define	AUE_ACL_DELETE_FILE	43092	/* FreeBSD. */
#define	AUE_ACL_DELETE_FD	43093	/* FreeBSD. */
#define	AUE_ACL_CHECK_FILE	43094	/* FreeBSD. */
#define	AUE_ACL_CHECK_FD	43095	/* FreeBSD. */
#define	AUE_ACL_GET_LINK	43096	/* FreeBSD. */
#define	AUE_ACL_SET_LINK	43097	/* FreeBSD. */
#define	AUE_ACL_DELETE_LINK	43098	/* FreeBSD. */
#define	AUE_ACL_CHECK_LINK	43099	/* FreeBSD. */
#define	AUE_SYSARCH		43100	/* FreeBSD. */
#define	AUE_EXTATTRCTL		43101	/* FreeBSD. */
#define	AUE_EXTATTR_GET_FILE	43102	/* FreeBSD. */
#define	AUE_EXTATTR_SET_FILE	43103	/* FreeBSD. */
#define	AUE_EXTATTR_LIST_FILE	43104	/* FreeBSD. */
#define	AUE_EXTATTR_DELETE_FILE	43105	/* FreeBSD. */
#define	AUE_EXTATTR_GET_FD	43106	/* FreeBSD. */
#define	AUE_EXTATTR_SET_FD	43107	/* FreeBSD. */
#define	AUE_EXTATTR_LIST_FD	43108	/* FreeBSD. */
#define	AUE_EXTATTR_DELETE_FD	43109	/* FreeBSD. */
#define	AUE_EXTATTR_GET_LINK	43110	/* FreeBSD. */
#define	AUE_EXTATTR_SET_LINK	43111	/* FreeBSD. */
#define	AUE_EXTATTR_LIST_LINK	43112	/* FreeBSD. */
#define	AUE_EXTATTR_DELETE_LINK	43113	/* FreeBSD. */
#define	AUE_KENV		43114	/* FreeBSD. */
#define	AUE_JAIL_ATTACH		43115	/* FreeBSD. */
#define	AUE_SYSCTL_WRITE	43116	/* FreeBSD. */
#define	AUE_IOPERM		43117	/* Linux. */
#define	AUE_READDIR		43118	/* Linux. */
#define	AUE_IOPL		43119	/* Linux. */
#define	AUE_VM86		43120	/* Linux. */
#define	AUE_MAC_GET_PROC	43121	/* FreeBSD/Darwin. */
#define	AUE_MAC_SET_PROC	43122	/* FreeBSD/Darwin. */
#define	AUE_MAC_GET_FD		43123	/* FreeBSD/Darwin. */
#define	AUE_MAC_GET_FILE	43124	/* FreeBSD/Darwin. */
#define	AUE_MAC_SET_FD		43125	/* FreeBSD/Darwin. */
#define	AUE_MAC_SET_FILE	43126	/* FreeBSD/Darwin. */
#define	AUE_MAC_SYSCALL		43127	/* FreeBSD. */
#define	AUE_MAC_GET_PID		43128	/* FreeBSD/Darwin. */
#define	AUE_MAC_GET_LINK	43129	/* FreeBSD/Darwin. */
#define	AUE_MAC_SET_LINK	43130	/* FreeBSD/Darwin. */
#define	AUE_MAC_EXECVE		43131	/* FreeBSD/Darwin. */
#define	AUE_GETPATH_FROMFD	43132	/* FreeBSD. */
#define	AUE_GETPATH_FROMADDR	43133	/* FreeBSD. */
#define	AUE_MQ_OPEN		43134	/* FreeBSD. */
#define	AUE_MQ_SETATTR		43135	/* FreeBSD. */
#define	AUE_MQ_TIMEDRECEIVE	43136	/* FreeBSD. */
#define	AUE_MQ_TIMEDSEND	43137	/* FreeBSD. */
#define	AUE_MQ_NOTIFY		43138	/* FreeBSD. */
#define	AUE_MQ_UNLINK		43139	/* FreeBSD. */
#define	AUE_LISTEN		43140	/* FreeBSD/Darwin/Linux. */
#define	AUE_MLOCKALL		43141	/* FreeBSD. */
#define	AUE_MUNLOCKALL		43142	/* FreeBSD. */
#define	AUE_CLOSEFROM		43143	/* FreeBSD. */
#define	AUE_FEXECVE		43144	/* FreeBSD. */
#define	AUE_FACCESSAT		43145	/* FreeBSD. */
#define	AUE_FCHMODAT		43146	/* FreeBSD. */
#define	AUE_LINKAT		43147	/* FreeBSD. */
#define	AUE_MKDIRAT		43148	/* FreeBSD. */
#define	AUE_MKFIFOAT		43149	/* FreeBSD. */
#define	AUE_MKNODAT		43150	/* FreeBSD. */
#define	AUE_READLINKAT		43151	/* FreeBSD. */
#define	AUE_SYMLINKAT		43152	/* FreeBSD. */
#define	AUE_MAC_GETFSSTAT	43153	/* Darwin. */
#define	AUE_MAC_GET_MOUNT	43154	/* Darwin. */
#define	AUE_MAC_GET_LCID	43155	/* Darwin. */
#define	AUE_MAC_GET_LCTX	43156	/* Darwin. */
#define	AUE_MAC_SET_LCTX	43157	/* Darwin. */
#define	AUE_MAC_MOUNT		43158	/* Darwin. */
#define	AUE_GETLCID		43159	/* Darwin. */
#define	AUE_SETLCID		43160	/* Darwin. */
#define	AUE_TASKNAMEFORPID	43161	/* Darwin. */
#define	AUE_ACCESS_EXTENDED	43162	/* Darwin. */
#define	AUE_CHMOD_EXTENDED	43163	/* Darwin. */
#define	AUE_FCHMOD_EXTENDED	43164	/* Darwin. */
#define	AUE_FSTAT_EXTENDED	43165	/* Darwin. */
#define	AUE_LSTAT_EXTENDED	43166	/* Darwin. */
#define	AUE_MKDIR_EXTENDED	43167	/* Darwin. */
#define	AUE_MKFIFO_EXTENDED	43168	/* Darwin. */
#define	AUE_OPEN_EXTENDED	43169	/* Darwin. */
#define	AUE_OPEN_EXTENDED_R	43170	/* Darwin. */
#define	AUE_OPEN_EXTENDED_RC	43171	/* Darwin. */
#define	AUE_OPEN_EXTENDED_RT	43172	/* Darwin. */
#define	AUE_OPEN_EXTENDED_RTC	43173	/* Darwin. */
#define	AUE_OPEN_EXTENDED_W	43174	/* Darwin. */
#define	AUE_OPEN_EXTENDED_WC	43175	/* Darwin. */
#define	AUE_OPEN_EXTENDED_WT	43176	/* Darwin. */
#define	AUE_OPEN_EXTENDED_WTC	43177	/* Darwin. */
#define	AUE_OPEN_EXTENDED_RW	43178	/* Darwin. */
#define	AUE_OPEN_EXTENDED_RWC	43179	/* Darwin. */
#define	AUE_OPEN_EXTENDED_RWT	43180	/* Darwin. */
#define	AUE_OPEN_EXTENDED_RWTC	43181	/* Darwin. */
#define	AUE_STAT_EXTENDED	43182	/* Darwin. */
#define	AUE_UMASK_EXTENDED	43183	/* Darwin. */
#define	AUE_OPENAT		43184	/* FreeBSD. */
#define	AUE_POSIX_OPENPT	43185	/* FreeBSD. */
#define	AUE_CAP_NEW		43186	/* TrustedBSD. */
#define	AUE_CAP_RIGHTS_GET	43187	/* TrustedBSD. */
#define	AUE_CAP_GETRIGHTS	AUE_CAP_RIGHTS_GET
#define	AUE_CAP_ENTER		43188	/* TrustedBSD. */
#define	AUE_CAP_GETMODE		43189	/* TrustedBSD. */
#define	AUE_POSIX_SPAWN		43190	/* Darwin. */
#define	AUE_FSGETPATH		43191	/* Darwin. */
#define	AUE_PREAD		43192	/* Darwin/FreeBSD. */
#define	AUE_PWRITE		43193	/* Darwin/FreeBSD. */
#define	AUE_FSCTL		43194	/* Darwin. */
#define	AUE_FFSCTL		43195	/* Darwin. */
#define	AUE_LPATHCONF		43196	/* FreeBSD. */
#define	AUE_PDFORK		43197	/* FreeBSD. */
#define	AUE_PDKILL		43198	/* FreeBSD. */
#define	AUE_PDGETPID		43199	/* FreeBSD. */
#define	AUE_PDWAIT		43200	/* FreeBSD. */
#define	AUE_WAIT6		43201	/* FreeBSD. */
#define	AUE_CAP_RIGHTS_LIMIT	43202	/* TrustedBSD. */
#define	AUE_CAP_IOCTLS_LIMIT	43203	/* TrustedBSD. */
#define	AUE_CAP_IOCTLS_GET	43204	/* TrustedBSD. */
#define	AUE_CAP_FCNTLS_LIMIT	43205	/* TrustedBSD. */
#define	AUE_CAP_FCNTLS_GET	43206	/* TrustedBSD. */
#define	AUE_BINDAT		43207	/* TrustedBSD. */
#define	AUE_CONNECTAT		43208	/* TrustedBSD. */
#define	AUE_CHFLAGSAT		43209	/* FreeBSD-specific. */
#define	AUE_PREADV		43210	/* FreeBSD-specific. */
#define	AUE_PWRITEV		43211	/* FreeBSD-specific. */
#define	AUE_POSIX_FALLOCATE	43212	/* FreeBSD-specific. */
#define	AUE_AIO_MLOCK		43213	/* FreeBSD-specific. */
#define	AUE_PROCCTL		43214	/* FreeBSD-specific. */
#define	AUE_AIO_READ		43215	/* FreeBSD-specific. */
#define	AUE_AIO_WRITE		43216	/* FreeBSD-specific. */
#define	AUE_AIO_RETURN		43217	/* FreeBSD-specific. */
#define	AUE_AIO_SUSPEND		43218	/* FreeBSD-specific. */
#define	AUE_AIO_CANCEL		43219	/* FreeBSD-specific. */
#define	AUE_AIO_ERROR		43220	/* FreeBSD-specific. */
#define	AUE_AIO_WAITCOMPLETE	43221	/* FreeBSD-specific. */
#define	AUE_AIO_FSYNC		43222	/* FreeBSD-specific. */
#define	AUE_THR_CREATE		43223	/* FreeBSD-specific. */
#define	AUE_THR_NEW		43224	/* FreeBSD-specific. */
#define	AUE_THR_EXIT		43225	/* FreeBSD-specific. */
#define	AUE_THR_KILL		43226	/* FreeBSD-specific. */
#define	AUE_THR_KILL2		43227	/* FreeBSD-specific. */
#define	AUE_SETFIB		43228	/* FreeBSD-specific. */
#define	AUE_LIO_LISTIO		43229	/* FreeBSD-specific. */
#define	AUE_SETUGID		43230	/* FreeBSD-specific. */
#define	AUE_SCTP_PEELOFF	43231	/* FreeBSD-specific. */
#define	AUE_SCTP_GENERIC_SENDMSG	43232	/* FreeBSD-specific. */
#define	AUE_SCTP_GENERIC_RECVMSG	43233	/* FreeBSD-specific. */
#define	AUE_JAIL_GET		43234	/* FreeBSD-specific. */
#define	AUE_JAIL_SET		43235	/* FreeBSD-specific. */
#define	AUE_JAIL_REMOVE		43236	/* FreeBSD-specific. */
#define	AUE_GETLOGINCLASS	43237	/* FreeBSD-specific. */
#define	AUE_SETLOGINCLASS	43238	/* FreeBSD-specific. */
#define	AUE_POSIX_FADVISE	43239	/* FreeBSD-specific. */
#define	AUE_SCTP_GENERIC_SENDMSG_IOV	43240	/* FreeBSD-specific. */

/*
 * Darwin BSM uses a number of AUE_O_* definitions, which are aliased to the
 * normal Solaris BSM identifiers.  _O_ refers to it being an old, or compat
 * interface.  In most cases, Darwin has never implemented these system calls
 * but picked up the fields in their system call table from their FreeBSD
 * import.  Happily, these have different names than the AUE_O* definitions
 * in Solaris BSM.
 */
#define	AUE_O_CREAT		AUE_OPEN_RWTC	/* Darwin */
#define	AUE_O_EXECVE		AUE_NULL	/* Darwin */
#define	AUE_O_SBREAK		AUE_NULL	/* Darwin */
#define	AUE_O_LSEEK		AUE_NULL	/* Darwin */
#define	AUE_O_MOUNT		AUE_NULL	/* Darwin */
#define	AUE_O_UMOUNT		AUE_NULL	/* Darwin */
#define	AUE_O_STAT		AUE_STAT	/* Darwin */
#define	AUE_O_LSTAT		AUE_LSTAT	/* Darwin */
#define	AUE_O_FSTAT		AUE_FSTAT	/* Darwin */
#define	AUE_O_GETPAGESIZE	AUE_NULL	/* Darwin */
#define	AUE_O_VREAD		AUE_NULL	/* Darwin */
#define	AUE_O_VWRITE		AUE_NULL	/* Darwin */
#define	AUE_O_MMAP		AUE_MMAP	/* Darwin */
#define	AUE_O_VADVISE		AUE_NULL	/* Darwin */
#define	AUE_O_VHANGUP		AUE_NULL	/* Darwin */
#define	AUE_O_VLIMIT		AUE_NULL	/* Darwin */
#define	AUE_O_WAIT		AUE_NULL	/* Darwin */
#define	AUE_O_GETHOSTNAME	AUE_NULL	/* Darwin */
#define	AUE_O_SETHOSTNAME	AUE_SYSCTL	/* Darwin */
#define	AUE_O_GETDOPT		AUE_NULL	/* Darwin */
#define	AUE_O_SETDOPT		AUE_NULL	/* Darwin */
#define	AUE_O_ACCEPT		AUE_NULL	/* Darwin */
#define	AUE_O_SEND		AUE_SENDMSG	/* Darwin */
#define	AUE_O_RECV		AUE_RECVMSG	/* Darwin */
#define	AUE_O_VTIMES		AUE_NULL	/* Darwin */
#define	AUE_O_SIGVEC		AUE_NULL	/* Darwin */
#define	AUE_O_SIGBLOCK		AUE_NULL	/* Darwin */
#define	AUE_O_SIGSETMASK	AUE_NULL	/* Darwin */
#define	AUE_O_SIGSTACK		AUE_NULL	/* Darwin */
#define	AUE_O_RECVMSG		AUE_RECVMSG	/* Darwin */
#define	AUE_O_SENDMSG		AUE_SENDMSG	/* Darwin */
#define	AUE_O_VTRACE		AUE_NULL	/* Darwin */
#define	AUE_O_RESUBA		AUE_NULL	/* Darwin */
#define	AUE_O_RECVFROM		AUE_RECVFROM	/* Darwin */
#define	AUE_O_SETREUID		AUE_SETREUID	/* Darwin */
#define	AUE_O_SETREGID		AUE_SETREGID	/* Darwin */
#define	AUE_O_GETDIRENTRIES	AUE_GETDIRENTRIES	/* Darwin */
#define	AUE_O_TRUNCATE		AUE_TRUNCATE	/* Darwin */
#define	AUE_O_FTRUNCATE		AUE_FTRUNCATE	/* Darwin */
#define	AUE_O_GETPEERNAME	AUE_NULL	/* Darwin */
#define	AUE_O_GETHOSTID		AUE_NULL	/* Darwin */
#define	AUE_O_SETHOSTID		AUE_NULL	/* Darwin */
#define	AUE_O_GETRLIMIT		AUE_NULL	/* Darwin */
#define	AUE_O_SETRLIMIT		AUE_SETRLIMIT	/* Darwin */
#define	AUE_O_KILLPG		AUE_KILL	/* Darwin */
#define	AUE_O_SETQUOTA		AUE_NULL	/* Darwin */
#define	AUE_O_QUOTA		AUE_NULL	/* Darwin */
#define	AUE_O_GETSOCKNAME	AUE_NULL	/* Darwin */
#define	AUE_O_GETDIREENTRIES	AUE_GETDIREENTRIES	/* Darwin */
#define	AUE_O_ASYNCDAEMON	AUE_NULL	/* Darwin */
#define	AUE_O_GETDOMAINNAME	AUE_NULL	/* Darwin */
#define	AUE_O_SETDOMAINNAME	AUE_SYSCTL	/* Darwin */
#define	AUE_O_PCFS_MOUNT	AUE_NULL	/* Darwin */
#define	AUE_O_EXPORTFS		AUE_NULL	/* Darwin */
#define	AUE_O_USTATE		AUE_NULL	/* Darwin */
#define	AUE_O_WAIT3		AUE_NULL	/* Darwin */
#define	AUE_O_RPAUSE		AUE_NULL	/* Darwin */
#define	AUE_O_GETDENTS		AUE_NULL	/* Darwin */

/*
 * Possible desired future values based on review of BSD/Darwin system calls.
 */
#define	AUE_ATGETMSG		AUE_NULL
#define	AUE_ATPUTMSG		AUE_NULL
#define	AUE_ATSOCKET		AUE_NULL
#define	AUE_ATPGETREQ		AUE_NULL
#define	AUE_ATPGETRSP		AUE_NULL
#define	AUE_ATPSNDREQ		AUE_NULL
#define	AUE_ATPSNDRSP		AUE_NULL
#define	AUE_BSDTHREADCREATE	AUE_NULL
#define	AUE_BSDTHREADTERMINATE	AUE_NULL
#define	AUE_BSDTHREADREGISTER	AUE_NULL
#define	AUE_CHUD		AUE_NULL
#define	AUE_CSOPS		AUE_NULL
#define	AUE_DUP			AUE_NULL
#define	AUE_FDATASYNC		AUE_NULL
#define	AUE_FGETATTRLIST	AUE_NULL
#define	AUE_FGETXATTR		AUE_NULL
#define	AUE_FLISTXATTR		AUE_NULL
#define	AUE_FREMOVEXATTR	AUE_NULL
#define	AUE_FSETATTRLIST	AUE_NULL
#define	AUE_FSETXATTR		AUE_NULL
#define	AUE_FSTATFS64		AUE_NULL
#define	AUE_FSTATV		AUE_NULL
#define	AUE_FSTAT64		AUE_NULL
#define	AUE_FSTAT64_EXTENDED	AUE_NULL
#define	AUE_GCCONTROL		AUE_NULL
#define	AUE_GETDIRENTRIES64	AUE_NULL
#define	AUE_GETDTABLESIZE	AUE_NULL
#define	AUE_GETEGID		AUE_NULL
#define	AUE_GETEUID		AUE_NULL
#define	AUE_GETFSSTAT64		AUE_NULL
#define	AUE_GETGID		AUE_NULL
#define	AUE_GETGROUPS		AUE_NULL
#define	AUE_GETITIMER		AUE_NULL
#define	AUE_GETLOGIN		AUE_NULL
#define	AUE_GETPEERNAME		AUE_NULL
#define	AUE_GETPGID		AUE_NULL
#define	AUE_GETPGRP		AUE_NULL
#define	AUE_GETPID		AUE_NULL
#define	AUE_GETPPID		AUE_NULL
#define	AUE_GETPRIORITY		AUE_NULL
#define	AUE_GETRLIMIT		AUE_NULL
#define	AUE_GETRUSAGE		AUE_NULL
#define	AUE_GETSGROUPS		AUE_NULL
#define	AUE_GETSID		AUE_NULL
#define	AUE_GETSOCKNAME		AUE_NULL
#define	AUE_GETTIMEOFDAY	AUE_NULL
#define	AUE_GETTID		AUE_NULL
#define	AUE_GETUID		AUE_NULL
#define	AUE_GETSOCKOPT		AUE_NULL
#define	AUE_GETWGROUPS		AUE_NULL
#define	AUE_GETXATTR		AUE_NULL
#define	AUE_IDENTITYSVC		AUE_NULL
#define	AUE_INITGROUPS		AUE_NULL
#define	AUE_IOPOLICYSYS		AUE_NULL
#define	AUE_ISSETUGID		AUE_NULL
#define	AUE_LIOLISTIO		AUE_NULL
#define	AUE_LISTXATTR		AUE_NULL
#define	AUE_LSTATV		AUE_NULL
#define	AUE_LSTAT64		AUE_NULL
#define	AUE_LSTAT64_EXTENDED	AUE_NULL
#define	AUE_MADVISE		AUE_NULL
#define	AUE_MINCORE		AUE_NULL
#define	AUE_MKCOMPLEX		AUE_NULL
#define	AUE_MODWATCH		AUE_NULL
#define	AUE_MSGCL		AUE_NULL
#define	AUE_MSYNC		AUE_NULL
#define	AUE_PROCINFO		AUE_NULL
#define	AUE_PTHREADCANCELED	AUE_NULL
#define	AUE_PTHREADCHDIR	AUE_NULL
#define	AUE_PTHREADCONDBROADCAST	AUE_NULL
#define	AUE_PTHREADCONDDESTORY	AUE_NULL
#define	AUE_PTHREADCONDINIT	AUE_NULL
#define	AUE_PTHREADCONDSIGNAL	AUE_NULL
#define	AUE_PTHREADCONDWAIT	AUE_NULL
#define	AUE_PTHREADFCHDIR	AUE_NULL
#define	AUE_PTHREADMARK		AUE_NULL
#define	AUE_PTHREADMUTEXDESTROY	AUE_NULL
#define	AUE_PTHREADMUTEXINIT	AUE_NULL
#define	AUE_PTHREADMUTEXTRYLOCK	AUE_NULL
#define	AUE_PTHREADMUTEXUNLOCK	AUE_NULL
#define	AUE_REMOVEXATTR		AUE_NULL
#define	AUE_SBRK		AUE_NULL
#define	AUE_SELECT		AUE_NULL
#define	AUE_SEMDESTROY		AUE_NULL
#define	AUE_SEMGETVALUE		AUE_NULL
#define	AUE_SEMINIT		AUE_NULL
#define	AUE_SEMPOST		AUE_NULL
#define	AUE_SEMTRYWAIT		AUE_NULL
#define	AUE_SEMWAIT		AUE_NULL
#define	AUE_SEMWAITSIGNAL	AUE_NULL
#define	AUE_SETITIMER		AUE_NULL
#define	AUE_SETSGROUPS		AUE_NULL
#define	AUE_SETTID		AUE_NULL
#define	AUE_SETTIDWITHPID	AUE_NULL
#define	AUE_SETWGROUPS		AUE_NULL
#define	AUE_SETXATTR		AUE_NULL
#define	AUE_SHAREDREGIONCHECK	AUE_NULL
#define	AUE_SHAREDREGIONMAP	AUE_NULL
#define	AUE_SIGACTION		AUE_NULL
#define	AUE_SIGALTSTACK		AUE_NULL
#define	AUE_SIGPENDING		AUE_NULL
#define	AUE_SIGPROCMASK		AUE_NULL
#define	AUE_SIGRETURN		AUE_NULL
#define	AUE_SIGSUSPEND		AUE_NULL
#define	AUE_SIGWAIT		AUE_NULL
#define	AUE_SSTK		AUE_NULL
#define	AUE_STACKSNAPSHOT	AUE_NULL
#define	AUE_STATFS64		AUE_NULL
#define	AUE_STATV		AUE_NULL
#define	AUE_STAT64		AUE_NULL
#define	AUE_STAT64_EXTENDED	AUE_NULL
#define	AUE_SYNC		AUE_NULL
#define	AUE_SYSCALL		AUE_NULL
#define	AUE_TABLE		AUE_NULL
#define	AUE_VMPRESSUREMONITOR	AUE_NULL
#define	AUE_WAITEVENT		AUE_NULL
#define	AUE_WAITID		AUE_NULL
#define	AUE_WATCHEVENT		AUE_NULL
#define	AUE_WORKQOPEN		AUE_NULL
#define	AUE_WORKQOPS		AUE_NULL

#endif /* !_BSM_AUDIT_KEVENTS_H_ */
