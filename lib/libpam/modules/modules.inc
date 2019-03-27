# $FreeBSD$

.include <src.opts.mk>

MODULES		 =
MODULES		+= pam_chroot
MODULES		+= pam_deny
MODULES		+= pam_echo
MODULES		+= pam_exec
MODULES		+= pam_ftpusers
MODULES		+= pam_group
MODULES		+= pam_guest
.if ${MK_KERBEROS} != "no"
MODULES		+= pam_krb5
MODULES		+= pam_ksu
.endif
MODULES		+= pam_lastlog
MODULES		+= pam_login_access
MODULES		+= pam_nologin
MODULES		+= pam_opie
MODULES		+= pam_opieaccess
MODULES		+= pam_passwdqc
MODULES		+= pam_permit
.if ${MK_RADIUS_SUPPORT} != "no"
MODULES		+= pam_radius
.endif
MODULES		+= pam_rhosts
MODULES		+= pam_rootok
MODULES		+= pam_securetty
MODULES		+= pam_self
.if ${MK_OPENSSH} != "no"
MODULES		+= pam_ssh
.endif
MODULES		+= pam_tacplus
MODULES		+= pam_unix
