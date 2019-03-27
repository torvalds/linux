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
# ident	"@(#)zfs_crypto.ksh	1.2	09/05/19 SMI"
#

cmd=$1
shift
options="$@"

case $cmd in
	create)
		# Get zfs name
		# eval zfsname=\${$#}

		if [[ $KEYSOURCE_DATASET == "passphrase" ]]; then
			options="-o encryption=$ENCRYPTION \
-o keysource=passphrase,file://$PASSPHRASE_FILE $options"
		elif [[ $KEYSOURCE_DATASET == "raw" ]]; then
			options="-o encryption=$ENCRYPTION \
-o keysource=raw,file://$RAW_KEY_FILE $options"
		elif [[ $KEYSOURCE_DATASET == "hex" ]]; then
			options="-o encryption=$ENCRYPTION \
-o keysource=hex,file://$HEX_KEY_FILE $options"
		elif [[ -n $KEYSOURCE_DATASET ]]; then
			log_note "Warning: invalid KEYSOURCE_DATASET \c"
			log_note "value: $KEYSOURCE_DATASET, ignore it"
		fi
		;;
	*)
		;;
esac

print $cmd $options
