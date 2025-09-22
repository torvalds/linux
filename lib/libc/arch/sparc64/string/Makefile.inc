#	$OpenBSD: Makefile.inc,v 1.5 2015/05/15 22:29:37 millert Exp $

SRCS+=	bcopy.c memcpy.c memmove.c \
	ffs.S \
	strlen.S \
	bcmp.c bzero.c memchr.c memcmp.c \
	memset.c strcat.c strchr.c \
	strcmp.c strcpy.c strcspn.c strlcat.c strlcpy.c \
	strncat.c strncmp.c strncpy.c strpbrk.c strrchr.c strsep.c \
	strspn.c strstr.c swab.c
