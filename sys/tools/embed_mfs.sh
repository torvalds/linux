#!/bin/sh
#
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (C) 2008 The FreeBSD Project. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in the
#   documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$ 
#
# Embed an MFS image into the kernel body or the loader body (expects space
# reserved via MD_ROOT_SIZE (kernel) or MD_IMAGE_SIZE (loader))
#
# $1: kernel or loader filename
# $2: MFS image filename
#

if [ $# -ne 2 ]; then
	echo "usage: $(basename $0) target mfs_image"
	exit 0
fi
if [ ! -w "$1" ]; then
	echo $1 not writable
	exit 1
fi

mfs_size=`stat -f '%z' $2 2> /dev/null`
# If we can't determine MFS image size - bail.
if [ -z ${mfs_size} ]; then
	echo "Can't determine MFS image size"
	exit 1
fi

err_no_mfs="Can't locate mfs section within "

if file -b $1 | grep -q '^ELF ..-bit .SB executable'; then

	sec_info=`elfdump -c $1 2> /dev/null | grep -A 5 -E "sh_name: oldmfs$"`
	# If we can't find the mfs section within the given kernel - bail.
	if [ -z "${sec_info}" ]; then
		echo "${err_no_mfs} $1"
		exit 1
	fi

	sec_size=`echo "${sec_info}" | awk '/sh_size/ {print $2}' 2>/dev/null`
	sec_start=`echo "${sec_info}" | \
	    awk '/sh_offset/ {print $2}' 2>/dev/null`

else

	#try to find start byte of MFS start flag otherwise - bail.
	sec_start=`strings -at d $1 | grep "MFS Filesystem goes here"` || \
	    { echo "${err_no_mfs} $1"; exit 1; }
	sec_start=`echo ${sec_start} | awk '{print $1}'`

	#try to find start byte of MFS end flag otherwise - bail.
	sec_end=`strings -at d $1 | \
	    grep "MFS Filesystem had better STOP here"` || \
	    { echo "${err_no_mfs} $1"; exit 1; }
	sec_end=`echo ${sec_end} | awk '{print $1}'`

	#calculate MFS section size
	sec_size=`expr ${sec_end} - ${sec_start}`

fi

# If the mfs section size is smaller than the mfs image - bail.
if [ ${sec_size} -lt ${mfs_size} ]; then
	echo "MFS image too large"
	exit 1
fi

# Dump the mfs image into the mfs section
dd if=$2 ibs=8192 of=$1 obs=${sec_start} oseek=1 conv=notrunc 2> /dev/null && \
    echo "MFS image embedded into $1" && exit 0
