#!/usr/bin/perl

###############################################################################
#
# hfsutils - tools for reading and writing Macintosh HFS volumes
# Copyright (C) 1996, 1997 Robert Leslie
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#
###############################################################################

die "Usage: $0 device-path\n" unless (@ARGV == 1);

($disk) = @ARGV;

format STDOUT_TOP =
 # Partition Type                HFS Volume Name                Start    Length
-------------------------------------------------------------------------------
.

format STDOUT =
@# @<<<<<<<<<<<<<<<<<<<<<<<<<<<< @<<<<<<<<<<<<<<<<<<<<<<<<<< @####### @########
$bnum, $pmParType, $drVN, $pmPyPartStart, $pmPartBlkCnt
.

open(DISK, $disk) || die "$disk: $!\n";

$bnum = 1;

do {
    seek(DISK, 512 * $bnum, 0) || die "seek: $!\n";
    read(DISK, $block, 512) || die "read: $!\n";

    ($pmSig, $pmMapBlkCnt, $pmPyPartStart, $pmPartBlkCnt, $pmParType) =
	(unpack('n2 N3 A32 A32 N10 A16', $block))[0, 2..4, 6];

    die "$disk: unsupported partition map\n" if ($pmSig == 0x5453);
    die "$disk: no partition map\n" unless ($pmSig == 0x504d);

    if ($pmParType eq 'Apple_HFS') {
	seek(DISK, 512 * ($pmPyPartStart + 2), 0) || die "seek: $!\n";
	read(DISK, $block, 512) || die "read: $!\n";

	($len, $drVN) = (unpack('n N2 n5 N2 n N n c A27', $block))[13, 14];
	$drVN = substr($drVN, 0, $len);
    } else {
	$drVN = '';
    }

    write;
} while ($bnum++ < $pmMapBlkCnt);

close(DISK);
