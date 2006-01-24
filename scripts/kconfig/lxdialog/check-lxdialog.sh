#!/bin/sh
# Check ncurses compatibility

# What library to link
ldflags()
{
	echo "main() {}" | $cc -lncursesw -xc - -o /dev/null 2> /dev/null
	if [ $? -eq 0 ]; then
		echo '-lncursesw'
		exit
	fi
	echo "main() {}" | $cc -lncurses -xc - -o /dev/null 2> /dev/null
	if [ $? -eq 0 ]; then
		echo '-lncurses'
		exit
	fi
	echo "main() {}" | $cc -lcurses -xc - -o /dev/null 2> /dev/null
	if [ $? -eq 0 ]; then
		echo '-lcurses'
		exit
	fi
	exit 1
}

# Where is ncurses.h?
ccflags()
{
	if [ -f /usr/include/ncurses/ncurses.h ]; then
		echo '-I/usr/include/ncurses -DCURSES_LOC="<ncurses.h>"'
	elif [ -f /usr/include/ncurses/curses.h ]; then
		echo '-I/usr/include/ncurses -DCURSES_LOC="<ncurses/curses.h>"'
	elif [ -f /usr/include/ncurses.h ]; then
		echo '-DCURSES_LOC="<ncurses.h>"'
	else
		echo '-DCURSES_LOC="<curses.h>"'
	fi
}

compiler=""
# Check if we can link to ncurses
check() {
	echo "main() {}" | $cc -xc - -o /dev/null 2> /dev/null
	if [ $? != 0 ]; then
		echo " *** Unable to find the ncurses libraries."          1>&2
		echo " *** make menuconfig require the ncurses libraries"  1>&2
		echo " *** "                                               1>&2
		echo " *** Install ncurses (ncurses-devel) and try again"  1>&2
		echo " *** "                                               1>&2
		exit 1
	fi
}

usage() {
	printf "Usage: $0 [-check compiler options|-header|-library]\n"
}

if [ $# == 0 ]; then
	usage
	exit 1
fi

case "$1" in
	"-check")
		shift
		cc="$@"
		check
		;;
	"-ccflags")
		ccflags
		;;
	"-ldflags")
		shift
		cc="$@"
		ldflags
		;;
	"*")
		usage
		exit 1
		;;
esac
