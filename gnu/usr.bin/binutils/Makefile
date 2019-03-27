# $FreeBSD$

.include <src.opts.mk>

SUBDIR=	libiberty \
	libbfd \
	libopcodes

SUBDIR.${MK_BINUTILS}+=	doc
SUBDIR.${MK_BINUTILS}+=	libbinutils
SUBDIR.${MK_BINUTILS}+=	as
SUBDIR.${MK_BINUTILS}+=	objdump

# When we use ld.lld as /usr/bin/ld, do not install the non-ifunc-capable
# GNU binutils 2.17.50 ld.
.if ${MK_LLD_IS_LD} == "no"
SUBDIR.${MK_BINUTILS}+=ld
.endif

SUBDIR_DEPEND_libbinutils=libbfd		# for bfdver.h
SUBDIR_DEPEND_as=libbfd libiberty libopcodes
SUBDIR_DEPEND_ld=libbfd libiberty
SUBDIR_DEPEND_objdump=libbfd libiberty libbinutils libopcodes

.if !make(install)
SUBDIR_PARALLEL=
.endif

.include <bsd.subdir.mk>
