#	$OpenBSD: Makefile.inc,v 1.8 2025/07/20 16:07:19 miod Exp $

SRCS+=	bcopy.S memcpy.S memmove.S \
	strchr.c strrchr.c \
	bcmp.c  bzero.c ffs.c memchr.c memcmp.c \
	memset.c strcat.c strcmp.c strcpy.c strcspn.c \
	strlen.c strlcat.c strlcpy.c strncat.c strncmp.c \
	strncpy.c strpbrk.c strsep.c strspn.c strstr.c swab.c
