dnl $FreeBSD$
dnl $OpenBSD: src/regress/usr.bin/m4/gnueval.m4,v 1.1 2012/04/12 16:58:15 espie Exp $
dnl exponentiation is right associative
eval(`4**2**3')
dnl priority between unary operators and *
eval(`4**2*3')
eval(`-4**3')
