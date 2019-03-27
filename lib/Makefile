#	@(#)Makefile	8.1 (Berkeley) 6/4/93
# $FreeBSD$

.include <src.opts.mk>

# The SUBDIR_BOOTSTRAP list is a small set of libraries which are used by many
# of the other libraries.  These are built first with a .WAIT between them
# and the main list to avoid needing a SUBDIR_DEPEND line on every library
# naming just these few items.

SUBDIR_BOOTSTRAP= \
	csu \
	.WAIT \
	libc \
	libc_nonshared \
	libcompiler_rt \
	${_libclang_rt} \
	${_libcplusplus} \
	${_libcxxrt} \
	libelf \
	msun

# The main list; please keep these sorted alphabetically.

SUBDIR=	${SUBDIR_BOOTSTRAP} \
	.WAIT \
	geom \
	libalias \
	libarchive \
	libauditd \
	libbegemot \
	libblocksruntime \
	libbsdstat \
	libbsm \
	libbz2 \
	libcalendar \
	libcam \
	libcapsicum \
	libcasper \
	libcompat \
	libcrypt \
	libdevctl \
	libdevinfo \
	libdevstat \
	libdl \
	libdwarf \
	libedit \
	libelftc \
	libevent \
	libexecinfo \
	libexpat \
	libfetch \
	libfigpar \
	libgeom \
	libifconfig \
	libipsec \
	libjail \
	libkiconv \
	libkvm \
	liblzma \
	libmemstat \
	libmd \
	libmt \
	lib80211 \
	libnetbsd \
	libnv \
	libopenbsd \
	libopie \
	libpam \
	libpathconv \
	libpcap \
	libpjdlog \
	${_libproc} \
	libprocstat \
	libregex \
	librpcsvc \
	librss \
	librt \
	${_librtld_db} \
	libsbuf \
	libsmb \
	libsqlite3 \
	libstdbuf \
	libstdthreads \
	libsysdecode \
	libtacplus \
	libthread_db \
	libucl \
	libufs \
	libugidfw \
	libulog \
	libutil \
	${_libvgl} \
	libwrap \
	libxo \
	liby \
	libz \
	libzstd \
	ncurses

# Inter-library dependencies.  When the makefile for a library contains LDADD
# libraries, those libraries should be listed as build order dependencies here.

SUBDIR_DEPEND_geom=	libufs
SUBDIR_DEPEND_libarchive= libz libbz2 libexpat liblzma libmd
SUBDIR_DEPEND_libauditdm= libbsm
SUBDIR_DEPEND_libbsnmp= ${_libnetgraph}
SUBDIR_DEPEND_libc++:= libcxxrt
SUBDIR_DEPEND_libc= libcompiler_rt
SUBDIR_DEPEND_libcam= libsbuf
SUBDIR_DEPEND_libcasper= libnv
SUBDIR_DEPEND_libdevstat= libkvm
SUBDIR_DEPEND_libdpv= libfigpar ncurses libutil
SUBDIR_DEPEND_libedit= ncurses
SUBDIR_DEPEND_libgeom= libexpat libsbuf
SUBDIR_DEPEND_librpcsec_gss= libgssapi
SUBDIR_DEPEND_libmagic= libz
SUBDIR_DEPEND_libmemstat= libkvm
SUBDIR_DEPEND_libopie= libmd
SUBDIR_DEPEND_libpam= libcrypt libopie ${_libradius} librpcsvc libtacplus libutil ${_libypclnt} ${_libcom_err} 
SUBDIR_DEPEND_libpjdlog= libutil
SUBDIR_DEPEND_libprocstat= libkvm libutil
SUBDIR_DEPEND_libradius= libmd
SUBDIR_DEPEND_libsmb= libkiconv
SUBDIR_DEPEND_libtacplus= libmd
SUBDIR_DEPEND_libulog= libmd
SUBDIR_DEPEND_libunbound= ${_libldns}
SUBDIR_DEPEND_liblzma= ${_libthr}
.if ${MK_OFED} != "no"
SUBDIR_DEPEND_libpcap= ofed
.endif

# NB: keep these sorted by MK_* knobs

