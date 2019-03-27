# $FreeBSD$

PACKAGE=	tests

_LIBARCHIVEDIR=	${SRCTOP}/contrib/libarchive

ATF_TESTS_SH+=	functional_test

BINDIR=		${TESTSDIR}

PROGS+=		bsdcpio_test

CFLAGS+=	-DPLATFORM_CONFIG_H=\"${SRCTOP}/lib/libarchive/config_freebsd.h\"
CFLAGS+=	-I${SRCTOP}/lib/libarchive -I${.OBJDIR}

CFLAGS+=	-I${.OBJDIR}
CFLAGS+=	-I${_LIBARCHIVEDIR}/cpio -I${_LIBARCHIVEDIR}/cpio/test
CFLAGS+=	-I${_LIBARCHIVEDIR}/libarchive
CFLAGS+=	-I${_LIBARCHIVEDIR}/libarchive_fe -I${_LIBARCHIVEDIR}/test_utils

# Uncomment to link against dmalloc
#LDADD+= -L/usr/local/lib -ldmalloc
#CFLAGS+= -I/usr/local/include -DUSE_DMALLOC

.PATH:	${_LIBARCHIVEDIR}/cpio
CPIO_SRCS= cmdline.c

.PATH:	${_LIBARCHIVEDIR}/libarchive_fe
CPIO_SRCS+= err.c

.PATH:	${_LIBARCHIVEDIR}/cpio/test
TESTS_SRCS=	\
	test_0.c				\
	test_basic.c				\
	test_cmdline.c				\
	test_extract_cpio_Z.c			\
	test_extract_cpio_bz2.c			\
	test_extract_cpio_grz.c			\
	test_extract_cpio_gz.c			\
	test_extract_cpio_lrz.c			\
	test_extract_cpio_lz.c			\
	test_extract_cpio_lz4.c			\
	test_extract_cpio_lzma.c		\
	test_extract_cpio_lzo.c			\
	test_extract_cpio_xz.c			\
	test_extract_cpio_zstd.c		\
	test_format_newc.c			\
	test_gcpio_compat.c			\
	test_missing_file.c			\
	test_option_0.c				\
	test_option_B_upper.c			\
	test_option_C_upper.c			\
	test_option_J_upper.c			\
	test_option_L_upper.c			\
	test_option_Z_upper.c			\
	test_option_a.c				\
	test_option_b64encode.c			\
	test_option_c.c				\
	test_option_d.c				\
	test_option_f.c				\
	test_option_grzip.c			\
	test_option_help.c			\
	test_option_l.c				\
	test_option_lrzip.c			\
	test_option_lz4.c			\
	test_option_lzma.c			\
	test_option_lzop.c			\
	test_option_m.c				\
	test_option_passphrase.c		\
	test_option_t.c				\
	test_option_u.c				\
	test_option_uuencode.c			\
	test_option_version.c			\
	test_option_xz.c			\
	test_option_y.c				\
	test_option_z.c				\
	test_option_zstd.c			\
	test_owner_parse.c			\
	test_passthrough_dotdot.c		\
	test_passthrough_reverse.c

SRCS.bsdcpio_test= list.h			\
	${CPIO_SRCS}				\
	${TESTS_SRCS}

.PATH:	${_LIBARCHIVEDIR}/test_utils
SRCS.bsdcpio_test+=	test_main.c		\
			test_utils.c

LIBADD.bsdcpio_test=	archive

list.h: ${TESTS_SRCS} Makefile
	@(cd ${_LIBARCHIVEDIR}/tar/test && \
	grep -h DEFINE_TEST ${.ALLSRC:N*Makefile}) > ${.TARGET}.tmp
	@mv ${.TARGET}.tmp ${.TARGET}

CLEANFILES+=	list.h list.h.tmp

${PACKAGE}FILES+= test_extract.cpio.Z.uu
${PACKAGE}FILES+= test_extract.cpio.bz2.uu
${PACKAGE}FILES+= test_extract.cpio.grz.uu
${PACKAGE}FILES+= test_extract.cpio.gz.uu
${PACKAGE}FILES+= test_extract.cpio.lrz.uu
${PACKAGE}FILES+= test_extract.cpio.lz.uu
${PACKAGE}FILES+= test_extract.cpio.lz4.uu
${PACKAGE}FILES+= test_extract.cpio.lzma.uu
${PACKAGE}FILES+= test_extract.cpio.lzo.uu
${PACKAGE}FILES+= test_extract.cpio.xz.uu
${PACKAGE}FILES+= test_extract.cpio.zst.uu
${PACKAGE}FILES+= test_gcpio_compat_ref.bin.uu
${PACKAGE}FILES+= test_gcpio_compat_ref.crc.uu
${PACKAGE}FILES+= test_gcpio_compat_ref.newc.uu
${PACKAGE}FILES+= test_gcpio_compat_ref.ustar.uu
${PACKAGE}FILES+= test_gcpio_compat_ref_nosym.bin.uu
${PACKAGE}FILES+= test_gcpio_compat_ref_nosym.crc.uu
${PACKAGE}FILES+= test_gcpio_compat_ref_nosym.newc.uu
${PACKAGE}FILES+= test_gcpio_compat_ref_nosym.ustar.uu
${PACKAGE}FILES+= test_option_f.cpio.uu
${PACKAGE}FILES+= test_option_m.cpio.uu
${PACKAGE}FILES+= test_option_passphrase.zip.uu
${PACKAGE}FILES+= test_option_t.cpio.uu
${PACKAGE}FILES+= test_option_t.stdout.uu
${PACKAGE}FILES+= test_option_tv.stdout.uu

.include <bsd.test.mk>
