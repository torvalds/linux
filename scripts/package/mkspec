#!/bin/sh
#
#	Output a simple RPM spec file.
#	This version assumes a minimum of RPM 4.13
#
#	The only gothic bit here is redefining install_post to avoid
#	stripping the symbols from files in the kernel which we want
#
#	Patched for non-x86 by Opencon (L) 2002 <opencon@rio.skydome.net>
#

set -eu

output=$1

mkdir -p "$(dirname "${output}")"

exec >"${output}"

if grep -q CONFIG_MODULES=y include/config/auto.conf; then
echo '%define with_devel %{?_without_devel: 0} %{?!_without_devel: 1}'
else
echo '%define with_devel 0'
fi

cat<<EOF
%define ARCH ${ARCH}
%define KERNELRELEASE ${KERNELRELEASE}
%define pkg_release $("${srctree}/scripts/build-version")
EOF

cat "${srctree}/scripts/package/kernel.spec"

# collect the user's name and email address for the changelog entry
if [ "$(command -v git)" ]; then
	name=$(git config user.name) || true
	email=$(git config user.email) || true
fi

if [ ! "${name:+set}" ]; then
	name=${KBUILD_BUILD_USER:-$(id -nu)}
fi

if [ ! "${email:+set}" ]; then
	buildhost=${KBUILD_BUILD_HOST:-$(hostname -f 2>/dev/null || hostname)}
	builduser=${KBUILD_BUILD_USER:-$(id -nu)}
	email="${builduser}@${buildhost}"
fi

cat << EOF

%changelog
* $(LC_ALL=C date +'%a %b %d %Y') ${name} <${email}>
- Custom built Linux kernel.
EOF
