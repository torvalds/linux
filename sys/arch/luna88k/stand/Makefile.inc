#	$OpenBSD: Makefile.inc,v 1.3 2023/02/24 23:36:10 aoyama Exp $
#	$NetBSD: Makefile.inc,v 1.1 2013/01/06 07:04:49 tsutsui Exp $

NOPIE=
BINDIR=		/usr/mdec
CFLAGS+=	-Wall -Werror -fno-stack-protector
CFLAGS+=	-mmemcpy
CFLAGS+=	-fno-builtin-printf -fno-builtin-putchar -fno-builtin-vprintf
