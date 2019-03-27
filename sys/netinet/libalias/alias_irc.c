/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Charles Mott <cm@linktel.net>
 * All rights reserved.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* Alias_irc.c intercepts packages contain IRC CTCP commands, and
	changes DCC commands to export a port on the aliasing host instead
	of an aliased host.

    For this routine to work, the DCC command must fit entirely into a
    single TCP packet.  This will usually happen, but is not
    guaranteed.

	 The interception is likely to change the length of the packet.
	 The handling of this is copied more-or-less verbatim from
	 ftp_alias.c

	 Initial version: Eivind Eklund <perhaps@yes.no> (ee) 97-01-29

	 Version 2.1:  May, 1997 (cjm)
	     Very minor changes to conform with
	     local/global/function naming conventions
	     within the packet alising module.
*/

/* Includes */
#ifdef _KERNEL
#include <sys/param.h>
#include <sys/ctype.h>
#include <sys/limits.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#else
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#endif

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#ifdef _KERNEL
#include <netinet/libalias/alias.h>
#include <netinet/libalias/alias_local.h>
#include <netinet/libalias/alias_mod.h>
#else
#include "alias_local.h"
#include "alias_mod.h"
#endif

#define IRC_CONTROL_PORT_NUMBER_1 6667
#define IRC_CONTROL_PORT_NUMBER_2 6668

#define PKTSIZE (IP_MAXPACKET + 1)
char *newpacket;

/* Local defines */
#define DBprintf(a)

static void
AliasHandleIrcOut(struct libalias *, struct ip *, struct alias_link *,	
		  int maxpacketsize);

static int
fingerprint(struct libalias *la, struct alias_data *ah)
{

	if (ah->dport == NULL || ah->lnk == NULL || ah->maxpktsize == 0)
		return (-1);
	if (ntohs(*ah->dport) == IRC_CONTROL_PORT_NUMBER_1
	    || ntohs(*ah->dport) == IRC_CONTROL_PORT_NUMBER_2)
		return (0);
	return (-1);
}

static int
protohandler(struct libalias *la, struct ip *pip, struct alias_data *ah)
{

	newpacket = malloc(PKTSIZE);
	if (newpacket) {
		AliasHandleIrcOut(la, pip, ah->lnk, ah->maxpktsize);
		free(newpacket);
	}
	return (0);
}

struct proto_handler handlers[] = {
	{
	  .pri = 90,
	  .dir = OUT,
	  .proto = TCP,
	  .fingerprint = &fingerprint,
	  .protohandler = &protohandler
	},
	{ EOH }
};

static int
mod_handler(module_t mod, int type, void *data)
{
	int error;

	switch (type) {
	case MOD_LOAD:
		error = 0;
		LibAliasAttachHandlers(handlers);
		break;
	case MOD_UNLOAD:
		error = 0;
		LibAliasDetachHandlers(handlers);
		break;
	default:
		error = EINVAL;
	}
	return (error);
}

#ifdef _KERNEL
static
#endif
moduledata_t alias_mod = {
       "alias_irc", mod_handler, NULL
};

/* Kernel module definition. */
#ifdef	_KERNEL
DECLARE_MODULE(alias_irc, alias_mod, SI_SUB_DRIVERS, SI_ORDER_SECOND);
MODULE_VERSION(alias_irc, 1);
MODULE_DEPEND(alias_irc, libalias, 1, 1, 1);
#endif

