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
# Currently supported distros, each validated in a fresh container:
#
#   - Fedora, on dnf, validated on Fedora 44, in a toolbx container:
#
#           toolbox create fedora:44
#           toolbox enter fedora:44
#
#   - Ubuntu, on apt-get, validated on Ubuntu 26.04, in a distrobox
#     container:
#
#           distrobox create --image ubuntu:26.04
#           distrobox enter ubuntu-26-04
#
#     The apt-get install runs with a noninteractive debconf frontend:
#     default-jdk, needed by the jvmti feature tests, pulls in tzdata,
#     which prompts for the timezone on a terminal and would block the
#     install, e.g. in a container shared with an interactive session.
#
#   - Debian, on apt-get, reusing the Ubuntu mapping, validated on
#     Debian 13 (trixie), in a distrobox container:
#
#           distrobox create --image debian:trixie
#           distrobox enter debian-trixie
#
#   - RHEL and its variants (RHEL, CentOS Stream, Rocky Linux, AlmaLinux,
#     Oracle Linux), on dnf (RHEL 8 and later), validated on CentOS
#     Stream 10, in a distrobox container:
#
#           distrobox create --image quay.io/centos/centos:stream10
#           distrobox enter centos-stream10
#
#     The RHEL family reuses most Fedora package names, with these
#     differences, handled by probing the enabled repos:
#
#       - zlib.h comes from zlib-ng-compat-devel on the RHEL 10 family,
#         zlib-devel on RHEL 9 and earlier;
#       - the JDK devel package is versioned, there is no
#         java-latest-openjdk-devel: java-21-openjdk-devel on the RHEL
#         10 family, java-17-openjdk-devel on RHEL 9, java-11-openjdk-
#         devel on RHEL 8;
#       - libbpf-devel and capstone-devel are in the CRB repo on RHEL
#         and CentOS Stream 10 ('dnf config-manager --set-enabled crb')
#         and in EPEL on RHEL 9 and earlier;
#       - libbabeltrace2-devel is not packaged on the RHEL 10 family.
#
#     On the RHEL family, packages not available on the enabled repos
#     are skipped and listed at the end of the run, so that a distro
#     with CRB/EPEL enabled gets the full set, and a distro without
#     them still installs what it can.
#
# Running inside a container keeps the host system unmodified.
#
# Usage: install-build-deps.sh [OPTIONS]
#
# Options:
#    --list       list the packages that would be installed, then exit
#    --dry-run    show the install command that would be run, without
#                 running it
#    --distro ID  force a distro: fedora, rhel, ubuntu, debian
#                 (default: auto-detect)
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
    --distro ID force a distro: fedora, rhel, ubuntu, debian
    -h, --help  print this help message

Distros supported: Fedora (dnf), validated in a toolbx container on
Fedora 44, Ubuntu (apt-get), validated in a distrobox container on
Ubuntu 26.04, Debian (apt-get), reusing the Ubuntu mapping, validated
in a distrobox container on Debian 13 (trixie), and the RHEL family
(dnf, RHEL 8 and later), validated on CentOS Stream 10 in a distrobox
container.  On the RHEL family, packages not available on the enabled
repos are skipped and listed at the end; some there, e.g.
libbpf-devel and capstone-devel, come from the CRB repo on RHEL and
CentOS Stream 10 ('dnf config-manager --set-enabled crb') and from
EPEL on RHEL 9 and earlier.
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
	# glibc headers (pulled in by the libc headers package) in the
	# base set, not the native libaio.h/io_submit API that
	# libaio-devel provides.
}

# ---------------------------------------------------------------------
# Check whether the first argument is provided by the enabled repos, or
# is already installed, returning 0 if so and printing nothing.
#
# dnf repoquery exits 0 even when the pattern does not match anything,
# so availability is judged by the output being non-empty, not by the
# exit code.
# ---------------------------------------------------------------------
pkg_available() {
	local p out
	for p in "$@"; do
		out=$(dnf repoquery --quiet --available "$p" 2>/dev/null)
		[ -n "$out" ] || out=$(dnf repoquery --quiet --installed "$p" 2>/dev/null)
		[ -n "$out" ] && return 0
	done
	return 1
}

