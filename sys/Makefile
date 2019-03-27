# $FreeBSD$

# Directories to include in cscope name file and TAGS.
CSCOPEDIRS=	bsm cam cddl compat conf contrib crypto ddb dev fs gdb \
		geom gnu isa kern libkern modules net net80211 \
		netgraph netinet netinet6 netipsec netpfil \
		netsmb nfs nfsclient nfsserver nlm ofed opencrypto \
		rpc security sys ufs vm xdr xen ${CSCOPE_ARCHDIR}
.if !defined(CSCOPE_ARCHDIR)
.if defined(ALL_ARCH)
CSCOPE_ARCHDIR = amd64 arm arm64 i386 mips powerpc riscv sparc64 x86
.else
CSCOPE_ARCHDIR = ${MACHINE}
.if ${MACHINE} != ${MACHINE_CPUARCH}
CSCOPE_ARCHDIR += ${MACHINE_CPUARCH}
.endif
.if ${MACHINE_CPUARCH} == "i386" || ${MACHINE_CPUARCH} == "amd64"
CSCOPE_ARCHDIR += x86
.endif
.endif
.endif

HTAGSFLAGS+= -at `awk -F= '/^RELEASE *=/{release=$2}; END {print "FreeBSD", release, "kernel"}' < conf/newvers.sh`

# You need the devel/cscope port for this.
cscope: cscope.out
cscope.out: ${.CURDIR}/cscope.files
	cd ${.CURDIR}; cscope -k -buq -p4 -v

${.CURDIR}/cscope.files: .PHONY
	cd ${.CURDIR}; \
		find ${CSCOPEDIRS} -name "*.[chSsly]" -a -type f > ${.TARGET}

cscope-clean:
	cd ${.CURDIR}; \
	    rm -f cscope.files cscope.out cscope.in.out cscope.po.out

#
# Installs SCM hooks to update the cscope database every time the source tree
# is updated.
# cscope understands incremental updates, so it's considerably faster when only
# a few files have changed.
#
HG_DIR=${.CURDIR}/../.hg
HG_HOOK=if [ \$$HG_ERROR -eq 0 ]; then cd sys && make -m ../share/mk cscope; fi
cscope-hook:
	@if [ -d ${HG_DIR} ]; then 					\
		if [ "`grep hooks ${HG_DIR}/hgrc`" = "" ]; then		\
			echo "[hooks]" >> ${HG_DIR}/hgrc;		\
			echo "update = ${HG_HOOK}" >> ${HG_DIR}/hgrc;	\
			echo "Hook installed in ${HG_DIR}/hgrc";	\
		else 							\
			echo "Mercurial update hook already exists.";	\
		fi;							\
	fi

# You need the devel/global and one of editor/emacs* ports for that.
TAGS ${.CURDIR}/TAGS: ${.CURDIR}/cscope.files
	rm -f ${.CURDIR}/TAGS
	cd ${.CURDIR}; xargs etags -a < ${.CURDIR}/cscope.files

.if !(make(cscope) || make(cscope-clean) || make(cscope-hook) || make(TAGS))
.include <src.opts.mk>

# Loadable kernel modules

.if defined(MODULES_WITH_WORLD)
SUBDIR+=modules
.endif

.include <bsd.subdir.mk>
.endif
