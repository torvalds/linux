#	$OpenBSD: Makefile,v 1.232 2024/08/12 20:56:55 guenther Exp $
#	$NetBSD: Makefile,v 1.59 1996/05/15 21:36:43 jtc Exp $

#	@(#)Makefile	5.45.1.1 (Berkeley) 5/6/91

# The ``rm -rf''s used below are safe because rm doesn't follow symbolic
# links.


.include <bsd.own.mk>

FILES=	a.out.h ar.h asr.h assert.h \
	bitstring.h blf.h bsd_auth.h \
	complex.h cpio.h ctype.h curses.h \
	db.h dirent.h dlfcn.h \
	elf.h elf_abi.h err.h errno.h \
	fenv.h float.h fnmatch.h fstab.h fts.h ftw.h \
	getopt.h glob.h grp.h \
	icdb.h ieeefp.h ifaddrs.h inttypes.h iso646.h \
	kvm.h \
	langinfo.h libgen.h limits.h link.h link_elf.h locale.h login_cap.h \
	math.h md5.h memory.h \
	ndbm.h netdb.h netgroup.h nlist.h nl_types.h \
	paths.h poll.h pthread.h pthread_np.h pwd.h \
	ranlib.h readpassphrase.h regex.h resolv.h rmd160.h \
	sched.h search.h setjmp.h semaphore.h sha1.h sha2.h \
	signal.h siphash.h sndio.h spawn.h stdbool.h stddef.h \
	stdio.h stdio_ext.h stdlib.h string.h strings.h sysexits.h \
	tar.h tgmath.h tib.h time.h ttyent.h \
	uchar.h unistd.h utime.h utmp.h uuid.h \
	vis.h  \
	wchar.h wctype.h

MFILES=	frame.h
LFILES=	endian.h fcntl.h syslog.h termios.h stdarg.h stdint.h varargs.h

DIRS=	arpa protocols rpc rpcsvc
LDIRS=	crypto ddb dev isofs miscfs msdosfs net netinet netinet6 \
	netmpls net80211 nfs ntfs scsi sys ufs uvm

# Places that need a prerequisite target met before includes
PRDIRS=	../lib/libcrypto ../lib/librpcsvc

# Directories with an includes target
RDIRS=	../lib/libagentx ../lib/libcbor ../lib/libcurses ../lib/libedit \
	../lib/libelf ../lib/libevent ../lib/libexpat ../lib/libfido2 \
	../lib/libform ../lib/libfuse ../lib/libkeynote ../lib/libmenu \
	../lib/libossaudio ../lib/libpanel ../lib/libpcap \
	../lib/libradius \
	../lib/libskey ../lib/libssl  ../lib/libtls \
	../lib/libusbhid ../lib/libutil ../lib/libz \
	../usr.bin/lex ../gnu/lib/libexecinfo ../gnu/lib/libreadline \
	../sys/arch/${MACHINE}

.if ${BUILD_GCC3:L} == "yes"
RDIRS+= ../gnu/usr.bin/gcc
PRDIRS+= ../gnu/lib/libstdc++
.endif
.if ${BUILD_GCC4:L} == "yes"
PRDIRS+= ../gnu/lib/libstdc++-v3 ../gnu/usr.bin/cc/include
.endif
.if ${BUILD_CLANG:L} == "yes"
RDIRS+= ../gnu/lib/libcxxabi ../gnu/lib/libcxx \
	 ../gnu/usr.bin/clang/libLLVM
.endif

# prereq implies includes
RDIRS += ${PRDIRS}

NOOBJ=	noobj

# Change SYS_INCLUDE in bsd.own.mk to "symlinks" if you don't want copies
SYS_INCLUDE?=	copies

RUN_MAKE = if test -f Makefile.bsd-wrapper; then ${MAKE} -f Makefile.bsd-wrapper $@; else ${MAKE} $@; fi

prereq:
	@for i in ${PRDIRS}; do \
		echo preparing in ${.CURDIR}/$$i; \
		cd ${.CURDIR}/$$i && ${RUN_MAKE}; \
	done

