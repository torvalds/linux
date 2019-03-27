# $FreeBSD$

.include <src.opts.mk>

SUBDIR=
.if ${MK_OPENSSL} != "no"
SUBDIR+=openssl
.if ${MK_OPENSSH} != "no"
SUBDIR+=scp sftp ssh ssh-add ssh-agent ssh-keygen ssh-keyscan
.endif
.endif

SUBDIR.${MK_TESTS}+= tests

SUBDIR_PARALLEL=

.include <bsd.subdir.mk>
