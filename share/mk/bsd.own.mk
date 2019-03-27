# $FreeBSD$
#
# The include file <bsd.own.mk> set common variables for owner,
# group, mode, and directories. Defaults are in brackets.
#
#
# +++ variables +++
#
# DESTDIR	Change the tree where the file gets installed. [not set]
#
# DISTDIR	Change the tree where the file for a distribution
# 		gets installed (see /usr/src/release/Makefile). [not set]
#
# COMPRESS_CMD	Program to compress documents.
#		Output is to stdout. [gzip -cn]
#
# COMPRESS_EXT	File name extension of ${COMPRESS_CMD} command. [.gz]
#
# BINOWN	Binary owner. [root]
#
# BINGRP	Binary group. [wheel]
#
# BINMODE	Binary mode. [555]
#
# NOBINMODE	Mode for non-executable files. [444]
#
# LIBDIR	Base path for libraries. [/usr/lib]
#
# LIBCOMPATDIR	Base path for compat libraries. [/usr/lib/compat]
#
# LIBDATADIR	Base path for misc. utility data files. [/usr/libdata]
#
# LIBEXECDIR	Base path for system daemons and utilities. [/usr/libexec]
#
# SHLIBDIR	Base path for shared libraries. [${LIBDIR}]
#
# LIBOWN	Library owner. [${BINOWN}]
#
# LIBGRP	Library group. [${BINGRP}]
#
# LIBMODE	Library mode. [${NOBINMODE}]
#
#
# DEBUGDIR	Base path for standalone debug files. [/usr/lib/debug]
#
# DEBUGMODE	Mode for debug files. [${NOBINMODE}]
#
#
# KMODDIR	Base path for loadable kernel modules
#		(see kld(4)). [/boot/modules]
#
# KMODOWN	Kernel and KLD owner. [${BINOWN}]
#
# KMODGRP	Kernel and KLD group. [${BINGRP}]
#
# KMODMODE	KLD mode. [${BINMODE}]
#
#
# SHAREDIR	Base path for architecture-independent ascii
#		text files. [/usr/share]
#
# SHAREOWN	ASCII text file owner. [root]
#
# SHAREGRP	ASCII text file group. [wheel]
#
# SHAREMODE	ASCII text file mode. [${NOBINMODE}]
#
#
# CONFDIR	Base path for configuration files. [/etc]
#
# CONFOWN	Configuration file owner. [root]
#
# CONFGRP	Configuration file group. [wheel]
#
# CONFMODE	Configuration file mode. [644]
#
#
# DIROWN	Directory owner. [root]
#
# DIRGRP	Directory group. [wheel]
#
# DIRMODE	Directory mode. [755]
#
#
# DOCDIR	Base path for system documentation (e.g. PSD, USD,
#		handbook, FAQ etc.). [${SHAREDIR}/doc]
#
# DOCOWN	Documentation owner. [${SHAREOWN}]
#
# DOCGRP	Documentation group. [${SHAREGRP}]
#
# DOCMODE	Documentation mode. [${NOBINMODE}]
#
#
# INFODIR	Base path for GNU's hypertext system
#		called Info (see info(1)). [${SHAREDIR}/info]
#
# INFOOWN	Info owner. [${SHAREOWN}]
#
# INFOGRP	Info group. [${SHAREGRP}]
#
# INFOMODE	Info mode. [${NOBINMODE}]
#
#
# MANDIR	Base path for manual installation. [${SHAREDIR}/man/man]
#
# MANOWN	Manual owner. [${SHAREOWN}]
#
# MANGRP	Manual group. [${SHAREGRP}]
#
# MANMODE	Manual mode. [${NOBINMODE}]
#
#
# NLSDIR	Base path for National Language Support files
#		installation. [${SHAREDIR}/nls]
#
# NLSOWN	National Language Support files owner. [${SHAREOWN}]
#
# NLSGRP	National Language Support files group. [${SHAREGRP}]
#
# NLSMODE	National Language Support files mode. [${NOBINMODE}]
#
# INCLUDEDIR	Base path for standard C include files [/usr/include]
#
# PKG_CMD	Program for creating and manipulating packages.
#               [pkg] 
#
# LINKOWN	Hard link owner [${BINOWN}]
#
# LINKGRP	Hard link group [${BINGRP}]
#
# LINKMODE	Hard link mode [${NOBINMODE}]
#
# SYMLINKOWN	Symbolic link owner [${BINOWN} or ${LIBOWN}]
#
# SYMLINKGRP	Symbolic link group [${BINGRP} or ${LIBGRP}]
#
# SYMLINKMODE	Symbolic link mode [755]

.if !target(__<bsd.own.mk>__)
__<bsd.own.mk>__:

.include <bsd.opts.mk>		# options now here or src.opts.mk

.if !defined(_WITHOUT_SRCCONF)

.if ${MK_CTF} != "no"
CTFCONVERT_CMD=	${CTFCONVERT} ${CTFFLAGS} ${.TARGET}
.elif defined(.PARSEDIR) || (defined(MAKE_VERSION) && ${MAKE_VERSION} >= 5201111300)
CTFCONVERT_CMD=
.else
CTFCONVERT_CMD=	@:
.endif 

