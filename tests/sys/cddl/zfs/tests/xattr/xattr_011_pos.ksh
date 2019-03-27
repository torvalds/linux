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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"@(#)xattr_011_pos.ksh	1.1	07/02/06 SMI"
#

# $FreeBSD$

. $STF_SUITE/include/libtest.kshlib
. $STF_SUITE/tests/xattr/xattr_common.kshlib

################################################################################
#
# __stc_assertion_start
#
# ID:  xattr_011_pos
#
# DESCRIPTION:
#
# Basic applications work with xattrs: cpio cp find mv pax tar
# 
# STRATEGY:
#	1. For each application
#       2. Create an xattr and archive/move/copy/find files with xattr support
#	3. Also check that when appropriate flag is not used, the xattr
#	   doesn't get copied
#
# TESTABILITY: explicit
#
# TEST_AUTOMATION_LEVEL: automated
#
# CODING_STATUS: COMPLETED (2006-12-15)
#
# __stc_assertion_end
#
################################################################################

function cleanup {

	log_must $RM $TESTDIR/myfile.${TESTCASE_ID}
}

log_assert "Basic applications work with xattrs: cpio cp find mv pax tar"
log_onexit cleanup

test_requires RUNAT

# Create a file, and set an xattr on it. This file is used in several of the
# test scenarios below.
log_must $TOUCH $TESTDIR/myfile.${TESTCASE_ID}
create_xattr $TESTDIR/myfile.${TESTCASE_ID} passwd /etc/passwd


# For the archive applications below (tar, cpio, pax)
# we create two archives, one with xattrs, one without
# and try various cpio options extracting the archives
# with and without xattr support, checking for correct behaviour


log_note "Checking cpio"
log_must $TOUCH $TESTDIR/cpio.${TESTCASE_ID}
create_xattr $TESTDIR/cpio.${TESTCASE_ID} passwd /etc/passwd
$ECHO $TESTDIR/cpio.${TESTCASE_ID} | $CPIO -o@ > $TMPDIR/xattr.${TESTCASE_ID}.cpio
$ECHO $TESTDIR/cpio.${TESTCASE_ID} | $CPIO -o > $TMPDIR/noxattr.${TESTCASE_ID}.cpio

# we should have no xattr here
log_must $CPIO -iu < $TMPDIR/xattr.${TESTCASE_ID}.cpio
log_mustnot eval "$RUNAT $TESTDIR/cpio.${TESTCASE_ID} $CAT passwd > /dev/null 2>&1"

# we should have an xattr here
log_must $CPIO -iu@ < $TMPDIR/xattr.${TESTCASE_ID}.cpio
log_must eval "$RUNAT $TESTDIR/cpio.${TESTCASE_ID} $CAT passwd > /dev/null 2>&1"

# we should have no xattr here
log_must $CPIO -iu < $TMPDIR/noxattr.${TESTCASE_ID}.cpio
log_mustnot eval "$RUNAT $TESTDIR/cpio.${TESTCASE_ID} $CAT passwd > /dev/null 2>&1"

# we should have no xattr here
log_must $CPIO -iu@ < $TMPDIR/noxattr.${TESTCASE_ID}.cpio
log_mustnot eval "$RUNAT $TESTDIR/cpio.${TESTCASE_ID} $CAT passwd > /dev/null 2>&1"
log_must $RM $TESTDIR/cpio.${TESTCASE_ID} $TMPDIR/xattr.${TESTCASE_ID}.cpio $TMPDIR/noxattr.${TESTCASE_ID}.cpio



log_note "Checking cp"
# check that with the right flag, the xattr is preserved
log_must $CP -@ $TESTDIR/myfile.${TESTCASE_ID} $TESTDIR/myfile2.${TESTCASE_ID}
compare_xattrs $TESTDIR/myfile.${TESTCASE_ID} $TESTDIR/myfile2.${TESTCASE_ID} passwd
log_must $RM $TESTDIR/myfile2.${TESTCASE_ID}

# without the right flag, there should be no xattr
log_must $CP $TESTDIR/myfile.${TESTCASE_ID} $TESTDIR/myfile2.${TESTCASE_ID}
log_mustnot eval "$RUNAT $TESTDIR/myfile2.${TESTCASE_ID} $LS passwd > /dev/null 2>&1"
log_must $RM $TESTDIR/myfile2.${TESTCASE_ID}



log_note "Checking find"
# create a file without xattrs, and check that find -xattr only finds
# our test file that has an xattr.
log_must $MKDIR $TESTDIR/noxattrs
log_must $TOUCH $TESTDIR/noxattrs/no-xattr

