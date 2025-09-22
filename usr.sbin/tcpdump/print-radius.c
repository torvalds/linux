/*	$OpenBSD: print-radius.c,v 1.13 2020/01/24 22:46:37 procter Exp $	*/

/*
 * Copyright (c) 1997 Thomas H. Ptacek. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <string.h>

/* RADIUS support for tcpdump, Thomas Ptacek <tqbf@enteract.com> */

#include "interface.h"
#include "radius.h"

static void r_print_att(int code, int len, const u_char *val);
static void r_print_int(int code, int len, const u_char *val);
static void r_print_address(int code, int len, const u_char *val);
static void r_print_string(int code, int len, const u_char *val);
static void r_print_hex(int code, int len, const u_char *val);

/* --------------------------------------------------------------- */

struct radius_ctable {
	int code;
	char *name;
};

/* map opcodes to strings */

#define DEFINED_OPCODES		11

static struct radius_ctable radius_codes[] = {
	{ -1, 					NULL	},
	{ RADIUS_CODE_ACCESS_REQUEST,		"Axs?"	},
	{ RADIUS_CODE_ACCESS_ACCEPT,		"Axs+"	},
	{ RADIUS_CODE_ACCESS_REJECT,		"Axs-"	},
	{ RADIUS_CODE_ACCOUNT_REQUEST,		"Act?"	},
	{ RADIUS_CODE_ACCOUNT_RESPONSE,		"Act+"	},
	{ RADIUS_CODE_ACCOUNT_STATUS,		"ActSt"	},
	{ RADIUS_CODE_PASSCHG_REQUEST,		"Pchg?"	},
	{ RADIUS_CODE_PASSCHG_ACCEPT,		"Pchg+"	},
	{ RADIUS_CODE_PASSCHG_REJECT,		"Pchg-"	},
	{ RADIUS_CODE_ACCOUNT_MESSAGE,		"ActMg"	},
	{ RADIUS_CODE_ACCESS_CHALLENGE,		"Axs!"	},
	{ -1,					NULL	}
};

/* --------------------------------------------------------------- */

#define MAX_VALUES 20	

struct radius_atable {
	int code;
	int encoding;
	char *name;
	char *values[MAX_VALUES];
};

/* map attributes to strings */

/* the right way to do this is probably to read these values out
 * of the actual RADIUS dictionary; this would require the machine
 * running tcpdump to have that file installed, and it's not my
 * program, so I'm not going to introduce new dependencies. Oh well. 
 */

static struct radius_atable radius_atts[] = {

{ RADIUS_ATT_USER_NAME, 	RD_STRING, 	"Name", 	{ NULL } },
{ RADIUS_ATT_PASSWORD, 		RD_HEX, 	"Pass", 	{ NULL } },
{ RADIUS_ATT_CHAP_PASS, 	RD_HEX, 	"CPass",	{ NULL } },
{ RADIUS_ATT_NAS_IP, 		RD_ADDRESS, 	"NAS-IP", 	{ NULL } },
{ RADIUS_ATT_NAS_PORT, 		RD_INT, 	"NAS-Pt", 	{ NULL } },

{ RADIUS_ATT_USER_SERVICE, 	RD_INT, 	"USvc", 	
{ "", "Login", "Framed", "DB-Lgn", "DB-Frm", "Out", "Shell", NULL } },

{ RADIUS_ATT_PROTOCOL, 		RD_INT, 	"FProt", 
{ "", "PPP", "SLIP", NULL } },

{ RADIUS_ATT_FRAMED_ADDRESS, 	RD_ADDRESS, 	"F-IP", 	{ NULL } },
{ RADIUS_ATT_NETMASK, 		RD_ADDRESS, 	"F-Msk", 	{ NULL } },
{ RADIUS_ATT_ROUTING, 		RD_INT, 	"F-Rtg", 	{ NULL } },
{ RADIUS_ATT_FILTER, 		RD_STRING, 	"FltID", 	{ NULL } },
{ RADIUS_ATT_MTU, 		RD_INT, 	"F-MTU", 	{ NULL } },
{ RADIUS_ATT_COMPRESSION, 	RD_INT, 	"F-Comp", 	{ NULL } },
{ RADIUS_ATT_LOGIN_HOST, 	RD_ADDRESS, 	"L-Hst", 	{ NULL } },

{ RADIUS_ATT_LOGIN_SERVICE, 	RD_INT, 	"L-Svc", 
{ "", "Telnt", "Rlog", "Clear", "PortM", NULL }				},

{ RADIUS_ATT_LOGIN_TCP_PORT, 	RD_INT, 	"L-Pt", 	{ NULL } },
{ RADIUS_ATT_OLD_PASSWORD, 	RD_HEX, 	"OPass", 	{ NULL } },
{ RADIUS_ATT_PORT_MESSAGE, 	RD_STRING, 	"PMsg", 	{ NULL } },
{ RADIUS_ATT_DIALBACK_NO, 	RD_STRING, 	"DB#", 		{ NULL } },
{ RADIUS_ATT_DIALBACK_NAME, 	RD_STRING, 	"DBNm", 	{ NULL } },
{ RADIUS_ATT_EXPIRATION, 	RD_DATE, 	"PExp", 	{ NULL } },
{ RADIUS_ATT_FRAMED_ROUTE, 	RD_STRING, 	"F-Rt", 	{ NULL } },
{ RADIUS_ATT_FRAMED_IPX, 	RD_ADDRESS, 	"F-IPX", 	{ NULL } },
{ RADIUS_ATT_CHALLENGE_STATE, 	RD_STRING, 	"CState", 	{ NULL } },
{ RADIUS_ATT_CLASS, 		RD_STRING, 	"Class", 	{ NULL } },
{ RADIUS_ATT_VENDOR_SPECIFIC, 	RD_HEX, 	"Vendor", 	{ NULL } },
{ RADIUS_ATT_SESSION_TIMEOUT, 	RD_INT, 	"S-TO", 	{ NULL } },
{ RADIUS_ATT_IDLE_TIMEOUT, 	RD_INT, 	"I-TO", 	{ NULL } },
{ RADIUS_ATT_TERMINATE_ACTION, 	RD_INT, 	"TermAct", 	{ NULL } },
{ RADIUS_ATT_CALLED_ID, 	RD_STRING, 	"Callee", 	{ NULL } },
{ RADIUS_ATT_CALLER_ID, 	RD_STRING, 	"Caller", 	{ NULL } },

{ RADIUS_ATT_STATUS_TYPE, 	RD_INT, 	"Stat", 
{ "", "Start", "Stop", NULL }					},

{ -1,				-1,		NULL, 		{ NULL } }

};

