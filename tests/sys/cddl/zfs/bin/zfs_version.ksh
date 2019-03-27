#! /usr/local/bin/ksh93 -p
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
# ident	"@(#)zfs_version.ksh	1.1	09/01/13 SMI"
#

cmd=$1
shift
options="$@"

o_version_option="no"
V_option="no"

while getopts :o:V: c
do
	case $c in
		o)
			if [[ "$OPTARG" == "version="* ]]; then
	   			o_version_option="yes"
	   		fi
	   		;;
		V)
			V_option="yes"
			;;
		*)
			;;
	esac
done
shift $(($OPTIND - 1))

case $cmd in
	create)
		if [[ "$ZFS_TEST_VERSION" != "0" ]] &&
		   [[ "$o_version_option" == "no" ]] &&
		   [[ "$V_option" == "no" ]]; then
			options="-o version=$ZFS_TEST_VERSION $options"
		fi
		;;
	*)
		;;
esac

print "$cmd $options"