.endif # !_WITHOUT_SRCCONF

# Binaries
BINOWN?=	root
BINGRP?=	wheel
BINMODE?=	555
NOBINMODE?=	444

KMODDIR?=	/boot/modules
KMODOWN?=	${BINOWN}
KMODGRP?=	${BINGRP}
KMODMODE?=	${BINMODE}
DTBDIR?=	/boot/dtb
DTBODIR?=	/boot/dtb/overlays
DTBOWN?=	root
DTBGRP?=	wheel
DTBMODE?=	444

# Use make.conf / environment LIBDIR as default if set...
.if !empty(_PREMK_LIBDIR)
LIBDIR_BASE?=	${_PREMK_LIBDIR}
.endif
# otherwise use our expected default value.
LIBDIR_BASE?=	/usr/lib
LIBDIR?=	${LIBDIR_BASE}
LIBCOMPATDIR?=	/usr/lib/compat
LIBDATADIR?=	/usr/libdata
LIBEXECDIR?=	/usr/libexec
SHLIBDIR?=	${LIBDIR}
LIBOWN?=	${BINOWN}
LIBGRP?=	${BINGRP}
LIBMODE?=	${NOBINMODE}

DEBUGDIR?=	/usr/lib/debug
DEBUGMODE?=	${NOBINMODE}


# Share files
SHAREDIR?=	/usr/share
SHAREOWN?=	root
SHAREGRP?=	wheel
SHAREMODE?=	${NOBINMODE}

CONFDIR?=	/etc
CONFOWN?=	root
CONFGRP?=	wheel
CONFMODE?=	644

MANDIR?=	${SHAREDIR}/man/man
MANOWN?=	${SHAREOWN}
MANGRP?=	${SHAREGRP}
MANMODE?=	${NOBINMODE}

DIROWN?=	root
DIRGRP?=	wheel
DIRMODE?=	755

DOCDIR?=	${SHAREDIR}/doc
DOCOWN?=	${SHAREOWN}
DOCGRP?=	${SHAREGRP}
DOCMODE?=	${NOBINMODE}

INFODIR?=	${SHAREDIR}/info
INFOOWN?=	${SHAREOWN}
INFOGRP?=	${SHAREGRP}
INFOMODE?=	${NOBINMODE}

NLSDIR?=	${SHAREDIR}/nls
NLSOWN?=	${SHAREOWN}
NLSGRP?=	${SHAREGRP}
NLSMODE?=	${NOBINMODE}

INCLUDEDIR?=	/usr/include

#
# install(1) parameters.
#
_LINKOWN?=	${LINKOWN:U${BINOWN}}
_LINKGRP?=	${LINKGRP:U${BINGRP}}
_LINKMODE?=	${LINKMODE:U${NOBINMODE}}
_SYMLINKOWN?=	${SYMLINKOWN:U${BINOWN}}
_SYMLINKGRP?=	${SYMLINKGRP:U${BINGRP}}
_SYMLINKMODE?=	${SYMLINKMODE:U755}
HRDLINK?=	-l h -o ${_LINKOWN} -g ${_LINKGRP} -m ${_LINKMODE}
MANHRDLINK?=	-l h -o ${MANOWN} -g ${MANGRP} -m ${MANMODE}
SYMLINK?=	-l s -o ${_SYMLINKOWN} -g ${_SYMLINKGRP} -m ${_SYMLINKMODE}
LSYMLINK?=	-l s -o ${LIBOWN} -g ${LIBGRP} -m ${LIBMODE}
RSYMLINK?=	-l rs -o ${_SYMLINKOWN} -g ${_SYMLINKGRP} -m ${_SYMLINKMODE}

INSTALL_LINK?=		${INSTALL} ${HRDLINK}
INSTALL_MANLINK?=	${INSTALL} ${MANHRDLINK}
INSTALL_SYMLINK?=	${INSTALL} ${SYMLINK}
INSTALL_LIBSYMLINK?=	${INSTALL} ${LSYMLINK}
INSTALL_RSYMLINK?=	${INSTALL} ${RSYMLINK}

# Common variables
.if !defined(DEBUG_FLAGS)
STRIP?=		-s
.endif

COMPRESS_CMD?=	gzip -cn
COMPRESS_EXT?=	.gz

# Set XZ_THREADS to 1 to disable multi-threading.
XZ_THREADS?=	0

.if !empty(XZ_THREADS)
XZ_CMD?=	xz -T ${XZ_THREADS}
.else
XZ_CMD?=	xz
.endif

PKG_CMD?=	pkg

# Pointer to the top directory into which tests are installed.  Should not be
# overridden by Makefiles, but the user may choose to set this in src.conf(5).
TESTSBASE?= /usr/tests

DEPENDFILE?=	.depend

# Compat for the moment -- old bsd.own.mk only included this when _WITHOUT_SRCCONF
# wasn't defined. bsd.ports.mk and friends depend on this behavior. Remove in 12.
.if !defined(_WITHOUT_SRCCONF)
.include <bsd.compiler.mk>
.endif # !_WITHOUT_SRCCONF

.endif	# !target(__<bsd.own.mk>__)
