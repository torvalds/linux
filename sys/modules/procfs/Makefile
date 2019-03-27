# $FreeBSD$

.PATH: ${SRCTOP}/sys/fs/procfs

KMOD=		procfs
SRCS=
SRCS+=		vnode_if.h
SRCS+=		procfs_dbregs.c
SRCS+=		procfs_fpregs.c
SRCS+=		procfs_ioctl.c
SRCS+=		procfs_map.c
SRCS+=		procfs_mem.c
SRCS+=		procfs_note.c
SRCS+=		procfs_osrel.c
SRCS+=		procfs_regs.c
SRCS+=		procfs_rlimit.c
SRCS+=		procfs_status.c
SRCS+=		procfs_type.c
SRCS+=		procfs.c

EXPORT_SYMS=
EXPORT_SYMS+=	procfs_attr
EXPORT_SYMS+=	procfs_candebug
EXPORT_SYMS+=	procfs_docurproc
EXPORT_SYMS+=	procfs_doprocfile
EXPORT_SYMS+=	procfs_doprocmem
EXPORT_SYMS+=	procfs_notsystem

.include <bsd.kmod.mk>
