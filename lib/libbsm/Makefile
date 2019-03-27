#
# $FreeBSD$
#

PACKAGE=	lib${LIB}
OPENBSMDIR=		${SRCTOP}/contrib/openbsm
_LIBBSMDIR=		${OPENBSMDIR}/libbsm

LIB=		bsm
SHLIB_MAJOR=	3

.PATH:		${_LIBBSMDIR}
.PATH:		${OPENBSMDIR}/bsm
.PATH:		${OPENBSMDIR}/man

SRCS=	bsm_audit.c							\
	bsm_class.c							\
	bsm_control.c							\
	bsm_domain.c							\
	bsm_errno.c							\
	bsm_event.c							\
	bsm_fcntl.c							\
	bsm_flags.c							\
	bsm_io.c							\
	bsm_mask.c							\
	bsm_notify.c							\
	bsm_socket_type.c						\
	bsm_token.c							\
	bsm_user.c							\
	bsm_wrappers.c

#
# Must use BSM include files from within the contrib area, not the system.
#
CFLAGS+=	-I${OPENBSMDIR} -I${_LIBBSMDIR}

WARNS?=		1

INCS=		audit_uevents.h libbsm.h
INCSDIR=	${INCLUDEDIR}/bsm

MAN=	libbsm.3							\
	au_class.3							\
	au_control.3							\
	au_domain.3							\
	au_errno.3							\
	au_event.3							\
	au_fcntl_cmd.3							\
	au_free_token.3							\
	au_io.3								\
	au_mask.3							\
	au_notify.3							\
	au_open.3							\
	au_socket_type.3						\
	au_token.3							\
	au_user.3							\
	audit_submit.3

#
# It seems like maybe some of these should be installed separately, since
# they're not all libbsm parts.
#
MAN+=	audit.2								\
	audit.log.5							\
	audit_class.5							\
	audit_control.5							\
	audit_event.5							\
	audit_user.5							\
	audit_warn.5							\
	auditctl.2							\
	auditon.2							\
	getaudit.2							\
	getauid.2							\
	setaudit.2							\
	setauid.2

MLINKS=	libbsm.3 bsm.3							\
	au_class.3 getauclassent.3					\
	au_class.3 getauclassent_r.3					\
	au_class.3 getauclassnam.3					\
	au_class.3 getauclassnam_3.3					\
	au_class.3 setauclass.3						\
	au_class.3 endauclass.3						\
	au_control.3 setac.3						\
	au_control.3 endac.3						\
	au_control.3 getacdir.3						\
	au_control.3 getacmin.3						\
	au_control.3 getacfilesz.3					\
	au_control.3 getacflg.3						\
	au_control.3 getacna.3						\
	au_control.3 getacpol.3						\
	au_control.3 getacqsize.3					\
	au_control.3 au_poltostr.3					\
	au_control.3 au_strtopol.3					\
	au_domain.3 au_bsm_to_domain.3					\
	au_domain.3 au_domain_to_bsm.3					\
	au_errno.3 au_bsm_to_errno.3					\
	au_errno.3 au_errno_to_bsm.3					\
	au_errno.3 au_strerror.3					\
	au_event.3 setauevent.3						\
	au_event.3 endauevent.3						\
	au_event.3 getauevent.3						\
	au_event.3 getauevent_r.3					\
	au_event.3 getauevnam.3						\
	au_event.3 getauevnam_r.3					\
	au_event.3 getauevnum.3						\
	au_event.3 getauevnum_r.3					\
	au_event.3 getauevnonam.3					\
	au_event.3 getauevnonam_r.3					\
	au_fcntl_cmd.3 au_bsm_to_fcntl_cmd.3				\
	au_fcntl_cmd.3 au_fcntl_cmd_t_bsm.3				\
	au_io.3	au_fetch_tok.3						\
	au_io.3	au_print_tok.3						\
	au_io.3	au_read_rec.3						\
	au_mask.3 au_preselect.3					\
	au_mask.3 getauditflagsbin.3					\
	au_mask.3 getauditflagschar.3					\
	au_notify.3 au_get_state.3					\
	au_notify.3 au_notify_initialize.3				\
	au_notify.3 au_notify_terminate.3				\
	au_open.3 au_close.3						\
	au_open.3 au_close_buffer.3					\
	au_open.3 au_close_token.3					\
	au_open.3 au_write.3						\
	au_socket_type.3 au_bsm_to_socket_type.3			\
	au_socket_type.3 au_socket_type_to_bsm.3			\
	au_token.3 au_to_arg32.3					\
	au_token.3 au_to_arg64.3					\
	au_token.3 au_to_arg.3						\
	au_token.3 au_to_attr64.3					\
	au_token.3 au_to_data.3						\
	au_token.3 au_to_exit.3						\
	au_token.3 au_to_groups.3					\
	au_token.3 au_to_newgroups.3					\
	au_token.3 au_to_in_addr.3					\
	au_token.3 au_to_in_addr_ex.3					\
	au_token.3 au_to_ip.3						\
	au_token.3 au_to_ipc.3						\
	au_token.3 au_to_ipc_perm.3					\
	au_token.3 au_to_iport.3					\
	au_token.3 au_to_opaque.3					\
	au_token.3 au_to_file.3						\
	au_token.3 au_to_text.3						\
	au_token.3 au_to_path.3						\
	au_token.3 au_to_process32.3					\
	au_token.3 au_to_process64.3					\
	au_token.3 au_to_process.3					\
	au_token.3 au_to_process32_ex.3					\
	au_token.3 au_to_process64_ex.3					\
	au_token.3 au_to_process_ex.3					\
	au_token.3 au_to_return32.3					\
	au_token.3 au_to_return64.3					\
	au_token.3 au_to_return.3					\
	au_token.3 au_to_seq.3						\
	au_token.3 au_to_sock_inet32.3					\
	au_token.3 au_to_sock_inet128.3					\
	au_token.3 au_to_sock_inet.3					\
	au_token.3 au_to_subject32.3					\
	au_token.3 au_to_subject64.3					\
	au_token.3 au_to_subject.3					\
	au_token.3 au_to_subject32_ex.3					\
	au_token.3 au_to_subject64_ex.3					\
	au_token.3 au_to_subject_ex.3					\
	au_token.3 au_to_me.3						\
	au_token.3 au_to_exec_args.3					\
	au_token.3 au_to_exec_env.3					\
	au_token.3 au_to_header.3					\
	au_token.3 au_to_header32.3					\
	au_token.3 au_to_header64.3					\
	au_token.3 au_to_trailer.3					\
	au_token.3 au_to_zonename.3					\
	au_user.3 setauuser.3						\
	au_user.3 endauuser.3						\
	au_user.3 getauuserent.3					\
	au_user.3 getauuserent_r.3					\
	au_user.3 getauusernam.3					\
	au_user.3 getauusernam_R.3					\
	au_user.3 au_user_mask.3					\
	au_user.3 getfauditflags.3					\
	getaudit.2 getaudit_addr.2					\
	setaudit.2 setaudit_addr.2

.include <bsd.lib.mk>

# Disable -Wcast-align.  Casting sa_local in au_to_socket_ex triggers this
# warning, but it's ok because sa_local must've originally pointed to a
# sockaddr_in or sockaddr_in6 anyway.
# Better would be to disable this warning in just that one function, but GCC
# 4.2 can't do that :( .
CWARNFLAGS.bsm_token.c+=	-Wno-cast-align
