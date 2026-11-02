#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026, Google LLC.
#
# Generic Boot Tracing Test Harness
# Builds a lightweight initramfs, applies bootconfigs and kernel cmdline parameters,
# boots QEMU, and verifies tracefs configuration using test scripts.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -d "$SCRIPT_DIR/boottime" ]; then
	SCRIPT_DIR="$SCRIPT_DIR/boottime"
fi
KERNEL_SRC="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"

# Source KTAP helpers
if [ -f "$SCRIPT_DIR/../../kselftest/ktap_helpers.sh" ]; then
	KSELFTEST_DIR="$(cd "$SCRIPT_DIR/../../kselftest" && pwd)"
elif [ -f "$SCRIPT_DIR/../kselftest/ktap_helpers.sh" ]; then
	KSELFTEST_DIR="$(cd "$SCRIPT_DIR/../kselftest" && pwd)"
else
	echo "Error: ktap_helpers.sh not found relative to $SCRIPT_DIR" >&2
	exit 1
fi
. "$KSELFTEST_DIR/ktap_helpers.sh"

BOOTCONFIG="${BOOTCONFIG:-"$KERNEL_SRC/tools/bootconfig/bootconfig"}"
KERNEL="${KERNEL:-"$KERNEL_SRC/arch/x86/boot/bzImage"}"
QEMU="${QEMU:-"qemu-system-x86_64"}"
BUSYBOX="${BUSYBOX:-"$(which busybox 2>/dev/null || echo "/usr/bin/busybox")"}"
TIMEOUT="${TIMEOUT:-60s}"
LOGDIR="${LOGDIR:-""}"
TARGET_TEST="all"

BUSYBOX_APPLETS=(
	sh
	cat
	mount
	umount
	echo
	sync
	reboot
	poweroff
	cpio
	grep
	sleep
	sed
)

usage() {
	echo "Usage: $0 [options]"
	echo "Options:"
	echo "  -b, --bootconfig PATH   Path to bootconfig tool (default: $BOOTCONFIG)"
	echo "  -k, --kernel PATH       Path to kernel image (default: $KERNEL)"
	echo "  -q, --qemu PATH         Path to QEMU executable (default: $QEMU)"
	echo "  -B, --busybox PATH      Path to busybox binary (default: $BUSYBOX)"
	echo "  -T, --timeout DURATION  QEMU execution timeout (default: $TIMEOUT)"
	echo "  -l, --logdir DIR        Directory to save QEMU log files"
	echo "  -t, --test NAME         Run specific test (e.g. 01-kprobe) or 'all'"
	echo "  -h, --help              Show this help message"
	exit 1
}

while [ $# -gt 0 ]; do
	case "$1" in
		-b|--bootconfig)
			BOOTCONFIG="$2"
			shift 2
			;;
		-k|--kernel)
			KERNEL="$2"
			shift 2
			;;
		-q|--qemu)
			QEMU="$2"
			shift 2
			;;
		-B|--busybox)
			BUSYBOX="$2"
			shift 2
			;;
		-T|--timeout)
			TIMEOUT="$2"
			shift 2
			;;
		-l|--logdir)
			LOGDIR="$2"
			shift 2
			;;
		-t|--test)
			TARGET_TEST="$2"
			shift 2
			;;
		-h|--help)
			usage
			;;
		*)
			echo "Unknown option: $1"
			usage
			;;
	esac
done

# Check host architecture
HOST_ARCH="$(uname -m)"
case "$HOST_ARCH" in
	x86_64|i686|i386|x86)
		;;
	*)
		if [ "$KERNEL" = "$KERNEL_SRC/arch/x86/boot/bzImage" ]; then
			ktap_print_header
			ktap_skip_all \
				"Default boot tracing test is x86 only ($HOST_ARCH)"
			exit 0
		fi
		;;
esac

# Ensure bootconfig tool is built if Makefile exists
if [ ! -x "$BOOTCONFIG" ]; then
	if [ -f "$KERNEL_SRC/tools/bootconfig/Makefile" ]; then
		make -C "$KERNEL_SRC/tools/bootconfig" > /dev/null 2>&1 || true
	fi
	if [ ! -x "$BOOTCONFIG" ]; then
		BOOTCONFIG="$(which bootconfig 2>/dev/null || true)"
	fi
fi

if [ ! -x "$BOOTCONFIG" ]; then
	ktap_print_header
	ktap_skip_all "bootconfig tool not found at $BOOTCONFIG"
	exit 0
fi

if [ ! -f "$KERNEL" ]; then
	ktap_print_header
	ktap_skip_all "Kernel image not found at $KERNEL"
	exit 0
fi

if ! command -v "$QEMU" >/dev/null 2>&1; then
	ktap_print_header
	ktap_skip_all "QEMU binary not found: $QEMU"
	exit 0
