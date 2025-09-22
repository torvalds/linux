#	$OpenBSD: Makefile.inc,v 1.10 2015/05/15 22:29:37 millert Exp $

SRCS+=	memmove.S memcpy.c \
	strchr.c strrchr.c \
	bcmp.c bzero.c ffs.c memchr.c memcmp.c \
	memset.c strcat.c strcmp.c strcpy.c strcspn.c strlen.c \
	strlcat.c strlcpy.c strncat.c strncmp.c strncpy.c \
	strpbrk.c strsep.c strspn.c strstr.c swab.c
