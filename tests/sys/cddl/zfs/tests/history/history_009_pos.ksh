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
# ident	"@(#)history_009_pos.ksh	1.4	09/01/12 SMI"
#

. $STF_SUITE/tests/history/history_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: history_009_pos
#
# DESCRIPTION:
#	Verify the delegation internal history are correctly.
#
# 	ul$<id>    identifies permssions granted locally for this userid.
# 	ud$<id>    identifies permissions granted on descendent datasets for
#		   this userid.
#	
#	Ul$<id>    identifies permission sets granted locally for this userid.
#	Ud$<id>    identifies permission sets granted on descendent datasets for
#		   this	userid.
#	
#	gl$<id>    identifies permissions granted locally for this groupid.
#	gd$<id>    identifies permissions granted on descendent datasets for
#		   this groupid.
#	
#	Gl$<id>    identifies permission sets granted locally for this groupid.
#	Gd$<id>    identifies permission sets granted on descendent datasets for
#	           this groupid.
#
#	el$        identifies permissions granted locally for everyone.
#	ed$        identifies permissions granted on descendent datasets for
#		   everyone.
#	
#	El$        identifies permission sets granted locally for everyone.
#	Ed$        identifies permission sets granted to descendent datasets
#		   for everyone.
#	
#	c-$        identifies permission to create at dataset creation time.
#	C-$        identifies permission sets to grant locally at dataset
#		   creation time.
#	
#	s-$@<name> permissions defined in specified set @<name>
#	S-$@<name> Sets defined in named set @<name>
#
# STRATEGY:
#	1. Create test group and user.
#	2. Define permission sets and verify the internal history correctly.
#	3. Separately verify the internal history above is correct.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-12-26)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

$ZFS 2>&1 | $GREP "allow" > /dev/null
(($? != 0)) && log_unsupported

function cleanup
{
	if [[ -f $REAL_HISTORY ]]; then
		log_must $RM -f $REAL_HISTORY
	fi
	if [[ -f $ADD_HISTORY ]]; then
		log_must $RM -f $ADD_HISTORY
	fi
	del_user $HIST_USER
	del_group $HIST_GROUP
}

log_assert "Verify the delegation internal history are correctly."
log_onexit cleanup

testfs=$TESTPOOL/$TESTFS
# Create history test group and user and get user id and group id
add_group $HIST_GROUP
add_user $HIST_GROUP $HIST_USER

uid=$($ID $HIST_USER | $AWK -F= '{print $2}'| $AWK -F"(" '{print $1}' )
gid=$($ID $HIST_USER | $AWK -F= '{print $3}'| $AWK -F"(" '{print $1}' )

# Initial original $REAL_HISTORY 
format_history $TESTPOOL $REAL_HISTORY -i

#
#	Keyword		subcmd		operating	allow_options
#
set -A array \
	"s-\$@basic"	"allow"		"-s @basic snapshot"    	\
	"S-\$@set"	"allow"		"-s @set @basic"		\
	"c-\\$"		"allow"		"-c create"			\
	"c-\\$"		"unallow"	"-c create"			\
	"C-\\$ @set"	"allow"		"-c @set"			\
	"C-\\$ @set"	"unallow"	"-c @set"			\
	"ul\$$uid"	"allow"		"-l -u $HIST_USER snapshot"	\
	"ul\$$uid"	"allow"		"-u $HIST_USER snapshot"	\
	"ul\$$uid"	"unallow"	"-u $HIST_USER snapshot"	\
	"Ul\$$uid"	"allow"		"-l -u $HIST_USER @set"		\
	"Ul\$$uid"	"allow"		"-u $HIST_USER @set"		\
	"Ul\$$uid"	"unallow"	"-u $HIST_USER @set"		\
	"ud\$$uid"	"allow"		"-d -u $HIST_USER snapshot"	\
	"ud\$$uid"	"allow"		"-u $HIST_USER snapshot"	\
	"ud\$$uid"	"unallow"	"-u $HIST_USER snapshot"	\
	"Ud\$$uid"	"allow"		"-d -u $HIST_USER @set"		\
	"Ud\$$uid"	"allow"		"-u $HIST_USER @set"		\
	"Ud\$$uid"	"unallow"	"-u $HIST_USER @set"		\
	"gl\$$gid"	"allow"		"-l -g $HIST_GROUP snapshot"	\
	"gl\$$gid"	"allow"		"-g $HIST_GROUP snapshot"	\
	"gl\$$gid"	"unallow"	"-g $HIST_GROUP snapshot"	\
	"Gl\$$gid"	"allow"		"-l -g $HIST_GROUP @set"	\
	"Gl\$$gid"	"allow"		"-g $HIST_GROUP @set"		\
	"Gl\$$gid"	"unallow"	"-g $HIST_GROUP @set"		\
	"gd\$$gid"	"allow"		"-d -g $HIST_GROUP snapshot"	\
	"gd\$$gid"	"allow"		"-g $HIST_GROUP snapshot"	\
	"gd\$$gid"	"unallow"	"-g $HIST_GROUP snapshot"	\
	"Gd\$$gid"	"allow"		"-d -g $HIST_GROUP @set"	\
	"Gd\$$gid"	"allow"		"-g $HIST_GROUP @set"		\
	"Gd\$$gid"	"unallow"	"-g $HIST_GROUP @set"		\
	"el\\$"		"allow"		"-l -e snapshot"		\
	"el\\$"		"allow"		"-e snapshot"			\
	"el\\$"		"unallow"	"-e snapshot"			\
	"El\\$"		"allow"		"-l -e @set"			\
	"El\\$"		"allow"		"-e @set"			\
	"El\\$"		"unallow"	"-e @set"			\
	"ed\\$"		"allow"		"-d -e snapshot"		\
	"ed\\$"		"allow"		"-e snapshot"			\
	"ed\\$"		"unallow"	"-e snapshot"			\
	"Ed\\$"		"allow"		"-d -e @set"			\
	"Ed\\$"		"allow"		"-e @set"			\
	"Ed\\$"		"unallow"	"-e @set"

typeset -i i=0
while ((i < ${#array[@]})); do
	keyword=${array[$i]}
	subcmd=${array[((i+1))]}
	options=${array[((i+2))]}

	log_must $ZFS $subcmd $options $testfs
	additional_history $TESTPOOL $ADD_HISTORY -i
	log_must verify_history $ADD_HISTORY $subcmd $testfs $keyword

	((i += 3))
done

log_pass "Verify the delegation internal history are correctly."
