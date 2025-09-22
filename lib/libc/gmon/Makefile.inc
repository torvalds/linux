#	$OpenBSD: Makefile.inc,v 1.7 2019/12/20 16:54:19 tb Exp $

# gmon sources
.PATH: ${LIBCSRCDIR}/gmon

SRCS+=	gmon.c mcount.c
MAN+=	moncontrol.3

# mcount cannot be compiled with profiling or pie
mcount.po:
	@echo "${COMPILE.c} ${NOPIE_FLAGS} ${.IMPSRC} -o ${.TARGET}"
	@${COMPILE.c} ${DFLAGS} ${NOPIE_FLAGS} ${.IMPSRC} -o ${.TARGET}.o
	@-mv $@.d $*.d
	@${LD} -X -r ${.TARGET}.o -o ${.TARGET}
	@rm -f ${.TARGET}.o
