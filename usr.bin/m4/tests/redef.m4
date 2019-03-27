dnl $FreeBSD$
dnl $OpenBSD: src/regress/usr.bin/m4/redef.m4,v 1.2 2001/09/27 22:40:58 espie Exp $
dnl check all properties of builtin are passed on, including args behavior
define(`mybuiltin',defn(`builtin'))dnl
builtin mybuiltin
define(`mydefine',defn(`define'))dnl
mydefine(`mydefn',defn(`defn'))dnl
mydefine(`myundefine',mydefn(`undefine'))dnl
myundefine(`defn')dnl
myundefine(`define')dnl
myundefine(`undefine')dnl
mydefine(`mydef2',mydefn(`mydefine'))dnl
mydefine(`mydef', mydefn(`define'))dnl
myundefine(`mydefine')dnl
mydef2(`A',`B')dnl
mydef(`C',`D')dnl
A C
