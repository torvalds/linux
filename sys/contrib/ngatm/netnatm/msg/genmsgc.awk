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
# $Begemot: libunimsg/netnatm/msg/genmsgc.awk,v 1.6 2004/07/08 08:22:04 brandt Exp $
#
# Generate message functions.
#
function begin() {
}

function first_entry() {
	print "/* This file was created automatically"
	print " * Source file: " id
	print " */"
	print ""
	print "#include <sys/types.h>"
	print "#include <sys/param.h>"
	print ""
	print "#ifdef _KERNEL"
	print "#include <sys/libkern.h>"
	print "#else"
	print "#include <string.h>"
	print "#endif"
	print "#include <netnatm/unimsg.h>"
	print "#include <netnatm/msg/unistruct.h>"
	print "#include <netnatm/msg/unimsglib.h>"
	print "#include <netnatm/msg/priv.h>"
	print "#include <netnatm/msg/privmsg.c>"
}

function end() {
	print ""
	print "const struct msgdecl *uni_msgtable[256] = {"
	for(i = 0; i < 256; i++) {
		if(decl[i] == "") {
			printf "\t&decl_unknown,"
		} else {
			printf "\t&%s,", decl[i]
		}
		printf "\t/* 0x%02x */\n", i
	}
	print "};"
}

function start_message() {
}

function end_message() {
		gen_print()
		gen_check()
		gen_encode()
		gen_decode()
		gen_reg()
}

function gen_print() {
	print ""
	print "static void"
	print "print_" msg "(struct uni_" msg " *msg, struct unicx *cx)"
	print "{"
	if(msgrep) {
		print "\tu_int i;"
		print ""
	}
	for(i = 0; i < cnt; i++) {
		ie = iename[i]
		uie = toupper(iename[i])
		if(ierep[i]) {
			print "\tif(msg->" ie "_repeat.h.present & UNI_IE_PRESENT)"
			print "\t\tuni_print_ie_internal(UNI_IE_REPEAT, (union uni_ieall *)&msg->" ie "_repeat, cx);"
		}
		if(ienum[i] == "-") {
			print "\tif(msg->" ie ".h.present & UNI_IE_PRESENT)"
			print "\t\tuni_print_ie_internal(UNI_IE_" uie ", (union uni_ieall *)&msg->" ie ", cx);"
		} else {
			print "\tfor(i = 0; i < " ienum[i] "; i++)"
			print "\t\tif(msg->" ie "[i].h.present & UNI_IE_PRESENT)"
			print "\t\t\tuni_print_ie_internal(UNI_IE_" uie ", (union uni_ieall *)&msg->" ie "[i], cx);"
		}
	}
	print "}"
}

function gen_check() {
	print ""
	print "static int"
	print "check_" msg "(struct uni_" msg " *m, struct unicx *cx)"
	print "{"
	print "\tint ret = 0;"
	if(msgrep) {
		print "\tu_int i;"
	}
	print ""
	for(i = 0; i < cnt; i++) {
		ie = iename[i]
		if(ierep[i]) {
			if(iecond[i] == "1") {
				print "\tret |= uni_check_ie(UNI_IE_REPEAT, (union uni_ieall *)&m->" ie "_repeat, cx);"
			} else {
				print "\tif(!(" iecond[i] "))"
				print "\t\tret |= IE_ISPRESENT(m->" ie "_repeat);"
				print "\telse"
				print "\t\tret |= uni_check_ie(UNI_IE_REPEAT, (union uni_ieall *)&m->" ie "_repeat, cx);"
			}
		}
		if(ienum[i] == "-") {
			if(iecond[i] == "1") {
				print "\tret |= uni_check_ie(UNI_IE_" toupper(ie) ", (union uni_ieall *)&m->" ie ", cx);"
			} else {
				print "\tif(!(" iecond[i] "))"
				print "\t\tret |= IE_ISPRESENT(m->" ie ");"
				print "\telse"
				print "\t\tret |= uni_check_ie(UNI_IE_" toupper(ie) ", (union uni_ieall *)&m->" ie ", cx);"
			}
		} else {
	    		print "\tfor(i = 0; i < " ienum[i]" ; i++) {"
			if(iecond[i] == "1") {
				print "\t\tret |= uni_check_ie(UNI_IE_" toupper(ie) ", (union uni_ieall *)&m->" ie "[i], cx);"
			} else {
				print "\t\tif(!(" iecond[i] "))"
				print "\t\t\tret |= IE_ISPRESENT(m->" ie "[i]);"
				print "\t\telse"
				print "\t\t\tret |= uni_check_ie(UNI_IE_" toupper(ie) ", (union uni_ieall *)&m->" ie "[i], cx);"
			}
			print "\t}"
		}
	}
	print ""
	print "\treturn ret;"
	print "}"
}

