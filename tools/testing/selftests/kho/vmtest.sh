#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

set -ue

CROSS_COMPILE="${CROSS_COMPILE:-""}"

test_dir=$(realpath "$(dirname "$0")")
kernel_dir=$(realpath "$test_dir/../../../..")

tmp_dir=$(mktemp -d /tmp/kho-test.XXXXXXXX)
headers_dir="$tmp_dir/usr"
initrd="$tmp_dir/initrd.cpio"

source "$test_dir/../kselftest/ktap_helpers.sh"

function usage() {
	cat <<EOF
$0 [-d build_dir] [-j jobs] [-t target_arch] [-h]
Options:
	-d)	path to the kernel build directory
	-j)	number of jobs for compilation, similar to -j in make
	-t)	run test for target_arch, requires CROSS_COMPILE set
		supported targets: aarch64, x86_64
	-h)	display this help
EOF
}

function cleanup() {
	rm -fr "$tmp_dir"
	ktap_finished
}
trap cleanup EXIT

function skip() {
	local msg=${1:-""}

	ktap_test_skip "$msg"
	exit "$KSFT_SKIP"
}

function fail() {
	local msg=${1:-""}

	ktap_test_fail "$msg"
	exit "$KSFT_FAIL"
}

function build_kernel() {
	local build_dir=$1
	local make_cmd=$2
	local arch_kconfig=$3
	local kimage=$4

	local kho_config="$tmp_dir/kho.config"
	local kconfig="$build_dir/.config"

	# enable initrd, KHO and KHO test in kernel configuration
	tee "$kconfig" > "$kho_config" <<EOF
CONFIG_BLK_DEV_INITRD=y
CONFIG_KEXEC_HANDOVER=y
CONFIG_TEST_KEXEC_HANDOVER=y
CONFIG_DEBUG_KERNEL=y
CONFIG_DEBUG_VM=y
$arch_kconfig
EOF

	make_cmd="$make_cmd -C $kernel_dir O=$build_dir"
	$make_cmd olddefconfig

	# verify that kernel confiration has all necessary options
	while read -r opt ; do
		grep "$opt" "$kconfig" &>/dev/null || skip "$opt is missing"
	done < "$kho_config"

	$make_cmd "$kimage"
	$make_cmd headers_install INSTALL_HDR_PATH="$headers_dir"
}

function mkinitrd() {
	local kernel=$1

	"$CROSS_COMPILE"gcc -s -static -Os -nostdinc -nostdlib \
			-fno-asynchronous-unwind-tables -fno-ident \
			-I "$headers_dir/include" \
			-I "$kernel_dir/tools/include/nolibc" \
			-o "$tmp_dir/init" "$test_dir/init.c"

	cat > "$tmp_dir/cpio_list" <<EOF
dir /dev 0755 0 0
dir /proc 0755 0 0
dir /debugfs 0755 0 0
nod /dev/console 0600 0 0 c 5 1
file /init $tmp_dir/init 0755 0 0
file /kernel $kernel 0644 0 0
EOF

	"$build_dir/usr/gen_init_cpio" "$tmp_dir/cpio_list" > "$initrd"
}

function run_qemu() {
	local qemu_cmd=$1
	local cmdline=$2
	local kernel=$3
	local serial="$tmp_dir/qemu.serial"

	cmdline="$cmdline kho=on panic=-1"

	$qemu_cmd -m 1G -smp 2 -no-reboot -nographic -nodefaults \
		  -accel kvm -accel hvf -accel tcg  \
		  -serial file:"$serial" \
		  -append "$cmdline" \
		  -kernel "$kernel" \
		  -initrd "$initrd"

	grep "KHO restore succeeded" "$serial" &> /dev/null || fail "KHO failed"
}

function target_to_arch() {
	local target=$1

	case $target in
	     aarch64) echo "arm64" ;;
	     x86_64) echo "x86" ;;
	     *) skip "architecture $target is not supported"
	esac
}

function main() {
	local build_dir="$kernel_dir/.kho"
	local jobs=$(($(nproc) * 2))
	local target="$(uname -m)"

	# skip the test if any of the preparation steps fails
	set -o errtrace
	trap skip ERR

	while getopts 'hd:j:t:' opt; do
		case $opt in
		d)
			build_dir="$OPTARG"
			;;
		j)
		        jobs="$OPTARG"
			;;
		t)
			target="$OPTARG"
			;;
		h)
			usage
			exit 0
			;;
		*)
			echo Unknown argument "$opt"
			usage
			exit 1
			;;
		esac
	done

	ktap_print_header
	ktap_set_plan 1

	if [[ "$target" != "$(uname -m)" ]] && [[ -z "$CROSS_COMPILE" ]]; then
		skip "Cross-platform testing needs to specify CROSS_COMPILE"
	fi

	mkdir -p "$build_dir"
	local arch=$(target_to_arch "$target")
	source "$test_dir/$arch.conf"

	# build the kernel and create initrd
	# initrd includes the kernel image that will be kexec'ed
	local make_cmd="make ARCH=$arch CROSS_COMPILE=$CROSS_COMPILE -j$jobs"
	build_kernel "$build_dir" "$make_cmd" "$QEMU_KCONFIG" "$KERNEL_IMAGE"

	local kernel="$build_dir/arch/$arch/boot/$KERNEL_IMAGE"
	mkinitrd "$kernel"

	run_qemu "$QEMU_CMD" "$KERNEL_CMDLINE" "$kernel"

	ktap_test_pass "KHO succeeded"
}

main "$@"
