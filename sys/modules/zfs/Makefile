# $FreeBSD$

SYSDIR?=${SRCTOP}/sys

KMOD=	zfs

SRCS=	bus_if.h device_if.h vnode_if.h opt_kstack_pages.h

SUNW=	${SYSDIR}/cddl/contrib/opensolaris

.PATH:	${SUNW}/common/acl
SRCS+=	acl_common.c
.PATH:	${SUNW}/common/avl
SRCS+=	avl.c
.PATH:	${SUNW}/common/nvpair
SRCS+=	opensolaris_nvpair.c
SRCS+=	opensolaris_nvpair_alloc_fixed.c
SRCS+=	opensolaris_fnvpair.c
.PATH:	${SYSDIR}/cddl/contrib/opensolaris/common/unicode
SRCS+=	u8_textprep.c

.PATH:	${SYSDIR}/cddl/compat/opensolaris/kern
SRCS+=	opensolaris_acl.c
SRCS+=	opensolaris_dtrace.c
SRCS+=	opensolaris_kobj.c
SRCS+=	opensolaris_kstat.c
SRCS+=	opensolaris_lookup.c
SRCS+=	opensolaris_policy.c
SRCS+=	opensolaris_string.c
SRCS+=	opensolaris_sysevent.c
SRCS+=	opensolaris_taskq.c
SRCS+=	opensolaris_uio.c
SRCS+=	opensolaris_vfs.c
SRCS+=	opensolaris_vm.c
SRCS+=	opensolaris_zone.c

_A=${SYSDIR}/cddl/contrib/opensolaris/common/atomic
.if exists(${_A}/${MACHINE_CPUARCH}/opensolaris_atomic.S)
.PATH:	${_A}/${MACHINE_CPUARCH}
SRCS+=		opensolaris_atomic.S
.elif exists(${_A}/${MACHINE_ARCH}/opensolaris_atomic.S)
.PATH:	${_A}/${MACHINE_ARCH}
SRCS+=		opensolaris_atomic.S
.else
SRCS+=		opensolaris_atomic.c
.endif

.PATH:	${SUNW}/uts/common/fs
SRCS+=	vnode.c

.PATH:	${SUNW}/uts/common/os
SRCS+=	callb.c
SRCS+=	fm.c
SRCS+=	list.c
SRCS+=	nvpair_alloc_system.c

.PATH:	${SUNW}/uts/common/zmod
SRCS+=	adler32.c
SRCS+=	opensolaris_crc32.c
SRCS+=	deflate.c
SRCS+=	inffast.c
SRCS+=	inflate.c
SRCS+=	inftrees.c
SRCS+=	trees.c
SRCS+=	zmod.c
SRCS+=	zmod_subr.c
SRCS+=	zutil.c

.PATH:	${SYSDIR}/crypto/sha2
SRCS+=	sha256c.c sha512c.c

.PATH:	${SYSDIR}/crypto/skein
SRCS+=	skein.c skein_block.c

.PATH:	${SUNW}/common/zfs
.include "${SUNW}/uts/common/Makefile.files"
.PATH:	${SUNW}/uts/common/fs/zfs
ZFS_SRCS=	${ZFS_OBJS:C/.o$/.c/}
SRCS+=	${ZFS_SRCS}
SRCS+=	vdev_geom.c
SRCS+=	trim_map.c
.PATH:	${SUNW}/uts/common/fs/zfs/lua
LUA_SRCS=	${LUA_OBJS:C/.o$/.c/}
SRCS+=	${LUA_SRCS}

# Use FreeBSD's namecache.
CFLAGS+=-DFREEBSD_NAMECACHE

CFLAGS+=-I${SYSDIR}/cddl/compat/opensolaris
CFLAGS+=-I${SUNW}/uts/common/fs/zfs
CFLAGS+=-I${SUNW}/uts/common/fs/zfs/lua
CFLAGS+=-I${SUNW}/uts/common/zmod
CFLAGS+=-I${SUNW}/uts/common
CFLAGS+=-I${SYSDIR}
CFLAGS+=-I${SUNW}/common/zfs
CFLAGS+=-I${SUNW}/common
CFLAGS+=-DBUILDING_ZFS
CFLAGS.gcc+=-fms-extensions

.if ${MACHINE_ARCH} == "powerpc64"
CFLAGS.gcc+=-mminimal-toc
.endif

.ifdef ZFS_DEBUG
CFLAGS+=-DDEBUG=1
DEBUG_FLAGS=-g
.endif

.include <bsd.kmod.mk>

CFLAGS+=	-include ${SYSDIR}/cddl/compat/opensolaris/sys/debug_compat.h

CWARNFLAGS+=-Wno-missing-prototypes
CWARNFLAGS+=-Wno-undef
CWARNFLAGS+=-Wno-strict-prototypes
CWARNFLAGS+=-Wno-cast-qual
CWARNFLAGS+=-Wno-parentheses
CWARNFLAGS+=-Wno-redundant-decls
CWARNFLAGS+=-Wno-missing-braces
CWARNFLAGS+=-Wno-uninitialized
CWARNFLAGS+=-Wno-unused
CWARNFLAGS+=-Wno-inline
CWARNFLAGS+=-Wno-switch
CWARNFLAGS+=-Wno-pointer-arith
