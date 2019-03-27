# $FreeBSD$

# this makefile gets hooked into everyone's dependencies so that
# distrib-dirs can be done in STAGE_OBJTOP
all:

# mtree makes a lot of noise if we are not root, 
# we don't need to see it.
stage-distrib-dirs: .META ${META_DEPS}
	mkdir -p ${STAGE_OBJTOP}
	INSTALL="sh ${SRCTOP}/tools/install.sh" ${.MAKE} -C ${SRCTOP}/etc \
	    distrib-dirs -DNO_FSCHG -DWITH_TESTS DESTDIR=${STAGE_OBJTOP}

.include <bsd.prog.mk>

.if ${.MAKE.LEVEL} > 0 && ${MK_STAGING} == "yes"
all: stage-distrib-dirs
.endif
