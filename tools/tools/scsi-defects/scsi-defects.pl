#!/usr/bin/perl
# 
# Copyright (C) 1997
# 	Peter Dufault, Joerg Wunsch.  All rights reserved.
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
# THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#
# Read and decode a SCSI disk's primary or grown defect list.
#

sub usage
{
    die "usage: scsi-defects raw-device-name [Glist|Plist]\n";
}


#
# Main
#

&usage if $#ARGV < 0 || $#ARGV > 1;

$ENV{'PATH'} = "/bin:/usr/bin:/sbin:/usr/sbin";

$dev = $ARGV[0];

# generic device name given?
if ($dev =~ /^[so]d\d+$/) { $dev = "/dev/r${dev}.ctl"; }

#
# Select what you want to read.  PList include the primary defect list
# from the factory.  GList is grown defects only.
#
if ($#ARGV > 0) {
    if ($ARGV[1] =~ /^[Gg]/) { $glist = 1; $plist = 0; }
    elsif ($ARGV[1] =~ /^[Pp]/) { $glist = 0; $plist = 1; }
    else { &usage; }
} else {
    $glist = 1; $plist = 0;
}

open(PIPE, "scsi -f $dev " .
     "-c '{ Op code} 37 0 0:3 v:1 v:1 5:3 0 0 0 0 4:i2 0' $plist $glist " .
     "-i 4 '{ stuff } *i2 { Defect list length } i2' |") ||
    die "Cannot pipe to scsi(8)\n";
chop($amnt = <PIPE>);
close(PIPE);

if ($amnt == 0) {
    print "There are no defects (in this list).\n";
    exit 0;
}

print "There are " . $amnt / 8 . " defects in this list.\n";

$amnt += 4;

open(PIPE, "scsi -f $dev " .
     "-c '{ Op code} 37 0 0:3 v:1 v:1 5:3 0 0 0 0 v:i2 0' $plist $glist " .
     "$amnt -i $amnt - |") ||
    die "Cannot pipe to scsi(8)\n";

read(PIPE, $buf, 4);		# defect list header

print "cylinder head  sector\n";

while(read(PIPE, $buf, 8)) {
    ($cylhi, $cyllo, $head, $sec) = unpack("CnCN", $buf);
    printf "%8u %4u  %6u\n", $cylhi*65536+$cyllo, $head, $sec;
}
close(PIPE);
