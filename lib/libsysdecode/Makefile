# $FreeBSD$

.include <src.opts.mk>

PACKAGE=lib${LIB}
LIB=	sysdecode

SRCS=	errno.c flags.c ioctl.c signal.c syscallnames.c utrace.c
INCS=	sysdecode.h

CFLAGS+= -I${.OBJDIR}
CFLAGS+= -I${SRCTOP}/sys
CFLAGS+= -I${SRCTOP}/libexec/rtld-elf

MAN=	sysdecode.3 \
	sysdecode_abi_to_freebsd_errno.3 \
	sysdecode_cap_rights.3 \
	sysdecode_cmsg_type.3 \
	sysdecode_enum.3 \
	sysdecode_fcntl_arg.3 \
	sysdecode_kevent.3 \
	sysdecode_ioctlname.3 \
	sysdecode_mask.3 \
	sysdecode_quotactl_cmd.3 \
	sysdecode_sctp_sinfo_flags.3 \
	sysdecode_sigcode.3 \
	sysdecode_sockopt_name.3 \
	sysdecode_socket_protocol.3 \
	sysdecode_syscallnames.3 \
	sysdecode_utrace.3
MLINKS= sysdecode_abi_to_freebsd_errno.3 sysdecode_freebsd_to_abi_errno.3
MLINKS+=sysdecode_enum.3 sysdecode_acltype.3 \
	sysdecode_enum.3 sysdecode_atfd.3 \
	sysdecode_enum.3 sysdecode_extattrnamespace.3 \
	sysdecode_enum.3 sysdecode_fadvice.3 \
	sysdecode_enum.3 sysdecode_fcntl_cmd.3 \
	sysdecode_enum.3 sysdecode_getfsstat_mode.3 \
	sysdecode_enum.3 sysdecode_getrusage_who.3 \
	sysdecode_enum.3 sysdecode_idtype.3 \
	sysdecode_enum.3 sysdecode_ipproto.3 \
	sysdecode_enum.3 sysdecode_kldsym_cmd.3 \
	sysdecode_enum.3 sysdecode_kldunload_flags.3 \
	sysdecode_enum.3 sysdecode_lio_listio_mode.3 \
	sysdecode_enum.3 sysdecode_madvice.3 \
	sysdecode_enum.3 sysdecode_minherit_flags.3 \
	sysdecode_enum.3 sysdecode_msgctl_cmd.3 \
	sysdecode_enum.3 sysdecode_nfssvc_flags.3 \
	sysdecode_enum.3 sysdecode_pathconf_name.3 \
	sysdecode_enum.3 sysdecode_prio_which.3 \
	sysdecode_enum.3 sysdecode_procctl_cmd.3 \
	sysdecode_enum.3 sysdecode_ptrace_request.3 \
	sysdecode_enum.3 sysdecode_rlimit.3 \
	sysdecode_enum.3 sysdecode_rtprio_function.3 \
	sysdecode_enum.3 sysdecode_scheduler_policy.3 \
	sysdecode_enum.3 sysdecode_sctp_pr_policy.3 \
	sysdecode_enum.3 sysdecode_semctl_cmd.3 \
	sysdecode_enum.3 sysdecode_shmctl_cmd.3 \
	sysdecode_enum.3 sysdecode_shutdown_how.3 \
	sysdecode_enum.3 sysdecode_sigbus_code.3 \
	sysdecode_enum.3 sysdecode_sigchld_code.3 \
	sysdecode_enum.3 sysdecode_sigfpe_code.3 \
	sysdecode_enum.3 sysdecode_sigill_code.3 \
	sysdecode_enum.3 sysdecode_signal.3 \
	sysdecode_enum.3 sysdecode_sigprocmask_how.3 \
	sysdecode_enum.3 sysdecode_sigsegv_code.3 \
	sysdecode_enum.3 sysdecode_sigtrap_code.3 \
	sysdecode_enum.3 sysdecode_sockaddr_family.3 \
	sysdecode_enum.3 sysdecode_socketdomain.3 \
	sysdecode_enum.3 sysdecode_sockettype.3 \
	sysdecode_enum.3 sysdecode_sockopt_level.3 \
	sysdecode_enum.3 sysdecode_sysarch_number.3 \
	sysdecode_enum.3 sysdecode_umtx_op.3 \
	sysdecode_enum.3 sysdecode_vmresult.3 \
	sysdecode_enum.3 sysdecode_whence.3
