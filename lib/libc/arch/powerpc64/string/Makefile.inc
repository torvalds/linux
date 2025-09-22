#	$OpenBSD: Makefile.inc,v 1.3 2020/06/29 15:30:58 kettenis Exp $

SRCS+=	bcopy.c memcpy.c memmove.c \
	strchr.c strrchr.c \
	bcmp.c bzero.c ffs.S memchr.c memcmp.c memset.c strcat.c \
	strcmp.c strcpy.c strcspn.c strlen.c strlcat.c strlcpy.c \
	strncat.c strncmp.c strncpy.c strpbrk.c strsep.c \
	strspn.c strstr.c swab.c
