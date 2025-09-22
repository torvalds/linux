#	$OpenBSD: Makefile.inc,v 1.7 2020/06/26 20:16:21 naddy Exp $

SRCS+=	memcpy.c memmove.S \
	strchr.c strrchr.c \
	bcmp.c bzero.c ffs.S memchr.c memcmp.c memset.c strcat.c \
	strcmp.c strcpy.c strcspn.c strlen.c strlcat.c strlcpy.c \
	strncat.c strncmp.c strncpy.c strpbrk.c strsep.c \
	strspn.c strstr.c swab.c
