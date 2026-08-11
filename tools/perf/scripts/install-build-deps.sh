#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# No 'set -e': the script uses explicit checks for its error paths, and
# is also run via '$(SHELL) .../install-build-deps.sh' from the make
# target in tools/perf/Makefile.perf, where a shebang option would be
# ignored anyway, so direct and make-driven runs behave the same.
#
# Install the development packages needed to build tools/perf.
#
# The package set is derived from the feature tests in tools/build/feature/:
# each feature test that perf may compile is mapped to the devel package
# that provides the headers/libraries it checks, so that a subsequent
# 'make -C tools/perf' build enables the corresponding perf features.
#
# This initial version installs the base toolchain needed by any build;
# the per-feature package mapping is added, per supported distro, by the
# follow-up patches in this series, which also validate each mapping in a
# fresh container, so the host system is not modified.
#
# Usage: install-build-deps.sh [OPTIONS]
#
# Options:
#    --list       list the packages that would be installed, then exit
#    --dry-run    show the install command that would be run, without
#                 running it
#    --distro ID  force a distro: fedora, ubuntu (default: auto-detect)
#    -h, --help   print this help message
#
# Requires root (or passwordless sudo) to actually install packages.

set -u

DISTRO=""

help() {
	cat <<EOF
Usage: $(basename "$0") [--list] [--dry-run] [--distro ID] [-h|--help]

Install the development packages needed to build tools/perf.

Options:
    --list      list the packages that would be installed, then exit
    --dry-run   show the install command that would be run, without running it
    --distro ID force a distro: fedora, ubuntu
    -h, --help  print this help message
EOF
	exit 0
}

# ---------------------------------------------------------------------
# Distro detection.  The install command that follows only differs in
# the package manager, which here is keyed off the distro ID; the
# per-feature package mapping is added per distro by the follow-up
# patches.
# ---------------------------------------------------------------------
detect_distro() {
	if [ -n "$DISTRO" ]; then
		echo "$DISTRO"
		return
	fi
	local id
	id=$( . /etc/os-release 2>/dev/null && echo "${ID:-}" )
	case "$id" in
	fedora)			echo "fedora" ;;
	ubuntu)			echo "ubuntu" ;;
	# RHEL and its derivatives share most Fedora package names, but the
	# mapping is only validated on Fedora, so don't auto-detect them.
	rhel|centos|rocky|alma|ol) echo "" ;;
	*)			echo "" ;;
	esac
}

# Base packages needed by any perf build, regardless of feature tests:
# compiler, libc headers, flex/bison for the parser, kernel headers
# for UAPI headers with no in-tree copy, e.g. <linux/capability.h>, and
# gcc-c++ (dnf) / g++ (apt) for the C++-based feature tests
# (cxa-demangle, llvm, llvm-perf), compiled with $(CXX), and
# pulls in libstdc++-devel / libstdc++-*-dev.
# python3-setuptools is needed to build the python binding (perf's
# util/setup.py uses it; without it binding is skipped with a warning).
# rust is not a header-based feature test: test-rust.bin just checks
# "$(RUSTC) --version" (tools/build/feature/Makefile), so it is mapped
# here like the other toolchain packages.
fedora_base_pkgs="gcc gcc-c++ make flex bison glibc-devel kernel-headers python3-setuptools rust"
debian_base_pkgs="gcc g++ make flex bison libc6-dev linux-libc-dev python3-setuptools rustc"

# ---------------------------------------------------------------------
# Assemble the unique package list.  While the per-feature mapping is
# being added per distro, only the base toolchain above is installed.
# ---------------------------------------------------------------------
package_set() {
	local distro="$1" srcdir="$2"
	case "$distro" in
	fedora)	echo "$fedora_base_pkgs" ;;
	ubuntu)	echo "$debian_base_pkgs" ;;
	esac
}

# ---------------------------------------------------------------------
# The install command proper for each supported package manager, plus
# the command massaged for --dry-run.
# ---------------------------------------------------------------------
install_cmd() {
	local distro="$1"; shift
	case "$distro" in
	fedora)
		echo "dnf install -y $*"
		;;
	ubuntu)
		# a fresh container has no package index, so update first.
		echo "apt-get update && apt-get install -y $*"
		;;
	esac
}

main() {
	local action="install"
	local srcdir distro pkgs cmd

	while [ $# -gt 0 ]; do
		case "$1" in
		--list)		action="list"; shift ;;
		--dry-run)	action="dry-run"; shift ;;
		--distro)
			[ $# -ge 2 ] || {
				echo "error: --distro requires an argument (fedora, rhel, ubuntu, debian)" >&2
				exit 1
			}
			DISTRO="$2"; shift 2 ;;
		-h|--help)	help ;;
		*)		echo "error: unknown argument: $1" >&2; exit 1 ;;
		esac
	done

	srcdir=$(cd "$(dirname "$0")/../../.." && pwd)
	distro=$(detect_distro)
	case "$distro" in
	fedora|ubuntu) ;;
	*)
		echo "error: unsupported distro (got '$distro'); the package mapping is not validated on other distros." >&2
		exit 1
		;;
	esac

	pkgs=$(package_set "$distro" "$srcdir")

	case "$action" in
	list)
		echo "$pkgs" | tr ' ' '\n' | grep -v '^$' | sort
		exit 0
		;;
	dry-run)
		install_cmd "$distro" $pkgs
		exit 0
		;;
	esac

	echo "The following packages will be installed to enable perf features:"
	echo "$pkgs" | tr ' ' '\n' | grep -v '^$' | sort | sed 's/^/  /'
	echo
	cmd=$(install_cmd "$distro" $pkgs)
	if [ "$(id -u)" -eq 0 ]; then
		sh -c "$cmd"
	else
		sudo sh -c "$cmd"
	fi || {
		echo "error: the install command failed, see the output above" >&2
		exit 1
	}
}

main "$@"