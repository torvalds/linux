# $FreeBSD$

.include <bsd.init.mk>

PROG=		boot1.sym
INTERNALPROG=
WARNS?=		6

CFLAGS+=	-DEFI_BOOT1
# We implement a slightly non-standard %S in that it always takes a
# CHAR16 that's common in UEFI-land instead of a wchar_t. This only
# seems to matter on arm64 where wchar_t defaults to an int instead
# of a short. There's no good cast to use here so just ignore the
# warnings for now.
CWARNFLAGS.boot1.c+=	-Wno-format

# Disable warnings that are currently incompatible with the zfs boot code
CWARNFLAGS.zfs_module.c += -Wno-array-bounds
CWARNFLAGS.zfs_module.c += -Wno-cast-align
CWARNFLAGS.zfs_module.c += -Wno-cast-qual
CWARNFLAGS.zfs_module.c += -Wno-missing-prototypes
CWARNFLAGS.zfs_module.c += -Wno-sign-compare
CWARNFLAGS.zfs_module.c += -Wno-unused-parameter
CWARNFLAGS.zfs_module.c += -Wno-unused-function

# architecture-specific loader code
SRCS=	boot1.c self_reloc.c start.S ufs_module.c
.if ${MK_LOADER_ZFS} != "no"
SRCS+=		zfs_module.c
CFLAGS.zfs_module.c+=	-I${ZFSSRC}
CFLAGS.zfs_module.c+=	-I${SYSDIR}/cddl/boot/zfs
CFLAGS.zfs_module.c+=	-I${SYSDIR}/crypto/skein
CFLAGS+=	-DEFI_ZFS_BOOT
.endif

.if ${COMPILER_TYPE} == "gcc" && ${COMPILER_VERSION} > 40201
CWARNFLAGS.self_reloc.c+=	-Wno-error=maybe-uninitialized
.endif

CFLAGS+=	-I${EFIINC}
CFLAGS+=	-I${EFIINCMD}
CFLAGS+=	-I${SYSDIR}/contrib/dev/acpica/include
CFLAGS+=	-DEFI_UFS_BOOT
.ifdef(EFI_DEBUG)
CFLAGS+=	-DEFI_DEBUG
.endif

# Always add MI sources and REGULAR efi loader bits
.PATH:		${EFISRC}/loader/arch/${MACHINE}
.PATH:		${EFISRC}/loader
.PATH:		${LDRSRC}
CFLAGS+=	-I${LDRSRC}

FILES=	boot1.efi boot1.efifat
FILESMODE_boot1.efi=	${BINMODE}

LDSCRIPT=	${EFISRC}/loader/arch/${MACHINE}/ldscript.${MACHINE}
LDFLAGS+=	-Wl,-T${LDSCRIPT},-Bsymbolic,-znotext -shared

.if ${MACHINE_CPUARCH} == "aarch64"
CFLAGS+=	-mgeneral-regs-only
.endif
.if ${MACHINE_CPUARCH} == "amd64" || ${MACHINE_CPUARCH} == "i386"
CFLAGS+=	-fPIC
LDFLAGS+=	-Wl,-znocombreloc
.endif

LIBEFI=		${BOOTOBJ}/efi/libefi/libefi.a

#
# Add libstand for the runtime functions used by the compiler - for example
# __aeabi_* (arm) or __divdi3 (i386).
# as well as required string and memory functions for all platforms.
#
DPADD+=		${LIBEFI} ${LIBSA}
LDADD+=		${LIBEFI} ${LIBSA}

DPADD+=		${LDSCRIPT}

boot1.efi: ${PROG}
	if ${NM} ${.ALLSRC} | grep ' U '; then \
		echo "Undefined symbols in ${.ALLSRC}"; \
		exit 1; \
	fi
	SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH} \
	${OBJCOPY} -j .peheader -j .text -j .sdata -j .data \
		-j .dynamic -j .dynsym -j .rel.dyn \
		-j .rela.dyn -j .reloc -j .eh_frame \
		--output-target=${EFI_TARGET} ${.ALLSRC} ${.TARGET}

# The following inserts our objects into a template FAT file system
# created by generate-fat.sh

.include "Makefile.fat"

boot1.efifat: boot1.efi
	@set -- `ls -l ${.ALLSRC}`; \
	x=$$(($$5-${BOOT1_MAXSIZE})); \
	if [ $$x -ge 0 ]; then \
	    echo "boot1 $$x bytes too large; regenerate FAT templates?" >&2 ;\
	    exit 1; \
	fi
	echo ${.OBJDIR}
	xz -d -c ${BOOTSRC}/efi/boot1/fat-${MACHINE}.tmpl.xz > ${.TARGET}
	${DD} if=${.ALLSRC} of=${.TARGET} seek=${BOOT1_OFFSET} conv=notrunc

CLEANFILES+= boot1.efi boot1.efifat

.include <bsd.prog.mk>
