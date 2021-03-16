
# SPDX-License-Identifier: GPL-2.0

# Overrides functions in gpio-mockup.sh to test using the GPIO SYSFS uAPI

SYSFS=`grep -w sysfs /proc/mounts | cut -f2 -d' '`
[ -d "$SYSFS" ] || skip "sysfs is not mounted"

GPIO_SYSFS="${SYSFS}/class/gpio"
[ -d "$GPIO_SYSFS" ] || skip "CONFIG_GPIO_SYSFS is not selected"

PLATFORM_SYSFS=$SYSFS/devices/platform

sysfs_nr=
sysfs_ldir=

# determine the sysfs GPIO number given the $chip and $offset
# e.g. gpiochip1:32
find_sysfs_nr()
{
	# e.g. /sys/devices/platform/gpio-mockup.1/gpiochip1
	local platform=$(find $PLATFORM_SYSFS -mindepth 2 -maxdepth 2 -type d -name $chip)
	[ "$platform" ] || fail "can't find platform of $chip"
	# e.g. /sys/devices/platform/gpio-mockup.1/gpio/gpiochip508/base
	local base=$(find ${platform%/*}/gpio/ -mindepth 2 -maxdepth 2 -type f -name base)
	[ "$base" ] || fail "can't find base of $chip"
	sysfs_nr=$(($(< "$base") + $offset))
	sysfs_ldir="$GPIO_SYSFS/gpio$sysfs_nr"
}

acquire_line()
{
	[ "$sysfs_nr" ] && return
	find_sysfs_nr
	echo "$sysfs_nr" > "$GPIO_SYSFS/export"
}

# The helpers being overridden...
get_line()
{
	[ -e "$sysfs_ldir/value" ] && echo $(< "$sysfs_ldir/value")
}

set_line()
{
	acquire_line

	for option in $*; do
		case $option in
		active-high)
			echo 0 > "$sysfs_ldir/active_low"
			;;
		active-low)
			echo 1 > "$sysfs_ldir/active_low"
			;;
		input)
			echo "in" > "$sysfs_ldir/direction"
			;;
		0)
			echo "out" > "$sysfs_ldir/direction"
			echo 0 > "$sysfs_ldir/value"
			;;
		1)
			echo "out" > "$sysfs_ldir/direction"
			echo 1 > "$sysfs_ldir/value"
			;;
		esac
	done
}

release_line()
{
	[ "$sysfs_nr" ] || return 0
	echo "$sysfs_nr" > "$GPIO_SYSFS/unexport"
	sysfs_nr=
	sysfs_ldir=
}
