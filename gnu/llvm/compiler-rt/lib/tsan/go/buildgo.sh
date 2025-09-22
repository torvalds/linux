#!/bin/sh

set -e

if [ "`uname -a | grep Linux`" != "" ]; then
	HOST_GOOS="linux"
	if [ "`uname -a | grep ppc64le`" != "" ]; then
		HOST_GOARCH="ppc64le"
	elif [ "`uname -a | grep x86_64`" != "" ]; then
		HOST_GOARCH="amd64"
	elif [ "`uname -a | grep aarch64`" != "" ]; then
		HOST_GOARCH="arm64"
	elif [ "`uname -a | grep loongarch64`" != "" ]; then
		HOST_GOARCH="loong64"
	elif [ "`uname -a | grep -i mips64`" != "" ]; then
		if [ "`lscpu | grep -i Little`" != "" ]; then
			HOST_GOARCH="mips64le"
		else
			HOST_GOARCH="mips64"
		fi
	elif [ "`uname -a | grep s390x`" != "" ]; then
		HOST_GOARCH="s390x"
	fi
elif [ "`uname -a | grep FreeBSD`" != "" ]; then
	HOST_GOOS="freebsd"
	HOST_GOARCH="amd64"
elif [ "`uname -a | grep NetBSD`" != "" ]; then
	HOST_GOOS="netbsd"
	HOST_GOARCH="amd64"
elif [ "`uname -a | grep Darwin`" != "" ]; then
	HOST_GOOS="darwin"
	if [ "`uname -a | grep x86_64`" != "" ]; then
		HOST_GOARCH="amd64"
	elif [ "`uname -a | grep arm64`" != "" ]; then
		HOST_GOARCH="arm64"
	fi
elif [ "`uname -a | grep MINGW`" != "" ]; then
	HOST_GOOS="windows"
	HOST_GOARCH="amd64"
fi

GOOS=${GOOS:-$HOST_GOOS}
GOARCH=${GOARCH:-$HOST_GOARCH}
SUFFIX="${GOOS}_${GOARCH}"

SRCS="
	tsan_go.cpp
	../rtl/tsan_external.cpp
	../rtl/tsan_flags.cpp
	../rtl/tsan_interface_atomic.cpp
	../rtl/tsan_md5.cpp
	../rtl/tsan_report.cpp
	../rtl/tsan_rtl.cpp
	../rtl/tsan_rtl_access.cpp
	../rtl/tsan_rtl_mutex.cpp
	../rtl/tsan_rtl_report.cpp
	../rtl/tsan_rtl_thread.cpp
	../rtl/tsan_rtl_proc.cpp
	../rtl/tsan_stack_trace.cpp
	../rtl/tsan_suppressions.cpp
	../rtl/tsan_sync.cpp
	../rtl/tsan_vector_clock.cpp
	../../sanitizer_common/sanitizer_allocator.cpp
	../../sanitizer_common/sanitizer_common.cpp
	../../sanitizer_common/sanitizer_common_libcdep.cpp
	../../sanitizer_common/sanitizer_deadlock_detector2.cpp
	../../sanitizer_common/sanitizer_file.cpp
	../../sanitizer_common/sanitizer_flag_parser.cpp
	../../sanitizer_common/sanitizer_flags.cpp
	../../sanitizer_common/sanitizer_libc.cpp
	../../sanitizer_common/sanitizer_mutex.cpp
	../../sanitizer_common/sanitizer_printf.cpp
	../../sanitizer_common/sanitizer_suppressions.cpp
	../../sanitizer_common/sanitizer_thread_registry.cpp
	../../sanitizer_common/sanitizer_stack_store.cpp
	../../sanitizer_common/sanitizer_stackdepot.cpp
	../../sanitizer_common/sanitizer_stacktrace.cpp
	../../sanitizer_common/sanitizer_symbolizer.cpp
	../../sanitizer_common/sanitizer_symbolizer_report.cpp
	../../sanitizer_common/sanitizer_termination.cpp
"

