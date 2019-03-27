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
# $Begemot: libunimsg/netnatm/msg/genmsgh.awk,v 1.4 2004/07/08 08:22:04 brandt Exp $
#
# Generate message header
#
function begin() {
}

function first_entry() {
	print "/* This file was created automatically"
	print " * Source file: " id
	print " */"
	print ""
	print "#ifndef _NETNATM_MSG_UNI_MSG_H_"
	print "#define _NETNATM_MSG_UNI_MSG_H_"
}

function end() {
	print ""
	print "union uni_msgall {"
	print "\tstruct uni_msghdr\thdr;"
	for(i = 0; i < mcnt; i++) {
		m = messages[i]
		if(msgcond[i] == "") {
			print "\tstruct uni_" m "\t" m ";"
		} else {
			print "\tstruct uni_" m "\t" m ";\t/* " msgcond[i] " */"
		}
	}
	print "};"
	print ""
	print "#endif"
}

function start_message() {
}

function end_message() {
	print ""
	print "struct uni_" msg " {"
	print "\tstruct uni_msghdr\thdr;"
	for(i = 0; i < cnt; i++) {
		if(ierep[i]) {
			print "\tstruct uni_ie_repeat\t" iename[i] "_repeat;"
		}
		if(ienum[i] != "-") {
			print "\tstruct uni_ie_" iename[i] "\t" iename[i] "[" ienum[i] "];"
		} else {
			print "\tstruct uni_ie_" iename[i] "\t" iename[i] ";"
		}
	}
	print "};"
}
