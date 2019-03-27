# $FreeBSD$

.PATH: ${SRCTOP}/sys/amd64/sgx

KMOD=	sgx
SRCS=	sgx.c sgxvar.h sgx_support.S
SRCS+=	opt_hwpmc_hooks.h opt_kstack_pages.h
DPSRCS=	assym.inc

sgx_support.o:  assym.inc
	${CC} -c -x assembler-with-cpp -DLOCORE ${CFLAGS} \
	    ${.IMPSRC} -o ${.TARGET}

.include <bsd.kmod.mk>