if [ "$GOOS" = "linux" ]; then
	OSCFLAGS="-fPIC -Wno-maybe-uninitialized"
	OSLDFLAGS="-lpthread -fPIC -fpie"
	SRCS="
		$SRCS
		../rtl/tsan_platform_linux.cpp
		../../sanitizer_common/sanitizer_posix.cpp
		../../sanitizer_common/sanitizer_posix_libcdep.cpp
		../../sanitizer_common/sanitizer_procmaps_common.cpp
		../../sanitizer_common/sanitizer_procmaps_linux.cpp
		../../sanitizer_common/sanitizer_linux.cpp
		../../sanitizer_common/sanitizer_linux_libcdep.cpp
		../../sanitizer_common/sanitizer_stoptheworld_linux_libcdep.cpp
		../../sanitizer_common/sanitizer_stoptheworld_netbsd_libcdep.cpp
		"
	if [ "$GOARCH" = "ppc64le" ]; then
		ARCHCFLAGS="-m64 -mcpu=power8 -fno-function-sections"
	elif [ "$GOARCH" = "amd64" ]; then
		if [ "$GOAMD64" = "v3" ]; then
			ARCHCFLAGS="-m64 -msse4.2"
		else
			ARCHCFLAGS="-m64 -msse3"
		fi
		OSCFLAGS="$OSCFLAGS -ffreestanding -Wno-unused-const-variable -Wno-unknown-warning-option"
	elif [ "$GOARCH" = "arm64" ]; then
		ARCHCFLAGS=""
	elif [ "$GOARCH" = "mips64le" ]; then
		ARCHCFLAGS="-mips64 -EL"
	elif [ "$GOARCH" = "mips64" ]; then
		ARCHCFLAGS="-mips64 -EB"
	elif [ "$GOARCH" = "s390x" ]; then
		SRCS="$SRCS ../../sanitizer_common/sanitizer_linux_s390.cpp"
		ARCHCFLAGS=""
	fi
elif [ "$GOOS" = "freebsd" ]; then
	# The resulting object still depends on libc.
	# We removed this dependency for Go runtime for other OSes,
	# and we should remove it for FreeBSD as well, but there is no pressing need.
	DEPENDS_ON_LIBC=1
	OSCFLAGS="-fno-strict-aliasing -fPIC -Werror"
	ARCHCFLAGS="-m64"
	OSLDFLAGS="-lpthread -fPIC -fpie"
	SRCS="
		$SRCS
		../rtl/tsan_platform_linux.cpp
		../../sanitizer_common/sanitizer_posix.cpp
		../../sanitizer_common/sanitizer_posix_libcdep.cpp
		../../sanitizer_common/sanitizer_procmaps_bsd.cpp
		../../sanitizer_common/sanitizer_procmaps_common.cpp
		../../sanitizer_common/sanitizer_linux.cpp
		../../sanitizer_common/sanitizer_linux_libcdep.cpp
		../../sanitizer_common/sanitizer_stoptheworld_linux_libcdep.cpp
		../../sanitizer_common/sanitizer_stoptheworld_netbsd_libcdep.cpp
	"
elif [ "$GOOS" = "netbsd" ]; then
	# The resulting object still depends on libc.
	# We removed this dependency for Go runtime for other OSes,
	# and we should remove it for NetBSD as well, but there is no pressing need.
	DEPENDS_ON_LIBC=1
	OSCFLAGS="-fno-strict-aliasing -fPIC -Werror"
	ARCHCFLAGS="-m64"
	OSLDFLAGS="-lpthread -fPIC -fpie"
	SRCS="
		$SRCS
		../rtl/tsan_platform_linux.cpp
		../../sanitizer_common/sanitizer_posix.cpp
		../../sanitizer_common/sanitizer_posix_libcdep.cpp
		../../sanitizer_common/sanitizer_procmaps_bsd.cpp
		../../sanitizer_common/sanitizer_procmaps_common.cpp
		../../sanitizer_common/sanitizer_linux.cpp
		../../sanitizer_common/sanitizer_linux_libcdep.cpp
		../../sanitizer_common/sanitizer_netbsd.cpp
		../../sanitizer_common/sanitizer_stoptheworld_linux_libcdep.cpp
		../../sanitizer_common/sanitizer_stoptheworld_netbsd_libcdep.cpp
	"
