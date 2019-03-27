#!/bin/sh

# make-manifest.sh: create checksums and package descriptions for the installer
#
#  Usage: make-manifest.sh foo1.txz foo2.txz ...
#
# The output file looks like this (tab-delimited):
#  foo1.txz SHA256-checksum Number-of-files foo1 Description Install-by-default
#
# $FreeBSD$

base="Base system"
kernel="Kernel"
ports="Ports tree"
src="System source tree"
lib32="32-bit compatibility libraries"
tests="Test suite"

desc_base="${base} (MANDATORY)"
desc_base_dbg="${base} (Debugging)"
desc_kernel="${kernel} (MANDATORY)"
desc_kernel_dbg="${kernel} (Debugging)"
desc_kernel_alt="Alternate ${kernel}"
desc_kernel_alt_dbg="Alternate ${kernel} (Debugging)"
desc_lib32="${lib32}"
desc_lib32_dbg="${lib32} (Debugging)"
desc_ports="${ports}"
desc_src="${src}"
desc_tests="${tests}"

default_src=off
default_ports=off
default_tests=off
default_base_dbg=off
default_lib32_dbg=off
default_kernel_alt=off
default_kernel_dbg=on
default_kernel_alt_dbg=off

for i in ${*}; do
	dist="${i}"
	distname="${i%%.txz}"
	distname="$(echo ${distname} | tr '-' '_')"
	distname="$(echo ${distname} | tr 'kernel.' 'kernel_')"
	hash="$(sha256 -q ${i})"
	nfiles="$(tar tvf ${i} | wc -l | tr -d ' ')"
	default="$(eval echo \${default_${distname}:-on})"
	desc="$(eval echo \"\${desc_${distname}}\")"

	case ${i} in
		doc.txz)
			continue
			;;
		kernel-dbg.txz)
			desc="${desc_kernel_dbg}"
			;;
		kernel.*-dbg.txz)
			desc="$(eval echo \"${desc_kernel_alt_dbg}\")"
			desc="${desc}: $(eval echo ${i%%-dbg.txz} | cut -f 2 -d '.')"
			default="$(eval echo \"${default_kernel_alt_dbg}\")"
			;;
		kernel.*.txz)
			desc="$(eval echo \"${desc_kernel_alt}\")"
			desc="${desc}: $(eval echo ${i%%.txz} | cut -f 2 -d '.')"
			default="$(eval echo \"${default_kernel_alt}\")"
			;;
		*)
			;;
	esac

	printf "${dist}\t${hash}\t${nfiles}\t${distname}\t\"${desc}\"\t${default}\n"
done

