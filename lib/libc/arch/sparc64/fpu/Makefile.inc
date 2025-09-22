#	$OpenBSD: Makefile.inc,v 1.2 2016/05/08 18:41:17 guenther Exp $
SRCS += fpu_add.c fpu_compare.c fpu_div.c fpu_explode.c fpu_implode.c \
	fpu_mul.c fpu_qp.c fpu_q.c fpu_sqrt.c fpu_subr.c
# unneeded, going way: fpu_reg.c

.PATH: ${.CURDIR}/arch/sparc64/fpu
