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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)history_007_pos.ksh	1.1	07/01/09 SMI"
#

. $STF_SUITE/tests/history/history_common.kshlib

#################################################################################
#
# __stc_assertion_start
#
# ID: history_007_pos
#
# DESCRIPTION:
#	Verify command history moves with pool while pool being migrated 
#
# STRATEGY:
#	1. Import uniform platform and cross platform pools
#	2. Contract the command history of the imported pool
#	3. Compare imported history log with the previous log.
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-10-25)
#
# __stc_assertion_end
#
################################################################################

verify_runnable "global"

function cleanup
{
	poolexists $migratedpoolname &&  \
		log_must $ZPOOL destroy -f $migratedpoolname

	[[ -d $import_dir ]] && $RM -rf $import_dir
}

log_assert "Verify command history moves with pool while migrating."
log_onexit cleanup

# The *.orig_history.txt files were made using the America/Denver timezone,
# and since "zpool history" outputs timestamps in localtime, the test must
# be run in that same timezone
export TZ="America/Denver"

tst_dir=$STF_SUITE/tests/history
import_dir=$TESTDIR/importdir.${TESTCASE_ID}
migrated_cmds_f=$import_dir/migrated_history.${TESTCASE_ID}
migratedpoolname=$MIGRATEDPOOLNAME
typeset -i RET=1
typeset -i linenum=0

[[ ! -d $import_dir ]] && log_must $MKDIR $import_dir

# We test the migrations on both uniform platform and cross platform
for arch in "i386" "sparc"; do
	log_must $CP $tst_dir/${arch}.orig_history.txt $import_dir
	orig_cmds_f=$import_dir/${arch}.orig_history.txt
	#remove blank line
	orig_cmds_f1=$import_dir/${arch}.orig_history_1.txt
	$CAT $orig_cmds_f | $GREP -v "^$" > $orig_cmds_f1
	
	log_must $CP $tst_dir/${arch}.migratedpool.DAT.Z $import_dir
	log_must $UNCOMPRESS $import_dir/${arch}.migratedpool.DAT.Z

	#destroy the pool with same name, so that import operation could succeed.
	poolexists $migratedpoolname &&  \
		log_must $ZPOOL destroy -f $migratedpoolname

	log_must $ZPOOL import -d $import_dir $migratedpoolname
	$ZPOOL history $migratedpoolname | $GREP -v "^$" >$migrated_cmds_f
	RET=$?
	(( $RET != 0 )) && log_fail "$ZPOOL histroy $migratedpoolname fails."

	# The migrated history file should differ with original history file on
	# two commands -- 'export' and 'import', which are included in migrated
	# history file but not in original history file. so, check the two commands
	# firstly in migrated history file and then delete them, and then compare 
	# this filtered file with the original history file. They should be identical
	# at this time. 
	for subcmd in "export" "import"; do
		$GREP "$subcmd" $migrated_cmds_f >/dev/null 2>&1
		RET=$?
		(( $RET != 0 )) && log_fail "zpool $subcmd is not logged for" \
				"the imported pool $migratedpoolname."
	done

	tmpfile=$import_dir/cmds_tmp.${TESTCASE_ID}
	linenum=`$CAT $migrated_cmds_f | $WC -l`
	(( linenum = linenum - 2 ))
	$HEAD -n $linenum $migrated_cmds_f > $tmpfile
	log_must $DIFF $tmpfile $orig_cmds_f1
	
	#cleanup for next loop testing
	log_must $ZPOOL destroy -f $migratedpoolname
	log_must $RM -f `$LS $import_dir`
done

log_pass "Command history moves with pool as expected while pool being migrated. "
