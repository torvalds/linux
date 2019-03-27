/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 nCircle Network Security, Inc.
 * All rights reserved.
 *
 * This software was developed by Robert N. M. Watson for the TrustedBSD
 * Project under contract to nCircle Network Security, Inc.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR, NCIRCLE NETWORK SECURITY,
 * INC., OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Privilege checking interface for BSD kernel.
 */
#ifndef _SYS_PRIV_H_
#define	_SYS_PRIV_H_

/*
 * Privilege list, sorted loosely by kernel subsystem.
 *
 * Think carefully before adding or reusing one of these privileges -- are
 * there existing instances referring to the same privilege?  Third party
 * vendors may request the assignment of privileges to be used in loadable
 * modules.  Particular numeric privilege assignments are part of the
 * loadable kernel module ABI, and should not be changed across minor
 * releases.
 *
 * When adding a new privilege, remember to determine if it's appropriate
 * for use in jail, and update the privilege switch in prison_priv_check()
 * in kern_jail.c as necessary.
 */

/*
 * Track beginning of privilege list.
 */
#define	_PRIV_LOWEST	1

/*
 * The remaining privileges typically correspond to one or a small
 * number of specific privilege checks, and have (relatively) precise
 * meanings.  They are loosely sorted into a set of base system
 * privileges, such as the ability to reboot, and then loosely by
 * subsystem, indicated by a subsystem name.
 */
#define	_PRIV_ROOT		1	/* Removed. */
#define	PRIV_ACCT		2	/* Manage process accounting. */
#define	PRIV_MAXFILES		3	/* Exceed system open files limit. */
#define	PRIV_MAXPROC		4	/* Exceed system processes limit. */
#define	PRIV_KTRACE		5	/* Set/clear KTRFAC_ROOT on ktrace. */
#define	PRIV_SETDUMPER		6	/* Configure dump device. */
#define	PRIV_REBOOT		8	/* Can reboot system. */
#define	PRIV_SWAPON		9	/* Can swapon(). */
#define	PRIV_SWAPOFF		10	/* Can swapoff(). */
#define	PRIV_MSGBUF		11	/* Can read kernel message buffer. */
#define	PRIV_IO			12	/* Can perform low-level I/O. */
#define	PRIV_KEYBOARD		13	/* Reprogram keyboard. */
#define	PRIV_DRIVER		14	/* Low-level driver privilege. */
#define	PRIV_ADJTIME		15	/* Set time adjustment. */
#define	PRIV_NTP_ADJTIME	16	/* Set NTP time adjustment. */
#define	PRIV_CLOCK_SETTIME	17	/* Can call clock_settime. */
#define	PRIV_SETTIMEOFDAY	18	/* Can call settimeofday. */
#define	_PRIV_SETHOSTID		19	/* Removed. */
#define	_PRIV_SETDOMAINNAME	20	/* Removed. */

/*
 * Audit subsystem privileges.
 */
#define	PRIV_AUDIT_CONTROL	40	/* Can configure audit. */
#define	PRIV_AUDIT_FAILSTOP	41	/* Can run during audit fail stop. */
#define	PRIV_AUDIT_GETAUDIT	42	/* Can get proc audit properties. */
#define	PRIV_AUDIT_SETAUDIT	43	/* Can set proc audit properties. */
#define	PRIV_AUDIT_SUBMIT	44	/* Can submit an audit record. */

/*
 * Credential management privileges.
 */
#define	PRIV_CRED_SETUID	50	/* setuid. */
#define	PRIV_CRED_SETEUID	51	/* seteuid to !ruid and !svuid. */
#define	PRIV_CRED_SETGID	52	/* setgid. */
#define	PRIV_CRED_SETEGID	53	/* setgid to !rgid and !svgid. */
#define	PRIV_CRED_SETGROUPS	54	/* Set process additional groups. */
#define	PRIV_CRED_SETREUID	55	/* setreuid. */
#define	PRIV_CRED_SETREGID	56	/* setregid. */
#define	PRIV_CRED_SETRESUID	57	/* setresuid. */
#define	PRIV_CRED_SETRESGID	58	/* setresgid. */
#define	PRIV_SEEOTHERGIDS	59	/* Exempt bsd.seeothergids. */
#define	PRIV_SEEOTHERUIDS	60	/* Exempt bsd.seeotheruids. */

