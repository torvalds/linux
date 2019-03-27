/*
 * Copyright (c) 2001-2003
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 * 	All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Author: Hartmut Brandt <harti@freebsd.org>
 *
 * $Begemot: libunimsg/netnatm/sig/unimkmsg.h,v 1.4 2003/09/19 12:03:34 hbb Exp $
 *
 * Macros to make messages.
 */

#define MK_MSG_ORIG(MSG,TYPE,CREF,FLAG) 				\
    do {								\
	(MSG)->mtype = (TYPE);						\
	(MSG)->u.hdr.cref.cref = (CREF);				\
	(MSG)->u.hdr.cref.flag = (FLAG);				\
	(MSG)->u.hdr.act = UNI_MSGACT_DEFAULT;				\
    } while(0)

#define MK_MSG_RESP(MSG,TYPE,CREF)					\
    do {								\
	(MSG)->mtype = (TYPE);						\
	(MSG)->u.hdr.cref.cref = (CREF)->cref;				\
	(MSG)->u.hdr.cref.flag = !(CREF)->flag;				\
	(MSG)->u.hdr.act = UNI_MSGACT_DEFAULT;				\
    } while(0)

#define MK_IE_CALLSTATE(IE,CS)						\
    do {								\
	(IE).h.present = 0;						\
	IE_SETPRESENT(IE);						\
	(IE).h.coding = UNI_CODING_ITU;					\
	(IE).h.act = UNI_IEACT_DEFAULT;					\
	(IE).state = CS;						\
    } while(0)

#define MK_IE_EPREF(IE,EPREF,FLAG)					\
    do {								\
	(IE).h.present = 0;						\
	IE_SETPRESENT(IE);						\
	(IE).h.coding = UNI_CODING_ITU;					\
	(IE).h.act = UNI_IEACT_DEFAULT;					\
	(IE).epref = EPREF;						\
	(IE).flag = FLAG;						\
    } while(0)

#define MK_IE_EPSTATE(IE,STATE)						\
    do {								\
	(IE).h.present = 0;						\
	IE_SETPRESENT(IE);						\
	(IE).h.coding = UNI_CODING_ITU;					\
	(IE).h.act = UNI_IEACT_DEFAULT;					\
	(IE).state = STATE;						\
    } while(0)

#define MK_IE_CAUSE(IE,LOC,CAUSE)					\
    do {								\
	(IE).h.present = 0;						\
	IE_SETPRESENT(IE);						\
	(IE).h.coding = UNI_CODING_ITU;					\
	(IE).h.act = UNI_IEACT_DEFAULT;					\
	(IE).loc = LOC;							\
	(IE).cause = CAUSE;						\
    } while(0)

#define ADD_CAUSE_MTYPE(IE,MTYPE)					\
    do {								\
	(IE).h.present |= UNI_CAUSE_MTYPE_P;				\
	(IE).u.mtype = MTYPE;						\
    } while(0)

#define ADD_CAUSE_CHANNID(IE,VPI,VCI)					\
    do {								\
	(IE).h.present |= UNI_CAUSE_VPCI_P;				\
	(IE).u.vpci.vpci = VPI;						\
	(IE).u.vpci.vci = VCI;						\
    } while(0)

#define ADD_CAUSE_TIMER(IE,TIMER)					\
    do {								\
	(IE).h.present |= UNI_CAUSE_TIMER_P;				\
	(IE).u.timer[0] = (TIMER)[0];					\
	(IE).u.timer[1] = (TIMER)[1];					\
	(IE).u.timer[2] = (TIMER)[2];					\
    } while(0)

/************************************************************/

#define COPY_FROM_RELEASE_COMPL(U,DEST)					\
    do {								\
	u_int _i, _j;							\
									\
	for(_i = _j = 0; _i < 2; _i++)					\
		if(IE_ISGOOD((U)->u.release_compl.cause[_i]))		\
			(DEST)->cause[_j++] =				\
			    (U)->u.release_compl.cause[_i];		\
	for(_i = _j = 0; _i < UNI_NUM_IE_GIT; _i++)			\
		if(IE_ISGOOD((U)->u.release_compl.git[_i]))		\
			(DEST)->git[_j++] =				\
			    (U)->u.release_compl.git[_i];		\
	if(IE_ISGOOD((U)->u.release_compl.uu))				\
		(DEST)->uu = (U)->u.release_compl.uu;			\
	if(IE_ISGOOD((U)->u.release_compl.crankback))			\
		(DEST)->crankback = (U)->u.release_compl.crankback;	\
    } while(0)

#define COPY_FROM_DROP_ACK(U,DEST)					\
    do {								\
	u_int _i, _j;							\
									\
	if(IE_ISGOOD((U)->u.drop_party_ack.epref))			\
		(DEST)->epref = (U)->u.drop_party_ack.epref;		\
	if(IE_ISGOOD((U)->u.drop_party_ack.cause))			\
		(DEST)->cause = (U)->u.drop_party_ack.cause;		\
	if(IE_ISGOOD((U)->u.drop_party_ack.uu))				\
		(DEST)->uu = (U)->u.drop_party_ack.uu;			\
	for(_i = _j = 0; _i < UNI_NUM_IE_GIT; _i++)			\
		if(IE_ISGOOD((U)->u.drop_party_ack.git[_i]))		\
			(DEST)->git[_j++] =				\
			    (U)->u.drop_party_ack.git[_i];		\
    } while(0)

#define COPY_FROM_ADD_REJ(U,DEST)					\
    do {								\
	u_int _i, _j;							\
									\
	if(IE_ISGOOD((U)->u.add_party_rej.epref))			\
		(DEST)->epref = (U)->u.add_party_rej.epref;		\
	if(IE_ISGOOD((U)->u.add_party_rej.cause))			\
		(DEST)->cause = (U)->u.add_party_rej.cause;		\
	if(IE_ISGOOD((U)->u.add_party_rej.uu))				\
		(DEST)->uu = (U)->u.add_party_rej.uu;			\
	for(_i = _j = 0; _i < UNI_NUM_IE_GIT; _i++)			\
		if(IE_ISGOOD((U)->u.add_party_rej.git[_i]))		\
			(DEST)->git[_j++] =				\
			    (U)->u.add_party_rej.git[_i];		\
    } while(0)
