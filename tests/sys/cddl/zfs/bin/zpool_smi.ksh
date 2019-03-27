#!/usr/local/bin/ksh93 -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

# $FreeBSD$

#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)zpool_smi.ksh	1.2	09/01/13 SMI"
#

function labelvtoc
{
	typeset disk=$1
	if [[ -z $disk ]]; then
		print "no disk is given."
		return 1
	fi

	/usr/sbin/format $disk << _EOF >/dev/null 2>&1
label
yes
_EOF

	labeltype=$(/usr/sbin/prtvtoc -fh /dev/${disk}s2 | \
		awk '{print $1}' | awk -F= '{print $2}' )
	if [[ -z $labeltype ]]; then
		print "${disk} not exist."
		return 1
	fi

	if [[ $labeltype == "34" ]]; then
	
		typeset label_file=$TMPDIR/labelvtoc.${TESTCASE_ID:-$$}
		typeset arch=$(uname -p)
	
		if [[ $arch == "i386" ]]; then
			print "label" > $label_file
			print "0" >> $label_file
	       		print "" >> $label_file
		 	print "q" >> $label_file
		 	print "q" >> $label_file

		 	fdisk -B /dev/${disk}p0 >/dev/null 2>&1
		 	# wait a while for fdisk finishes
			/usr/sbin/devfsadm > /dev/null 2>&1
		elif [[ $arch == "sparc" ]]; then
			print "label" > $label_file
			print "0" >> $label_file
			print "" >> $label_file
			print "" >> $label_file
			print "" >> $label_file
			print "q" >> $label_file
		else
	   		print "unknow arch type : $arch"
			return 1
		fi

		format -e -s -d $disk -f $label_file
		typeset -i ret_val=$?
		rm -f $label_file
		#
		# wait the format to finish
		#
		/usr/sbin/devfsadm > /dev/null 2>&1
		if (( ret_val != 0 )); then
			print "unable to label $disk as VTOC."
			return 1
		fi
	fi

	return 0
}

cmd=$1

if [[ -z $cmd ]]; then
	return 0
fi

shift


typeset option
case $cmd in
	create|add|attach|detach|replace|remove|online|offline|clear)
		for arg in $@; do
			if [[ $arg == "/dev/"* ]]; then
				arg=${arg#/dev/}
			fi

			print $arg | egrep "^c[0-F]+([td][0-F]+)+$" > /dev/null 2>&1

			if [[ $? -eq 0 ]] ; then
				labelvtoc $arg
				if [[ $? -eq 0 ]] ; then
					arg=${arg}s2
				fi
			fi
			option="${option} $arg"
		done
		;;
	*)
		option="$@"
		;;
esac

case $cmd in
	create|add|attach|replace)
		if [[ $option != *"-f"* ]]; then
			cmd="${cmd} -f"
		fi
		;;
	*)
		;;
esac

print $cmd $option
