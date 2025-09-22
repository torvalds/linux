#	$OpenBSD: Makefile.inc,v 1.3 2014/11/30 19:43:56 deraadt Exp $

SRCS+=	bcopy.S memcpy.c memmove.c \
	bcmp.S bzero.S ffs.S strcmp.S strlen.S \
	strchr.S strrchr.S \
	memchr.c memcmp.c memset.c strcat.c strcpy.c strcspn.c \
	strlcat.c strlcpy.c strncat.c strncmp.c strncpy.c \
	strpbrk.c strsep.c strspn.c strstr.c swab.c
