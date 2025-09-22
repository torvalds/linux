#
#	$OpenBSD: Makefile,v 1.46 2022/05/23 16:58:11 krw Exp $
#
# Copyright (c) 1997 Tobias Weingartner
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

PROG=	fdisk
SRCS=	fdisk.c user.c misc.c disk.c mbr.c part.c cmd.c manual.c gpt.c
DPADD=	${LIBUTIL}
LDADD=	-lutil
CLEANFILES += fdisk.cat8 manual.c

.include <bsd.own.mk>

.ifdef NOMAN
manual.c:
	(echo 'const unsigned char manpage[] = {'; \
	echo 'no manual' | gzip -9c | hexdump -ve '"0x" 1/1 "%02x,"'; \
	echo '};'; echo 'const int manpage_sz = sizeof(manpage);') > manual.c
.else
fdisk.cat8:	fdisk.8
	mandoc -Tascii ${.ALLSRC} > ${.TARGET}

manual.c:	fdisk.cat8
	(echo 'const unsigned char manpage[] = {'; \
	cat fdisk.cat8 | gzip -9c | hexdump -ve '"0x" 1/1 "%02x,"'; \
	echo '};'; echo 'const int manpage_sz = sizeof(manpage);') > manual.c
.endif

MAN=	fdisk.8

.if ${MACHINE} == "amd64" || ${MACHINE} == "i386" || ${MACHINE} == "landisk"
CFLAGS += -DHAS_MBR
.endif

# XXX gross
.if ${MACHINE_ARCH} == "sh"
CFLAGS += -fno-builtin-memcpy
.endif

.include <bsd.prog.mk>
