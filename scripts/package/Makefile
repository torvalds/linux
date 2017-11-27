# Makefile for the different targets used to generate full packages of a kernel
# It uses the generic clean infrastructure of kbuild

# RPM target
# ---------------------------------------------------------------------------
# The rpm target generates two rpm files:
# /usr/src/packages/SRPMS/kernel-2.6.7rc2-1.src.rpm
# /usr/src/packages/RPMS/i386/kernel-2.6.7rc2-1.<arch>.rpm
# The src.rpm files includes all source for the kernel being built
# The <arch>.rpm includes kernel configuration, modules etc.
#
# Process to create the rpm files
# a) clean the kernel
# b) Generate .spec file
# c) Build a tar ball, using symlink to make kernel version
#    first entry in the path
# d) and pack the result to a tar.gz file
# e) generate the rpm files, based on kernel.spec
# - Use /. to avoid tar packing just the symlink

# Note that the rpm-pkg target cannot be used with KBUILD_OUTPUT,
# but the binrpm-pkg target can; for some reason O= gets ignored.

# Remove hyphens since they have special meaning in RPM filenames
KERNELPATH := kernel-$(subst -,_,$(KERNELRELEASE))
KDEB_SOURCENAME ?= linux-$(KERNELRELEASE)
export KDEB_SOURCENAME
# Include only those top-level files that are needed by make, plus the GPL copy
TAR_CONTENT := $(KBUILD_ALLDIRS) .config .scmversion Makefile \
               Kbuild Kconfig COPYING $(wildcard localversion*)
MKSPEC     := $(srctree)/scripts/package/mkspec

quiet_cmd_src_tar = TAR     $(2).tar.gz
      cmd_src_tar = \
if test "$(objtree)" != "$(srctree)"; then \
	echo "Building source tarball is not possible outside the"; \
	echo "kernel source tree. Don't set KBUILD_OUTPUT, or use the"; \
	echo "binrpm-pkg or bindeb-pkg target instead."; \
	false; \
fi ; \
$(srctree)/scripts/setlocalversion --save-scmversion; \
tar -cz $(RCS_TAR_IGNORE) -f $(2).tar.gz \
	--transform 's:^:$(2)/:S' $(TAR_CONTENT) $(3); \
rm -f $(objtree)/.scmversion

# rpm-pkg
# ---------------------------------------------------------------------------
rpm-pkg: FORCE
	$(MAKE) clean
	$(CONFIG_SHELL) $(MKSPEC) >$(objtree)/kernel.spec
	$(call cmd,src_tar,$(KERNELPATH),kernel.spec)
	+rpmbuild $(RPMOPTS) --target $(UTS_MACHINE) -ta $(KERNELPATH).tar.gz \
	--define='_smp_mflags %{nil}'

# binrpm-pkg
# ---------------------------------------------------------------------------
binrpm-pkg: FORCE
	$(MAKE) KBUILD_SRC=
	$(CONFIG_SHELL) $(MKSPEC) prebuilt > $(objtree)/binkernel.spec
	+rpmbuild $(RPMOPTS) --define "_builddir $(objtree)" --target \
		$(UTS_MACHINE) -bb $(objtree)/binkernel.spec

