#
# $FreeBSD$
#

.include <src.opts.mk>

PROG=	priv
SRCS=	main.c				\
	priv_acct.c			\
	priv_adjtime.c			\
	priv_audit_control.c		\
	priv_audit_getaudit.c		\
	priv_audit_setaudit.c		\
	priv_audit_submit.c		\
	priv_clock_settime.c		\
	priv_cred.c			\
	priv_io.c			\
	priv_kenv_set.c			\
	priv_kenv_unset.c		\
	priv_msgbuf.c			\
	priv_netinet_ipsec.c		\
	priv_netinet_raw.c		\
	priv_proc_setlogin.c		\
	priv_proc_setrlimit.c		\
	priv_sched_rtprio.c		\
	priv_sched_setpriority.c	\
	priv_settimeofday.c		\
	priv_sysctl_write.c		\
	priv_vfs_chflags.c		\
	priv_vfs_chmod.c		\
	priv_vfs_chown.c		\
	priv_vfs_chroot.c		\
	priv_vfs_clearsugid.c		\
	priv_vfs_extattr_system.c	\
	priv_vfs_fhopen.c		\
	priv_vfs_fhstat.c		\
	priv_vfs_fhstatfs.c		\
	priv_vfs_generation.c		\
	priv_vfs_getfh.c		\
	priv_vfs_read_write.c		\
	priv_vfs_setgid.c		\
	priv_vfs_stickyfile.c		\
	priv_vfs_utimes.c		\
	priv_vm_madv_protect.c		\
	priv_vm_mlock.c			\
	priv_vm_munlock.c

MAN=
WARNS?=	3

LIBADD+=	ipsec

.if ${MK_INET6_SUPPORT} != "no"
CFLAGS+= -DINET6
.endif

.include <bsd.prog.mk>