/*
 * Debugging privileges.
 */
#define	PRIV_DEBUG_DIFFCRED	80	/* Exempt debugging other users. */
#define	PRIV_DEBUG_SUGID	81	/* Exempt debugging setuid proc. */
#define	PRIV_DEBUG_UNPRIV	82	/* Exempt unprivileged debug limit. */
#define	PRIV_DEBUG_DENIED	83	/* Exempt P2_NOTRACE. */

/*
 * Dtrace privileges.
 */
#define	PRIV_DTRACE_KERNEL	90	/* Allow use of DTrace on the kernel. */
#define	PRIV_DTRACE_PROC	91	/* Allow attaching DTrace to process. */
#define	PRIV_DTRACE_USER	92	/* Process may submit DTrace events. */

/*
 * Firmware privilegs.
 */
#define	PRIV_FIRMWARE_LOAD	100	/* Can load firmware. */

/*
 * Jail privileges.
 */
#define	PRIV_JAIL_ATTACH	110	/* Attach to a jail. */
#define	PRIV_JAIL_SET		111	/* Set jail parameters. */
#define	PRIV_JAIL_REMOVE	112	/* Remove a jail. */

/*
 * Kernel environment privileges.
 */
#define	PRIV_KENV_SET		120	/* Set kernel env. variables. */
#define	PRIV_KENV_UNSET		121	/* Unset kernel env. variables. */

/*
 * Loadable kernel module privileges.
 */
#define	PRIV_KLD_LOAD		130	/* Load a kernel module. */
#define	PRIV_KLD_UNLOAD		131	/* Unload a kernel module. */

/*
 * Privileges associated with the MAC Framework and specific MAC policy
 * modules.
 */
#define	PRIV_MAC_PARTITION	140	/* Privilege in mac_partition policy. */
#define	PRIV_MAC_PRIVS		141	/* Privilege in the mac_privs policy. */

/*
 * Process-related privileges.
 */
#define	PRIV_PROC_LIMIT		160	/* Exceed user process limit. */
#define	PRIV_PROC_SETLOGIN	161	/* Can call setlogin. */
#define	PRIV_PROC_SETRLIMIT	162	/* Can raise resources limits. */
#define	PRIV_PROC_SETLOGINCLASS	163	/* Can call setloginclass(2). */

/*
 * System V IPC privileges.
 */
#define	PRIV_IPC_READ		170	/* Can override IPC read perm. */
#define	PRIV_IPC_WRITE		171	/* Can override IPC write perm. */
#define	PRIV_IPC_ADMIN		172	/* Can override IPC owner-only perm. */
#define	PRIV_IPC_MSGSIZE	173	/* Exempt IPC message queue limit. */

/*
 * POSIX message queue privileges.
 */
#define	PRIV_MQ_ADMIN		180	/* Can override msgq owner-only perm. */

/*
 * Performance monitoring counter privileges.
 */
#define	PRIV_PMC_MANAGE		190	/* Can administer PMC. */
#define	PRIV_PMC_SYSTEM		191	/* Can allocate a system-wide PMC. */

/*
 * Scheduling privileges.
 */
#define	PRIV_SCHED_DIFFCRED	200	/* Exempt scheduling other users. */
#define	PRIV_SCHED_SETPRIORITY	201	/* Can set lower nice value for proc. */
#define	PRIV_SCHED_RTPRIO	202	/* Can set real time scheduling. */
#define	PRIV_SCHED_SETPOLICY	203	/* Can set scheduler policy. */
#define	PRIV_SCHED_SET		204	/* Can set thread scheduler. */
#define	PRIV_SCHED_SETPARAM	205	/* Can set thread scheduler params. */
#define	PRIV_SCHED_CPUSET	206	/* Can manipulate cpusets. */
#define	PRIV_SCHED_CPUSET_INTR	207	/* Can adjust IRQ to CPU binding. */

/*
 * POSIX semaphore privileges.
 */
#define	PRIV_SEM_WRITE		220	/* Can override sem write perm. */

/*
 * Signal privileges.
 */
