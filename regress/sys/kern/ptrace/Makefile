#	$OpenBSD: Makefile,v 1.4 2024/11/27 05:27:21 anton Exp $

SUBDIR+=	xstate

PROG=	ptrace

read_i:
	${.OBJDIR}/${PROG} -ri
read_d:
	${.OBJDIR}/${PROG} -rd
read_i_bad:
	${.OBJDIR}/${PROG} -rib
read_d_bad:
	${.OBJDIR}/${PROG} -rdb

io_read_i:
	${.OBJDIR}/${PROG} -Iri
io_read_d:
	${.OBJDIR}/${PROG} -Ird
io_read_i_bad:
	${.OBJDIR}/${PROG} -Irib
io_read_d_bad:
	${.OBJDIR}/${PROG} -Irdb

REGRESS_TARGETS=read_i read_d read_i_bad read_d_bad \
	io_read_i io_read_d io_read_i_bad io_read_d_bad

${REGRESS_TARGETS}: ${PROG}

.include <bsd.regress.mk>
