# $FreeBSD$

.PATH: ${.CURDIR}/../../include

LIB=		egacy
SRC=
INCSGROUPS=	INCS SYSINCS
INCS=

SYSINCSDIR=	${INCLUDEDIR}/sys

BOOTSTRAPPING?=	0

_WITH_PWCACHEDB!= grep -c pwcache_groupdb /usr/include/grp.h || true
.if ${_WITH_PWCACHEDB} == 0
.PATH: ${.CURDIR}/../../contrib/libc-pwcache
CFLAGS+=	-I${.CURDIR}/../../contrib/libc-pwcache \
		-I${.CURDIR}/../../lib/libc/include
SRCS+=		pwcache.c
.endif

_WITH_STRSVIS!=	grep -c strsvis /usr/include/vis.h || true
.if ${_WITH_STRSVIS} == 0
.PATH: ${.CURDIR}/../../contrib/libc-vis
SRCS+=		vis.c
CFLAGS+=	-I${.CURDIR}/../../contrib/libc-vis \
		-I${.CURDIR}/../../lib/libc/include
.endif

_WITH_REALLOCARRAY!= grep -c reallocarray /usr/include/stdlib.h || true
.if ${_WITH_REALLOCARRAY} == 0
.PATH: ${.CURDIR}/../../lib/libc/stdlib
INCS+=		stdlib.h
SRCS+=		reallocarray.c
CFLAGS+=	-I${.CURDIR}/../../lib/libc/include
.endif

_WITH_UTIMENS!= grep -c utimensat /usr/include/sys/stat.h || true
.if ${_WITH_UTIMENS} == 0
SYSINCS+=	stat.h
SRCS+=		futimens.c utimensat.c
.endif

_WITH_EXPLICIT_BZERO!= grep -c explicit_bzero /usr/include/strings.h || true
.if ${_WITH_EXPLICIT_BZERO} == 0
.PATH: ${SRCTOP}/sys/libkern
INCS+=		strings.h
SRCS+=		explicit_bzero.c
.endif

.if exists(/usr/include/capsicum_helpers.h)
_WITH_CAPH_ENTER!= grep -c caph_enter /usr/include/capsicum_helpers.h || true
.endif
.if !defined(_WITH_CAPH_ENTER) || ${_WITH_CAPH_ENTER} == 0
.PATH: ${SRCTOP}/lib/libcapsicum
INCS+=		capsicum_helpers.h
.PATH: ${SRCTOP}/lib/libcasper/libcasper
INCS+=		libcasper.h
.endif

.if empty(SRCS)
SRCS=		dummy.c
.endif

.if defined(CROSS_BUILD_TESTING)
SUBDIR=		cross-build
.endif

# Needed to build config (since it uses libnv)
SYSINCS+=	${SRCTOP}/sys/sys/nv.h ${SRCTOP}/sys/sys/cnv.h

# We want to run the build with only ${WORLDTMP} in $PATH to ensure we don't
# accidentally run tools that are incompatible but happen to be in $PATH.
# This is especially important when building on Linux/MacOS where many of the
# programs used during the build accept different flags or generate different
# output. On those platforms we only symlink the tools known to be compatible
# (e.g. basic utilities such as mkdir) into ${WORLDTMP} and build all others
# from the FreeBSD sources during the bootstrap-tools stage.

# basic commands: It is fine to use the host version for all of these even on
# Linux/MacOS since we only use flags that are supported by all of them.
_host_tools_to_symlink=	basename bzip2 bunzip2 chmod chown cmp comm cp date \
	dirname echo env false find fmt gzip gunzip head hostname id ln ls \
	mkdir mv nice patch rm realpath sh sleep stat tee touch tr true uname \
	uniq wc which

# We also need a symlink to the absolute path to the make binary used for
# the toplevel makefile. This is not necessarily the same as `which make`
# since e.g. on Linux and MacOS that will be GNU make.
_make_abs!=	which "${MAKE}"
_host_abs_tools_to_symlink=	${_make_abs}:make ${_make_abs}:bmake

host-symlinks:
	@echo "Linking host tools into ${DESTDIR}/bin"
.for _tool in ${_host_tools_to_symlink}
	@if [ ! -e "${DESTDIR}/bin/${_tool}" ]; then \
		source_path=`which ${_tool}`; \
		if [ ! -e "$${source_path}" ] ; then \
			echo "Cannot find host tool '${_tool}'"; false; \
		fi; \
		ln -sfnv "$${source_path}" "${DESTDIR}/bin/${_tool}"; \
	fi
.endfor
.for _tool in ${_host_abs_tools_to_symlink}
	@source_path="${_tool:S/:/ /:[1]}"; \
	target_path="${DESTDIR}/bin/${_tool:S/:/ /:[2]}"; \
	if [ ! -e "$${target_path}" ] ; then \
		if [ ! -e "$${source_path}" ] ; then \
			echo "Host tool '${src_path}' is missing"; false; \
		fi; \
		ln -sfnv "$${source_path}" "$${target_path}"; \
	fi
.endfor

# Create all the directories that are needed during the legacy, bootstrap-tools
# and cross-tools stages. We do this here using mkdir since mtree may not exist
# yet (this happens if we are crossbuilding from Linux/Mac).
installdirs:
.for _dir in bin usr/lib usr/include lib/geom lib/casper
	mkdir -p "${DESTDIR}/${_dir}"
.endfor
# Link usr/bin, sbin, and usr/sbin to bin so that it doesn't matter whether a
# bootstrap tool was added to WORLTMP with a symlink or by building it in the
# bootstrap-tools phase. We could also overrride BINDIR when building bootstrap
# tools but adding the symlinks is easier and means all tools are also
# in the directory that they are installed to normally.

.for _dir in sbin usr/sbin usr/bin
# delete existing directories from before r340157
	@if [ ! -L ${DESTDIR}/${_dir} ]; then \
	    echo "removing old non-symlink ${DESTDIR}/${_dir}"; \
	    rm -rf "${DESTDIR}/${_dir}"; \
	fi
.endfor
	ln -sfn bin ${DESTDIR}/sbin
	ln -sfn ../bin ${DESTDIR}/usr/bin
	ln -sfn ../bin ${DESTDIR}/usr/sbin
.for _group in ${INCSGROUPS:NINCS}
	mkdir -p "${DESTDIR}/${${_group}DIR}"
.endfor

.include <bsd.lib.mk>