#define	PRIV_SIGNAL_DIFFCRED	230	/* Exempt signalling other users. */
#define	PRIV_SIGNAL_SUGID	231	/* Non-conserv signal setuid proc. */

/*
 * Sysctl privileges.
 */
#define	PRIV_SYSCTL_DEBUG	240	/* Can invoke sysctl.debug. */
#define	PRIV_SYSCTL_WRITE	241	/* Can write sysctls. */
#define	PRIV_SYSCTL_WRITEJAIL	242	/* Can write sysctls, jail permitted. */

/*
 * TTY privileges.
 */
#define	PRIV_TTY_CONSOLE	250	/* Set console to tty. */
#define	PRIV_TTY_DRAINWAIT	251	/* Set tty drain wait time. */
#define	PRIV_TTY_DTRWAIT	252	/* Set DTR wait on tty. */
#define	PRIV_TTY_EXCLUSIVE	253	/* Override tty exclusive flag. */
#define	_PRIV_TTY_PRISON	254	/* Removed. */
#define	PRIV_TTY_STI		255	/* Simulate input on another tty. */
#define	PRIV_TTY_SETA		256	/* Set tty termios structure. */

/*
 * UFS-specific privileges.
 */
#define	PRIV_UFS_EXTATTRCTL	270	/* Can configure EAs on UFS1. */
#define	PRIV_UFS_QUOTAOFF	271	/* quotaoff(). */
#define	PRIV_UFS_QUOTAON	272	/* quotaon(). */
#define	PRIV_UFS_SETUSE		273	/* setuse(). */

/*
 * ZFS-specific privileges.
 */
#define	PRIV_ZFS_POOL_CONFIG	280	/* Can configure ZFS pools. */
#define	PRIV_ZFS_INJECT		281	/* Can inject faults in the ZFS fault
					   injection framework. */
#define	PRIV_ZFS_JAIL		282	/* Can attach/detach ZFS file systems
					   to/from jails. */

/*
 * NFS-specific privileges.
 */
#define	PRIV_NFS_DAEMON		290	/* Can become the NFS daemon. */
#define	PRIV_NFS_LOCKD		291	/* Can become NFS lock daemon. */

/*
 * VFS privileges.
 */
#define	PRIV_VFS_READ		310	/* Override vnode DAC read perm. */
#define	PRIV_VFS_WRITE		311	/* Override vnode DAC write perm. */
#define	PRIV_VFS_ADMIN		312	/* Override vnode DAC admin perm. */
#define	PRIV_VFS_EXEC		313	/* Override vnode DAC exec perm. */
#define	PRIV_VFS_LOOKUP		314	/* Override vnode DAC lookup perm. */
#define	PRIV_VFS_BLOCKRESERVE	315	/* Can use free block reserve. */
#define	PRIV_VFS_CHFLAGS_DEV	316	/* Can chflags() a device node. */
#define	PRIV_VFS_CHOWN		317	/* Can set user; group to non-member. */
#define	PRIV_VFS_CHROOT		318	/* chroot(). */
#define	PRIV_VFS_RETAINSUGID	319	/* Can retain sugid bits on change. */
#define	PRIV_VFS_EXCEEDQUOTA	320	/* Exempt from quota restrictions. */
#define	PRIV_VFS_EXTATTR_SYSTEM	321	/* Operate on system EA namespace. */
#define	PRIV_VFS_FCHROOT	322	/* fchroot(). */
#define	PRIV_VFS_FHOPEN		323	/* Can fhopen(). */
#define	PRIV_VFS_FHSTAT		324	/* Can fhstat(). */
#define	PRIV_VFS_FHSTATFS	325	/* Can fhstatfs(). */
#define	PRIV_VFS_GENERATION	326	/* stat() returns generation number. */
#define	PRIV_VFS_GETFH		327	/* Can retrieve file handles. */
#define	PRIV_VFS_GETQUOTA	328	/* getquota(). */
#define	PRIV_VFS_LINK		329	/* bsd.hardlink_check_uid */
#define	PRIV_VFS_MKNOD_BAD	330	/* Was: mknod() can mark bad inodes. */
#define	PRIV_VFS_MKNOD_DEV	331	/* Can mknod() to create dev nodes. */
#define	PRIV_VFS_MKNOD_WHT	332	/* Can mknod() to create whiteout. */
#define	PRIV_VFS_MOUNT		333	/* Can mount(). */
#define	PRIV_VFS_MOUNT_OWNER	334	/* Can manage other users' file systems. */
#define	PRIV_VFS_MOUNT_EXPORTED	335	/* Can set MNT_EXPORTED on mount. */
#define	PRIV_VFS_MOUNT_PERM	336	/* Override dev node perms at mount. */
#define	PRIV_VFS_MOUNT_SUIDDIR	337	/* Can set MNT_SUIDDIR on mount. */
#define	PRIV_VFS_MOUNT_NONUSER	338	/* Can perform a non-user mount. */
#define	PRIV_VFS_SETGID		339	/* Can setgid if not in group. */
#define	PRIV_VFS_SETQUOTA	340	/* setquota(). */
#define	PRIV_VFS_STICKYFILE	341	/* Can set sticky bit on file. */
#define	PRIV_VFS_SYSFLAGS	342	/* Can modify system flags. */
#define	PRIV_VFS_UNMOUNT	343	/* Can unmount(). */
#define	PRIV_VFS_STAT		344	/* Override vnode MAC stat perm. */

