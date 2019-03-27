#!/usr/local/bin/ksh93
#
# Copyright (c) 2017 Spectra Logic Corporation
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions, and the following disclaimer,
#    without modification.
# 2. Redistributions in binary form must reproduce at minimum a disclaimer       
#    substantially similar to the "NO WARRANTY" disclaimer below
#    ("Disclaimer") and any redistribution must be conditioned upon
#    including a substantially similar Disclaimer requirement for further
#    binary redistribution.
#
# NO WARRANTY
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL     
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
# IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGES.
#
# $FreeBSD$

. $STF_SUITE/include/libtest.kshlib
set_disks

# A destroyed pool cannot be imported, even if an out-of-date non-destroyed
# label is present
#
# This situation arose when a user activated a spare, removed the spare disk,
# destroyed the pool, reinserted the spare disk, and then tried to import the
# pool.  Since the pool was destroyed, nothing should've happened.  But the
# spare disk had a non-destroyed label, so zpool tried to import it.  A panic
# ensued.
#
# More generally, this situation can happen any time the following things happen:
# 1) A disk gets removed with its label intact
# 2) The pool's configuration changes
# 3) The pool gets destroyed
# 4) Somebody tries to import the pool

log_must $ZPOOL create -f $TESTPOOL mirror ${DISK0} ${DISK1}

# Offline a disk so it's label won't get updated by the upcoming destroy
log_must $ZPOOL offline $TESTPOOL ${DISK0}

# Now change the pool's configuration, so DISK0's label will be out-of-date
log_must $ZPOOL attach $TESTPOOL ${DISK1} ${DISK2}

# Destroy the pool, so DISK1's and DISK2's labels will be in the destroyed
# state, leaving DISK0's label as the most recent non-destroyed label
log_must $ZPOOL destroy $TESTPOOL

# Now try to import the pool.  It should fail.
log_mustnot $ZPOOL import $TESTPOOL

log_pass
