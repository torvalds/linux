#!/bin/sh
#
# builddeb 1.3
# Copyright 2003 Wichert Akkerman <wichert@wiggy.net>
#
# Simple script to generate a deb package for a Linux kernel. All the
# complexity of what to do with a kernel after it is installed or removed
# is left to other scripts and packages: they can install scripts in the
# /etc/kernel/{pre,post}{inst,rm}.d/ directories (or an alternative location
# specified in KDEB_HOOKDIR) that will be called on package install and
# removal.

set -e

create_package() {
	local pname="$1" pdir="$2"

	cp debian/copyright "$pdir/usr/share/doc/$pname/"
	cp debian/changelog "$pdir/usr/share/doc/$pname/changelog.Debian"
	gzip -9 "$pdir/usr/share/doc/$pname/changelog.Debian"
	sh -c "cd '$pdir'; find . -type f ! -path './DEBIAN/*' -printf '%P\0' \
		| xargs -r0 md5sum > DEBIAN/md5sums"

	# Fix ownership and permissions
	chown -R root:root "$pdir"
	chmod -R go-w "$pdir"

	# Attempt to find the correct Debian architecture
	local forcearch="" debarch=""
	case "$UTS_MACHINE" in
	i386|ia64|alpha)
		debarch="$UTS_MACHINE" ;;
	x86_64)
		debarch=amd64 ;;
	sparc*)
		debarch=sparc ;;
	s390*)
		debarch=s390 ;;
	ppc*)
		debarch=powerpc ;;
	parisc*)
		debarch=hppa ;;
	mips*)
		debarch=mips$(grep -q CPU_LITTLE_ENDIAN=y .config && echo el) ;;
	arm*)
		debarch=arm$(grep -q CONFIG_AEABI=y .config && echo el) ;;
	*)
		echo "" >&2
		echo "** ** **  WARNING  ** ** **" >&2
		echo "" >&2
		echo "Your architecture doesn't have it's equivalent" >&2
		echo "Debian userspace architecture defined!" >&2
		echo "Falling back to using your current userspace instead!" >&2
		echo "Please add support for $UTS_MACHINE to ${0} ..." >&2
		echo "" >&2
	esac
	if [ -n "$KBUILD_DEBARCH" ] ; then
		debarch="$KBUILD_DEBARCH"
	fi
	if [ -n "$debarch" ] ; then
		forcearch="-DArchitecture=$debarch"
	fi

	# Create the package
	dpkg-gencontrol -isp $forcearch -Vkernel:debarch="${debarch:-$(dpkg --print-architecture)}" -p$pname -P"$pdir"
	dpkg --build "$pdir" ..
}

# Some variables and settings used throughout the script
version=$KERNELRELEASE
revision=$(cat .version)
if [ -n "$KDEB_PKGVERSION" ]; then
	packageversion=$KDEB_PKGVERSION
else
	packageversion=$version-$revision
fi
tmpdir="$objtree/debian/tmp"
fwdir="$objtree/debian/fwtmp"
kernel_headers_dir="$objtree/debian/hdrtmp"
libc_headers_dir="$objtree/debian/headertmp"
packagename=linux-image-$version
fwpackagename=linux-firmware-image
kernel_headers_packagename=linux-headers-$version
libc_headers_packagename=linux-libc-dev

if [ "$ARCH" = "um" ] ; then
	packagename=user-mode-linux-$version
fi