/*
 * Virtual memory privileges.
 */
#define	PRIV_VM_MADV_PROTECT	360	/* Can set MADV_PROTECT. */
#define	PRIV_VM_MLOCK		361	/* Can mlock(), mlockall(). */
#define	PRIV_VM_MUNLOCK		362	/* Can munlock(), munlockall(). */
#define	PRIV_VM_SWAP_NOQUOTA	363	/*
					 * Can override the global
					 * swap reservation limits.
					 */
#define	PRIV_VM_SWAP_NORLIMIT	364	/*
					 * Can override the per-uid
					 * swap reservation limits.
					 */

/*
 * Device file system privileges.
 */
#define	PRIV_DEVFS_RULE		370	/* Can manage devfs rules. */
#define	PRIV_DEVFS_SYMLINK	371	/* Can create symlinks in devfs. */

/*
 * Random number generator privileges.
 */
#define	PRIV_RANDOM_RESEED	380	/* Closing /dev/random reseeds. */

/*
 * Network stack privileges.
 */
#define	PRIV_NET_BRIDGE		390	/* Administer bridge. */
#define	PRIV_NET_GRE		391	/* Administer GRE. */
#define	_PRIV_NET_PPP		392	/* Removed. */
#define	_PRIV_NET_SLIP		393	/* Removed. */
#define	PRIV_NET_BPF		394	/* Monitor BPF. */
#define	PRIV_NET_RAW		395	/* Open raw socket. */
#define	PRIV_NET_ROUTE		396	/* Administer routing. */
#define	PRIV_NET_TAP		397	/* Can open tap device. */
#define	PRIV_NET_SETIFMTU	398	/* Set interface MTU. */
#define	PRIV_NET_SETIFFLAGS	399	/* Set interface flags. */
#define	PRIV_NET_SETIFCAP	400	/* Set interface capabilities. */
#define	PRIV_NET_SETIFNAME	401	/* Set interface name. */
#define	PRIV_NET_SETIFMETRIC	402	/* Set interface metrics. */
#define	PRIV_NET_SETIFPHYS	403	/* Set interface physical layer prop. */
#define	PRIV_NET_SETIFMAC	404	/* Set interface MAC label. */
#define	PRIV_NET_ADDMULTI	405	/* Add multicast addr. to ifnet. */
#define	PRIV_NET_DELMULTI	406	/* Delete multicast addr. from ifnet. */
#define	PRIV_NET_HWIOCTL	407	/* Issue hardware ioctl on ifnet. */
#define	PRIV_NET_SETLLADDR	408	/* Set interface link-level address. */
#define	PRIV_NET_ADDIFGROUP	409	/* Add new interface group. */
#define	PRIV_NET_DELIFGROUP	410	/* Delete interface group. */
#define	PRIV_NET_IFCREATE	411	/* Create cloned interface. */
#define	PRIV_NET_IFDESTROY	412	/* Destroy cloned interface. */
#define	PRIV_NET_ADDIFADDR	413	/* Add protocol addr to interface. */
#define	PRIV_NET_DELIFADDR	414	/* Delete protocol addr on interface. */
#define	PRIV_NET_LAGG		415	/* Administer lagg interface. */
#define	PRIV_NET_GIF		416	/* Administer gif interface. */
#define	PRIV_NET_SETIFVNET	417	/* Move interface to vnet. */
#define	PRIV_NET_SETIFDESCR	418	/* Set interface description. */
#define	PRIV_NET_SETIFFIB	419	/* Set interface fib. */
#define	PRIV_NET_VXLAN		420	/* Administer vxlan. */
#define	PRIV_NET_SETLANPCP	421	/* Set LAN priority. */
#define	PRIV_NET_SETVLANPCP	PRIV_NET_SETLANPCP /* Alias Set VLAN priority */

