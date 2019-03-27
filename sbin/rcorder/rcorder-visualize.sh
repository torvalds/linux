#!/bin/sh
# $NetBSD: rcorder-visualize.sh,v 1.5 2009/08/09 17:08:53 apb Exp $
#
# Copyright (c) 2009 by Joerg Sonnenberger
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
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# $FreeBSD$
#
# Simple script to show the dependency graph for rc scripts.
# Output is in the dot(1) language and can be rendered using
#	sh rcorder-visualize | dot -T svg -o rcorder.svg
# dot(1) can be found in graphics/graphviz in pkgsrc.

rc_files=${*:-/etc/rc.d/*}

{
echo ' digraph {'
for f in $rc_files; do
< $f awk '
/# PROVIDE: /	{ provide = $3 }
/# REQUIRE: /	{ for (i = 3; i <= NF; i++) requires[$i] = $i }
/# BEFORE: /	{ for (i = 3; i <= NF; i++) befores[$i] = $i }

END {
	print "    \"" provide "\";"
	for (x in requires) print "    \"" provide "\"->\"" x "\";"
	for (x in befores) print "    \"" x "\"->\"" provide "\";"
}
'
done
echo '}'
}