SUBDIR.${MK_ATM}+=	libngatm
SUBDIR.${MK_BEARSSL}+=	libbearssl libsecureboot
SUBDIR.${MK_BLACKLIST}+=libblacklist
SUBDIR.${MK_BLUETOOTH}+=libbluetooth libsdp
SUBDIR.${MK_BSNMP}+=	libbsnmp

.if !defined(COMPAT_32BIT) && !defined(COMPAT_SOFTFP)
SUBDIR.${MK_CLANG}+=	clang
.endif

SUBDIR.${MK_CUSE}+= 	libcuse
SUBDIR.${MK_CXX}+=	libdevdctl
SUBDIR.${MK_TOOLCHAIN}+=libpe
SUBDIR.${MK_DIALOG}+=	libdpv
SUBDIR.${MK_FILE}+=	libmagic
SUBDIR.${MK_GPIO}+=	libgpio
SUBDIR.${MK_GSSAPI}+=	libgssapi librpcsec_gss
SUBDIR.${MK_ICONV}+=	libiconv_modules
SUBDIR.${MK_KERBEROS_SUPPORT}+=	libcom_err
SUBDIR.${MK_LDNS}+=	libldns

# The libraries under libclang_rt can only be built by clang, and only make
# sense to build when clang is enabled at all.  Furthermore, they can only be
# built for certain architectures.
.if ${MK_CLANG} != "no" && ${COMPILER_TYPE} == "clang" && \
    (${MACHINE_CPUARCH} == "aarch64" || ${MACHINE_CPUARCH} == "amd64" || \
    ${MACHINE_CPUARCH} == "arm" || ${MACHINE_CPUARCH} == "i386")
_libclang_rt=	libclang_rt
.endif

.if ${MK_LIBCPLUSPLUS} != "no"
_libcxxrt=	libcxxrt
_libcplusplus=	libc++
_libcplusplus+=	libc++experimental
_libcplusplus+=	libc++fs
.endif

SUBDIR.${MK_EFI}+=	libefivar
SUBDIR.${MK_GOOGLETEST}+=	googletest
SUBDIR.${MK_LIBTHR}+=	libthr
SUBDIR.${MK_LLVM_LIBUNWIND}+=	libgcc_eh
SUBDIR.${MK_LLVM_LIBUNWIND}+=	libgcc_s
SUBDIR.${MK_NAND}+=	libnandfs
SUBDIR.${MK_NETGRAPH}+=	libnetgraph
SUBDIR.${MK_NIS}+=	libypclnt

.if ${MACHINE_CPUARCH} == "i386" || ${MACHINE_CPUARCH} == "amd64"
_libvgl=	libvgl
.endif

.if ${MACHINE_CPUARCH} == "aarch64"
SUBDIR.${MK_PMC}+=	libopencsd
.endif

.if ${MACHINE_CPUARCH} == "amd64"
SUBDIR.${MK_PMC}+=	libipt
SUBDIR.${MK_BHYVE}+=	libvmmapi
.endif

.if ${MACHINE_CPUARCH} != "sparc64"
_libproc=	libproc
_librtld_db=	librtld_db
.endif

SUBDIR.${MK_OPENMP}+=	libomp
SUBDIR.${MK_OPENSSL}+=	libmp
SUBDIR.${MK_PMC}+=	libpmc libpmcstat
SUBDIR.${MK_RADIUS_SUPPORT}+=	libradius
SUBDIR.${MK_SENDMAIL}+=	libmilter libsm libsmdb libsmutil
SUBDIR.${MK_TELNET}+=	libtelnet
SUBDIR.${MK_TESTS_SUPPORT}+=	atf
SUBDIR.${MK_TESTS}+=	tests
SUBDIR.${MK_UNBOUND}+=	libunbound
SUBDIR.${MK_USB}+=	libusbhid libusb
SUBDIR.${MK_OFED}+=	ofed
SUBDIR.${MK_VERIEXEC}+=	libveriexec
SUBDIR.${MK_ZFS}+=	libbe

.if !make(install)
SUBDIR_PARALLEL=
.endif

.include <bsd.subdir.mk>
