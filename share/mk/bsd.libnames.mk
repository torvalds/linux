# $FreeBSD$

# The include file <bsd.libnames.mk> define library names.
# Other include files (e.g. bsd.prog.mk, bsd.lib.mk) include this
# file where necessary.

.if !target(__<bsd.init.mk>__)
.error bsd.libnames.mk cannot be included directly.
.endif

LIBDESTDIR=	${SYSROOT:U${DESTDIR}}

.sinclude <src.libnames.mk>

# Src directory locations are also defined in src.libnames.mk.

LIBCRT0?=	${LIBDESTDIR}${LIBDIR_BASE}/crt0.o

LIB80211?=	${LIBDESTDIR}${LIBDIR_BASE}/lib80211.a
LIBALIAS?=	${LIBDESTDIR}${LIBDIR_BASE}/libalias.a
LIBARCHIVE?=	${LIBDESTDIR}${LIBDIR_BASE}/libarchive.a
LIBASN1?=	${LIBDESTDIR}${LIBDIR_BASE}/libasn1.a
LIBATM?=	${LIBDESTDIR}${LIBDIR_BASE}/libatm.a
LIBAUDITD?=	${LIBDESTDIR}${LIBDIR_BASE}/libauditd.a
LIBAVL?=	${LIBDESTDIR}${LIBDIR_BASE}/libavl.a
LIBBE?=		${LIBDESTDIR}${LIBDIR_BASE}/libbe.a
LIBBEGEMOT?=	${LIBDESTDIR}${LIBDIR_BASE}/libbegemot.a
LIBBLACKLIST?=	${LIBDESTDIR}${LIBDIR_BASE}/libblacklist.a
LIBBLUETOOTH?=	${LIBDESTDIR}${LIBDIR_BASE}/libbluetooth.a
LIBBSDXML?=	${LIBDESTDIR}${LIBDIR_BASE}/libbsdxml.a
LIBBSM?=	${LIBDESTDIR}${LIBDIR_BASE}/libbsm.a
LIBBSNMP?=	${LIBDESTDIR}${LIBDIR_BASE}/libbsnmp.a
LIBBZ2?=	${LIBDESTDIR}${LIBDIR_BASE}/libbz2.a
LIBC?=		${LIBDESTDIR}${LIBDIR_BASE}/libc.a
LIBCALENDAR?=	${LIBDESTDIR}${LIBDIR_BASE}/libcalendar.a
LIBCAM?=	${LIBDESTDIR}${LIBDIR_BASE}/libcam.a
LIBCOMPAT?=	${LIBDESTDIR}${LIBDIR_BASE}/libcompat.a
LIBCOMPILER_RT?=${LIBDESTDIR}${LIBDIR_BASE}/libcompiler_rt.a
LIBCOM_ERR?=	${LIBDESTDIR}${LIBDIR_BASE}/libcom_err.a
LIBCPLUSPLUS?=	${LIBDESTDIR}${LIBDIR_BASE}/libc++.a
LIBCRYPT?=	${LIBDESTDIR}${LIBDIR_BASE}/libcrypt.a
LIBCRYPTO?=	${LIBDESTDIR}${LIBDIR_BASE}/libcrypto.a
LIBCTF?=	${LIBDESTDIR}${LIBDIR_BASE}/libctf.a
LIBCURSES?=	${LIBDESTDIR}${LIBDIR_BASE}/libcurses.a
LIBCUSE?=	${LIBDESTDIR}${LIBDIR_BASE}/libcuse.a
LIBCXGB4?=	${LIBDESTDIR}${LIBDIR_BASE}/libcxgb4.a
LIBCXXRT?=	${LIBDESTDIR}${LIBDIR_BASE}/libcxxrt.a
LIBC_PIC?=	${LIBDESTDIR}${LIBDIR_BASE}/libc_pic.a
LIBDEVCTL?=	${LIBDESTDIR}${LIBDIR_BASE}/libdevctl.a
LIBDEVDCTL?=	${LIBDESTDIR}${LIBDIR_BASE}/libdevdctl.a
LIBDEVINFO?=	${LIBDESTDIR}${LIBDIR_BASE}/libdevinfo.a
LIBDEVSTAT?=	${LIBDESTDIR}${LIBDIR_BASE}/libdevstat.a
LIBDIALOG?=	${LIBDESTDIR}${LIBDIR_BASE}/libdialog.a
LIBDL?=		${LIBDESTDIR}${LIBDIR_BASE}/libdl.a
LIBDNS?=	${LIBDESTDIR}${LIBDIR_BASE}/libdns.a
LIBDPV?=	${LIBDESTDIR}${LIBDIR_BASE}/libdpv.a
LIBDTRACE?=	${LIBDESTDIR}${LIBDIR_BASE}/libdtrace.a
LIBDWARF?=	${LIBDESTDIR}${LIBDIR_BASE}/libdwarf.a
LIBEDIT?=	${LIBDESTDIR}${LIBDIR_BASE}/libedit.a
LIBEFIVAR?=	${LIBDESTDIR}${LIBDIR_BASE}/libefivar.a
LIBELF?=	${LIBDESTDIR}${LIBDIR_BASE}/libelf.a
LIBEXECINFO?=	${LIBDESTDIR}${LIBDIR_BASE}/libexecinfo.a
LIBFETCH?=	${LIBDESTDIR}${LIBDIR_BASE}/libfetch.a
LIBFIGPAR?=	${LIBDESTDIR}${LIBDIR_BASE}/libfigpar.a
LIBFL?=		"don't use LIBFL, use LIBL"
LIBFORM?=	${LIBDESTDIR}${LIBDIR_BASE}/libform.a
LIBG2C?=	${LIBDESTDIR}${LIBDIR_BASE}/libg2c.a
LIBGEOM?=	${LIBDESTDIR}${LIBDIR_BASE}/libgeom.a
LIBGNUREGEX?=	${LIBDESTDIR}${LIBDIR_BASE}/libgnuregex.a
LIBGPIO?=	${LIBDESTDIR}${LIBDIR_BASE}/libgpio.a
LIBGSSAPI?=	${LIBDESTDIR}${LIBDIR_BASE}/libgssapi.a
LIBGSSAPI_KRB5?= ${LIBDESTDIR}${LIBDIR_BASE}/libgssapi_krb5.a
LIBHDB?=	${LIBDESTDIR}${LIBDIR_BASE}/libhdb.a
LIBHEIMBASE?=	${LIBDESTDIR}${LIBDIR_BASE}/libheimbase.a
LIBHEIMNTLM?=	${LIBDESTDIR}${LIBDIR_BASE}/libheimntlm.a
LIBHEIMSQLITE?=	${LIBDESTDIR}${LIBDIR_BASE}/libheimsqlite.a
LIBHX509?=	${LIBDESTDIR}${LIBDIR_BASE}/libhx509.a
LIBIBCM?=	${LIBDESTDIR}${LIBDIR_BASE}/libibcm.a
LIBIBMAD?=	${LIBDESTDIR}${LIBDIR_BASE}/libibmad.a
LIBIBNETDISC?=	${LIBDESTDIR}${LIBDIR_BASE}/libibnetdisc.a
LIBIBUMAD?=	${LIBDESTDIR}${LIBDIR_BASE}/libibumad.a
LIBIBVERBS?=	${LIBDESTDIR}${LIBDIR_BASE}/libibverbs.a
LIBIPSEC?=	${LIBDESTDIR}${LIBDIR_BASE}/libipsec.a
LIBIPT?=	${LIBDESTDIR}${LIBDIR_BASE}/libipt.a
LIBJAIL?=	${LIBDESTDIR}${LIBDIR_BASE}/libjail.a
LIBKADM5CLNT?=	${LIBDESTDIR}${LIBDIR_BASE}/libkadm5clnt.a
LIBKADM5SRV?=	${LIBDESTDIR}${LIBDIR_BASE}/libkadm5srv.a
LIBKAFS5?=	${LIBDESTDIR}${LIBDIR_BASE}/libkafs5.a
LIBKDC?=	${LIBDESTDIR}${LIBDIR_BASE}/libkdc.a
LIBKEYCAP?=	${LIBDESTDIR}${LIBDIR_BASE}/libkeycap.a
LIBKICONV?=	${LIBDESTDIR}${LIBDIR_BASE}/libkiconv.a
LIBKRB5?=	${LIBDESTDIR}${LIBDIR_BASE}/libkrb5.a
LIBKVM?=	${LIBDESTDIR}${LIBDIR_BASE}/libkvm.a
LIBL?=		${LIBDESTDIR}${LIBDIR_BASE}/libl.a
LIBLN?=		"don't use LIBLN, use LIBL"
LIBLZMA?=	${LIBDESTDIR}${LIBDIR_BASE}/liblzma.a
LIBM?=		${LIBDESTDIR}${LIBDIR_BASE}/libm.a
LIBMAGIC?=	${LIBDESTDIR}${LIBDIR_BASE}/libmagic.a
LIBMD?=		${LIBDESTDIR}${LIBDIR_BASE}/libmd.a
LIBMEMSTAT?=	${LIBDESTDIR}${LIBDIR_BASE}/libmemstat.a
LIBMENU?=	${LIBDESTDIR}${LIBDIR_BASE}/libmenu.a
LIBMILTER?=	${LIBDESTDIR}${LIBDIR_BASE}/libmilter.a
LIBMLX4?=	${LIBDESTDIR}${LIBDIR_BASE}/libmlx4.a
LIBMLX5?=	${LIBDESTDIR}${LIBDIR_BASE}/libmlx5.a
LIBMP?=		${LIBDESTDIR}${LIBDIR_BASE}/libmp.a
LIBMT?=		${LIBDESTDIR}${LIBDIR_BASE}/libmt.a
LIBNANDFS?=	${LIBDESTDIR}${LIBDIR_BASE}/libnandfs.a
LIBNCURSES?=	${LIBDESTDIR}${LIBDIR_BASE}/libncurses.a
LIBNCURSESW?=	${LIBDESTDIR}${LIBDIR_BASE}/libncursesw.a
LIBNETGRAPH?=	${LIBDESTDIR}${LIBDIR_BASE}/libnetgraph.a
LIBNGATM?=	${LIBDESTDIR}${LIBDIR_BASE}/libngatm.a
LIBNV?=		${LIBDESTDIR}${LIBDIR_BASE}/libnv.a
LIBNVPAIR?=	${LIBDESTDIR}${LIBDIR_BASE}/libnvpair.a
LIBOPENCSD?=	${LIBDESTDIR}${LIBDIR_BASE}/libopencsd.a
LIBOPENSM?=	${LIBDESTDIR}${LIBDIR_BASE}/libopensm.a
LIBOPIE?=	${LIBDESTDIR}${LIBDIR_BASE}/libopie.a
LIBOSMCOMP?=	${LIBDESTDIR}${LIBDIR_BASE}/libosmcomp.a
LIBOSMVENDOR?=	${LIBDESTDIR}${LIBDIR_BASE}/libosmvendor.a
LIBPAM?=	${LIBDESTDIR}${LIBDIR_BASE}/libpam.a
LIBPANEL?=	${LIBDESTDIR}${LIBDIR_BASE}/libpanel.a
LIBPANELW?=	${LIBDESTDIR}${LIBDIR_BASE}/libpanelw.a
LIBPCAP?=	${LIBDESTDIR}${LIBDIR_BASE}/libpcap.a
LIBPJDLOG?=	${LIBDESTDIR}${LIBDIR_BASE}/libpjdlog.a
LIBPMC?=	${LIBDESTDIR}${LIBDIR_BASE}/libpmc.a
LIBPROC?=	${LIBDESTDIR}${LIBDIR_BASE}/libproc.a
LIBPROCSTAT?=	${LIBDESTDIR}${LIBDIR_BASE}/libprocstat.a
LIBPTHREAD?=	${LIBDESTDIR}${LIBDIR_BASE}/libpthread.a
LIBRADIUS?=	${LIBDESTDIR}${LIBDIR_BASE}/libradius.a
LIBRDMACM?=	${LIBDESTDIR}${LIBDIR_BASE}/librdmacm.a
LIBREGEX?=	${LIBDESTDIR}${LIBDIR_BASE}/libregex.a
LIBROKEN?=	${LIBDESTDIR}${LIBDIR_BASE}/libroken.a
LIBRPCSEC_GSS?=	${LIBDESTDIR}${LIBDIR_BASE}/librpcsec_gss.a
LIBRPCSVC?=	${LIBDESTDIR}${LIBDIR_BASE}/librpcsvc.a
LIBRT?=		${LIBDESTDIR}${LIBDIR_BASE}/librt.a
LIBRTLD_DB?=	${LIBDESTDIR}${LIBDIR_BASE}/librtld_db.a
LIBSBUF?=	${LIBDESTDIR}${LIBDIR_BASE}/libsbuf.a
LIBSDP?=	${LIBDESTDIR}${LIBDIR_BASE}/libsdp.a
LIBSMB?=	${LIBDESTDIR}${LIBDIR_BASE}/libsmb.a
LIBSSL?=	${LIBDESTDIR}${LIBDIR_BASE}/libssl.a
LIBSSP_NONSHARED?=	${LIBDESTDIR}${LIBDIR_BASE}/libssp_nonshared.a
LIBSTDCPLUSPLUS?= ${LIBDESTDIR}${LIBDIR_BASE}/libstdc++.a
LIBSTDTHREADS?=	${LIBDESTDIR}${LIBDIR_BASE}/libstdthreads.a
LIBSYSDECODE?=	${LIBDESTDIR}${LIBDIR_BASE}/libsysdecode.a
LIBTACPLUS?=	${LIBDESTDIR}${LIBDIR_BASE}/libtacplus.a
LIBTERMCAP?=	${LIBDESTDIR}${LIBDIR_BASE}/libtermcap.a
LIBTERMCAPW?=	${LIBDESTDIR}${LIBDIR_BASE}/libtermcapw.a
LIBTERMLIB?=	"don't use LIBTERMLIB, use LIBTERMCAP"
LIBTINFO?=	"don't use LIBTINFO, use LIBNCURSES"
LIBUFS?=	${LIBDESTDIR}${LIBDIR_BASE}/libufs.a
LIBUGIDFW?=	${LIBDESTDIR}${LIBDIR_BASE}/libugidfw.a
LIBULOG?=	${LIBDESTDIR}${LIBDIR_BASE}/libulog.a
LIBUMEM?=	${LIBDESTDIR}${LIBDIR_BASE}/libumem.a
LIBUSB?=	${LIBDESTDIR}${LIBDIR_BASE}/libusb.a
LIBUSBHID?=	${LIBDESTDIR}${LIBDIR_BASE}/libusbhid.a
LIBUTIL?=	${LIBDESTDIR}${LIBDIR_BASE}/libutil.a
LIBUUTIL?=	${LIBDESTDIR}${LIBDIR_BASE}/libuutil.a
LIBVGL?=	${LIBDESTDIR}${LIBDIR_BASE}/libvgl.a
LIBVMMAPI?=	${LIBDESTDIR}${LIBDIR_BASE}/libvmmapi.a
LIBWIND?=	${LIBDESTDIR}${LIBDIR_BASE}/libwind.a
LIBWRAP?=	${LIBDESTDIR}${LIBDIR_BASE}/libwrap.a
LIBXO?=		${LIBDESTDIR}${LIBDIR_BASE}/libxo.a
LIBXPG4?=	${LIBDESTDIR}${LIBDIR_BASE}/libxpg4.a
LIBY?=		${LIBDESTDIR}${LIBDIR_BASE}/liby.a
LIBYPCLNT?=	${LIBDESTDIR}${LIBDIR_BASE}/libypclnt.a
LIBZ?=		${LIBDESTDIR}${LIBDIR_BASE}/libz.a
LIBZFS?=	${LIBDESTDIR}${LIBDIR_BASE}/libzfs.a
LIBZFS_CORE?=	${LIBDESTDIR}${LIBDIR_BASE}/libzfs_core.a
LIBZPOOL?=	${LIBDESTDIR}${LIBDIR_BASE}/libzpool.a

