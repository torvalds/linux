#	$OpenBSD: Makefile,v 1.3 2023/01/27 16:39:22 miod Exp $

PROG = fpaccuracy
SRCS = Gamma.c INV.c Pix.c acos.c acosh.c asin.c asinh.c atan.c \
       atanh.c cos.c cosh.c erf.c erfc.c exp.c fpaccuracy.c j0.c \
       j1.c lgamma.c log.c log10.c pow2_x.c powx_275.c sin.c \
       sincos.c sincos2.c sinh.c sqrt.c tan.c tanh.c y0.c y1.c
LDADD = -lm
DPADD = ${LIBM}
CFLAGS+= -fno-builtin
CLEANFILES += fpaccuracy.out

.include <bsd.regress.mk>
