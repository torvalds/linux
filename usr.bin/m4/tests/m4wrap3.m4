dnl $FreeBSD$
dnl $OpenBSD: src/regress/usr.bin/m4/m4wrap3.m4,v 1.1 2005/03/02 10:12:41 espie Exp $
dnl Another test, this time for multiple wrappers
dnl Check the behavior in presence of recursive m4wraps
dnl both for POSIX m4 and for gnu-m4 mode
m4wrap(`this is
')dnl
m4wrap(`a string
')dnl
m4wrap(`m4wrap(`recurse
')')dnl
normal m4 stuff
