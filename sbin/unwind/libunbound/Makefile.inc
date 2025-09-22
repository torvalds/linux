#	$OpenBSD: Makefile.inc,v 1.2 2022/06/18 16:17:29 florian Exp $

#DEBUG=	-g -DDEBUG=3 -O0
CFLAGS+= -Wall -I${.CURDIR}
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith
CFLAGS+= -Wsign-compare

.PATH: ${.CURDIR}/libunbound

.include "${.CURDIR}/libunbound/dns64/Makefile.inc"
.include "${.CURDIR}/libunbound/iterator/Makefile.inc"
.include "${.CURDIR}/libunbound/libunbound/Makefile.inc"
.include "${.CURDIR}/libunbound/respip/Makefile.inc"
.include "${.CURDIR}/libunbound/services/Makefile.inc"
.include "${.CURDIR}/libunbound/services/cache/Makefile.inc"
.include "${.CURDIR}/libunbound/sldns/Makefile.inc"
.include "${.CURDIR}/libunbound/util/Makefile.inc"
.include "${.CURDIR}/libunbound/util/data/Makefile.inc"
.include "${.CURDIR}/libunbound/util/storage/Makefile.inc"
.include "${.CURDIR}/libunbound/validator/Makefile.inc"
