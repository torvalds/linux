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
# $Begemot: libunimsg/netnatm/sig/genmsgcpyc.awk,v 1.4 2004/07/08 08:22:18 brandt Exp $
#
# Generate copy functions for messages
#
function begin() {
}

function first_entry() {
	print "/* This file was created automatically"
	print " * Source file: " id
	print " * $FreeBSD$"
	print " */"
	print ""
	print "#include <netnatm/msg/unistruct.h>"
	print "#include <netnatm/sig/unimsgcpy.h>"
}

function end() {
}

function start_message() {
}

function end_message() {
	print ""
	print "void"
	print "copy_msg_" msg "(struct uni_" msg " *src, struct uni_" msg " *dst)"
	print "{"
	for(i = 0; i < cnt; i++) {
		if(ienum[i] != "-") {
			print "\tu_int s, d;"
			print ""
			break
		}
	}
	for(i = 0; i < cnt; i++) {
		ie = iename[i]
		if(ierep[i]) {
			print "\tif(IE_ISGOOD(src->" ie "_repeat))"
			print "\t\tdst->" ie "_repeat = src->" ie "_repeat;"
		}
		if(ienum[i] != "-") {
			print "\tfor(s = d = 0; s < "ienum[i]"; s++)"
			print "\t\tif(IE_ISGOOD(src->"ie"[s]))"
			print "\t\t\tdst->"ie"[d++] = src->"ie"[s];"
		} else {
			print "\tif(IE_ISGOOD(src->"ie"))"
			print "\t\tdst->"ie" = src->"ie";"
		}
	}
	print "}"
}