includes:
	@echo installing ${FILES}
	@for i in ${FILES}; do \
		cmp -s $$i ${DESTDIR}/usr/include/$$i || \
		    ${INSTALL} ${INSTALL_COPY} -m 444 $$i ${DESTDIR}/usr/include/$$i; \
	done
	@echo installing ${DIRS}
	@for i in ${DIRS}; do \
		${INSTALL} -d -o ${BINOWN} -g ${BINGRP} -m 755 \
			${DESTDIR}/usr/include/$$i; \
		cd ${.CURDIR}/$$i && for j in *.[ih]; do \
			cmp -s $$j ${DESTDIR}/usr/include/$$i/$$j || \
			${INSTALL} ${INSTALL_COPY} -m 444 $$j ${DESTDIR}/usr/include/$$i/$$j; \
		done; \
	done
	@echo installing ${LFILES}
	@for i in ${LFILES}; do \
		rm -f ${DESTDIR}/usr/include/$$i && \
		ln -s sys/$$i ${DESTDIR}/usr/include/$$i; \
	done
	@echo installing ${MFILES}
	@for i in ${MFILES}; do \
		rm -f ${DESTDIR}/usr/include/$$i && \
		ln -s machine/$$i ${DESTDIR}/usr/include/$$i; \
	done
	@for i in ${RDIRS}; do \
		echo installing in ${.CURDIR}/$$i; \
		cd ${.CURDIR}/$$i && ${RUN_MAKE}; \
	done
	chown -RP ${BINOWN}:${BINGRP} ${DESTDIR}/usr/include
	find ${DESTDIR}/usr/include \
		-type f -exec chmod a=r {} + -o \
		\( -type d -o -type l \) -exec chmod -h u=rwx,go=rx {} +

copies:
	@echo copies: ${LDIRS}
	@for i in ${LDIRS}; do \
		rm -rf ${DESTDIR}/usr/include/$$i && \
		${INSTALL} -d -o ${BINOWN} -g ${BINGRP} -m 755 \
			${DESTDIR}/usr/include/$$i ; \
	done
	cd ../sys; \
	pax -rw -pa -L \
	    `find ${LDIRS} -follow -type f -name '*.h' '!' \
	    '(' -path 'dev/microcode/*' -or -path 'dev/pci/drm/*' ')' \
	    -print` ${DESTDIR}/usr/include
	cd ${DESTDIR}/usr/include && rm -rf ${MACHINE} ${MACHINE_CPU} machine
	${INSTALL} -d -o ${BINOWN} -g ${BINGRP} -m 755 \
		${DESTDIR}/usr/include/${MACHINE}
	pax -rw -pa -s "|\.\./sys/arch/${MACHINE}/include||" \
	    ../sys/arch/${MACHINE}/include/*.h \
	    ${DESTDIR}/usr/include/${MACHINE}
	if test ${MACHINE} != ${MACHINE_CPU} -a \
	    -d ../sys/arch/${MACHINE_CPU}/include; then \
		${INSTALL} -d -o ${BINOWN} -g ${BINGRP} -m 755 \
	    	    ${DESTDIR}/usr/include/${MACHINE_CPU}; \
		pax -rw -pa -s "|\.\./sys/arch/${MACHINE_CPU}/include||" \
		    ../sys/arch/${MACHINE_CPU}/include/*.h \
		    ${DESTDIR}/usr/include/${MACHINE_CPU}; \
	fi
	ln -sf ${MACHINE} ${DESTDIR}/usr/include/machine; \

symlinks:
	@echo symlinks: ${LDIRS}
	@for i in ${LDIRS}; do \
		rm -rf ${DESTDIR}/usr/include/$$i && \
		ln -s /sys/$$i ${DESTDIR}/usr/include/$$i; \
	done
	cd ${DESTDIR}/usr/include && rm -rf ${MACHINE} ${MACHINE_CPU} machine
	ln -s /sys/arch/${MACHINE}/include ${DESTDIR}/usr/include/${MACHINE}
	if test ${MACHINE} != ${MACHINE_CPU} -a \
	    -d ../sys/arch/${MACHINE_CPU}/include ; then \
		ln -s /sys/arch/${MACHINE_CPU}/include \
		    ${DESTDIR}/usr/include/${MACHINE_CPU} ; \
	fi
	ln -sf ${MACHINE} ${DESTDIR}/usr/include/machine

includes: ${SYS_INCLUDE}

.PHONY: prereq includes copies symlink
.include <bsd.prog.mk>
