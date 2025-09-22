#	$OpenBSD: Makefile.inc,v 1.9 2015/05/15 22:29:37 millert Exp $

SRCS+=	bcopy.S memcpy.c memmove.S \
	strchr.c strrchr.c \
	bzero.S ffs.S \
	memchr.c memcmp.c memset.c strcat.c bcmp.c \
	strcmp.c strcpy.c strcspn.c strlen.c strlcat.c strlcpy.c \
	strncat.c strncmp.c strncpy.c strpbrk.c strsep.c \
	strspn.c strstr.c swab.c
