# _arch is undefined if /usr/lib/rpm/platform/*/macros was not included.
%{!?_arch: %define _arch dummy}
%{!?make: %define make make}
%define makeflags %{?_smp_mflags} ARCH=%{ARCH}

Name: kernel
Summary: The Linux Kernel
Version: %(echo %{KERNELRELEASE} | sed -e 's/-/_/g')
Release: %{pkg_release}
License: GPL
Group: System Environment/Kernel
Vendor: The Linux Community
URL: https://www.kernel.org
Source0: linux.tar.gz
Source1: config
Source2: diff.patch
Provides: kernel-%{KERNELRELEASE}
BuildRequires: bc binutils bison dwarves
BuildRequires: (elfutils-devel or libdw-devel)
BuildRequires: (elfutils-libelf-devel or libelf-devel) flex
BuildRequires: gcc make openssl openssl-devel perl python3 rsync

%description
The Linux Kernel, the operating system core itself

%package headers
Summary: Header files for the Linux kernel for use by glibc
Group: Development/System
Obsoletes: kernel-headers < %{version}
Provides: kernel-headers = %{version}
%description headers
Kernel-headers includes the C header files that specify the interface
between the Linux kernel and userspace libraries and programs.  The
header files define structures and constants that are needed for
building most standard programs and are also needed for rebuilding the
glibc package.

%if %{with_devel}
%package devel
Summary: Development package for building kernel modules to match the %{version} kernel
Group: System Environment/Kernel
AutoReqProv: no
%description -n kernel-devel
This package provides kernel headers and makefiles sufficient to build modules
against the %{version} kernel package.
%endif

%if %{with_debuginfo}
# list of debuginfo-related options taken from distribution kernel.spec
# files
%undefine _include_minidebuginfo
%undefine _find_debuginfo_dwz_opts
%undefine _unique_build_ids
%undefine _unique_debug_names
%undefine _unique_debug_srcs
%undefine _debugsource_packages
%undefine _debuginfo_subpackages
%global _find_debuginfo_opts -r
%global _missing_build_ids_terminate_build 1
%global _no_recompute_build_ids 1
%{debug_package}
%endif
# some (but not all) versions of rpmbuild emit %%debug_package with
# %%install. since we've already emitted it manually, that would cause
# a package redefinition error. ensure that doesn't happen
%define debug_package %{nil}

# later, we make all modules executable so that find-debuginfo.sh strips
# them up. but they don't actually need to be executable, so remove the
# executable bit, taking care to do it _after_ find-debuginfo.sh has run
%define __spec_install_post \
	%{?__debug_package:%{__debug_install_post}} \
	%{__arch_install_post} \
	%{__os_install_post} \
	find %{buildroot}/lib/modules/%{KERNELRELEASE} -name "*.ko" -type f \\\
		| xargs --no-run-if-empty chmod u-x

%prep
%setup -q -n linux
cp %{SOURCE1} .config
patch -p1 < %{SOURCE2}

%build
%{make} %{makeflags} KERNELRELEASE=%{KERNELRELEASE} KBUILD_BUILD_VERSION=%{release}

%install
mkdir -p %{buildroot}/lib/modules/%{KERNELRELEASE}
cp $(%{make} %{makeflags} -s image_name) %{buildroot}/lib/modules/%{KERNELRELEASE}/vmlinuz
# DEPMOD=true makes depmod no-op. We do not package depmod-generated files.
%{make} %{makeflags} INSTALL_MOD_PATH=%{buildroot} DEPMOD=true modules_install
%{make} %{makeflags} INSTALL_HDR_PATH=%{buildroot}/usr headers_install
cp System.map %{buildroot}/lib/modules/%{KERNELRELEASE}
cp .config %{buildroot}/lib/modules/%{KERNELRELEASE}/config
if %{make} %{makeflags} run-command KBUILD_RUN_COMMAND='test -d ${srctree}/arch/${SRCARCH}/boot/dts' 2>/dev/null; then
	%{make} %{makeflags} INSTALL_DTBS_PATH=%{buildroot}/lib/modules/%{KERNELRELEASE}/dtb dtbs_install
