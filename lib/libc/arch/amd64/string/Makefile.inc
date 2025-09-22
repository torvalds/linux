#	$OpenBSD: Makefile.inc,v 1.8 2014/12/09 15:10:39 reyk Exp $

SRCS+=	memcpy.c memmove.S \
	strchr.S strrchr.S \
	bcmp.S bzero.S ffs.S memchr.S \
	memset.S strcat.c strcmp.S \
	memcmp.c strcpy.c strcspn.c strlen.S strlcat.c \
	strlcpy.c strncat.c strncmp.c strncpy.c strpbrk.c \
	strsep.c strspn.c strstr.c swab.c
