# $FreeBSD$

.include "${.CURDIR}/Makefile.inc"

CONFS=	atrun
CONFSDIR=	/etc/cron.d
CONFSNAME=	at
PROG=	at
SRCS=	at.c panic.c parsetime.c perm.c
LINKS=	${BINDIR}/at ${BINDIR}/atq \
	${BINDIR}/at ${BINDIR}/atrm \
	${BINDIR}/at ${BINDIR}/batch
MLINKS=	at.1 batch.1 \
	at.1 atq.1 \
	at.1 atrm.1

NO_WFORMAT=

BINOWN=	root
BINMODE= 4555
CLEANFILES+= at.1

at.1: at.man
	@${ECHO} Making ${.TARGET:T} from ${.ALLSRC:T}; \
	sed -e \
		"s@_ATSPOOL_DIR@$(ATSPOOL_DIR)@g; \
		s@_ATJOB_DIR@$(ATJOB_DIR)@g; \
		s@_DEFAULT_BATCH_QUEUE@$(DEFAULT_BATCH_QUEUE)@g; \
		s@_DEFAULT_AT_QUEUE@$(DEFAULT_AT_QUEUE)@g; \
		s@_LOADAVG_MX@$(LOADAVG_MX)@g; \
		s@_PERM_PATH@$(PERM_PATH)@g; \
		s@_LOCKFILE@$(LOCKFILE)@g" \
		< ${.ALLSRC} > ${.TARGET}

.include <bsd.prog.mk>