# ---------------------------------------------------------------------
# Return the first of its arguments that pkg_available() accepts.
# ---------------------------------------------------------------------
pick_available() {
	local p
	for p in "$@"; do
		if pkg_available "$p"; then
			echo "$p"
			return 0
		fi
	done
	return 1
}

# ---------------------------------------------------------------------
# Return the RHEL-family package(s) providing the devel requirements of
# a feature test.  This family reuses the Fedora mapping, whose names
# apply to it as well, except for the features whose Fedora package
# does not exist there, kept in the case below.
#
# Whether the resulting package is actually installable is checked in
# package_set(), as some of these names live in the CRB or EPEL repos,
# which may not be enabled: on the RHEL 10 family libbpf-devel and
# capstone-devel come from CRB, and libbabeltrace2-devel is not
# packaged at all.
# ---------------------------------------------------------------------
rhel_pkg_for() {
	local feat="$1"
	case "$feat" in
	zlib)
		# zlib-ng-compat-devel on the RHEL 10 family, zlib-devel on
		# RHEL 9 and earlier.  If neither is on the enabled repos,
		# fall back to the first candidate so that package_set()
		# reports it as missing instead of silently dropping the
		# feature from the end-of-run note.
		pick_available zlib-devel zlib-ng-compat-devel || echo "zlib-devel"
		;;
	jvmti|jvmti-cmlr)
		# No java-latest-openjdk-devel on the RHEL family: the JDK
		# devel package is versioned per release.
		pick_available java-latest-openjdk-devel java-21-openjdk-devel java-17-openjdk-devel java-11-openjdk-devel || echo "java-latest-openjdk-devel"
		;;
	*)
		fedora_pkg_for "$feat"
		;;
	esac
}

# ---------------------------------------------------------------------
# Return the Debian/Ubuntu package(s) providing the devel requirements
# of a feature test in tools/build/feature/.  This mapping is shared by
# Debian and Ubuntu, whose package names for these devel packages match,
# and is validated on Ubuntu 26.04 and Debian 13 (trixie).
# ---------------------------------------------------------------------
debian_pkg_for() {
	local feat="$1"
	case "$feat" in
	libelf|libelf-getphdrnum|libelf-gelf_getnote|libelf-getshdrstrndx)
		echo "libelf-dev"
		;;
	libelf-zstd)
		echo "libelf-dev libzstd-dev"
		;;
	libdw)
		echo "libdw-dev"
		;;
	libdebuginfod)
		echo "libdebuginfod-dev"
		;;
	libnuma|numa_num_possible_cpus)
		echo "libnuma-dev"
		;;
	libzstd)
		echo "libzstd-dev"
		;;
	zlib)
		echo "zlib1g-dev"
		;;
	lzma)
		echo "liblzma-dev"
		;;
	libslang)
		echo "libslang2-dev"
		;;
	libcapstone)
		echo "libcapstone-dev"
		;;
	libpython)
		echo "python3-dev"
		;;
	libtraceevent)
		echo "libtraceevent-dev"
		;;
	# The cxa-demangle feature test links against libstdc++ builtin
	# demangling, so on Debian-derived distros it is covered by the
	# g++ base package below, which pulls in libstdc++-*-dev.
	cxa-demangle)
		;;
	libbpf)
		echo "libbpf-dev"
		;;
	babeltrace2-ctf-writer)
		echo "libbabeltrace2-dev"
		;;
	libopenssl)
		echo "libssl-dev"
		;;
	libpfm4)
		echo "libpfm4-dev"
		;;
	sdt)
		echo "systemtap-sdt-dev"
		;;
	# test-clang-bpf-co-re.c invokes the clang binary (not libclang-cpp),
	# so clang suffices; llvm-dev brings llvm-config, needed by the
	# llvm/llvm-perf tests below.
	clang-bpf-co-re)
		echo "clang llvm-dev"
		;;
	llvm|llvm-perf)
		echo "llvm-dev"
		;;
	jvmti|jvmti-cmlr)
		echo "default-jdk"
		;;
	esac
	# Same rationale as in fedora_pkg_for() above for unmapped tests.
}

