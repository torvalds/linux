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
# Supported distros, each mapping validated in a fresh container, so the
# host system is not modified:
#
#   - Fedora, on dnf, validated on Fedora 44, in a toolbx container:
#
#           toolbox create fedora:44
#           toolbox enter fedora:44
#
# Usage: install-build-deps.sh [OPTIONS]
#
# Options:
#    --list       list the packages that would be installed, then exit
#    --dry-run    show the install command that would be run, without
#                 running it
#    --distro ID  force a distro: fedora (default: auto-detect)
#    -h, --help   print this help message
#
# Requires root (or passwordless sudo) to actually install packages.

set -u

DISTRO=""

help() {
	cat <<EOF
Usage: $(basename "$0") [--list] [--dry-run] [--distro ID] [-h|--help]

Install the development packages needed to build tools/perf.

The package set is derived from the feature tests in tools/build/feature/,
mapping each one with an external dependency to the devel package providing
it, so the corresponding feature gets enabled on a build.

Options:
    --list      list the packages that would be installed, then exit
    --dry-run   show the install command that would be run, without running it
    --distro ID force a distro: fedora
    -h, --help  print this help message

Distro supported: Fedora (dnf), validated on a fresh Fedora 44 toolbx
container.
EOF
	exit 0
}

# ---------------------------------------------------------------------
# Return the Fedora package(s) providing the devel requirements of a
# feature test in tools/build/feature/.  Multiple packages are separated
# by spaces; an empty result means the test has no external devel
# dependency (pure toolchain/glibc, or no equivalent Fedora package).
#
# The mapping was built by checking, for each feature test, which Fedora
# devel package provides the headers the test compiles against.
# ---------------------------------------------------------------------
fedora_pkg_for() {
	local feat="$1"
	case "$feat" in
	libelf|libelf-getphdrnum|libelf-gelf_getnote|libelf-getshdrstrndx)
		echo "elfutils-libelf-devel"
		;;
	libelf-zstd)
		echo "elfutils-libelf-devel libzstd-devel"
		;;
	libdw)
		echo "elfutils-devel"
		;;
	# test-libdebuginfod.c includes <elfutils/debuginfod.h>, which is
	# provided by elfutils-debuginfod-client-devel, not elfutils-devel.
	libdebuginfod)
		echo "elfutils-debuginfod-client-devel"
		;;
	libnuma|numa_num_possible_cpus)
		echo "numactl-devel"
		;;
	libzstd)
		echo "libzstd-devel"
		;;
	zlib)
		echo "zlib-devel"
		;;
	lzma)
		echo "xz-devel"
		;;
	libslang)
		echo "slang-devel"
		;;
	libcapstone)
		echo "capstone-devel"
		;;
	libpython)
		echo "python3-devel"
		;;
	libtraceevent)
		echo "libtraceevent-devel"
		;;
	cxa-demangle)
		echo "libstdc++-devel"
		;;
	libbpf)
		echo "libbpf-devel"
		;;
	babeltrace2-ctf-writer)
		echo "libbabeltrace2-devel"
		;;
	libopenssl)
		echo "openssl-devel"
		;;
	libpfm4)
		echo "libpfm-devel"
		;;
	sdt)
		echo "systemtap-sdt-devel"
		;;
	clang-bpf-co-re)
		echo "clang-devel"
		;;
	llvm|llvm-perf)
		echo "llvm-devel"
		;;
	jvmti|jvmti-cmlr)
		echo "java-latest-openjdk-devel"
		;;
	esac
	# Pure toolchain/libc features (backtrace, eventfd, fortify-source,
	# gettid, glibc, hello, reallocarray, pthread-*, stackprotector-all,
	# timerfd, scandirat, sched_getcpu, setns, file-handle,
	# bionic...) need no external package: their
	# requirements are covered by the base packages below.  Features that
	# perf's own build does not check (libcap, libcheck, libcpupower,
	# libtracefs, whose feature tests exist for rtla/bpftool/rv) and the
	# opt-in features, which a default build does not enable: the libbfd
	# disassembler family (libbfd, libbfd-threadsafe, libbfd-liberty,
	# disassembler-*, cplus-demangle), only linked on BUILD_NONDISTRO
	# builds and deprecated in favor of capstone, GTK2, LIBPERL and
	# LIBUNWIND support (ifdef GTK2 / ifdef LIBPERL / LIBUNWIND=1),
	# and CoreSight (ifdef CORESIGHT), are deliberately not mapped.
	# libaio is not mapped either: its
	# test uses the POSIX AIO API (aio.h, aio_*, -lrt), provided by
	# glibc headers (pulled in by the glibc-devel base package), not
	# the native libaio.h/io_submit API that libaio-devel provides.
}

# ---------------------------------------------------------------------
# Distro detection
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

# ---------------------------------------------------------------------
# Feature test enumeration: mirror of the tests checked during a build,
# from the actual test-*.c / test-*.cpp sources in tools/build/feature/.
# ---------------------------------------------------------------------
feature_tests() {
	local srcdir="$1"
	local f
	for f in "$srcdir"/tools/build/feature/test-*.c "$srcdir"/tools/build/feature/test-*.cpp; do
		[ -e "$f" ] || continue
		basename "$f" | sed -e 's/^test-//' -e 's/\.c$//' -e 's/\.cpp$//'
	done
}

# Base packages needed by any build, regardless of feature tests:
# compiler, libc headers, flex/bison for the parser, kernel headers
# for UAPI headers with no in-tree copy, e.g. <linux/capability.h>, and
# gcc-c++ is needed by the C++-based feature tests (cxa-demangle, llvm,
# llvm-perf), which are compiled with $(CXX), pulling in
# libstdc++-devel.  python3-setuptools is needed to build the python
# (perf's util/setup.py uses it); rust is checked by the rust feature
# test (test-rust.bin just runs "$(RUSTC) --version").

fedora_base_pkgs="gcc gcc-c++ make flex bison glibc-devel kernel-headers python3-setuptools rust"
debian_base_pkgs="gcc g++ make flex bison libc6-dev linux-libc-dev python3-setuptools rustc"

# ---------------------------------------------------------------------
# Assemble the unique package list: the base toolchain plus, for each
# feature test the distro's package mapping knows about, its package(s).
# Installing an already-present package is a no-op for both dnf and
# apt-get, making this idempotent.
# ---------------------------------------------------------------------
package_set() {
	local distro="$1" srcdir="$2"
	local feat pkg pkgs

	case "$distro" in
	fedora)	pkgs="$fedora_base_pkgs" ;;
	ubuntu)	pkgs="$debian_base_pkgs" ;;
	esac

	if [ "$distro" = "fedora" ]; then
		for feat in $(feature_tests "$srcdir"); do
			pkg=$(fedora_pkg_for "$feat")
			for pkg in $pkg; do
				case " $pkgs " in
				*" $pkg "*) ;;
				*) pkgs="$pkgs $pkg" ;;
				esac
			done
		done
	fi
	echo "$pkgs"
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
		echo "Supported and validated: Fedora 44 (fresh toolbx container)." >&2
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