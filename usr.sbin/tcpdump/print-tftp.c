/*	$OpenBSD: print-tftp.c,v 1.14 2020/01/24 22:46:37 procter Exp $	*/

/*
 * Copyright (c) 1990, 1991, 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Format and print trivial file transfer protocol packets.
 */

#include <sys/time.h>

#include <netinet/in.h>

#ifdef SEGSIZE
#undef SEGSIZE					/* SINIX sucks */
#endif
#include <arpa/tftp.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "interface.h"
#include "addrtoname.h"

/* op code to string mapping */
static struct tok op2str[] = {
	{ RRQ,		"RRQ" },	/* read request */
	{ WRQ,		"WRQ" },	/* write request */
	{ DATA,		"DATA" },	/* data packet */
	{ ACK,		"ACK" },	/* acknowledgement */
	{ ERROR,	"ERROR" },	/* error code */
	{ 0,		NULL }
};

/* error code to string mapping */
static struct tok err2str[] = {
	{ EUNDEF,	"EUNDEF" },	/* not defined */
	{ ENOTFOUND,	"ENOTFOUND" },	/* file not found */
	{ EACCESS,	"EACCESS" },	/* access violation */
	{ ENOSPACE,	"ENOSPACE" },	/* disk full or allocation exceeded */
	{ EBADOP,	"EBADOP" },	/* illegal TFTP operation */
	{ EBADID,	"EBADID" },	/* unknown transfer ID */
	{ EEXISTS,	"EEXISTS" },	/* file already exists */
	{ ENOUSER,	"ENOUSER" },	/* no such user */
	{ 0,		NULL }
};

/*
 * Print trivial file transfer program requests
 */
void
tftp_print(const u_char *bp, u_int length)
{
	const struct tftphdr *tp;
	const char *cp;
	const u_char *p;
	int opcode, i;
	static char tstr[] = " [|tftp]";

	tp = (const struct tftphdr *)bp;

	/* Print length */
	printf("%d", length);

	/* Print tftp request type */
	TCHECK(tp->th_opcode);
	opcode = ntohs(tp->th_opcode);
	cp = tok2str(op2str, "tftp-#%d", opcode);
	printf(" %s", cp);
	/* Bail if bogus opcode */
	if (*cp == 't')
		return;

	switch (opcode) {

	case RRQ:
	case WRQ:
		/*
		 * XXX Not all arpa/tftp.h's specify th_stuff as any
		 * array; use address of th_block instead
		 */
#ifdef notdef
		p = (u_char *)tp->th_stuff;
#else
		p = (u_char *)&tp->th_block;
#endif
		printf(" \"");
		i = fn_print(p, snapend);
		putchar('"');
		if (i)
			goto trunc;
		break;

	case ACK:
	case DATA:
		TCHECK(tp->th_block);
		printf(" block %d", ntohs(tp->th_block));
		break;

	case ERROR:
		/* Print error code string */
		TCHECK(tp->th_code);
		printf(" %s ", tok2str(err2str, "tftp-err-#%d \"",
				       ntohs(tp->th_code)));
		/* Print error message string */
		putchar('"');
		i = fn_print((const u_char *)tp->th_data, snapend);
		putchar('"');
		if (i)
			goto trunc;
		break;

	default:
		/* We shouldn't get here */
		printf("(unknown #%d)", opcode);
		break;
	}
	return;
trunc:
	printf("%s", tstr);
	return;
}
