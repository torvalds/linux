#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

UTS_MACHINE=$1
CC_VERSION="$2"
LD=$3

if test -z "$KBUILD_BUILD_USER"; then
	LINUX_COMPILE_BY=$(whoami | sed 's/\\/\\\\/')
else
	LINUX_COMPILE_BY=$KBUILD_BUILD_USER
fi
if test -z "$KBUILD_BUILD_HOST"; then
	LINUX_COMPILE_HOST=`uname -n`
else
	LINUX_COMPILE_HOST=$KBUILD_BUILD_HOST
fi

LD_VERSION=$(LC_ALL=C $LD -v | head -n1 |
		sed -e 's/(compatible with [^)]*)//' -e 's/[[:space:]]*$//')

cat <<EOF
#define UTS_MACHINE		"${UTS_MACHINE}"
#define LINUX_COMPILE_BY	"${LINUX_COMPILE_BY}"
#define LINUX_COMPILE_HOST	"${LINUX_COMPILE_HOST}"
#define LINUX_COMPILER		"${CC_VERSION}, ${LD_VERSION}"
EOF