# Setup the directory structure
rm -rf "$tmpdir" "$fwdir" "$kernel_headers_dir" "$libc_headers_dir"
mkdir -m 755 -p "$tmpdir/DEBIAN"
mkdir -p  "$tmpdir/lib" "$tmpdir/boot" "$tmpdir/usr/share/doc/$packagename"
mkdir -m 755 -p "$fwdir/DEBIAN"
mkdir -p "$fwdir/lib" "$fwdir/usr/share/doc/$fwpackagename"
mkdir -m 755 -p "$libc_headers_dir/DEBIAN"
mkdir -p "$libc_headers_dir/usr/share/doc/$libc_headers_packagename"
mkdir -m 755 -p "$kernel_headers_dir/DEBIAN"
mkdir -p "$kernel_headers_dir/usr/share/doc/$kernel_headers_packagename"
mkdir -p "$kernel_headers_dir/lib/modules/$version/"
if [ "$ARCH" = "um" ] ; then
	mkdir -p "$tmpdir/usr/lib/uml/modules/$version" "$tmpdir/usr/bin"
fi

# Build and install the kernel
if [ "$ARCH" = "um" ] ; then
	$MAKE linux
	cp System.map "$tmpdir/usr/lib/uml/modules/$version/System.map"
	cp .config "$tmpdir/usr/share/doc/$packagename/config"
	gzip "$tmpdir/usr/share/doc/$packagename/config"
	cp $KBUILD_IMAGE "$tmpdir/usr/bin/linux-$version"
else 
	cp System.map "$tmpdir/boot/System.map-$version"
	cp .config "$tmpdir/boot/config-$version"
	# Not all arches include the boot path in KBUILD_IMAGE
	if [ -e $KBUILD_IMAGE ]; then
		cp $KBUILD_IMAGE "$tmpdir/boot/vmlinuz-$version"
	else
		cp arch/$ARCH/boot/$KBUILD_IMAGE "$tmpdir/boot/vmlinuz-$version"
	fi
fi

