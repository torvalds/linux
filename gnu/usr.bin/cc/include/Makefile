# $FreeBSD$

.include <bsd.own.mk>

.include "../Makefile.inc"

INCSDIR=${INCLUDEDIR}/gcc/${GCCVER}

.PATH: ${GCCDIR}/config/${GCC_CPU} ${SRCTOP}/contrib/llvm/tools/clang/lib/Headers

.if ${TARGET_ARCH} == "i386" || ${TARGET_ARCH} == "amd64"
INCS=	ammintrin.h emmintrin.h mmintrin.h mm3dnow.h pmmintrin.h \
	tmmintrin.h xmmintrin.h mm_malloc.h
INCS+=	wmmintrin.h __wmmintrin_aes.h __wmmintrin_pclmul.h
.elif ${TARGET_ARCH} == "arm"
INCS=	mmintrin.h
.elif ${TARGET_ARCH} == "powerpc" || ${TARGET_ARCH} == "powerpc64" || \
    ${TARGET_ARCH} == "powerpcspe"
INCS=	ppc-asm.h altivec.h spe.h
.endif

mm_malloc.h: pmm_malloc.h
	@rm -rf ${.TARGET}
	@cp ${.ALLSRC} ${.TARGET}
CLEANFILES+=	mm_malloc.h

.include <bsd.prog.mk>
