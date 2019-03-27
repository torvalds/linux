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
# $Begemot: libunimsg/netnatm/msg/parsemsg.awk,v 1.3 2003/09/19 11:58:15 hbb Exp $
#
# Parse the message definition file
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
	state=0
	id = " * ???"
	mcnt=0
	begin()
}
END {
	end()
}

state==0 && $1=="start" {
	if(NF < 3) error("bad number of fields in message start "$0)
	state = 1
	msg = $2
	code = parse_hex($3)
	messages[mcnt] = msg
	msgcond[mcnt] = $4
	msgrep = 0
	msgrepie = 0
	cnt = 0
	if(mcnt == 0) first_entry()
	start_message()
	next
}

state==1 && $1=="end" {
	state=0
	mcnt++
	end_message()
	next
}
state==1 {
	iename[cnt]=$1
	if($2 == "") $2="-"
	if(match($2, "[A-Za-z][A-Za-z0-9_]*/R") == 1) {
		ienum[cnt]=substr($2, 1, length($2)-2)
		ierep[cnt]=1
		msgrepie=1
	} else {
		ierep[cnt]=0
		ienum[cnt]=$2
	}
	if(ienum[cnt] != "-") msgrep = 1
	if($3 == "" || $3 == "-") {
		$3 = "1"
	} else {
		gsub("[a-zA-Z][a-zA-Z0-9]*", "cx->&", $3)
	}
	iecond[cnt] = $3
	cnt++
	next
}

{
	error("bad line: "$0)
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

function error(str)
{
	print "error:" str >"/dev/stderr"
	exit 1
}