fi

if [ ! -x "$BUSYBOX" ]; then
	ktap_print_header
	ktap_skip_all "busybox binary not found at $BUSYBOX"
	exit 0
fi

# Verify file command existence and architecture compatibility
if ! command -v file >/dev/null 2>&1; then
	ktap_print_header
	ktap_skip_all "file tool not found"
	exit 0
fi

KERNEL_INFO="$(file -bL "$KERNEL" 2>/dev/null || true)"
case "$KERNEL_INFO" in
	*x86*)
		;;
	*)
		ktap_print_header
		ktap_skip_all "Kernel architecture is not x86 ($KERNEL_INFO)"
		exit 0
		;;
esac

BUSYBOX_INFO="$(file -bL "$BUSYBOX" 2>/dev/null || true)"
case "$BUSYBOX_INFO" in
	*x86-64*|*x86_64*|*80386*|*i386*)
		;;
	*)
		ktap_print_header
		ktap_skip_all \
			"busybox binary architecture is incompatible with x86 kernel ($BUSYBOX_INFO)"
		exit 0
		;;
esac

if ! command -v cpio >/dev/null 2>&1; then
	ktap_print_header
	ktap_skip_all "cpio tool not found"
	exit 0
fi

if ! command -v timeout >/dev/null 2>&1; then
	ktap_print_header
	ktap_skip_all "timeout tool not found"
	exit 0
fi

double_timeout() {
	local val="$1"
	if [[ "$val" =~ ^([0-9]+)([a-z]*)$ ]]; then
		local num="${BASH_REMATCH[1]}"
		local unit="${BASH_REMATCH[2]}"
		echo "$((num * 2))$unit"
	else
		echo "120s"
	fi
}