$FIND $TESTDIR -xattr | $GREP myfile.${TESTCASE_ID}
if [ $? -ne 0 ]
then
	log_fail "find -xattr didn't find our file that had an xattr."
fi
$FIND $TESTDIR -xattr | $GREP no-xattr
if [ $? -eq 0 ]
then
	log_fail "find -xattr found a file that didn't have an xattr."
fi
log_must $RM -rf $TESTDIR/noxattrs



log_note "Checking mv"
# mv doesn't have any flags to preserve/ommit xattrs - they're
# always moved.
log_must $TOUCH $TESTDIR/mvfile.${TESTCASE_ID}
create_xattr $TESTDIR/mvfile.${TESTCASE_ID} passwd /etc/passwd
log_must $MV $TESTDIR/mvfile.${TESTCASE_ID} $TESTDIR/mvfile2.${TESTCASE_ID}
verify_xattr $TESTDIR/mvfile2.${TESTCASE_ID} passwd /etc/passwd
log_must $RM $TESTDIR/mvfile2.${TESTCASE_ID}


log_note "Checking pax"
log_must $TOUCH $TESTDIR/pax.${TESTCASE_ID}
create_xattr $TESTDIR/pax.${TESTCASE_ID} passwd /etc/passwd
log_must $PAX -w -f $TESTDIR/noxattr.pax $TESTDIR/pax.${TESTCASE_ID}
log_must $PAX -w@ -f $TESTDIR/xattr.pax $TESTDIR/pax.${TESTCASE_ID}
log_must $RM $TESTDIR/pax.${TESTCASE_ID}

# we should have no xattr here
log_must $PAX -r -f $TESTDIR/noxattr.pax
log_mustnot eval "$RUNAT $TESTDIR/pax.${TESTCASE_ID} $CAT passwd > /dev/null 2>&1"
log_must $RM $TESTDIR/pax.${TESTCASE_ID} 

# we should have no xattr here
log_must $PAX -r@ -f $TESTDIR/noxattr.pax
log_mustnot eval "$RUNAT $TESTDIR/pax.${TESTCASE_ID} $CAT passwd > /dev/null 2>&1"
log_must $RM $TESTDIR/pax.${TESTCASE_ID}


# we should have an xattr here
log_must $PAX -r@ -f $TESTDIR/xattr.pax
verify_xattr $TESTDIR/pax.${TESTCASE_ID} passwd /etc/passwd
log_must $RM $TESTDIR/pax.${TESTCASE_ID}

# we should have no xattr here
log_must $PAX -r -f $TESTDIR/xattr.pax
log_mustnot eval "$RUNAT $TESTDIR/pax.${TESTCASE_ID} $CAT passwd > /dev/null 2>&1"
log_must $RM $TESTDIR/pax.${TESTCASE_ID} $TESTDIR/noxattr.pax $TESTDIR/xattr.pax


log_note "Checking tar"
log_must $TOUCH $TESTDIR/tar.${TESTCASE_ID}
create_xattr $TESTDIR/tar.${TESTCASE_ID} passwd /etc/passwd
log_must $TAR cf $TESTDIR/noxattr.tar $TESTDIR/tar.${TESTCASE_ID}
log_must $TAR c@f $TESTDIR/xattr.tar $TESTDIR/tar.${TESTCASE_ID}
log_must $RM $TESTDIR/tar.${TESTCASE_ID}

# we should have no xattr here
log_must $TAR xf $TESTDIR/xattr.tar
log_mustnot eval "$RUNAT $TESTDIR/tar.${TESTCASE_ID} $CAT passwd > /dev/null 2>&1"
log_must $RM $TESTDIR/tar.${TESTCASE_ID}

# we should have an xattr here
log_must $TAR x@f $TESTDIR/xattr.tar
verify_xattr $TESTDIR/tar.${TESTCASE_ID} passwd /etc/passwd
log_must $RM $TESTDIR/tar.${TESTCASE_ID}

# we should have no xattr here
log_must $TAR xf $TESTDIR/noxattr.tar
log_mustnot eval "$RUNAT $TESTDIR/tar.${TESTCASE_ID} $CAT passwd > /dev/null 2>&1"
log_must $RM $TESTDIR/tar.${TESTCASE_ID}

# we should have no xattr here
log_must $TAR x@f $TESTDIR/noxattr.tar
log_mustnot eval "$RUNAT $TESTDIR/tar.${TESTCASE_ID} $CAT passwd > /dev/null 2>&1"
log_must $RM $TESTDIR/tar.${TESTCASE_ID} $TESTDIR/noxattr.tar $TESTDIR/xattr.tar