clean-files += $(objtree)/*.spec

# Deb target
# ---------------------------------------------------------------------------
quiet_cmd_builddeb = BUILDDEB
      cmd_builddeb = set -e; \
	test `id -u` = 0 || \
	test -n "$(KBUILD_PKG_ROOTCMD)" || { \
		which fakeroot >/dev/null 2>&1 && \
		KBUILD_PKG_ROOTCMD="fakeroot -u"; \
	} || { \
		echo; \
		echo "builddeb must be run as root (or using fakeroot)."; \
		echo "KBUILD_PKG_ROOTCMD is unset and fakeroot not found."; \
		echo "Try setting KBUILD_PKG_ROOTCMD to a command to acquire"; \
		echo "root privileges (e.g., 'fakeroot -u' or 'sudo')."; \
		false; \
	} && \
	\
	$$KBUILD_PKG_ROOTCMD $(CONFIG_SHELL) \
		$(srctree)/scripts/package/builddeb $@

deb-pkg: FORCE
	$(MAKE) clean
	$(call cmd,src_tar,$(KDEB_SOURCENAME))
	$(MAKE) KBUILD_SRC=
	+$(call cmd,builddeb)

bindeb-pkg: FORCE
	$(MAKE) KBUILD_SRC=
	+$(call cmd,builddeb)

clean-dirs += $(objtree)/debian/

# snap-pkg
# ---------------------------------------------------------------------------
snap-pkg: FORCE
	rm -rf $(objtree)/snap
	mkdir $(objtree)/snap
	$(MAKE) clean
	$(call cmd,src_tar,$(KERNELPATH))
	sed "s@KERNELRELEASE@$(KERNELRELEASE)@; \
		s@SRCTREE@$(shell realpath $(KERNELPATH).tar.gz)@" \
		$(srctree)/scripts/package/snapcraft.template > \
		$(objtree)/snap/snapcraft.yaml
	cd $(objtree)/snap && \
	snapcraft --target-arch=$(UTS_MACHINE)

clean-dirs += $(objtree)/snap/

# tarball targets
# ---------------------------------------------------------------------------
tar%pkg: FORCE
	$(MAKE) KBUILD_SRC=
	$(CONFIG_SHELL) $(srctree)/scripts/package/buildtar $@

clean-dirs += $(objtree)/tar-install/


# perf-pkg - generate a source tarball with perf source
# ---------------------------------------------------------------------------

perf-tar=perf-$(KERNELVERSION)

quiet_cmd_perf_tar = TAR
      cmd_perf_tar = \
git --git-dir=$(srctree)/.git archive --prefix=$(perf-tar)/         \
	HEAD^{tree} $$(cd $(srctree);                               \
		       echo $$(cat tools/perf/MANIFEST)) \
	-o $(perf-tar).tar;                                         \
mkdir -p $(perf-tar);                                               \
git --git-dir=$(srctree)/.git rev-parse HEAD > $(perf-tar)/HEAD;    \
(cd $(srctree)/tools/perf;                                          \
util/PERF-VERSION-GEN $(CURDIR)/$(perf-tar)/);              \
tar rf $(perf-tar).tar $(perf-tar)/HEAD $(perf-tar)/PERF-VERSION-FILE; \
rm -r $(perf-tar);                                                  \
$(if $(findstring tar-src,$@),,                                     \
$(if $(findstring bz2,$@),bzip2,                                    \
$(if $(findstring gz,$@),gzip,                                      \
$(if $(findstring xz,$@),xz,                                        \
$(error unknown target $@))))                                       \
	-f -9 $(perf-tar).tar)

perf-%pkg: FORCE
	$(call cmd,perf_tar)

# Help text displayed when executing 'make help'
# ---------------------------------------------------------------------------
help: FORCE
	@echo '  rpm-pkg             - Build both source and binary RPM kernel packages'
	@echo '  binrpm-pkg          - Build only the binary kernel RPM package'
	@echo '  deb-pkg             - Build both source and binary deb kernel packages'
	@echo '  bindeb-pkg          - Build only the binary kernel deb package'
	@echo '  snap-pkg            - Build only the binary kernel snap package (will connect to external hosts)'
	@echo '  tar-pkg             - Build the kernel as an uncompressed tarball'
	@echo '  targz-pkg           - Build the kernel as a gzip compressed tarball'
	@echo '  tarbz2-pkg          - Build the kernel as a bzip2 compressed tarball'
	@echo '  tarxz-pkg           - Build the kernel as a xz compressed tarball'
	@echo '  perf-tar-src-pkg    - Build $(perf-tar).tar source tarball'
	@echo '  perf-targz-src-pkg  - Build $(perf-tar).tar.gz source tarball'
	@echo '  perf-tarbz2-src-pkg - Build $(perf-tar).tar.bz2 source tarball'
	@echo '  perf-tarxz-src-pkg  - Build $(perf-tar).tar.xz source tarball'
