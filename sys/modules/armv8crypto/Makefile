# $FreeBSD$

.PATH: ${SRCTOP}/sys/crypto/armv8

KMOD=	armv8crypto
SRCS=	armv8_crypto.c
SRCS+=	device_if.h bus_if.h opt_bus.h cryptodev_if.h

OBJS+=	armv8_crypto_wrap.o

# Remove -nostdinc so we can get the intrinsics.
armv8_crypto_wrap.o: armv8_crypto_wrap.c
	${CC} -c ${CFLAGS:C/^-O2$/-O3/:N-nostdinc:N-mgeneral-regs-only} \
	    -I${SRCTOP}/sys/crypto/armv8 \
	    ${WERROR} ${PROF} \
	     -march=armv8-a+crypto ${.IMPSRC}
	${CTFCONVERT_CMD}

armv8_crypto_wrap.o: armv8_crypto.h

.include <bsd.kmod.mk>
