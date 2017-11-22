ACLOCAL_AMFLAGS = -I config

include config/rpm.am
include config/deb.am
include config/tgz.am


if CONFIG_USER

endif
if CONFIG_KERNEL
SUBDIRS = module include

extradir = @prefix@/src/zfs-$(VERSION)
extra_HEADERS = zfs.release.in zfs_config.h.in

kerneldir = @prefix@/src/zfs-$(VERSION)/$(LINUX_VERSION)
nodist_kernel_HEADERS = zfs.release zfs_config.h module/$(LINUX_SYMBOLS)
endif

AUTOMAKE_OPTIONS = foreign
EXTRA_DIST  = autogen.sh copy-builtin
EXTRA_DIST += config/config.awk config/rpm.am config/deb.am config/tgz.am
EXTRA_DIST += META DISCLAIMER COPYRIGHT README.markdown OPENSOLARIS.LICENSE

distclean-local::
	-$(RM) -R autom4te*.cache
	-find . \( -name SCCS -o -name BitKeeper -o -name .svn -o -name CVS \
		-o -name .pc -o -name .hg -o -name .git \) -prune -o \
		\( -name '*.orig' -o -name '*.rej' -o -name '*~' \
		-o -name '*.bak' -o -name '#*#' -o -name '.*.orig' \
		-o -name '.*.rej' -o -name '.script-config' -o -size 0 \
		-o -name '*%' -o -name '.*.cmd' -o -name 'core' \
		-o -name 'Makefile' -o -name 'Module.symvers' \
		-o -name '*.order' -o -name '*.markers' \) \
		-type f -print | xargs $(RM)

dist-hook:
	sed -i 's/Release:[[:print:]]*/Release:      $(RELEASE)/' \
		$(distdir)/META

checkstyle: cstyle shellcheck flake8 commitcheck

commitcheck:
	@if git rev-parse --git-dir > /dev/null 2>&1; then \
		scripts/commitcheck.sh; \
	fi

cstyle:
	@find ${top_srcdir} -name '*.[hc]' ! -name 'zfs_config.*' \
		! -name '*.mod.c' -type f -exec scripts/cstyle.pl -cpP {} \+

shellcheck:
	@if type shellcheck > /dev/null 2>&1; then \
		shellcheck --exclude=SC1090 --format=gcc scripts/paxcheck.sh \
			scripts/zloop.sh \
			scripts/zfs-tests.sh \
			scripts/zfs.sh \
			scripts/commitcheck.sh \
			$$(find cmd/zed/zed.d/*.sh -type f) \
			$$(find cmd/zpool/zpool.d/* -executable); \
	fi

lint: cppcheck paxcheck

cppcheck:
	@if type cppcheck > /dev/null 2>&1; then \
		cppcheck --quiet --force --error-exitcode=2 \
			--suppressions-list=.github/suppressions.txt \
			-UHAVE_SSE2 -UHAVE_AVX512F \
			${top_srcdir}; \
	fi

paxcheck:
	@if type scanelf > /dev/null 2>&1; then \
		scripts/paxcheck.sh ${top_srcdir}; \
	fi

flake8:
	@if type flake8 > /dev/null 2>&1; then \
		flake8 ${top_srcdir}; \
	fi

ctags:
	$(RM) tags
	find $(top_srcdir) -name .git -prune -o -name '*.[hc]' | xargs ctags

etags:
	$(RM) TAGS
	find $(top_srcdir) -name .pc -prune -o -name '*.[hc]' | xargs etags -a

tags: ctags etags

pkg: @DEFAULT_PACKAGE@
pkg-dkms: @DEFAULT_PACKAGE@-dkms
pkg-kmod: @DEFAULT_PACKAGE@-kmod
pkg-utils: @DEFAULT_PACKAGE@-utils
