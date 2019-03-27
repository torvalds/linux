# $FreeBSD$

SUBDIR= \
	include \
	libibverbs \
	libibcm \
	libibumad \
	librdmacm \
	libibmad \
	complib \
	libvendor \
	libcxgb4 \
	libmlx4 \
	libmlx5 \
	libibnetdisc \
	libopensm

SUBDIR_DEPEND_libibcm= libibverbs
SUBDIR_DEPEND_librdmacm= libibverbs
SUBDIR_DEPEND_libibmad= libibumad
SUBDIR_DEPEND_libvendor= libibumad
SUBDIR_DEPEND_libcxgb4= libibverbs
SUBDIR_DEPEND_libmlx4= libibverbs
SUBDIR_DEPEND_libmlx5= libibverbs
SUBDIR_DEPEND_libibnetdisc= libibmad libibumad complib

SUBDIR_PARALLEL=

.include <bsd.subdir.mk>

