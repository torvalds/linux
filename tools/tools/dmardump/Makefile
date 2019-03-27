# $FreeBSD$

PROG=	dmardump
SRCS=	dmardump.c
MAN=

# Pull in bits from acpidump
ACPIDUMP=${.CURDIR}/../../../usr.sbin/acpi/acpidump
.PATH:	${ACPIDUMP}
SRCS+=	acpi_user.c acpi.c
CFLAGS+= -I${ACPIDUMP}

CFLAGS+= -I${.CURDIR}/../../../sys

.include <bsd.prog.mk>
