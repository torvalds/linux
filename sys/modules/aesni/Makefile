# $FreeBSD$

.PATH: ${SRCTOP}/sys/crypto/aesni
.PATH: ${SRCTOP}/contrib/llvm/tools/clang/lib/Headers

KMOD=	aesni
SRCS=	aesni.c
SRCS+=	aeskeys_${MACHINE_CPUARCH}.S
SRCS+=	device_if.h bus_if.h opt_bus.h cryptodev_if.h

OBJS+=	aesni_ghash.o aesni_wrap.o
OBJS+=	intel_sha1.o intel_sha256.o

# Remove -nostdinc so we can get the intrinsics.
aesni_ghash.o: aesni_ghash.c
	# XXX - gcc won't understand -mpclmul
	${CC} -c ${CFLAGS:C/^-O2$/-O3/:N-nostdinc} ${WERROR} ${PROF} \
	     -mmmx -msse -msse4 -maes -mpclmul ${.IMPSRC}
	${CTFCONVERT_CMD}

aesni_wrap.o: aesni_wrap.c
	${CC} -c ${CFLAGS:C/^-O2$/-O3/:N-nostdinc} ${WERROR} ${PROF} \
	     -mmmx -msse -msse4 -maes ${.IMPSRC}
	${CTFCONVERT_CMD}

intel_sha1.o: intel_sha1.c
	${CC} -c ${CFLAGS:C/^-O2$/-O3/:N-nostdinc} ${WERROR} ${PROF} \
	     -mmmx -msse -msse4 -msha ${.IMPSRC}
	${CTFCONVERT_CMD}

intel_sha256.o: intel_sha256.c
	${CC} -c ${CFLAGS:C/^-O2$/-O3/:N-nostdinc} ${WERROR} ${PROF} \
	     -mmmx -msse -msse4 -msha ${.IMPSRC}
	${CTFCONVERT_CMD}

aesni_ghash.o: aesni.h
aesni_wrap.o: aesni.h
intel_sha1.o: sha_sse.h immintrin.h shaintrin.h tmmintrin.h xmmintrin.h
intel_sha256.o: sha_sse.h immintrin.h shaintrin.h tmmintrin.h xmmintrin.h

.include <bsd.kmod.mk>

CWARNFLAGS.aesni_ghash.c=	${NO_WCAST_QUAL}
CWARNFLAGS.aesni_wrap.c=	${NO_WCAST_QUAL}
