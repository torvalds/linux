# $OpenBSD: Makefile.inc,v 1.2 2020/06/26 20:16:21 naddy Exp $

SRCS+=	bcopy.c memcpy.c memmove.c \
	strchr.c strrchr.c \
	bcmp.c bzero.c ffs.S memchr.c memcmp.c memset.c \
	strcmp.c strncmp.c \
	strcat.c strcpy.c strcspn.c strlen.c strlcat.c strlcpy.c \
	strncat.c  strncpy.c strpbrk.c strsep.c \
	strspn.c swab.c strstr.c
