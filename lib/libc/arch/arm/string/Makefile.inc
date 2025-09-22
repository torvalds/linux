#	$OpenBSD: Makefile.inc,v 1.9 2016/04/28 08:38:20 jsg Exp $

SRCS+=	bcopy.S memcpy.c memmove.S _memcpy.S \
	strchr.c strrchr.c \
	bcmp.c bzero.S ffs.S memchr.c memcmp.S memset.S \
	strcmp.S strncmp.S \
	strcat.c strcpy.c strcspn.c strlen.c strlcat.c strlcpy.c \
	strncat.c  strncpy.c strpbrk.c strsep.c \
	strspn.c swab.c strstr.c

