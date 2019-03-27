#
# Copyright (c) 2001-2003
# Fraunhofer Institute for Open Communication Systems (FhG Fokus).
# 	All rights reserved.
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
# Author: Hartmut Brandt <harti@freebsd.org>
#
# $Begemot: libunimsg/netnatm/msg/parseie.awk,v 1.3 2003/09/19 11:58:15 hbb Exp $
#
# Parse the IE definition file
#
match($0, "Begemot:")!=0 {
	gsub("^[^$]*", "")
	gsub("[^$]*$", "")
	id = $0
	next
}

/^#/ {
	next
}
NF == 0 {
	next
}

BEGIN {
	iecnt = 0
	id = " * ???"
	begin()
}

END {
	end()
}

#
# Syntax is:
# element <name> <code> <coding> [<maxlen> [<options>*]]
#
$1=="element" {
	if(iecnt == 0) first_element()
	if(NF < 4) {
		error("Bad number of args: " $0)
	}
	ie = $2
	file = $2
	number = parse_hex($3)
	coding = $4
	if(coding == "itu") {
		ncoding = 0
	} else if(coding == "net") {
		ncoding = 3
	} else {
		error("bad coding " coding)
	}
	if(NF == 4) {
		element_default()
		file=""
	} else {
		len = $5
		parse_options()
		element()
	}
	ies[iecnt] = ie
	codings[iecnt] = coding
	files[iecnt] = file
	iecnt++
	next
}

{
	error("Bad line: " $0)
}

function parse_options() {
	access = 0
	cond = ""
	for(i = 6; i <= NF; i++) {
		if($i == "access") {
			access = 1
		} else if($i == "-") {
		} else if(index($i, "file=") == 1) {
			file=substr($i, 6)
		} else {
			if(cond != "") {
				error("Too many conditions: "$0)
			}
			cond = $i
		}
	}
}

function parse_hex(str,		n)
{
	n = 0
	if(substr(str,1,2) != "0x") {
		error("bad hex number" str)
	}
	for(i = 3; i <= length(str); i++) {
		c = substr(str,i,1)
		if(match(c,"[0-9]") != 0) {
			n = 16 * n + c
		} else if(match(c,"[a-f]")) {
			if(c == "a") n = 16 * n + 10
			if(c == "b") n = 16 * n + 11
			if(c == "c") n = 16 * n + 12
			if(c == "d") n = 16 * n + 13
			if(c == "e") n = 16 * n + 14
			if(c == "f") n = 16 * n + 15
		} else if(match(c,"[A-F]")) {
			if(c == "A") n = 16 * n + 10
			if(c == "B") n = 16 * n + 11
			if(c == "C") n = 16 * n + 12
			if(c == "D") n = 16 * n + 13
			if(c == "E") n = 16 * n + 14
			if(c == "F") n = 16 * n + 15
		} else {
			error("bad hex digit '" c "'")
		}
	}
	return n
}

# function error(str)
# {
# 	print "error:" str >"/dev/stderr"
# 	exit 1
# }