static void
AliasHandleIrcOut(struct libalias *la,
    struct ip *pip,		/* IP packet to examine */
    struct alias_link *lnk,	/* Which link are we on? */
    int maxsize			/* Maximum size of IP packet including
				 * headers */
)
{
	int hlen, tlen, dlen;
	struct in_addr true_addr;
	u_short true_port;
	char *sptr;
	struct tcphdr *tc;
	int i;			/* Iterator through the source */

/* Calculate data length of TCP packet */
	tc = (struct tcphdr *)ip_next(pip);
	hlen = (pip->ip_hl + tc->th_off) << 2;
	tlen = ntohs(pip->ip_len);
	dlen = tlen - hlen;

	/*
	 * Return if data length is too short - assume an entire PRIVMSG in
	 * each packet.
	 */
	if (dlen < (int)sizeof(":A!a@n.n PRIVMSG A :aDCC 1 1a") - 1)
		return;

/* Place string pointer at beginning of data */
	sptr = (char *)pip;
	sptr += hlen;
	maxsize -= hlen;	/* We're interested in maximum size of
				 * data, not packet */

	/* Search for a CTCP command [Note 1] */
	for (i = 0; i < dlen; i++) {
		if (sptr[i] == '\001')
			goto lFOUND_CTCP;
	}
	return;			/* No CTCP commands in  */
	/* Handle CTCP commands - the buffer may have to be copied */
lFOUND_CTCP:
	{
		unsigned int copyat = i;
		unsigned int iCopy = 0;	/* How much data have we written to
					 * copy-back string? */
		unsigned long org_addr;	/* Original IP address */
		unsigned short org_port;	/* Original source port
						 * address */

lCTCP_START:
		if (i >= dlen || iCopy >= PKTSIZE)
			goto lPACKET_DONE;
		newpacket[iCopy++] = sptr[i++];	/* Copy the CTCP start
						 * character */
		/* Start of a CTCP */
		if (i + 4 >= dlen)	/* Too short for DCC */
			goto lBAD_CTCP;
		if (sptr[i + 0] != 'D')
			goto lBAD_CTCP;
		if (sptr[i + 1] != 'C')
			goto lBAD_CTCP;
		if (sptr[i + 2] != 'C')
			goto lBAD_CTCP;
		if (sptr[i + 3] != ' ')
			goto lBAD_CTCP;
		/* We have a DCC command - handle it! */
		i += 4;		/* Skip "DCC " */
		if (iCopy + 4 > PKTSIZE)
			goto lPACKET_DONE;
		newpacket[iCopy++] = 'D';
		newpacket[iCopy++] = 'C';
		newpacket[iCopy++] = 'C';
		newpacket[iCopy++] = ' ';

		DBprintf(("Found DCC\n"));
		/*
		 * Skip any extra spaces (should not occur according to
		 * protocol, but DCC breaks CTCP protocol anyway
		 */
		while (sptr[i] == ' ') {
			if (++i >= dlen) {
				DBprintf(("DCC packet terminated in just spaces\n"));
				goto lPACKET_DONE;
			}
		}

		DBprintf(("Transferring command...\n"));
		while (sptr[i] != ' ') {
			newpacket[iCopy++] = sptr[i];
			if (++i >= dlen || iCopy >= PKTSIZE) {
				DBprintf(("DCC packet terminated during command\n"));
				goto lPACKET_DONE;
			}
		}
		/* Copy _one_ space */
		if (i + 1 < dlen && iCopy < PKTSIZE)
			newpacket[iCopy++] = sptr[i++];

		DBprintf(("Done command - removing spaces\n"));
		/*
		 * Skip any extra spaces (should not occur according to
		 * protocol, but DCC breaks CTCP protocol anyway
		 */
		while (sptr[i] == ' ') {
			if (++i >= dlen) {
				DBprintf(("DCC packet terminated in just spaces (post-command)\n"));
				goto lPACKET_DONE;
			}
		}

		DBprintf(("Transferring filename...\n"));
		while (sptr[i] != ' ') {
			newpacket[iCopy++] = sptr[i];
			if (++i >= dlen || iCopy >= PKTSIZE) {
				DBprintf(("DCC packet terminated during filename\n"));
				goto lPACKET_DONE;
			}
		}
		/* Copy _one_ space */
		if (i + 1 < dlen && iCopy < PKTSIZE)
			newpacket[iCopy++] = sptr[i++];

		DBprintf(("Done filename - removing spaces\n"));
		/*
		 * Skip any extra spaces (should not occur according to
		 * protocol, but DCC breaks CTCP protocol anyway
		 */
		while (sptr[i] == ' ') {
			if (++i >= dlen) {
				DBprintf(("DCC packet terminated in just spaces (post-filename)\n"));
				goto lPACKET_DONE;
			}
		}

		DBprintf(("Fetching IP address\n"));
		/* Fetch IP address */
		org_addr = 0;
		while (i < dlen && isdigit(sptr[i])) {
			if (org_addr > ULONG_MAX / 10UL) {	/* Terminate on overflow */
				DBprintf(("DCC Address overflow (org_addr == 0x%08lx, next char %c\n", org_addr, sptr[i]));
				goto lBAD_CTCP;
			}
			org_addr *= 10;
			org_addr += sptr[i++] - '0';
		}
		DBprintf(("Skipping space\n"));
		if (i + 1 >= dlen || sptr[i] != ' ') {
			DBprintf(("Overflow (%d >= %d) or bad character (%02x) terminating IP address\n", i + 1, dlen, sptr[i]));
			goto lBAD_CTCP;
		}
		/*
		 * Skip any extra spaces (should not occur according to
		 * protocol, but DCC breaks CTCP protocol anyway, so we
		 * might as well play it safe
		 */
		while (sptr[i] == ' ') {
			if (++i >= dlen) {
				DBprintf(("Packet failure - space overflow.\n"));
				goto lPACKET_DONE;
			}
		}
		DBprintf(("Fetching port number\n"));
		/* Fetch source port */
		org_port = 0;
		while (i < dlen && isdigit(sptr[i])) {
			if (org_port > 6554) {	/* Terminate on overflow
						 * (65536/10 rounded up */
				DBprintf(("DCC: port number overflow\n"));
				goto lBAD_CTCP;
			}
			org_port *= 10;
			org_port += sptr[i++] - '0';
		}
		/* Skip illegal addresses (or early termination) */
		if (i >= dlen || (sptr[i] != '\001' && sptr[i] != ' ')) {
			DBprintf(("Bad port termination\n"));
			goto lBAD_CTCP;
		}
		DBprintf(("Got IP %lu and port %u\n", org_addr, (unsigned)org_port));

		/* We've got the address and port - now alias it */
		{
			struct alias_link *dcc_lnk;
			struct in_addr destaddr;


			true_port = htons(org_port);
			true_addr.s_addr = htonl(org_addr);
			destaddr.s_addr = 0;

			/* Sanity/Security checking */
			if (!org_addr || !org_port ||
			    pip->ip_src.s_addr != true_addr.s_addr ||
			    org_port < IPPORT_RESERVED)
				goto lBAD_CTCP;

			/*
			 * Steal the FTP_DATA_PORT - it doesn't really
			 * matter, and this would probably allow it through
			 * at least _some_ firewalls.
			 */
			dcc_lnk = FindUdpTcpOut(la, true_addr, destaddr,
			    true_port, 0,
			    IPPROTO_TCP, 1);
			DBprintf(("Got a DCC link\n"));
			if (dcc_lnk) {
				struct in_addr alias_address;	/* Address from aliasing */
				u_short alias_port;	/* Port given by
							 * aliasing */
				int n;

#ifndef NO_FW_PUNCH
				/* Generate firewall hole as appropriate */
				PunchFWHole(dcc_lnk);
#endif

				alias_address = GetAliasAddress(lnk);
				n = snprintf(&newpacket[iCopy],
				    PKTSIZE - iCopy,
				    "%lu ", (u_long) htonl(alias_address.s_addr));
				if (n < 0) {
					DBprintf(("DCC packet construct failure.\n"));
					goto lBAD_CTCP;
				}
				if ((iCopy += n) >= PKTSIZE) {	/* Truncated/fit exactly
										 * - bad news */
					DBprintf(("DCC constructed packet overflow.\n"));
					goto lBAD_CTCP;
				}
				alias_port = GetAliasPort(dcc_lnk);
				n = snprintf(&newpacket[iCopy],
				    PKTSIZE - iCopy,
				    "%u", htons(alias_port));
				if (n < 0) {
					DBprintf(("DCC packet construct failure.\n"));
					goto lBAD_CTCP;
				}
				iCopy += n;
				/*
				 * Done - truncated cases will be taken
				 * care of by lBAD_CTCP
				 */
				DBprintf(("Aliased IP %lu and port %u\n", alias_address.s_addr, (unsigned)alias_port));
			}
		}
		/*
		 * An uninteresting CTCP - state entered right after '\001'
		 * has been pushed.  Also used to copy the rest of a DCC,
		 * after IP address and port has been handled
		 */
lBAD_CTCP:
		for (; i < dlen && iCopy < PKTSIZE; i++, iCopy++) {
			newpacket[iCopy] = sptr[i];	/* Copy CTCP unchanged */
			if (sptr[i] == '\001') {
				goto lNORMAL_TEXT;
			}
		}
		goto lPACKET_DONE;
		/* Normal text */
lNORMAL_TEXT:
		for (; i < dlen && iCopy < PKTSIZE; i++, iCopy++) {
			newpacket[iCopy] = sptr[i];	/* Copy CTCP unchanged */
			if (sptr[i] == '\001') {
				goto lCTCP_START;
			}
		}
		/* Handle the end of a packet */
lPACKET_DONE:
		iCopy = iCopy > maxsize - copyat ? maxsize - copyat : iCopy;
		memcpy(sptr + copyat, newpacket, iCopy);

/* Save information regarding modified seq and ack numbers */
		{
			int delta;

			SetAckModified(lnk);
			tc = (struct tcphdr *)ip_next(pip);				
			delta = GetDeltaSeqOut(tc->th_seq, lnk);
			AddSeq(lnk, delta + copyat + iCopy - dlen, pip->ip_hl,
			    pip->ip_len, tc->th_seq, tc->th_off);
		}

		/* Revise IP header */
		{
			u_short new_len;

			new_len = htons(hlen + iCopy + copyat);
			DifferentialChecksum(&pip->ip_sum,
			    &new_len,
			    &pip->ip_len,
			    1);
			pip->ip_len = new_len;
		}

		/* Compute TCP checksum for revised packet */
		tc->th_sum = 0;
#ifdef _KERNEL
		tc->th_x2 = 1;
#else
		tc->th_sum = TcpChecksum(pip);
#endif
		return;
	}
}

/* Notes:
	[Note 1]
	The initial search will most often fail; it could be replaced with a 32-bit specific search.
	Such a search would be done for 32-bit unsigned value V:
	V ^= 0x01010101;				  (Search is for null bytes)
	if( ((V-0x01010101)^V) & 0x80808080 ) {
     (found a null bytes which was a 01 byte)
	}
   To assert that the processor is 32-bits, do
   extern int ircdccar[32];        (32 bits)
   extern int ircdccar[CHAR_BIT*sizeof(unsigned int)];
   which will generate a type-error on all but 32-bit machines.

	[Note 2] This routine really ought to be replaced with one that
	creates a transparent proxy on the aliasing host, to allow arbitrary
	changes in the TCP stream.  This should not be too difficult given
	this base;  I (ee) will try to do this some time later.
	*/
