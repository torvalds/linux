#	$OpenBSD: Makefile.inc,v 1.9 2014/12/11 20:41:08 tedu Exp $

SRCS+=	memcpy.c memmove.S \
	strchr.S strrchr.S \
	bcmp.S bzero.S ffs.S memchr.S memcmp.S swab.c \
	memset.S strcat.S strcmp.S strcpy.S \
	strcspn.c strlen.c strlcat.c strlcpy.c \
	strncat.c strncmp.S strncpy.c strpbrk.c strsep.c \
	strspn.c strstr.c