/*
 * 802.11-related privileges.
 */
#define	PRIV_NET80211_GETKEY	440	/* Query 802.11 keys. */
#define	PRIV_NET80211_MANAGE	441	/* Administer 802.11. */

/*
 * Placeholder for AppleTalk privileges, not supported anymore.
 */
#define	_PRIV_NETATALK_RESERVEDPORT	450	/* Bind low port number. */

/*
 * ATM privileges.
 */
#define	PRIV_NETATM_CFG		460
#define	PRIV_NETATM_ADD		461
#define	PRIV_NETATM_DEL		462
#define	PRIV_NETATM_SET		463

/*
 * Bluetooth privileges.
 */
#define	PRIV_NETBLUETOOTH_RAW	470	/* Open raw bluetooth socket. */

/*
 * Netgraph and netgraph module privileges.
 */
#define	PRIV_NETGRAPH_CONTROL	480	/* Open netgraph control socket. */
#define	PRIV_NETGRAPH_TTY	481	/* Configure tty for netgraph. */

/*
 * IPv4 and IPv6 privileges.
 */
#define	PRIV_NETINET_RESERVEDPORT	490	/* Bind low port number. */
#define	PRIV_NETINET_IPFW	491	/* Administer IPFW firewall. */
#define	PRIV_NETINET_DIVERT	492	/* Open IP divert socket. */
#define	PRIV_NETINET_PF		493	/* Administer pf firewall. */
#define	PRIV_NETINET_DUMMYNET	494	/* Administer DUMMYNET. */
#define	PRIV_NETINET_CARP	495	/* Administer CARP. */
#define	PRIV_NETINET_MROUTE	496	/* Administer multicast routing. */
#define	PRIV_NETINET_RAW	497	/* Open netinet raw socket. */
#define	PRIV_NETINET_GETCRED	498	/* Query netinet pcb credentials. */
#define	PRIV_NETINET_ADDRCTRL6	499	/* Administer IPv6 address scopes. */
#define	PRIV_NETINET_ND6	500	/* Administer IPv6 neighbor disc. */
#define	PRIV_NETINET_SCOPE6	501	/* Administer IPv6 address scopes. */
#define	PRIV_NETINET_ALIFETIME6	502	/* Administer IPv6 address lifetimes. */
#define	PRIV_NETINET_IPSEC	503	/* Administer IPSEC. */
#define	PRIV_NETINET_REUSEPORT	504	/* Allow [rapid] port/address reuse. */
#define	PRIV_NETINET_SETHDROPTS	505	/* Set certain IPv4/6 header options. */
#define	PRIV_NETINET_BINDANY	506	/* Allow bind to any address. */
#define	PRIV_NETINET_HASHKEY	507	/* Get and set hash keys for IPv4/6. */

/*
 * Placeholders for IPX/SPX privileges, not supported any more.
 */
#define	_PRIV_NETIPX_RESERVEDPORT	520	/* Bind low port number. */
#define	_PRIV_NETIPX_RAW		521	/* Open netipx raw socket. */

/*
 * NCP privileges.
 */
#define	PRIV_NETNCP		530	/* Use another user's connection. */

/*
 * SMB privileges.
 */
#define	PRIV_NETSMB		540	/* Use another user's connection. */

/*
 * VM86 privileges.
 */
#define	PRIV_VM86_INTCALL	550	/* Allow invoking vm86 int handlers. */

/*
 * Set of reserved privilege values, which will be allocated to code as
 * needed, in order to avoid renumbering later privileges due to insertion.
 */