typedef void (*aselector)(int code, int len, const u_char *data);
aselector atselector[] = {
	r_print_hex, 
	r_print_int, 
	r_print_int, 
	r_print_address,
	r_print_string,
	r_print_hex
};

static void r_print_att(int code, int len, const u_char *data) {
	struct radius_atable *atp;
	int i;

	for(atp = radius_atts; atp->code != -1; atp++) 
		if(atp->code == code)
			break;

	if(atp->code == -1) {
		if(vflag > 1) {
			printf(" %d =", code);
			atselector[RD_HEX](code, len, data);
		} else 
			printf(" %d", code);

		return;
	}

	printf(" %s =", atp->name);

	if(atp->encoding == RD_INT && *atp->values) {
		u_int32_t k = ntohl((*(int *)data));

		for(i = 0; atp->values[i] != NULL; i++) 
			/* SHOOT ME */ ;

		if(k < i) {
			printf(" %s",
				atp->values[k]);
			return;
		}
	}	
			
	atselector[atp->encoding](code, len, data);	
}

static void r_print_int(int code, int len, const u_char *data) {
	if(len < 4) {
		printf(" ?");
		return;
	}

	printf(" %d", ntohl((*(int *)data)));
}

static void r_print_address(int code, int len, const u_char *data) {
	if(len < 4) {
		printf(" ?");
		return;
	}

	printf(" %s", inet_ntoa((*(struct in_addr *)data)));
}

static void r_print_string(int code, int len, const u_char *data) {
	char string[128];

	if(!len) {
		printf(" ?");
		return;
	}

	if(len > 127)
		len = 127;

	memset(string, 0, 128);
	memcpy(string, data, len);

	printf(" ");
	safeputs(string);
}

static void r_print_hex(int code, int len, const u_char *data) {
	int i;	

	/* excuse me */

	printf(" [");
	
	for(i = 0; i < len; i++)
		printf("%02x", data[i]);

	fputc(']', stdout);
}

void radius_print(const u_char *data, u_int len) {
	const struct radius_header *rhp;
	const u_char *pp;
	int first, l, ac, al;

	if(len < sizeof(struct radius_header)) {
		printf("[|radius]");
		return;
	}

	rhp = (struct radius_header *) data;

	if(rhp->code > DEFINED_OPCODES ||
	   rhp->code < 1) 
		printf("Code:%d id:%x [%d]",
		rhp->code, rhp->id, ntohs(rhp->len));
	else
		printf("%s id:%x [%d]",
			radius_codes[rhp->code].name,
			rhp->id, ntohs(rhp->len));

	if(ntohs(rhp->len) > len) {
		printf("[|radius]");
		return;
	}

	l = len - RADFIXEDSZ;
	if(!l)
		return;
	else
		pp = data + RADFIXEDSZ;

	first = 1;
	while(l) {
		if(!first)
			fputc(',', stdout);
		else
			first = 0;

		ac = *pp++;
		al = *pp++;

		if(al > l || al < 2) {
			printf(" [|radius]");
			return;
		}
	
		al -= 2;

		r_print_att(ac, al, pp);

		pp += al; l -= al + 2;
	}
}
