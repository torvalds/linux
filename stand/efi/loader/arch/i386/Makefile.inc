# $FreeBSD$

SRCS+=	start.S \
	elf32_freebsd.c \
	exec.c

.PATH:	${BOOTSRC}/i386/libi386
SRCS+=	nullconsole.c \
	comconsole.c \
	spinconsole.c

CFLAGS+=	-fPIC -DTERM_EMU
LDFLAGS+=	-Wl,-znocombreloc
