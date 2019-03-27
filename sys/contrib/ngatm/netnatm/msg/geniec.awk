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
# $Begemot: libunimsg/netnatm/msg/geniec.awk,v 1.4 2003/10/10 14:50:05 hbb Exp $
#
# Generate table for IE parsing.
#
# This function is called before the first line
#
function begin() {
	for(i = 0; i < 256; i++) {
		for(j = 0; j < 4; j++) {
			decl[i,j] = ""
		}
	}
}

#
# This function is called after the last line.
#
function end() {
	print ""
	print "const struct iedecl *uni_ietable[256][4] = {"
	for(i = 0; i < 256; i++) {
		printf "\t{"
		for(j = 0; j < 4; j++) {
			if(decl[i,j] == "") {
				printf " NULL,"
			} else {
				printf " &%s,", decl[i,j]
			}
		}
		printf " }, /* 0x%02x */\n", i
	}
	print "};"
}

#
# This function is called just when the first information element was found
#
function first_element() {
	print "/* This file was created automatically"
	print " * Source file: " id
	print " */"
	print ""
}

#
# This is called, when the information element is defaulted (there is
# only the name and the coding scheme
#
function element_default() {
	print ""
	print "static const struct iedecl decl_" coding "_" ie " = {"
	print "\tUNIFL_DEFAULT,"
	print "\t0,"
	print "\t(uni_print_f)NULL,"
	print "\t(uni_check_f)NULL,"
	print "\t(uni_encode_f)NULL,"
	print "\t(uni_decode_f)NULL"
	print "};"
	decl[number,ncoding] = "decl_" coding "_" ie
}

#
# This is found for a real, non-default IE
#
function element() {
	print ""
	print "static void uni_ie_print_" coding "_" ie "(struct uni_ie_" ie " *, struct unicx *);"
	print "static int uni_ie_check_" coding "_" ie "(struct uni_ie_" ie " *, struct unicx *);"
	print "static int uni_ie_encode_" coding "_" ie "(struct uni_msg *, struct uni_ie_" ie " *, struct unicx *);"
	print "static int uni_ie_decode_" coding "_" ie "(struct uni_ie_" ie " *, struct uni_msg *, u_int, struct unicx *);"
	print ""
	print "static struct iedecl decl_" coding "_" ie " = {"
	if(access)	print "\tUNIFL_ACCESS,"
	else		print "\t0,"
	print "\t" len ","
	print "\t(uni_print_f)uni_ie_print_" coding "_" ie ","
	print "\t(uni_check_f)uni_ie_check_" coding "_" ie ","
	print "\t(uni_encode_f)uni_ie_encode_" coding "_" ie ","
	print "\t(uni_decode_f)uni_ie_decode_" coding "_" ie ""
	print "};"
	decl[number,ncoding] = "decl_" coding "_" ie
}
