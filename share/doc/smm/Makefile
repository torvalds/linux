#	From: @(#)Makefile	8.1 (Berkeley) 6/10/93
# $FreeBSD$

.include <src.opts.mk>

# The following modules do not describe FreeBSD:
# 14.uucpimpl, 15.uucpnet

# The following modules do not build/install:
# 13.amd (documentation is TeXinfo)
# 16.security 17.password (encumbered)

SUBDIR=	title \
	contents \
	01.setup \
	02.config \
	03.fsck \
	04.quotas \
	05.fastfs \
	06.nfs \
	${_07.lpd} \
	${_08.sendmailop} \
	11.timedop \
	12.timed \
	18.net

.if ${MK_SENDMAIL} != "no"
_08.sendmailop=	08.sendmailop 
.endif

.if ${MK_LPR} != "no"
_07.lpd=	07.lpd
.endif

SUBDIR_PARALLEL=

.include <bsd.subdir.mk>
