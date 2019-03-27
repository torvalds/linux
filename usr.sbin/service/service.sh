#!/bin/sh

# $FreeBSD$

# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
#  Copyright (c) 2009 Douglas Barton
#  All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions
#  are met:
#  1. Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
#  2. Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
#  THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
#  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
#  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
#  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
#  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
#  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
#  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
#  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
#  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
#  SUCH DAMAGE.

. /etc/rc.subr
load_rc_config 'XXX'

usage () {
	echo ''
	echo 'Usage:'
	echo "${0##*/} [-j <jail name or id>] -e"
	echo "${0##*/} [-j <jail name or id>] -R"
	echo "${0##*/} [-j <jail name or id>] [-v] -l | -r"
	echo "${0##*/} [-j <jail name or id>] [-v] <rc.d script> start|stop|etc."
	echo "${0##*/} -h"
	echo ''
	echo "-j	Perform actions within the named jail"
	echo '-e	Show services that are enabled'
	echo "-R	Stop and start enabled $local_startup services"
	echo "-l	List all scripts in /etc/rc.d and $local_startup"
	echo '-r	Show the results of boot time rcorder'
	echo '-v	Verbose'
	echo ''
}

while getopts 'j:ehlrRv' COMMAND_LINE_ARGUMENT ; do
	case "${COMMAND_LINE_ARGUMENT}" in
	j)	JAIL="${OPTARG}" ;;
	e)	ENABLED=eopt ;;
	h)	usage ; exit 0 ;;
	l)	LIST=lopt ;;
	r)	RCORDER=ropt ;;
	R)	RESTART=Ropt ;;
	v)	VERBOSE=vopt ;;
	*)	usage ; exit 1 ;;
	esac
done
shift $(( $OPTIND - 1 ))

if [ -n "${JAIL}" ]; then
	# We need to rebuild the command line before passing it on.
	# We do not send the -j argument into the jail.
	args=""
	[ -n "${ENABLED}" ] && args="${args} -e"
	[ -n "${LIST}" ] && args="${args} -l"
	[ -n "${RCORDER}" ] && args="${args} -r"
	[ -n "${RESTART}" ] && args="${args} -R"
	[ -n "${VERBOSE}" ] && args="${args} -v"

	# Call jexec(8) with the rebuild args and any positional args that
	# were left in $@
	/usr/sbin/jexec -l "${JAIL}" /usr/sbin/service $args "$@"
	exit $?
fi

if [ -n "$RESTART" ]; then
	skip="-s nostart"
	if [ `/sbin/sysctl -n security.jail.jailed` -eq 1 ]; then
		skip="$skip -s nojail"
	fi
	[ -n "$local_startup" ] && find_local_scripts_new
	files=`rcorder ${skip} ${local_rc} 2>/dev/null`

	for file in `reverse_list ${files}`; do
		if grep -q ^rcvar $file; then
			eval `grep ^name= $file`
			eval `grep ^rcvar $file`
			if [ -n "$rcvar" ]; then
				load_rc_config_var ${name} ${rcvar}
			fi
			checkyesno $rcvar 2>/dev/null && run_rc_script ${file} stop
		fi
	done
	for file in $files; do
		if grep -q ^rcvar $file; then
			eval `grep ^name= $file`
			eval `grep ^rcvar $file`
			checkyesno $rcvar 2>/dev/null && run_rc_script ${file} start
		fi
	done

	exit 0
fi

if [ -n "$ENABLED" -o -n "$RCORDER" ]; then
	# Copied from /etc/rc
	skip="-s nostart"
	if [ `/sbin/sysctl -n security.jail.jailed` -eq 1 ]; then
		skip="$skip -s nojail"
	fi
	[ -n "$local_startup" ] && find_local_scripts_new
	files=`rcorder ${skip} /etc/rc.d/* ${local_rc} 2>/dev/null`
fi

if [ -n "$ENABLED" ]; then
	for file in $files; do
		if grep -q ^rcvar $file; then
			eval `grep ^name= $file`
			eval `grep ^rcvar $file`
			if [ -n "$rcvar" ]; then
				load_rc_config_var ${name} ${rcvar}
			fi
			checkyesno $rcvar 2>/dev/null && echo $file
		fi
	done
	exit 0
fi

if [ -n "$LIST" ]; then
	for dir in /etc/rc.d $local_startup; do
		[ -n "$VERBOSE" ] && echo "From ${dir}:"
		[ -d ${dir} ] && /bin/ls -1 ${dir}
	done
	exit 0
fi

if [ -n "$RCORDER" ]; then
	for file in $files; do
		echo $file
		if [ -n "$VERBOSE" ]; then
			case "$file" in
			*/${early_late_divider})
				echo '========= Early/Late Divider =========' ;;
			esac
		fi
	done
	exit 0
fi

if [ $# -gt 1 ]; then
	script=$1
	shift
else
	usage
	exit 1
fi

cd /
for dir in /etc/rc.d $local_startup; do
	if [ -x "$dir/$script" ]; then
		[ -n "$VERBOSE" ] && echo "$script is located in $dir"
		exec env -i HOME=/ PATH=/sbin:/bin:/usr/sbin:/usr/bin $dir/$script "$@"
	fi
done

# If the script was not found
echo "$script does not exist in /etc/rc.d or the local startup"
echo "directories (${local_startup}), or is not executable"
exit 1
