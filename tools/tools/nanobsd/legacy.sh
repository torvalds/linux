#!/bin/sh
#
# Copyright (c) 2005 Poul-Henning Kamp.
# Copyright (c) 2016 M. Warner Losh.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

# Media geometry, only relevant if bios doesn't understand LBA.
[ -n "$NANO_SECTS" ] || NANO_SECTS=63
[ -n "$NANO_HEADS" ] || NANO_HEADS=16

# Functions and variable definitions used by the legacy nanobsd
# image building system.

create_diskimage ( ) (
	pprint 2 "build diskimage"
	pprint 3 "log: ${NANO_LOG}/_.di"

	(
	echo $NANO_MEDIASIZE $NANO_IMAGES \
		$NANO_SECTS $NANO_HEADS \
		$NANO_CODESIZE $NANO_CONFSIZE $NANO_DATASIZE |
	awk '
	{
		printf "# %s\n", $0

		# size of cylinder in sectors
		cs = $3 * $4

		# number of full cylinders on media
		cyl = int ($1 / cs)

		# output fdisk geometry spec, truncate cyls to 1023
		if (cyl <= 1023)
			print "g c" cyl " h" $4 " s" $3
		else
			print "g c" 1023 " h" $4 " s" $3

		if ($7 > 0) {
			# size of data partition in full cylinders
			dsl = int (($7 + cs - 1) / cs)
		} else {
			dsl = 0;
		}

		# size of config partition in full cylinders
		csl = int (($6 + cs - 1) / cs)

		if ($5 == 0) {
			# size of image partition(s) in full cylinders
			isl = int ((cyl - dsl - csl) / $2)
		} else {
			isl = int (($5 + cs - 1) / cs)
		}

		# First image partition start at second track
		print "p 1 165 " $3, isl * cs - $3
		c = isl * cs;

		# Second image partition (if any) also starts offset one
		# track to keep them identical.
		if ($2 > 1) {
			print "p 2 165 " $3 + c, isl * cs - $3
			c += isl * cs;
		}

		# Config partition starts at cylinder boundary.
		print "p 3 165 " c, csl * cs
		c += csl * cs

		# Data partition (if any) starts at cylinder boundary.
		if ($7 > 0) {
			print "p 4 165 " c, dsl * cs
		} else if ($7 < 0 && $1 > c) {
			print "p 4 165 " c, $1 - c
		} else if ($1 < c) {
			print "Disk space overcommitted by", \
			    c - $1, "sectors" > "/dev/stderr"
			exit 2
		}

		# Force slice 1 to be marked active. This is necessary
		# for booting the image from a USB device to work.
		print "a 1"
	}
	' > ${NANO_LOG}/_.fdisk

	IMG=${NANO_DISKIMGDIR}/${NANO_IMGNAME}
	MNT=${NANO_OBJ}/_.mnt
	mkdir -p ${MNT}

	if [ "${NANO_MD_BACKING}" = "swap" ] ; then
		MD=`mdconfig -a -t swap -s ${NANO_MEDIASIZE} -x ${NANO_SECTS} \
			-y ${NANO_HEADS}`
	else
		echo "Creating md backing file..."
		rm -f ${IMG}
		dd if=/dev/zero of=${IMG} seek=${NANO_MEDIASIZE} count=0
		MD=`mdconfig -a -t vnode -f ${IMG} -x ${NANO_SECTS} \
			-y ${NANO_HEADS}`
	fi

	trap "echo 'Running exit trap code' ; df -i ${MNT} ; nano_umount ${MNT} || true ; mdconfig -d -u $MD" 1 2 15 EXIT

	fdisk -i -f ${NANO_LOG}/_.fdisk ${MD}
	fdisk ${MD}
	# XXX: params
	# XXX: pick up cached boot* files, they may not be in image anymore.
	if [ -f ${NANO_WORLDDIR}/${NANO_BOOTLOADER} ]; then
		boot0cfg -B -b ${NANO_WORLDDIR}/${NANO_BOOTLOADER} ${NANO_BOOT0CFG} ${MD}
	fi
	if [ -f ${NANO_WORLDDIR}/boot/boot ]; then
		bsdlabel -w -B -b ${NANO_WORLDDIR}/boot/boot ${MD}${NANO_SLICE_ROOT}
	else
		bsdlabel -w ${MD}${NANO_SLICE_ROOT}
	fi
	bsdlabel ${MD}${NANO_SLICE_ROOT}

	# Create first image
	populate_slice /dev/${MD}${NANO_ROOT} ${NANO_WORLDDIR} ${MNT} "${NANO_ROOT}"
	mount /dev/${MD}${NANO_ROOT} ${MNT}
	echo "Generating mtree..."
	( cd "${MNT}" && mtree -c ) > ${NANO_LOG}/_.mtree
	( cd "${MNT}" && du -k ) > ${NANO_LOG}/_.du
	nano_umount "${MNT}"

	if [ $NANO_IMAGES -gt 1 -a $NANO_INIT_IMG2 -gt 0 ] ; then
		# Duplicate to second image (if present)
		echo "Duplicating to second image..."
		dd conv=sparse if=/dev/${MD}${NANO_SLICE_ROOT} of=/dev/${MD}${NANO_SLICE_ALTROOT} bs=64k
		mount /dev/${MD}${NANO_ALTROOT} ${MNT}
		for f in ${MNT}/etc/fstab ${MNT}/conf/base/etc/fstab
		do
			sed -i "" "s=${NANO_DRIVE}${NANO_SLICE_ROOT}=${NANO_DRIVE}${NANO_SLICE_ALTROOT}=g" $f
		done
		nano_umount ${MNT}
		# Override the label from the first partition so we
		# don't confuse glabel with duplicates.
		if [ -n "${NANO_LABEL}" ]; then
			tunefs -L ${NANO_LABEL}"${NANO_ALTROOT}" /dev/${MD}${NANO_ALTROOT}
		fi
	fi

	# Create Config slice
	populate_cfg_slice /dev/${MD}${NANO_SLICE_CFG} "${NANO_CFGDIR}" ${MNT} "${NANO_SLICE_CFG}"

	# Create Data slice, if any.
	if [ -n "$NANO_SLICE_DATA" -a "$NANO_SLICE_CFG" = "$NANO_SLICE_DATA" -a \
	   "$NANO_DATASIZE" -ne 0 ]; then
		pprint 2 "NANO_SLICE_DATA is the same as NANO_SLICE_CFG, fix."
		exit 2
	fi
	if [ $NANO_DATASIZE -ne 0 -a -n "$NANO_SLICE_DATA" ] ; then
		populate_data_slice /dev/${MD}${NANO_SLICE_DATA} "${NANO_DATADIR}" ${MNT} "${NANO_SLICE_DATA}"
	fi

	if [ "${NANO_MD_BACKING}" = "swap" ] ; then
		if [ ${NANO_IMAGE_MBRONLY} ]; then
			echo "Writing out _.disk.mbr..."
			dd if=/dev/${MD} of=${NANO_DISKIMGDIR}/_.disk.mbr bs=512 count=1
		else
			echo "Writing out ${NANO_IMGNAME}..."
			dd if=/dev/${MD} of=${IMG} bs=64k
		fi

		echo "Writing out ${NANO_IMGNAME}..."
		dd conv=sparse if=/dev/${MD} of=${IMG} bs=64k
	fi

	if ${do_copyout_partition} ; then
		echo "Writing out ${NANO_IMG1NAME}..."
		dd conv=sparse if=/dev/${MD}${NANO_SLICE_ROOT} \
		   of=${NANO_DISKIMGDIR}/${NANO_IMG1NAME} bs=64k
	fi
	mdconfig -d -u $MD

	trap - 1 2 15 EXIT

	) > ${NANO_LOG}/_.di 2>&1
)