# enforce the 2 -lpthread and -lc to always be the last in that exact order
.if defined(LDADD)
.if ${LDADD:M-lpthread}
LDADD:=	${LDADD:N-lpthread} -lpthread
.endif
.if ${LDADD:M-lc}
LDADD:=	${LDADD:N-lc} -lc
.endif
.endif

# Only do this for src builds.
.if defined(SRCTOP)
.if defined(_LIBRARIES) && defined(LIB) && \
    ${_LIBRARIES:M${LIB}} != ""
.if !defined(LIB${LIB:tu})
.error ${.CURDIR}: Missing value for LIB${LIB:tu} in ${_this:T}.  Likely should be: LIB${LIB:tu}?= $${LIBDESTDIR}$${LIBDIR_BASE}/lib${LIB}.a
.endif
.endif

# Derive LIB*SRCDIR from LIB*DIR
.for lib in ${_LIBRARIES}
LIB${lib:tu}SRCDIR?=	${SRCTOP}/${LIB${lib:tu}DIR:S,^${OBJTOP}/,,}
.endfor
.else

# Out of tree builds

# There are LIBADD defined in an out-of-tree build.  Are they *all*
# in-tree libraries?  If so convert them to LDADD to support
# partial checkouts.
.if !empty(LIBADD)
_convert_libadd=	1
.for l in ${LIBADD}
.if empty(LIB${l:tu})
_convert_libadd=	0
.endif
.endfor
.if ${_convert_libadd} == 1
.warning Converting out-of-tree build LIBADDs into LDADD.  This is not fully supported.
.for l in ${LIBADD}
LDADD+=	-l${l}
.endfor
.endif
.endif

.endif	# defined(SRCTOP)