if grep -q '^CONFIG_MODULES=y' .config ; then
	INSTALL_MOD_PATH="$tmpdir" $MAKE KBUILD_SRC= modules_install
	rm -f "$tmpdir/lib/modules/$version/build"
	rm -f "$tmpdir/lib/modules/$version/source"
	if [ "$ARCH" = "um" ] ; then
		mv "$tmpdir/lib/modules/$version"/* "$tmpdir/usr/lib/uml/modules/$version/"
		rmdir "$tmpdir/lib/modules/$version"
	fi
fi

if [ "$ARCH" != "um" ]; then
	$MAKE headers_check KBUILD_SRC=
	$MAKE headers_install KBUILD_SRC= INSTALL_HDR_PATH="$libc_headers_dir/usr"
fi

# Install the maintainer scripts
# Note: hook scripts under /etc/kernel are also executed by official Debian
# kernel packages, as well as kernel packages built using make-kpkg
debhookdir=${KDEB_HOOKDIR:-/etc/kernel}
for script in postinst postrm preinst prerm ; do
	mkdir -p "$tmpdir$debhookdir/$script.d"
	cat <<EOF > "$tmpdir/DEBIAN/$script"
#!/bin/sh

set -e

# Pass maintainer script parameters to hook scripts
export DEB_MAINT_PARAMS="\$*"

test -d $debhookdir/$script.d && run-parts --arg="$version" $debhookdir/$script.d
exit 0
EOF
	chmod 755 "$tmpdir/DEBIAN/$script"
done

# Try to determine maintainer and email values
if [ -n "$DEBEMAIL" ]; then
       email=$DEBEMAIL
elif [ -n "$EMAIL" ]; then
       email=$EMAIL
else
       email=$(id -nu)@$(hostname -f)
fi
if [ -n "$DEBFULLNAME" ]; then
       name=$DEBFULLNAME
elif [ -n "$NAME" ]; then
       name=$NAME
else
       name="Anonymous"
fi
maintainer="$name <$email>"

# Generate a simple changelog template
cat <<EOF > debian/changelog
linux-upstream ($packageversion) unstable; urgency=low

  * Custom built Linux kernel.

 -- $maintainer  $(date -R)
EOF

# Generate copyright file
cat <<EOF > debian/copyright
This is a packacked upstream version of the Linux kernel.

The sources may be found at most Linux ftp sites, including:
ftp://ftp.kernel.org/pub/linux/kernel

Copyright: 1991 - 2009 Linus Torvalds and others.

The git repository for mainline kernel development is at:
git://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux-2.6.git

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2 dated June, 1991.

On Debian GNU/Linux systems, the complete text of the GNU General Public
License version 2 can be found in \`/usr/share/common-licenses/GPL-2'.
EOF

# Generate a control file
cat <<EOF > debian/control
Source: linux-upstream
Section: kernel
Priority: optional
Maintainer: $maintainer
Standards-Version: 3.8.4
Homepage: http://www.kernel.org/
EOF

if [ "$ARCH" = "um" ]; then
	cat <<EOF >> debian/control

Package: $packagename
Provides: linux-image, linux-image-2.6, linux-modules-$version
Architecture: any
Description: User Mode Linux kernel, version $version
 User-mode Linux is a port of the Linux kernel to its own system call
 interface.  It provides a kind of virtual machine, which runs Linux
 as a user process under another Linux kernel.  This is useful for
 kernel development, sandboxes, jails, experimentation, and
 many other things.
 .
 This package contains the Linux kernel, modules and corresponding other
 files, version: $version.
EOF

else
	cat <<EOF >> debian/control

Package: $packagename
Provides: linux-image, linux-image-2.6, linux-modules-$version
Suggests: $fwpackagename
Architecture: any
Description: Linux kernel, version $version
 This package contains the Linux kernel, modules and corresponding other
 files, version: $version.
EOF

fi

# Build header package
(cd $srctree; find . -name Makefile -o -name Kconfig\* -o -name \*.pl > "$objtree/debian/hdrsrcfiles")
(cd $srctree; find arch/$SRCARCH/include include scripts -type f >> "$objtree/debian/hdrsrcfiles")
(cd $objtree; find .config Module.symvers include scripts -type f >> "$objtree/debian/hdrobjfiles")
destdir=$kernel_headers_dir/usr/src/linux-headers-$version
mkdir -p "$destdir"
(cd $srctree; tar -c -f - -T "$objtree/debian/hdrsrcfiles") | (cd $destdir; tar -xf -)
(cd $objtree; tar -c -f - -T "$objtree/debian/hdrobjfiles") | (cd $destdir; tar -xf -)
ln -sf "/usr/src/linux-headers-$version" "$kernel_headers_dir/lib/modules/$version/build"
rm -f "$objtree/debian/hdrsrcfiles" "$objtree/debian/hdrobjfiles"

cat <<EOF >> debian/control

Package: $kernel_headers_packagename
Provides: linux-headers, linux-headers-2.6
Architecture: any
Description: Linux kernel headers for $KERNELRELEASE on \${kernel:debarch}
 This package provides kernel header files for $KERNELRELEASE on \${kernel:debarch}
 .
 This is useful for people who need to build external modules
EOF

# Do we have firmware? Move it out of the way and build it into a package.
if [ -e "$tmpdir/lib/firmware" ]; then
	mv "$tmpdir/lib/firmware" "$fwdir/lib/"

	cat <<EOF >> debian/control

Package: $fwpackagename
Architecture: all
Description: Linux kernel firmware, version $version
 This package contains firmware from the Linux kernel, version $version.
EOF

	create_package "$fwpackagename" "$fwdir"
fi

cat <<EOF >> debian/control

Package: $libc_headers_packagename
Section: devel
Provides: linux-kernel-headers
Architecture: any
Description: Linux support headers for userspace development
 This package provides userspaces headers from the Linux kernel.  These headers
 are used by the installed headers for GNU glibc and other system libraries.
EOF

if [ "$ARCH" != "um" ]; then
	create_package "$kernel_headers_packagename" "$kernel_headers_dir"
	create_package "$libc_headers_packagename" "$libc_headers_dir"
fi

create_package "$packagename" "$tmpdir"

exit 0
