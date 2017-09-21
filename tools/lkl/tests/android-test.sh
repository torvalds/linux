#!/system/bin/sh

set -e

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export PATH=${script_dir}:${PATH}
export LKL_ANDROID_TEST=1
export TMPDIR=/data/local/tmp
export CONFIG_AUTO_LKL_POSIX_HOST=y
export LKL_TEST_DHCP=1

sed -i "s/\/bin\/bash/\/system\/bin\/sh /" ${script_dir}/../bin/lkl-hijack.sh

cd ${script_dir}
boot -d ${script_dir}/disk.img  -t ext4
sh -x ${script_dir}/hijack-test.sh
sh ${script_dir}/net.sh
