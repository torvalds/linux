#	$OpenBSD: Makefile.inc,v 1.5 2015/05/15 22:29:37 millert Exp $

SRCS+=	bcopy.S memcpy.c memmove.S \
	bzero.S ffs.S memset.S \
	strchr.c strrchr.c \
	bcmp.c memcmp.c memchr.c strcat.c strcmp.c strcpy.c strcspn.c \
	strlen.c strlcat.c strlcpy.c strncat.c strncmp.c strncpy.c \
	strpbrk.c strsep.c strspn.c strstr.c swab.c
