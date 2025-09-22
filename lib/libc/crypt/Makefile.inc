#	$OpenBSD: Makefile.inc,v 1.27 2016/03/30 06:38:41 jmc Exp $

.PATH: ${LIBCSRCDIR}/arch/${MACHINE_CPU}/crypt ${LIBCSRCDIR}/crypt

SRCS+=	crypt.c cryptutil.c arc4random.c arc4random_uniform.c \
	blowfish.c bcrypt.c

MAN+=	crypt.3 crypt_checkpass.3 blowfish.3 arc4random.3
