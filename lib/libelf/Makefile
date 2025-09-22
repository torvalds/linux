#	$OpenBSD: Makefile,v 1.1 2019/02/01 05:27:37 jsg Exp $

LIB=	elf

VERSION_SCRIPT= ${.CURDIR}/Symbols.map

HDRS=	libelf.h gelf.h

GENSRC=	libelf_fsize.c libelf_msize.c libelf_convert.c
CLEANFILES += ${GENSRC}

SRCS=	elf.c elf_begin.c elf_cntl.c elf_end.c elf_errmsg.c elf_errno.c \
	elf_data.c elf_fill.c elf_flag.c elf_getarhdr.c elf_getarsym.c \
	elf_getbase.c elf_getident.c elf_hash.c elf_kind.c elf_memory.c \
	elf_next.c elf_open.c elf_rand.c elf_rawfile.c elf_phnum.c elf_shnum.c \
	elf_shstrndx.c elf_scn.c elf_strptr.c elf_update.c elf_version.c \
	gelf_cap.c gelf_checksum.c gelf_dyn.c gelf_ehdr.c gelf_getclass.c \
	gelf_fsize.c gelf_move.c gelf_phdr.c gelf_rel.c gelf_rela.c \
	gelf_shdr.c gelf_sym.c gelf_syminfo.c gelf_symshndx.c gelf_xlate.c \
	libelf_align.c libelf_allocate.c libelf_ar.c libelf_ar_util.c \
	libelf_checksum.c libelf_data.c libelf_ehdr.c libelf_elfmachine.c \
	libelf_extended.c libelf_memory.c libelf_open.c libelf_phdr.c \
	libelf_shdr.c libelf_xlate.c ${GENSRC}

MAN=	elf.3 elf_begin.3 elf_cntl.3 elf_end.3 elf_errmsg.3 elf_fill.3 \
	elf_flagdata.3 elf_getarhdr.3 elf_getarsym.3 elf_getbase.3 \
	elf_getdata.3 elf_getident.3 elf_getscn.3 elf_getphdrnum.3 \
	elf_getphnum.3 elf_getshdrnum.3 elf_getshnum.3 elf_getshdrstrndx.3 \
	elf_getshstrndx.3 elf_hash.3 elf_kind.3 elf_memory.3 elf_next.3 \
	elf_open.3 elf_rawfile.3 elf_rand.3 elf_strptr.3 elf_update.3 \
	elf_version.3 gelf.3 gelf_checksum.3 gelf_fsize.3 gelf_getcap.3 \
	gelf_getclass.3 gelf_getdyn.3 gelf_getehdr.3 gelf_getmove.3 \
	gelf_getphdr.3 gelf_getrel.3 gelf_getrela.3 gelf_getshdr.3 \
	gelf_getsym.3 gelf_getsyminfo.3 gelf_getsymshndx.3 gelf_newehdr.3 \
	gelf_newphdr.3 gelf_update_ehdr.3 gelf_xlatetof.3

CPPFLAGS+= -I${.CURDIR}

.m4.c:
	m4 -DSRCDIR=${.CURDIR} $< > $@

PC_FILES=libelf.pc
CLEANFILES+=${PC_FILES}

includes:
	@cd ${.CURDIR}; for i in $(HDRS); do \
	    j="cmp -s $$i ${DESTDIR}/usr/include/$$i || \
		${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} \
		-m 444 $$i ${DESTDIR}/usr/include"; \
	    echo $$j; \
	    eval "$$j"; \
	done

all: ${PC_FILES}
${PC_FILES}:
	/bin/sh ${.CURDIR}/generate_pkgconfig.sh -c ${.CURDIR} -o ${.OBJDIR}

beforeinstall:
	${INSTALL} ${INSTALL_COPY} -o root -g ${SHAREGRP} \
	    -m ${SHAREMODE} ${.OBJDIR}/${PC_FILES} ${DESTDIR}/usr/lib/pkgconfig/

.include <bsd.lib.mk>
