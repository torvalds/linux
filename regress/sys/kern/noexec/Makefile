#	$OpenBSD: Makefile,v 1.27 2022/11/27 15:13:15 anton Exp $

PROG=		noexec
SRCS=		noexec.c testfly.S
CFLAGS=		-Wall -fno-inline
LDFLAGS=	-pthread
LDSTATIC=	${STATIC}

REGRESS_TARGETS+=			nxtext-mmap	nxtext-mprotect
REGRESS_TARGETS+=	nxmmap				nxmmap-mprotect
REGRESS_TARGETS+=	nxdata		nxdata-mmap	nxdata-mprotect
REGRESS_TARGETS+=	nxbss		nxbss-mmap	nxbss-mprotect
REGRESS_TARGETS+=	nxheap		nxheap-mmap	nxheap-mprotect
REGRESS_TARGETS+=	nxstack		nxstack-mmap	nxstack-mprotect
REGRESS_TARGETS+=	nxmutable	nxmutable-mmap	nxmutable-mprotect

nxtext-mmap: ${PROG}
	./${PROG} -T -m

nxtext-mprotect: ${PROG}
	./${PROG} -T -p

nxdata: ${PROG}
	./${PROG} -D

nxdata-mprotect: ${PROG}
	./${PROG} -D -p

nxdata-mmap: ${PROG}
	./${PROG} -D -m

nxbss: ${PROG}
	./${PROG} -B

nxbss-mmap: ${PROG}
	./${PROG} -B -m

nxbss-mprotect: ${PROG}
	./${PROG} -B -p

nxheap: ${PROG}
	./${PROG} -H

nxheap-mmap: ${PROG}
	./${PROG} -H -m

nxheap-mprotect: ${PROG}
	./${PROG} -H -p

nxstack: ${PROG}
	./${PROG} -S

nxstack-mmap: ${PROG}
	./${PROG} -S -m

nxstack-mprotect: ${PROG}
	./${PROG} -S -p

nxmmap: ${PROG}
	./${PROG} -m

nxmmap-mprotect: ${PROG}
	./${PROG} -m -p

nxmutable: ${PROG}
	./${PROG} -M

nxmutable-mmap: ${PROG}
	./${PROG} -M -m

nxmutable-mprotect: ${PROG}
	./${PROG} -M -p

.if ${MACHINE} != "alpha" && \
    ${MACHINE} != "amd64" && \
    ${MACHINE} != "arm64" && \
    ${MACHINE} != "armv7" && \
    ${MACHINE} != "hppa" && \
    ${MACHINE} != "i386" && \
    ${MACHINE} != "octeon" && \
    ${MACHINE} != "powerpc64" && \
    ${MACHINE} != "riscv64" && \
    ${MACHINE} != "sparc64"
REGRESS_TARGETS=run-regress-skiparch
run-regress-skiparch:
	# this architecture has no test for this
	@echo SKIPPED
.endif

.include <bsd.regress.mk>
