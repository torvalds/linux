# $FreeBSD$

PROG=	unix_cmsg
SRCS=	${AUTOSRCS} unix_cmsg.c uc_common.h uc_common.c \
	t_generic.h t_generic.c t_peercred.h t_peercred.c \
	t_cmsgcred.h t_cmsgcred.c t_sockcred.h t_sockcred.c \
	t_cmsgcred_sockcred.h t_cmsgcred_sockcred.c t_cmsg_len.h t_cmsg_len.c \
	uc_check_time.h uc_check_time.c
CLEANFILES+=	${AUTOSRCS}
MAN=
CFLAGS+=	-I${.CURDIR} -I${.OBJDIR}
WARNS?=	3

REXP_bintime= 's|%%TTYPE%%|bintime|g ; s|%%DTYPE%%|bintime|g ; \
    s|%%SCM_TTYPE%%|SCM_BINTIME|g ; \
    s|%%MAJ_MEMB%%|sec|g ; s|%%MIN_MEMB%%|frac|g'
REXP_timeval= 's|%%TTYPE%%|timeval|g ; s|%%DTYPE%%|timeval|g ; \
    s|%%SCM_TTYPE%%|SCM_TIMESTAMP|g ; \
    s|%%MAJ_MEMB%%|tv_sec|g ; s|%%MIN_MEMB%%|tv_usec|g'
REXP_timespec_real= 's|%%TTYPE%%|timespec_real|g ; s|%%DTYPE%%|timespec|g ; \
    s|%%SCM_TTYPE%%|SCM_REALTIME|g ; \
    s|%%MAJ_MEMB%%|tv_sec|g ; s|%%MIN_MEMB%%|tv_nsec|g'
REXP_timespec_mono= 's|%%TTYPE%%|timespec_mono|g ; s|%%DTYPE%%|timespec|g ; \
    s|%%SCM_TTYPE%%|SCM_MONOTONIC|g ; \
    s|%%MAJ_MEMB%%|tv_sec|g ; s|%%MIN_MEMB%%|tv_nsec|g'

.for ttype in bintime timeval timespec_real timespec_mono
AUTOSRCS+=	t_${ttype}.h t_${ttype}.c

t_${ttype}.o: t_${ttype}.c t_${ttype}.h

t_${ttype}.c: t_xxxtime.c.in
	sed ${REXP_${ttype}} < ${.ALLSRC} > ${.TARGET}

t_${ttype}.h: t_xxxtime.h.in
	sed ${REXP_${ttype}} < ${.ALLSRC} > ${.TARGET}
.endfor

.include <bsd.prog.mk>
