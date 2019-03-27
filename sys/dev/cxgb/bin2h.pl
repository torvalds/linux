#!/usr/bin/perl -w 

#$FreeBSD$

if ($#ARGV != 1) {
  print "bin2h.pl <firmware> <headername>\n";
  exit 1;
} 

my $success = open INPUT, "$ARGV[0]";
unless ($success) {
  print "failed to open input\n";
  exit 1;
}
$success = open OUTPUT, ">$ARGV[1].h";
unless ($success) {
  print "failed to open output\n";
  exit 1;
}


my $license = <<END;
/**************************************************************************

SPDX-License-Identifier: BSD-2-Clause-FreeBSD

Copyright (c) 2007-2009, Chelsio Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

 2. Neither the name of the Chelsio Corporation nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN22
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.


\$FreeBSD\$

***************************************************************************/
END
print OUTPUT "$license\n";


my $binary;

my ($dev,$ino,$mode,$nlink,$uid,$gid,$rdev,$size,
 $atime,$mtime,$ctime,$blksize,$blocks)
  = stat($ARGV[0]);


print OUTPUT "#define U (unsigned char)\n\n"; 

print OUTPUT "static unsigned int $ARGV[1]_length = $size;\n";
print OUTPUT "static unsigned char $ARGV[1]" . "[$size]" . " = {\n";

for (my $i = 0; $i < $size; $i += 4) {
  my $number_read = read(INPUT, $binary, 4);
  my ($a, $b, $c, $d) = unpack("C C C C", $binary);
  $buf = sprintf("\tU 0x%02X, U 0x%02X, U 0x%02X, U 0x%02X, \n", $a, $b, $c, $d);
  print OUTPUT $buf;
}
print OUTPUT "};\n";



