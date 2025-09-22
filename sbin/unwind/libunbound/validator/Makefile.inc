#	$OpenBSD: Makefile.inc,v 1.1 2019/01/23 13:05:27 florian Exp $

.PATH: ${.CURDIR}/libunbound/validator

SRCS+=	autotrust.c val_anchor.c val_kcache.c val_kentry.c val_neg.c \
	val_nsec.c val_nsec3.c val_secalgo.c val_sigcrypt.c val_utils.c \
	validator.c
