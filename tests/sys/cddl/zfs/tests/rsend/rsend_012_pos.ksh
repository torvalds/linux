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
# ident	"@(#)rsend_012_pos.ksh	1.2	09/05/19 SMI"
#

. $STF_SUITE/tests/rsend/rsend.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: rsend_012_pos
#
# DESCRIPTION:
#	zfs send -R will backup all the filesystem properties correctly.
#
# STRATEGY:
#	1. Setting properties for all the filesystem and volumes randomly
#	2. Backup all the data from POOL by send -R
#	3. Restore all the data in POOL2
#	4. Verify all the perperties in two pools are same
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2007-08-27)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function rand_set_prop
{
	typeset dtst=$1
	typeset prop=$2
	shift 2
	typeset value=$(random_get $@)

	log_must eval "$ZFS set $prop='$value' $dtst"
}

function edited_prop
{
	typeset behaviour=$1
	typeset ds=$2
	typeset backfile=$TESTDIR/edited_prop_$ds

	case $behaviour in
		"get") 
			typeset props=$($ZFS inherit 2>&1 | \
				$AWK '$2=="YES" {print $1}' | \
				$EGREP -v "^vol|\.\.\.$")
			for item in $props ; do
				$ZFS get -H -o property,value $item $ds >> \
					$backfile
				if (($? != 0)); then
					log_fail "zfs get -H -o property,value"\ 
						"$item $ds > $backfile"
				fi
			done
			;;
		"set")
			if [[ ! -f $backfile ]] ; then
				log_fail "$ds need backup properties firstly."
			fi

			typeset prop value
			while read prop value ; do
				eval $ZFS set $prop='$value' $ds
				if (($? != 0)); then
					log_fail "$ZFS set $prop=$value $ds"
				fi
			done < $backfile
			;;
		*)
			log_fail "Unrecognized behaviour: $behaviour"
	esac
}

function cleanup
{
	log_must cleanup_pool $POOL
	log_must cleanup_pool $POOL2

	log_must edited_prop "set" $POOL
	log_must edited_prop "set" $POOL2

	typeset prop
	for prop in $(fs_inherit_prop) ; do
		log_must $ZFS inherit $prop $POOL
		log_must $ZFS inherit $prop $POOL2
	done

	#if is_shared $POOL; then
	#	log_must $ZFS set sharenfs=off $POOL
	#fi
	log_must setup_test_model $POOL

	if [[ -d $TESTDIR ]]; then
		log_must $RM -rf $TESTDIR/*
	fi
}

log_assert "Verify zfs send -R will backup all the filesystem properties " \
	"correctly."
log_onexit cleanup

log_must edited_prop "get" $POOL
log_must edited_prop "get" $POOL2

for fs in "$POOL" "$POOL/pclone" "$POOL/$FS" "$POOL/$FS/fs1" \
	"$POOL/$FS/fs1/fs2" "$POOL/$FS/fs1/fclone" ; do
	rand_set_prop $fs aclinherit "discard" "noallow" "secure" "passthrough"
	rand_set_prop $fs checksum "on" "off" "fletcher2" "fletcher4" "sha256"
	rand_set_prop $fs aclmode "discard" "groupmask" "passthrough"
	rand_set_prop $fs atime "on" "off"
	rand_set_prop $fs checksum "on" "off" "fletcher2" "fletcher4" "sha256"
	rand_set_prop $fs compression "on" "off" "lzjb" "gzip" \
		"gzip-1" "gzip-2" "gzip-3" "gzip-4" "gzip-5" "gzip-6"   \
		"gzip-7" "gzip-8" "gzip-9"
	rand_set_prop $fs copies "1" "2" "3"
	rand_set_prop $fs devices "on" "off"
	rand_set_prop $fs exec "on" "off"
	rand_set_prop $fs quota "512M" "1024M"
	rand_set_prop $fs recordsize "512" "2K" "8K" "32K" "128K"
	rand_set_prop $fs setuid "on" "off"
	rand_set_prop $fs snapdir "hidden" "visible"
	rand_set_prop $fs xattr "on" "off"
	rand_set_prop $fs user:prop "aaa" "bbb" "23421" "()-+?"
done

for vol in "$POOL/vol" "$POOL/$FS/vol" ; do
	rand_set_prop $vol checksum "on" "off" "fletcher2" "fletcher4" "sha256"
	rand_set_prop $vol compression "on" "off" "lzjb" "gzip" \
		"gzip-1" "gzip-2" "gzip-3" "gzip-4" "gzip-5" "gzip-6"   \
		"gzip-7" "gzip-8" "gzip-9"
	rand_set_prop $vol readonly "on" "off"
	rand_set_prop $vol copies "1" "2" "3"
	rand_set_prop $vol user:prop "aaa" "bbb" "23421" "()-+?"
done


# Verify inherited property can be received
rand_set_prop $POOL sharenfs "on" "off" "rw"

#
# Duplicate POOL2 for testing
#
log_must eval "$ZFS send -R $POOL@final > $BACKDIR/pool-final-R"
log_must eval "$ZFS receive -d -F $POOL2 < $BACKDIR/pool-final-R"

#
# Define all the POOL/POOL2 datasets pair
#
set -A pair 	"$POOL" 		"$POOL2" 		\
		"$POOL/$FS" 		"$POOL2/$FS" 		\
		"$POOL/$FS/fs1"		"$POOL2/$FS/fs1"	\
		"$POOL/$FS/fs1/fs2"	"$POOL2/$FS/fs1/fs2"	\
		"$POOL/pclone"		"$POOL2/pclone"		\
		"$POOL/$FS/fs1/fclone"	"$POOL2/$FS/fs1/fclone" \
		"$POOL/vol"		"$POOL2/vol" 		\
		"$POOL/$FS/vol"		"$POOL2/$FS/vol"

typeset -i i=0
while ((i < ${#pair[@]})); do
	log_must cmp_ds_prop ${pair[$i]} ${pair[((i+1))]}

	((i += 2))
done


$ZPOOL upgrade -v | $GREP "Snapshot properties" > /dev/null 2>&1
if (( $? == 0 )) ; then
	i=0
	while ((i < ${#pair[@]})); do
		log_must cmp_ds_prop ${pair[$i]}@final ${pair[((i+1))]}@final
		((i += 2))
	done
fi

log_pass "Verify zfs send -R will backup all the filesystem properties " \
	"correctly."