#define	_PRIV_RESERVED0		560
#define	_PRIV_RESERVED1		561
#define	_PRIV_RESERVED2		562
#define	_PRIV_RESERVED3		563
#define	_PRIV_RESERVED4		564
#define	_PRIV_RESERVED5		565
#define	_PRIV_RESERVED6		566
#define	_PRIV_RESERVED7		567
#define	_PRIV_RESERVED8		568
#define	_PRIV_RESERVED9		569
#define	_PRIV_RESERVED10	570
#define	_PRIV_RESERVED11	571
#define	_PRIV_RESERVED12	572
#define	_PRIV_RESERVED13	573
#define	_PRIV_RESERVED14	574
#define	_PRIV_RESERVED15	575

/*
 * Define a set of valid privilege numbers that can be used by loadable
 * modules that don't yet have privilege reservations.  Ideally, these should
 * not be used, since their meaning is opaque to any policies that are aware
 * of specific privileges, such as jail, and as such may be arbitrarily
 * denied.
 */
#define	PRIV_MODULE0		600
#define	PRIV_MODULE1		601
#define	PRIV_MODULE2		602
#define	PRIV_MODULE3		603
#define	PRIV_MODULE4		604
#define	PRIV_MODULE5		605
#define	PRIV_MODULE6		606
#define	PRIV_MODULE7		607
#define	PRIV_MODULE8		608
#define	PRIV_MODULE9		609
#define	PRIV_MODULE10		610
#define	PRIV_MODULE11		611
#define	PRIV_MODULE12		612
#define	PRIV_MODULE13		613
#define	PRIV_MODULE14		614
#define	PRIV_MODULE15		615

/*
 * DDB(4) privileges.
 */
#define	PRIV_DDB_CAPTURE	620	/* Allow reading of DDB capture log. */

/*
 * Arla/nnpfs privileges.
 */
#define	PRIV_NNPFS_DEBUG	630	/* Perforn ARLA_VIOC_NNPFSDEBUG. */

/*
 * cpuctl(4) privileges.
 */
#define PRIV_CPUCTL_WRMSR	640	/* Write model-specific register. */
#define PRIV_CPUCTL_UPDATE	641	/* Update cpu microcode. */

/*
 * Capi4BSD privileges.
 */
#define	PRIV_C4B_RESET_CTLR	650	/* Load firmware, reset controller. */
#define	PRIV_C4B_TRACE		651	/* Unrestricted CAPI message tracing. */

/*
 * OpenAFS privileges.
 */
#define	PRIV_AFS_ADMIN		660	/* Can change AFS client settings. */
#define	PRIV_AFS_DAEMON		661	/* Can become the AFS daemon. */

/*
 * Resource Limits privileges.
 */
#define	PRIV_RCTL_GET_RACCT	670
#define	PRIV_RCTL_GET_RULES	671
#define	PRIV_RCTL_GET_LIMITS	672
#define	PRIV_RCTL_ADD_RULE	673
#define	PRIV_RCTL_REMOVE_RULE	674

/*
 * mem(4) privileges.
 */
#define	PRIV_KMEM_READ		680	/* Open mem/kmem for reading. */
#define	PRIV_KMEM_WRITE		681	/* Open mem/kmem for writing. */

/*
 * Track end of privilege list.
 */
#define	_PRIV_HIGHEST		682

/*
 * Validate that a named privilege is known by the privilege system.  Invalid
 * privileges presented to the privilege system by a priv_check interface
 * will result in a panic.  This is only approximate due to sparse allocation
 * of the privilege space.
 */
#define	PRIV_VALID(x)	((x) > _PRIV_LOWEST && (x) < _PRIV_HIGHEST)

#ifdef _KERNEL
/*
 * Privilege check interfaces, modeled after historic suser() interfaces, but
 * with the addition of a specific privilege name.  No flags are currently
 * defined for the API.  Historically, flags specified using the real uid
 * instead of the effective uid, and whether or not the check should be
 * allowed in jail.
 */
struct thread;
struct ucred;
int	priv_check(struct thread *td, int priv);
int	priv_check_cred(struct ucred *cred, int priv);
#endif

#endif /* !_SYS_PRIV_H_ */