fi
ln -fns /usr/src/kernels/%{KERNELRELEASE} %{buildroot}/lib/modules/%{KERNELRELEASE}/build
%if %{with_devel}
%{make} %{makeflags} run-command KBUILD_RUN_COMMAND='${srctree}/scripts/package/install-extmod-build %{buildroot}/usr/src/kernels/%{KERNELRELEASE}'
%endif

{
	echo "/lib/modules/%{KERNELRELEASE}"

	for x in alias alias.bin builtin.alias.bin builtin.bin dep dep.bin \
				devname softdep symbols symbols.bin weakdep; do
		echo "%ghost /lib/modules/%{KERNELRELEASE}/modules.${x}"
	done

	for x in System.map config vmlinuz; do
		echo "%ghost /boot/${x}-%{KERNELRELEASE}"
	done

	if [ -d "%{buildroot}/lib/modules/%{KERNELRELEASE}/dtb" ];then
		find "%{buildroot}/lib/modules/%{KERNELRELEASE}/dtb" -printf "%%%ghost /boot/dtb-%{KERNELRELEASE}/%%P\n"
	fi

	echo "%exclude /lib/modules/%{KERNELRELEASE}/build"
} > %{buildroot}/kernel.list

# make modules executable so that find-debuginfo.sh strips them. this
# will be undone later in %%__spec_install_post
find %{buildroot}/lib/modules/%{KERNELRELEASE} -name "*.ko" -type f \
	| xargs --no-run-if-empty chmod u+x

%if %{with_debuginfo}
# copying vmlinux directly to the debug directory means it will not get
# stripped (but its source paths will still be collected + fixed up)
mkdir -p %{buildroot}/usr/lib/debug/lib/modules/%{KERNELRELEASE}
cp vmlinux %{buildroot}/usr/lib/debug/lib/modules/%{KERNELRELEASE}
%endif

%clean
rm -rf %{buildroot}
rm -f debugfiles.list debuglinks.list debugsourcefiles.list debugsources.list \
	elfbins.list

%post
if [ -x /usr/bin/kernel-install ]; then
	/usr/bin/kernel-install add %{KERNELRELEASE} /lib/modules/%{KERNELRELEASE}/vmlinuz
fi
for file in vmlinuz System.map config; do
	if ! cmp --silent "/lib/modules/%{KERNELRELEASE}/${file}" "/boot/${file}-%{KERNELRELEASE}"; then
		cp "/lib/modules/%{KERNELRELEASE}/${file}" "/boot/${file}-%{KERNELRELEASE}"
	fi
done
if [ -d "/lib/modules/%{KERNELRELEASE}/dtb" ] && \
   ! diff -rq "/lib/modules/%{KERNELRELEASE}/dtb" "/boot/dtb-%{KERNELRELEASE}" >/dev/null 2>&1; then
	rm -rf "/boot/dtb-%{KERNELRELEASE}"
	cp -r "/lib/modules/%{KERNELRELEASE}/dtb" "/boot/dtb-%{KERNELRELEASE}"
fi
if [ ! -e "/lib/modules/%{KERNELRELEASE}/modules.dep" ]; then
	/usr/sbin/depmod %{KERNELRELEASE}
fi

%preun
if [ -x /usr/bin/kernel-install ]; then
kernel-install remove %{KERNELRELEASE}
fi

%files -f %{buildroot}/kernel.list
%defattr (-, root, root)
%exclude /kernel.list

%files headers
%defattr (-, root, root)
/usr/include

%if %{with_devel}
%files devel
%defattr (-, root, root)
/usr/src/kernels/%{KERNELRELEASE}
/lib/modules/%{KERNELRELEASE}/build
%endif
