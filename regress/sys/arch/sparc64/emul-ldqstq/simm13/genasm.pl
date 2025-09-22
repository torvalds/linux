#!/usr/bin/perl
#	$OpenBSD: genasm.pl,v 1.3 2024/08/06 05:39:48 claudio Exp $
#
# Copyright (c) 2003 Jason L. Wright (jason@thought.net)
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
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
# ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#

print "#include <machine/asm.h>\n";
print "#define _LOCORE\n";
print "#include <machine/ctlreg.h>\n\n";

for ($i = -4096; $i <= 4095; $i++) {
	if ($i < 0) {
		$name = -$i;
		$name = "_$name";
	} else {
		$name = $i;
	}
	print "ENTRY(simm13_ldq_$name)\n";
	print "	sub	%o0, $i, %o0\n";
	print "	retl\n";
	print "	 ldq [%o0 + $i], %f0\n\n";

	print "ENTRY(simm13_stq_$name)\n";
	print "	sub	%o0, $i, %o0\n";
	print "	retl\n";
	print "	 stq %f0, [%o0 + $i]\n\n";
}