MLINKS+=sysdecode_fcntl_arg.3 sysdecode_fcntl_arg_p.3
MLINKS+=sysdecode_kevent.3 sysdecode_kevent_fflags.3 \
	sysdecode_kevent.3 sysdecode_kevent_filter.3 \
	sysdecode_kevent.3 sysdecode_kevent_flags.3
MLINKS+=sysdecode_mask.3 sysdecode_accessmode.3 \
	sysdecode_mask.3 sysdecode_atflags.3 \
	sysdecode_mask.3 sysdecode_capfcntlrights.3 \
	sysdecode_mask.3 sysdecode_fcntl_fileflags.3 \
	sysdecode_mask.3 sysdecode_fileflags.3 \
	sysdecode_mask.3 sysdecode_filemode.3 \
	sysdecode_mask.3 sysdecode_flock_operation.3 \
	sysdecode_mask.3 sysdecode_mlockall_flags.3 \
	sysdecode_mask.3 sysdecode_mmap_flags.3 \
	sysdecode_mask.3 sysdecode_mmap_prot.3 \
	sysdecode_mask.3 sysdecode_mount_flags.3 \
	sysdecode_mask.3 sysdecode_msg_flags.3 \
	sysdecode_mask.3 sysdecode_msync_flags.3 \
	sysdecode_mask.3 sysdecode_open_flags.3 \
	sysdecode_mask.3 sysdecode_pipe2_flags.3 \
	sysdecode_mask.3 sysdecode_reboot_howto.3 \
	sysdecode_mask.3 sysdecode_rfork_flags.3 \
	sysdecode_mask.3 sysdecode_semget_flags.3 \
	sysdecode_mask.3 sysdecode_sendfile_flags.3 \
	sysdecode_mask.3 sysdecode_shmat_flags.3 \
	sysdecode_mask.3 sysdecode_sctp_nxt_flags.3 \
	sysdecode_mask.3 sysdecode_sctp_rcv_flags.3 \
	sysdecode_mask.3 sysdecode_sctp_snd_flags.3 \
	sysdecode_mask.3 sysdecode_socket_type.3 \
	sysdecode_mask.3 sysdecode_thr_create_flags.3 \
	sysdecode_mask.3 sysdecode_umtx_cvwait_flags.3 \
	sysdecode_mask.3 sysdecode_umtx_rwlock_flags.3 \
	sysdecode_mask.3 sysdecode_vmprot.3 \
	sysdecode_mask.3 sysdecode_wait4_options.3 \
	sysdecode_mask.3 sysdecode_wait6_options.3

CLEANFILES= ioctl.c ioctl.c.tmp tables.h tables.h.tmp

.if defined(COMPAT_32BIT)
CPP+=	-m32
.endif

.if ${MK_PF} != "no"
CFLAGS+=-DPF
.endif

# Workaround duplicate declarations in <netinet/ip_compat.h>
CFLAGS.gcc.ioctl.c+= -Wno-redundant-decls

CFLAGS.gcc+=	${CFLAGS.gcc.${.IMPSRC}}

DEPENDOBJS+=	tables.h
tables.h: mktables
	sh ${.CURDIR}/mktables ${SYSROOT:U${DESTDIR}}${INCLUDEDIR} ${.TARGET}.tmp && \
	    mv -f ${.TARGET}.tmp ${.TARGET}

# mkioctls runs find(1) for headers so needs to rebuild every time.  This used
# to be a hack only done in buildworld.
.if !defined(_SKIP_BUILD)
ioctl.c.tmp: .PHONY
.endif
ioctl.c.tmp: mkioctls .META
	env CPP="${CPP}" MK_PF="${MK_PF}" \
	    /bin/sh ${.CURDIR}/mkioctls ${SYSROOT:U${DESTDIR}}${INCLUDEDIR} > ${.TARGET}

ioctl.c: ioctl.c.tmp
	if [ ! -e ${.TARGET} ] || ! cmp -s ${.TARGET} ${.TARGET}.tmp; then \
		mv -f ${.TARGET}.tmp ${.TARGET}; \
	fi

beforedepend: ioctl.c tables.h

.include <bsd.lib.mk>
