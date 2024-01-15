# _arch is undefined if /usr/lib/rpm/platform/*/macros was not included.
%{!?_arch: %define _arch dummy}
%{!?make: %define make make}
%define makeflags %{?_smp_mflags} ARCH=%{ARCH}
%define __spec_install_post /usr/lib/rpm/brp-compress || :
%define debug_package %{nil}

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
BuildRequires: (elfutils-libelf-devel or libelf-devel) flex
BuildRequires: gcc make openssl openssl-devel perl python3 rsync

%description
The Linux Kernel, the operating system core itself

%package headers
Summary: Header files for the Linux kernel for use by glibc
Group: Development/System
Obsoletes: kernel-headers
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

%prep
%setup -q -n linux
cp %{SOURCE1} .config
patch -p1 < %{SOURCE2}

%build
%{make} %{makeflags} KERNELRELEASE=%{KERNELRELEASE} KBUILD_BUILD_VERSION=%{release}

%install
mkdir -p %{buildroot}/boot
%ifarch ia64
mkdir -p %{buildroot}/boot/efi
cp $(%{make} %{makeflags} -s image_name) %{buildroot}/boot/efi/vmlinuz-%{KERNELRELEASE}
ln -s efi/vmlinuz-%{KERNELRELEASE} %{buildroot}/boot/
%else
cp $(%{make} %{makeflags} -s image_name) %{buildroot}/boot/vmlinuz-%{KERNELRELEASE}
%endif
%{make} %{makeflags} INSTALL_MOD_PATH=%{buildroot} modules_install
%{make} %{makeflags} INSTALL_HDR_PATH=%{buildroot}/usr headers_install
cp System.map %{buildroot}/boot/System.map-%{KERNELRELEASE}
cp .config %{buildroot}/boot/config-%{KERNELRELEASE}
ln -fns /usr/src/kernels/%{KERNELRELEASE} %{buildroot}/lib/modules/%{KERNELRELEASE}/build
%if %{with_devel}
%{make} %{makeflags} run-command KBUILD_RUN_COMMAND='${srctree}/scripts/package/install-extmod-build %{buildroot}/usr/src/kernels/%{KERNELRELEASE}'
%endif

%clean
rm -rf %{buildroot}

%post
if [ -x /sbin/installkernel -a -r /boot/vmlinuz-%{KERNELRELEASE} -a -r /boot/System.map-%{KERNELRELEASE} ]; then
cp /boot/vmlinuz-%{KERNELRELEASE} /boot/.vmlinuz-%{KERNELRELEASE}-rpm
cp /boot/System.map-%{KERNELRELEASE} /boot/.System.map-%{KERNELRELEASE}-rpm
rm -f /boot/vmlinuz-%{KERNELRELEASE} /boot/System.map-%{KERNELRELEASE}
/sbin/installkernel %{KERNELRELEASE} /boot/.vmlinuz-%{KERNELRELEASE}-rpm /boot/.System.map-%{KERNELRELEASE}-rpm
rm -f /boot/.vmlinuz-%{KERNELRELEASE}-rpm /boot/.System.map-%{KERNELRELEASE}-rpm
fi

%preun
if [ -x /sbin/new-kernel-pkg ]; then
new-kernel-pkg --remove %{KERNELRELEASE} --rminitrd --initrdfile=/boot/initramfs-%{KERNELRELEASE}.img
elif [ -x /usr/bin/kernel-install ]; then
kernel-install remove %{KERNELRELEASE}
fi

%postun
if [ -x /sbin/update-bootloader ]; then
/sbin/update-bootloader --remove %{KERNELRELEASE}
fi

%files
%defattr (-, root, root)
/lib/modules/%{KERNELRELEASE}
%exclude /lib/modules/%{KERNELRELEASE}/build
/boot/*

%files headers
%defattr (-, root, root)
/usr/include

%if %{with_devel}
%files devel
%defattr (-, root, root)
/usr/src/kernels/%{KERNELRELEASE}
/lib/modules/%{KERNELRELEASE}/build
%endif