elif [ "$GOOS" = "darwin" ]; then
	OSCFLAGS="-fPIC -Wno-unused-const-variable -Wno-unknown-warning-option -mmacosx-version-min=10.7"
	OSLDFLAGS="-lpthread -fPIC -fpie -mmacosx-version-min=10.7"
	SRCS="
		$SRCS
		../rtl/tsan_platform_mac.cpp
		../../sanitizer_common/sanitizer_mac.cpp
		../../sanitizer_common/sanitizer_mac_libcdep.cpp
		../../sanitizer_common/sanitizer_posix.cpp
		../../sanitizer_common/sanitizer_posix_libcdep.cpp
		../../sanitizer_common/sanitizer_procmaps_mac.cpp
	"
	if [ "$GOARCH" = "amd64" ]; then
		ARCHCFLAGS="-m64"
	elif [ "$GOARCH" = "arm64" ]; then
		ARCHCFLAGS=""
	fi
elif [ "$GOOS" = "windows" ]; then
	OSCFLAGS="-Wno-error=attributes -Wno-attributes -Wno-unused-const-variable -Wno-unknown-warning-option"
	ARCHCFLAGS="-m64"
	OSLDFLAGS=""
	SRCS="
		$SRCS
		../rtl/tsan_platform_windows.cpp
		../../sanitizer_common/sanitizer_win.cpp
	"
else
	echo Unknown platform
	exit 1
fi

CC=${CC:-gcc}
IN_TMPDIR=${IN_TMPDIR:-0}
SILENT=${SILENT:-0}

if [ $IN_TMPDIR != "0" ]; then
  DIR=$(mktemp -qd /tmp/gotsan.XXXXXXXXXX)
  cleanup() {
    rm -rf $DIR
  }
  trap cleanup EXIT
else
  DIR=.
fi

SRCS="$SRCS $ADD_SRCS"
for F in $SRCS; do
	echo "#line 1 \"$F\""
	cat $F
done > $DIR/gotsan.cpp

FLAGS=" -I../rtl -I../.. -I../../sanitizer_common -I../../../include -std=c++17 -Wall -fno-exceptions -fno-rtti -DSANITIZER_GO=1 -DSANITIZER_DEADLOCK_DETECTOR_VERSION=2 $OSCFLAGS $ARCHCFLAGS $EXTRA_CFLAGS"
DEBUG_FLAGS="$FLAGS -DSANITIZER_DEBUG=1 -g"
FLAGS="$FLAGS -DSANITIZER_DEBUG=0 -O3 -fomit-frame-pointer"

if [ "$DEBUG" = "" ]; then
	# Do a build test with debug flags.
	$CC $DIR/gotsan.cpp -c -o $DIR/race_debug_$SUFFIX.syso $DEBUG_FLAGS $CFLAGS
else
	FLAGS="$DEBUG_FLAGS"
fi

if [ "$SILENT" != "1" ]; then
  echo $CC gotsan.cpp -c -o $DIR/race_$SUFFIX.syso $FLAGS $CFLAGS
fi
$CC $DIR/gotsan.cpp -c -o $DIR/race_$SUFFIX.syso $FLAGS $CFLAGS

$CC $OSCFLAGS $ARCHCFLAGS test.c $DIR/race_$SUFFIX.syso -g -o $DIR/test $OSLDFLAGS $LDFLAGS

# Verify that no libc specific code is present.
if [ "$DEPENDS_ON_LIBC" != "1" ]; then
	if nm $DIR/race_$SUFFIX.syso | grep -q __libc_; then
		printf -- '%s seems to link to libc\n' "race_$SUFFIX.syso"
		exit 1
	fi
fi

if [ "$SKIP_TEST" = "1" ]; then
	exit 0
fi

if [ "`uname -a | grep NetBSD`" != "" ]; then
  # Turn off ASLR in the test binary.
  /usr/sbin/paxctl +a $DIR/test
fi
export GORACE="exitcode=0 atexit_sleep_ms=0"
if [ "$SILENT" != "1" ]; then
  $DIR/test
else
  $DIR/test 2>/dev/null
fi
