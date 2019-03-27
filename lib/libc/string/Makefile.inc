#	@(#)Makefile.inc	8.1 (Berkeley) 6/4/93
# $FreeBSD$

.PATH: ${LIBC_SRCTOP}/${LIBC_ARCH}/string ${LIBC_SRCTOP}/string
.PATH: ${SRCTOP}/sys/libkern

CFLAGS+= -I${LIBC_SRCTOP}/locale

# machine-independent string sources
MISRCS+=bcmp.c bcopy.c bzero.c explicit_bzero.c \
	ffs.c ffsl.c ffsll.c fls.c flsl.c flsll.c \
	memccpy.c memchr.c memrchr.c memcmp.c \
	memcpy.c memmem.c memmove.c memset.c memset_s.c \
	stpcpy.c stpncpy.c strcasecmp.c \
	strcat.c strcasestr.c strchr.c strchrnul.c strcmp.c strcoll.c strcpy.c\
	strcspn.c strdup.c strerror.c strlcat.c strlcpy.c strlen.c strmode.c \
	strncat.c strncmp.c strncpy.c strndup.c strnlen.c strnstr.c \
	strpbrk.c strrchr.c strsep.c strsignal.c strspn.c strstr.c strtok.c \
	strxfrm.c swab.c \
	timingsafe_bcmp.c \
	timingsafe_memcmp.c \
	wcpcpy.c wcpncpy.c wcscasecmp.c wcscat.c \
	wcschr.c wcscmp.c wcscoll.c wcscpy.c wcscspn.c wcsdup.c \
	wcslcat.c wcslcpy.c wcslen.c wcsncasecmp.c wcsncat.c wcsncmp.c \
	wcsncpy.c wcsnlen.c wcspbrk.c \
	wcsrchr.c wcsspn.c wcsstr.c wcstok.c wcswidth.c wcsxfrm.c wmemchr.c \
	wmemcmp.c \
	wmemcpy.c wmemmove.c wmemset.c

SYM_MAPS+=	${LIBC_SRCTOP}/string/Symbol.map


# machine-dependent string sources
.sinclude "${LIBC_SRCTOP}/${LIBC_ARCH}/string/Makefile.inc"

MAN+=	bcmp.3 bcopy.3 bstring.3 bzero.3 ffs.3 index.3 memccpy.3 memchr.3 \
	memcmp.3 memcpy.3 memmem.3 memmove.3 memset.3 strcasecmp.3 strcat.3 \
	strchr.3 strcmp.3 strcoll.3 strcpy.3 strdup.3 strerror.3 \
	string.3 strlcpy.3 strlen.3 strmode.3 strpbrk.3 strsep.3 \
	strspn.3 strstr.3 strtok.3 strxfrm.3 swab.3 \
	timingsafe_bcmp.3 \
	wcscoll.3 wcstok.3 \
	wcswidth.3 wcsxfrm.3 wmemchr.3

MLINKS+=bzero.3 explicit_bzero.3
MLINKS+=ffs.3 ffsl.3 \
	ffs.3 ffsll.3 \
	ffs.3 fls.3 \
	ffs.3 flsl.3 \
	ffs.3 flsll.3
MLINKS+=index.3 rindex.3
MLINKS+=memchr.3 memrchr.3
MLINKS+=memset.3 memset_s.3
MLINKS+=strcasecmp.3 strncasecmp.3 \
	strcasecmp.3 strcasecmp_l.3 \
	strcasecmp.3 strncasecmp_l.3
MLINKS+=strcat.3 strncat.3
MLINKS+=strchr.3 strrchr.3 \
	strchr.3 strchrnul.3
MLINKS+=strcmp.3 strncmp.3
MLINKS+=strcoll.3 strcoll_l.3
MLINKS+=strcpy.3 stpcpy.3 \
	strcpy.3 stpncpy.3 \
	strcpy.3 strncpy.3
MLINKS+=strdup.3 strndup.3
MLINKS+=strerror.3 perror.3 \
	strerror.3 strerror_r.3 \
	strerror.3 sys_errlist.3 \
	strerror.3 sys_nerr.3
MLINKS+=strlcpy.3 strlcat.3
MLINKS+=strlen.3 strnlen.3
MLINKS+=strspn.3 strcspn.3
MLINKS+=strstr.3 strcasestr.3 \
	strstr.3 strnstr.3 \
	strstr.3 strcasestr_l.3
MLINKS+=strtok.3 strtok_r.3
MLINKS+=strxfrm.3 strxfrm_l.3
MLINKS+=timingsafe_bcmp.3 timingsafe_memcmp.3
MLINKS+=wmemchr.3 wcpcpy.3 \
	wmemchr.3 wcpncpy.3 \
	wmemchr.3 wcscasecmp.3 \
	wmemchr.3 wcscat.3 \
	wmemchr.3 wcschr.3 \
	wmemchr.3 wcscmp.3 \
	wmemchr.3 wcscpy.3 \
	wmemchr.3 wcscspn.3 \
	wmemchr.3 wcsdup.3 \
	wmemchr.3 wcslcat.3 \
	wmemchr.3 wcslcpy.3 \
	wmemchr.3 wcslen.3 \
	wmemchr.3 wcsncasecmp.3 \
	wmemchr.3 wcsncat.3 \
	wmemchr.3 wcsncmp.3 \
	wmemchr.3 wcsncpy.3 \
	wmemchr.3 wcsnlen.3 \
	wmemchr.3 wcspbrk.3 \
	wmemchr.3 wcsrchr.3 \
	wmemchr.3 wcsspn.3 \
	wmemchr.3 wcsstr.3 \
	wmemchr.3 wmemcmp.3 \
	wmemchr.3 wmemcpy.3 \
	wmemchr.3 wmemmove.3 \
	wmemchr.3 wmemset.3
