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

# Do we have rpmbuild, otherwise fall back to the older rpm
RPM := $(shell if [ -x "/usr/bin/rpmbuild" ]; then echo rpmbuild; \
	           else echo rpm; fi)

# Remove hyphens since they have special meaning in RPM filenames
KERNELPATH := kernel-$(subst -,,$(KERNELRELEASE))
MKSPEC     := $(srctree)/scripts/package/mkspec
PREV       := set -e; cd ..;

# rpm-pkg
# ---------------------------------------------------------------------------
$(objtree)/kernel.spec: $(MKSPEC) $(srctree)/Makefile
	$(CONFIG_SHELL) $(MKSPEC) > $@

rpm-pkg rpm: $(objtree)/kernel.spec FORCE
	@if test -n "$(KBUILD_OUTPUT)"; then \
		echo "Building source + binary RPM is not possible outside the"; \
		echo "kernel source tree. Don't set KBUILD_OUTPUT, or use the"; \
		echo "binrpm-pkg target instead."; \
		false; \
	fi
	$(MAKE) clean
	$(PREV) ln -sf $(srctree) $(KERNELPATH)
	$(CONFIG_SHELL) $(srctree)/scripts/setlocalversion > $(objtree)/.scmversion
	$(PREV) tar -cz $(RCS_TAR_IGNORE) -f $(KERNELPATH).tar.gz $(KERNELPATH)/.
	$(PREV) rm $(KERNELPATH)
	rm -f $(objtree)/.scmversion
	set -e; \
	$(CONFIG_SHELL) $(srctree)/scripts/mkversion > $(objtree)/.tmp_version
	set -e; \
	mv -f $(objtree)/.tmp_version $(objtree)/.version

	$(RPM) $(RPMOPTS) --target $(UTS_MACHINE) -ta ../$(KERNELPATH).tar.gz
	rm ../$(KERNELPATH).tar.gz

clean-files := $(objtree)/kernel.spec

# binrpm-pkg
# ---------------------------------------------------------------------------
$(objtree)/binkernel.spec: $(MKSPEC) $(srctree)/Makefile
	$(CONFIG_SHELL) $(MKSPEC) prebuilt > $@

binrpm-pkg: $(objtree)/binkernel.spec FORCE
	$(MAKE) KBUILD_SRC=
	set -e; \
	$(CONFIG_SHELL) $(srctree)/scripts/mkversion > $(objtree)/.tmp_version
	set -e; \
	mv -f $(objtree)/.tmp_version $(objtree)/.version

	$(RPM) $(RPMOPTS) --define "_builddir $(objtree)" --target \
		$(UTS_MACHINE) -bb $<

clean-files += $(objtree)/binkernel.spec

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
		$(srctree)/scripts/package/builddeb

deb-pkg: FORCE
	$(MAKE) KBUILD_SRC=
	$(call cmd,builddeb)

clean-dirs += $(objtree)/debian/


# tarball targets
# ---------------------------------------------------------------------------
tar%pkg: FORCE
	$(MAKE) KBUILD_SRC=
	$(CONFIG_SHELL) $(srctree)/scripts/package/buildtar $@

clean-dirs += $(objtree)/tar-install/


# Help text displayed when executing 'make help'
# ---------------------------------------------------------------------------
help: FORCE
	@echo '  rpm-pkg         - Build both source and binary RPM kernel packages'
	@echo '  binrpm-pkg      - Build only the binary kernel package'
	@echo '  deb-pkg         - Build the kernel as an deb package'
	@echo '  tar-pkg         - Build the kernel as an uncompressed tarball'
	@echo '  targz-pkg       - Build the kernel as a gzip compressed tarball'
	@echo '  tarbz2-pkg      - Build the kernel as a bzip2 compressed tarball'