# Base packages needed by any perf build, regardless of feature tests:
# compiler, libc headers, flex/bison for the parser, kernel headers
# for UAPI headers with no in-tree copy, e.g. <linux/capability.h>, and
# gcc-c++ (dnf) / g++ (apt) is needed by the C++-based feature tests
# (cxa-demangle, llvm, llvm-perf), compiled with $(CXX), and
# pulls in libstdc++-devel / libstdc++-*-dev.
# python3-setuptools is needed to build the python binding (perf's
# util/setup.py uses it; without it binding is skipped with a warning).
# rust is not a header-based feature test: test-rust.bin just checks
# "$(RUSTC) --version" (tools/build/feature/Makefile), so it is mapped
# here like the other toolchain packages.
fedora_base_pkgs="gcc gcc-c++ make flex bison glibc-devel kernel-headers python3-setuptools rust"
# pkg-config mirrors the pkgconf-pkg-config package that Fedora
# installs by default, needed by the babeltrace2 feature test and
# libopenssl's pkg-config checks.
debian_base_pkgs="gcc g++ make pkg-config flex bison libc6-dev linux-libc-dev python3-setuptools rustc"
# pkg-config: like Fedora, on pkgconf since Fedora 26 / RHEL 8, with
# 'pkgconfig' live only as a virtual Provides of the pkgconf-pkg-config
# subpackage.  List it explicitly because, unlike Fedora, a minimal
# RHEL-family container may not have it preinstalled.
rhel_base_pkgs="gcc gcc-c++ make flex bison glibc-devel kernel-headers pkgconf-pkg-config python3-setuptools rust"

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
	rhel|centos|rocky|almalinux|ol)
		# All share the Fedora package names (with the exceptions
		# handled by rhel_pkg_for()) and dnf.
		echo "rhel" ;;
	ubuntu)			echo "ubuntu" ;;
	debian)			echo "debian" ;;
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

# ---------------------------------------------------------------------
# Assemble the unique package list.  Since the full feature set is mapped
# unconditionally, this installs the complete devel environment; installing
# an already-present package is a no-op for both dnf and apt-get, making
# this idempotent.  On the RHEL family, packages not available on the
# enabled repos are skipped and echoed on stderr, reported at the end of
# the run by the caller (package_set() runs in a subshell, so a variable
# would not survive it).
# ---------------------------------------------------------------------
package_set() {
	local distro="$1" srcdir="$2"
	local feat pkg pkgs base_pkgs missing_pkgs=""
	case "$distro" in
	fedora)	pkgs="$fedora_base_pkgs" ;;
	rhel)	pkgs="$rhel_base_pkgs" ;;
	ubuntu|debian)	pkgs="$debian_base_pkgs" ;;
	esac

	# The base set must be checked against the enabled repos as well:
	# e.g. 'rust' is only shipped as the rust-toolset AppStream module
	# on RHEL 8 and 9, where dnf would abort the whole transaction
	# with "No match for argument: rust" instead of skipping it, so
	# unavailable base packages are skipped and noted like the mapped
	# ones below.
	if [ "$distro" = "rhel" ]; then
		base_pkgs="$pkgs"
		pkgs=""
		for pkg in $base_pkgs; do
			if pkg_available "$pkg"; then
				pkgs="$pkgs $pkg"
			else
				missing_pkgs="$missing_pkgs $pkg"
			fi
		done
	fi

	for feat in $(feature_tests "$srcdir"); do
		case "$distro" in
		fedora)		pkg=$(fedora_pkg_for "$feat") ;;
		rhel)		pkg=$(rhel_pkg_for "$feat") ;;
		*)		pkg=$(debian_pkg_for "$feat") ;;
		esac
		# An empty result means the feature test has no external devel
		# dependency (see the unmapped list in fedora_pkg_for()), so
		# there is nothing to install and nothing to report as missing:
		# packages that are genuinely unavailable on the RHEL family
		# surface through rhel_pkg_for() falling back to its first
		# candidate, so that the pkg_available() check below catches
		# them and adds them to missing_pkgs.
		[ -n "$pkg" ] || continue
		for pkg in $pkg; do
			if [ "$distro" = "rhel" ] && ! pkg_available "$pkg"; then
				missing_pkgs="$missing_pkgs $pkg"
				continue
			fi
			case " $pkgs " in
			*" $pkg "*) ;;
			*) pkgs="$pkgs $pkg" ;;
			esac
		done
	done
	[ -n "$missing_pkgs" ] && echo "$missing_pkgs" >&2
	echo "$pkgs"
}

