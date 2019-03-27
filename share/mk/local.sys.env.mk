# $FreeBSD$

# This makefile is for customizations that should be done early

.if !defined(_TARGETS)
# some things we do only once
_TARGETS:= ${.TARGETS}
.export _TARGETS
.endif

# some handy macros
_this = ${.PARSEDIR:tA}/${.PARSEFILE}
# some useful modifiers

# A useful trick for testing multiple :M's against something
# :L says to use the variable's name as its value - ie. literal
# got = ${clean* destroy:${M_ListToMatch:S,V,.TARGETS,}}
M_ListToMatch = L:@m@$${V:M$$m}@
# match against our initial targets (see above)
M_L_TARGETS = ${M_ListToMatch:S,V,_TARGETS,}

# turn a list into a set of :N modifiers
# NskipFoo = ${Foo:${M_ListToSkip}}
M_ListToSkip= O:u:ts::S,:,:N,g:S,^,N,

# type should be a builtin in any sh since about 1980,
# AUTOCONF := ${autoconf:L:${M_whence}}
M_type = @x@(type $$x 2> /dev/null); echo;@:sh:[0]:N* found*:[@]:C,[()],,g
M_whence = ${M_type}:M/*:[1]

# convert a path to a valid shell variable
M_P2V = tu:C,[./-],_,g

# these are handy
# we can use this for a cheap timestamp at the start of a target's script,
# but not at the end - since make will expand both at the same time.
TIME_STAMP_FMT = @ %s [%Y-%m-%d %T]
TIME_STAMP = ${TIME_STAMP_FMT:localtime}
# this will produce the same output but as of when date(1) is run.
TIME_STAMP_DATE = `date '+${TIME_STAMP_FMT}'`
TIME_STAMP_END?= ${TIME_STAMP_DATE}

# Simplify auto.obj.mk mkdir -p handling and avoid unneeded/redundant
# error spam and show a proper error.
Mkdirs= Mkdirs() { mkdir -p $$* || :; }

.if !empty(.MAKEFLAGS:M-s)
ECHO_TRACE?=	true
.endif

.include "src.sys.env.mk"