function gen_encode() {
	print ""
	print "static int"
	print "encode_" msg "(struct uni_msg *msg, struct uni_" msg " *p, struct unicx *cx)"
	print "{"
	print "\tu_int mlen;"
	if(msgrep) {
		print "\tu_int i;"
	}
	print ""
	print "\tif(uni_encode_msg_hdr(msg, &p->hdr, UNI_" toupper(msg) ", cx, &mlen))"
	print "\t\treturn (-2);"
	print ""
	for(i = 0; i < cnt; i++) {
		ie = iename[i]
		if(ierep[i]) {
			print "\tif((p->" ie "_repeat.h.present & UNI_IE_PRESENT) &&"
	   		print "\t   uni_encode_ie(UNI_IE_" toupper(ie) ", msg, (union uni_ieall *)&p->" ie "_repeat, cx))"
			print "\t\treturn (0x10000000 + UNI_IE_" toupper(ie) ");"
		}
		if(ienum[i] == "-") {
			print "\tif((p->" ie ".h.present & UNI_IE_PRESENT) &&"
	   		print "\t   uni_encode_ie(UNI_IE_" toupper(ie) ", msg, (union uni_ieall *)&p->" ie ", cx))"
			print "\t\treturn (UNI_IE_" toupper(ie) ");"
		} else {
			print "\tfor(i = 0; i < " ienum[i] "; i++)"
			print "\t\tif((p->" ie "[i].h.present & UNI_IE_PRESENT) &&"
	   		print "\t\t   uni_encode_ie(UNI_IE_" toupper(ie) ", msg, (union uni_ieall *)&p->" ie "[i], cx))"
			print "\t\treturn ((i << 16) + UNI_IE_" toupper(ie) ");"
		}
	}
	print ""
	print "\tmsg->b_buf[mlen+0] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 8;"
	print "\tmsg->b_buf[mlen+1] = ((msg->b_wptr-msg->b_rptr)-mlen-2) >> 0;"
 	print ""
	print "\treturn (0);"
	print "}"
}

function gen_decode() {
	print ""
	print "static int"
	print "decode_" msg "(struct uni_" msg " *out, struct uni_msg *msg,"
	print "    enum uni_ietype ie, struct uni_iehdr *hdr, u_int ielen,"
	print "    struct unicx *cx)"
	print "{"
	if (msgrep) {
		print "	u_int i;"
		print ""
	}
	print "	switch (ie) {"

	rep=0
	for (i = 0; i < cnt; i++) {
		ie = iename[i]
		print ""
	  	print "	  case UNI_IE_" toupper(ie) ":"
		if (iecond[i] != "1") {
			print "		if (!(" iecond[i] "))"
			print "			return (DEC_ILL);"
		}
		if (ierep[i]) {
			rep=1
			print "		if (IE_ISPRESENT(cx->repeat))"
			print "			out->" ie "_repeat = cx->repeat;"
		}
		if (ienum[i] == "-") {
			print "		out->" ie ".h = *hdr;"
			print "		if (hdr->present & UNI_IE_ERROR)"
			print "			return (DEC_ERR);"
			print "		if(uni_decode_ie_body(UNI_IE_"toupper(ie)", (union uni_ieall *)&out->"ie", msg, ielen, cx))"
			print "			return (DEC_ERR);"

		} else {
			print "		for(i = 0; i < " ienum[i] "; i++)"
			print "			if (!IE_ISPRESENT(out->" ie "[i])) {"
			print "				out->" ie "[i].h = *hdr;"
			print "				if (hdr->present & UNI_IE_ERROR)"
			print "					return (DEC_ERR);"
			print "				if(uni_decode_ie_body(UNI_IE_"toupper(ie)", (union uni_ieall *)&out->"ie"[i], msg, ielen, cx))"
			print "					return (DEC_ERR);"
			print "				break;"
			print "			}"
		}
		print "		break;"
	}
	if(rep) {
		print ""
		print "	  case UNI_IE_REPEAT:"
		print "		cx->repeat.h = *hdr;"
		print "		if (hdr->present & UNI_IE_ERROR)"
		print "			return (DEC_ERR);"
		print "		if (uni_decode_ie_body(UNI_IE_REPEAT, (union uni_ieall *)&cx->repeat, msg, ielen, cx))"
		print "			return (DEC_ERR);"
		print "		break;"
	}

	print ""
	print "	  default:"
	print "		return (DEC_ILL);"
	print "	}"
	print "	return (DEC_OK);"
	print "}"
}

function gen_reg() {
	print ""
	print "static const struct msgdecl decl_" msg " = {"
	print "\t0,"
	print "\t\"" msg "\","
	print "\t(uni_msg_print_f)print_" msg ","
	print "\t(uni_msg_check_f)check_" msg ","
	print "\t(uni_msg_encode_f)encode_" msg ","
	print "\t(uni_msg_decode_f)decode_" msg
	print "};"
	decl[code] = "decl_" msg
}