# ---------------------------------------------------------------------
# Report the RHEL-family packages that were skipped because the enabled
# repos do not provide them.
# ---------------------------------------------------------------------
note_missing() {
	[ -n "$1" ] || return 0
	echo "Note: these packages are not available on the enabled repos" >&2
	echo "and were not installed, so the features they enable will stay" >&2
	echo "off in a perf build:" >&2
	echo "$1" | tr ' ' '\n' | grep -v '^$' | sort -u | sed 's/^/  /' >&2
	echo >&2
}

# ---------------------------------------------------------------------
# The install command proper for each supported package manager, plus
# the command massaged for --dry-run.
# ---------------------------------------------------------------------
install_cmd() {
	local distro="$1"; shift
	case "$distro" in
	fedora|rhel)
		echo "dnf install -y $*"
		;;
	ubuntu|debian)
		# a fresh container has no package index, so update first; run
		# with a noninteractive debconf frontend: default-jdk, used by
		# the jvmti feature tests, pulls in tzdata, which prompts for
		# the timezone on a terminal and would block the install.  The
		# env var prefix works with the 'sh -c' invocation below, and
		# shows up in --dry-run.
		echo "DEBIAN_FRONTEND=noninteractive apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y $*"
		;;
	esac
}

main() {
	local action="install"
	# Package accumulation happens in package_set(), which, together
	# with the *_pkg_for() helpers, declares the per-feature 'pkg'
	# locals; main() holds no per-feature package state, just the
	# distro-wide list assembled by package_set().
	local srcdir distro pkgs cmd missing_file missing_pkgs

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
	fedora|rhel|ubuntu|debian) ;;
	*)
		echo "error: unsupported distro (got '$distro'); the package mapping is not validated on other distros." >&2
		echo "Supported and validated: Fedora 44 (toolbx container), Ubuntu 26.04 and Debian 13 (distrobox containers), and CentOS Stream 10 (distrobox container)." >&2
		exit 1
		;;
	esac

	# The RHEL family mapping probes the enabled repos with dnf
	# repoquery and installs with dnf, so reject the yum-only members of
	# the family (RHEL 7 and earlier, e.g. Oracle Linux 7) with a clear
	# error instead of silently judging every package as unavailable.
	if [ "$distro" = "rhel" ] && ! command -v dnf >/dev/null 2>&1; then
		echo "error: dnf not found, the RHEL family package mapping requires dnf (RHEL 8 and later)." >&2
		exit 1
	fi

	missing_file=$(mktemp) || {
		echo "error: cannot create a temporary file (mktemp failed)" >&2
		exit 1
	}
	# package_set() runs in a subshell, doing dnf repoquery loops that
	# may be interrupted: a Ctrl-C during those loops hits these traps,
	# turning SIGINT/SIGTERM into an exit that triggers the EXIT trap,
	# removing the temp file, so the script terminates instead of
	# resuming after the interrupted command, leaving no file behind.
	trap 'rm -f "$missing_file"' EXIT
	trap 'exit 1' INT TERM
	pkgs=$(package_set "$distro" "$srcdir" 2>"$missing_file")
	missing_pkgs=$(cat "$missing_file")
	rm -f "$missing_file"
	trap - EXIT INT TERM

	case "$action" in
	list)
		echo "$pkgs" | tr ' ' '\n' | grep -v '^$' | sort
		note_missing "$missing_pkgs"
		exit 0
		;;
	dry-run)
		install_cmd "$distro" $pkgs
		note_missing "$missing_pkgs"
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
	note_missing "$missing_pkgs"
}

main "$@"
