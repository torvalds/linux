# $FreeBSD$

.include <bsd.init.mk>

LIB=	efi
WARNS?=	2

SRCS=	delay.c \
	devicename.c \
	devpath.c \
	efi_console.c \
	efi_driver_utils.c \
	efichar.c \
	efienv.c \
	efinet.c \
	efipart.c \
	efizfs.c \
	env.c \
	errno.c \
	handles.c \
	libefi.c \
	wchar.c

.if ${MACHINE_CPUARCH} == "amd64" || ${MACHINE_CPUARCH} == "i386"
SRCS+=	time.c
.elif ${MACHINE_CPUARCH} == "aarch64" || ${MACHINE_CPUARCH} == "arm"
SRCS+=	time_event.c
.endif

# We implement a slightly non-standard %S in that it always takes a
# CHAR16 that's common in UEFI-land instead of a wchar_t. This only
# seems to matter on arm64 where wchar_t defaults to an int instead
# of a short. There's no good cast to use here so just ignore the
# warnings for now.
CWARNFLAGS.efinet.c+=	-Wno-format
CWARNFLAGS.efipart.c+=	-Wno-format
CWARNFLAGS.env.c+=	-Wno-format

.if ${MACHINE_CPUARCH} == "aarch64"
CFLAGS+=	-mgeneral-regs-only
.endif
.if ${MACHINE_ARCH} == "amd64"
CFLAGS+= -fPIC -mno-red-zone
.endif
CFLAGS+= -I${EFIINC}
CFLAGS+= -I${EFIINCMD}
.if ${MK_LOADER_ZFS} != "no"
CFLAGS+=	-I${ZFSSRC}
CFLAGS+=	-DEFI_ZFS_BOOT
.endif

# Pick up the bootstrap header for some interface items
CFLAGS+= -I${LDRSRC}

# Handle FreeBSD specific %b and %D printf format specifiers
CFLAGS+= ${FORMAT_EXTENSIONS}

# Do not use TERM_EMU on arm and arm64 as it doesn't behave well with serial console
.if ${MACHINE_CPUARCH} != "arm" && ${MACHINE_CPUARCH} != "aarch64"
CFLAGS+= -DTERM_EMU
.endif

.include <bsd.lib.mk>
