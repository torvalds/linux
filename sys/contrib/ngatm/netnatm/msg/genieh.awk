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
# $Begemot: libunimsg/netnatm/msg/genieh.awk,v 1.4 2004/07/08 08:22:03 brandt Exp $
#
# Generate IE header file
#
function begin() {
}

function first_element() {
	print "/* This file was created automatically"
	print " * Source file: " id
	print " */"
	print ""
	print "#ifndef _NETNATM_MSG_UNI_IE_H_"
	print "#define _NETNATM_MSG_UNI_IE_H_"
	print ""
	print "union uni_ieall {"
	print "	struct uni_iehdr h;"
}

function end() {
	print "};"
	print ""
	print "#endif"
}

function element_default() {
}

function element() {
	if(ie in u) return
	u[ie] = 1
	print "	struct uni_ie_" ie " " ie ";"
}