run_single_test() {
	local test_script="$1"
	local name
	local dir
	name="$(basename "$test_script" .sh)"
	dir="$(dirname "$test_script")"

	local bconf_file=""
	if [ -f "$SCRIPT_DIR/bootconfigs/$name.bconf" ]; then
		bconf_file="$SCRIPT_DIR/bootconfigs/$name.bconf"
	elif [ -f "$dir/$name.bconf" ]; then
		bconf_file="$dir/$name.bconf"
	fi

	local test_cmdline=""
	if [ -f "$SCRIPT_DIR/cmdlines/$name.cmdline" ]; then
		test_cmdline="$(cat "$SCRIPT_DIR/cmdlines/$name.cmdline")"
	elif [ -f "$dir/$name.cmdline" ]; then
		test_cmdline="$(cat "$dir/$name.cmdline")"
	fi

	local inline_cmdline
	inline_cmdline="$(grep -E '^# *CMDLINE:' "$test_script" | sed 's/^# *CMDLINE://' | xargs || true)"
	if [ -n "$inline_cmdline" ]; then
		test_cmdline="$test_cmdline $inline_cmdline"
	fi

	local test_qemuopts=""
	if [ -f "$SCRIPT_DIR/qemuopts/$name.qemuopts" ]; then
		test_qemuopts="$(cat "$SCRIPT_DIR/qemuopts/$name.qemuopts")"
	elif [ -f "$dir/$name.qemuopts" ]; then
		test_qemuopts="$(cat "$dir/$name.qemuopts")"
	fi

	local inline_qemuopts
	inline_qemuopts="$(grep -E '^# *QEMUOPTS:' "$test_script" | sed 's/^# *QEMUOPTS://' | xargs || true)"
	if [ -n "$inline_qemuopts" ]; then
		test_qemuopts="$test_qemuopts $inline_qemuopts"
	fi

	local test_timeout="$TIMEOUT"
	local inline_timeout
	inline_timeout="$(grep -E '^# *TIMEOUT:' "$test_script" | sed 's/^# *TIMEOUT://' | xargs || true)"
	if [ -n "$inline_timeout" ]; then
		test_timeout="$inline_timeout"
	fi

	local is_reboot=0
	if grep -q -E '^# *REBOOT: *1' "$test_script" || [ -f "$dir/$name.reboot" ]; then
		is_reboot=1
	fi

	local effective_timeout="$test_timeout"
	if [ "$is_reboot" -eq 1 ]; then
		effective_timeout="$(double_timeout "$test_timeout")"
	fi

	local workdir
	workdir="$(mktemp -d)"
	if [ -z "$workdir" ] || [ ! -d "$workdir" ]; then
		ktap_test_fail "$name (failed to create temporary directory)"
		return 0
	fi
	trap 'rm -rf "$workdir"' EXIT

	local test_applets=("${BUSYBOX_APPLETS[@]}")
	local inline_applets
	inline_applets="$(grep -E '^# *APPLETS:' "$test_script" | sed 's/^# *APPLETS://' | xargs || true)"
	if [ -n "$inline_applets" ]; then
		read -r -a extra_applets <<< "$inline_applets" || true
		test_applets+=("${extra_applets[@]}")
	fi

	local rootfs="$workdir/rootfs"
	mkdir -p "$rootfs"/{bin,sbin,etc,proc,sys,dev,tmp}

	cp -L "$BUSYBOX" "$rootfs/bin/busybox"
	chmod +x "$rootfs/bin/busybox"
	(cd "$rootfs/bin" && for applet in "${test_applets[@]}"; do ln -sf busybox "$applet"; done)

	if command -v ldd >/dev/null 2>&1; then
		for lib in $(ldd "$BUSYBOX" 2>/dev/null | grep -o '/[^ ]*' || true); do
			if [ -f "$lib" ]; then
				mkdir -p "$rootfs$(dirname "$lib")"
				cp -L "$lib" "$rootfs$lib" 2>/dev/null || true
			fi
		done
	fi

	cat << 'EOF' > "$rootfs/init"
#!/bin/sh
mount -t proc proc /proc 2>/dev/null
mount -t sysfs sys /sys 2>/dev/null
mount -t tracefs nodev /sys/kernel/tracing 2>/dev/null

/bin/check_test.sh
RET=$?

if [ $RET -eq 0 ]; then
	echo "TEST RESULT: PASS"
else
	echo "TEST RESULT: FAIL"
fi
sync
echo o > /proc/sysrq-trigger 2>/dev/null || true
poweroff -f 2>/dev/null || reboot -f 2>/dev/null || true
while true; do sleep 100 2>/dev/null || break; done
EOF
	chmod +x "$rootfs/init"

	cp "$test_script" "$rootfs/bin/check_test.sh"
	chmod +x "$rootfs/bin/check_test.sh"

	local initramfs="$workdir/initramfs.cpio"
	(cd "$rootfs" && find . | cpio -o -H newc --quiet) > "$initramfs"

	if [ -n "$bconf_file" ] && [ -f "$bconf_file" ]; then
		"$BOOTCONFIG" -a "$bconf_file" "$initramfs" > /dev/null
	fi


	local logfile
	local base_cmdline="bootconfig console=tty0 console=ttyS0 panic=-1"
	if [ -n "$test_cmdline" ]; then
		base_cmdline="$base_cmdline $test_cmdline"
	fi

	if [ -n "$LOGDIR" ]; then
		mkdir -p "$LOGDIR"
		logfile="$LOGDIR/$name.log"
		base_cmdline="$base_cmdline dump_bconf"
	else
		logfile="$workdir/qemu.log"
		base_cmdline="quiet $base_cmdline"
	fi

	local qemu_args=()
	if [ -n "$test_qemuopts" ]; then
		read -r -d '' -a qemu_args <<< "$test_qemuopts" || true
	fi

	if [ "$is_reboot" -ne 1 ]; then
		qemu_args+=("-no-reboot")
	fi

	timeout "$effective_timeout" "$QEMU" -kernel "$KERNEL" \
		-initrd "$initramfs" \
		-append "$base_cmdline" \
		-display none \
		-serial stdio \
		"${qemu_args[@]}" < /dev/null > "$logfile" 2>&1 || true

	if grep -q "TEST RESULT: PASS" "$logfile"; then
		ktap_test_pass "$name"
	else
		ktap_test_fail "$name"
		ktap_print_msg "QEMU Console Output for $name:"
		while IFS= read -r line; do
			ktap_print_msg "$line"
		done < "$logfile"
	fi

	rm -rf "$workdir"
	trap - EXIT
	return 0
}

TEST_SCRIPTS=()
if [ -d "$SCRIPT_DIR/tests" ]; then
	while IFS= read -r f; do
		[ -n "$f" ] && TEST_SCRIPTS+=("$f")
	done < <(find "$SCRIPT_DIR/tests" -type f -name "*.sh" | sort)
fi

TOTAL_TESTS=0
for test_script in "${TEST_SCRIPTS[@]}"; do
	name="$(basename "$test_script" .sh)"
	if [ "$TARGET_TEST" != "all" ] && [ "$TARGET_TEST" != "$name" ]; then
		continue
	fi
	TOTAL_TESTS=$((TOTAL_TESTS + 1))
done

ktap_print_header
ktap_set_plan "$TOTAL_TESTS"

for test_script in "${TEST_SCRIPTS[@]}"; do
	name="$(basename "$test_script" .sh)"

	if [ "$TARGET_TEST" != "all" ] && [ "$TARGET_TEST" != "$name" ]; then
		continue
	fi

	run_single_test "$test_script"
done

ktap_finished
