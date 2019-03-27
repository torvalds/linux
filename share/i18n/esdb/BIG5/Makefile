# $FreeBSD$
# $NetBSD: Makefile.inc,v 1.3 2008/10/25 22:35:36 apb Exp $

CODE=	Big5
ESUBDIR= BIG5
NO_PREPROC= yes

.include "../Makefile.part"

.for i in ${PART:S/:/@/}
Big5_$i_variable!= sed \
	'/^\#/d;/^ *$$/d;/^${i:S/@/:/}[ 	]/!d;s/^[^ 	]*[ 	]*//' \
	${.CURDIR}/Big5.variable
.endfor
.for i in ${PART}
# XXX: FIXME
Big5-${i:S/:/@/}.src: Big5.src Big5.variable
	sed -e 's/encoding/Big5-$i/' \
		-e 's/variable/${Big5_$i_variable}/' \
		${.CURDIR}/Big5.src > $@
	@echo ${.TARGET} >>.tmpfiles
.endfor
