/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1988, 1992 The University of Utah and the Center
 *	for Software Science (CSS).
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Center for Software Science of the University of Utah Computer
 * Science Department.  CSS requests users of this software to return
 * to css-dist@cs.utah.edu any improvements that they make and grant
 * CSS redistribution rights.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)utils.c	8.1 (Berkeley) 6/4/93
 *
 * From: Utah Hdr: utils.c 3.1 92/07/06
 * Author: Jeff Forys, University of Utah CSS
 */

#ifndef lint
#if 0
static const char sccsid[] = "@(#)utils.c	8.1 (Berkeley) 6/4/93";
#endif
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/param.h>
#include <sys/time.h>
#include <netinet/in.h>

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include "defs.h"

/*
**  DispPkt -- Display the contents of an RMPCONN packet.
**
**	Parameters:
**		rconn - packet to be displayed.
**		direct - direction packet is going (DIR_*).
**
**	Returns:
**		Nothing.
**
**	Side Effects:
**		None.
*/
void
DispPkt(RMPCONN *rconn, int direct)
{
	static const char BootFmt[] = "\t\tRetCode:%u SeqNo:%x SessID:%x Vers:%u";
	static const char ReadFmt[] = "\t\tRetCode:%u Offset:%x SessID:%x\n";

	struct tm *tmp;
	struct rmp_packet *rmp;
	int i, omask;
	u_int32_t t;

	/*
	 *  Since we will be working with RmpConns as well as DbgFp, we
	 *  must block signals that can affect either.
	 */
	omask = sigblock(sigmask(SIGHUP)|sigmask(SIGUSR1)|sigmask(SIGUSR2));

	if (DbgFp == NULL) {			/* sanity */
		(void) sigsetmask(omask);
		return;
	}

	/* display direction packet is going using '>>>' or '<<<' */
	fputs((direct==DIR_RCVD)?"<<< ":(direct==DIR_SENT)?">>> ":"", DbgFp);

	/* display packet timestamp */
	tmp = localtime((time_t *)&rconn->tstamp.tv_sec);
	fprintf(DbgFp, "%02d:%02d:%02d.%06ld   ", tmp->tm_hour, tmp->tm_min,
	        tmp->tm_sec, rconn->tstamp.tv_usec);

	/* display src or dst addr and information about network interface */
	fprintf(DbgFp, "Addr: %s   Intf: %s\n", EnetStr(rconn), IntfName);

	rmp = &rconn->rmp;

	/* display IEEE 802.2 Logical Link Control header */
	(void) fprintf(DbgFp, "\t802.2 LLC: DSAP:%x SSAP:%x CTRL:%x\n",
               rmp->hp_llc.dsap, rmp->hp_llc.ssap, ntohs(rmp->hp_llc.cntrl));

	/* display HP extensions to 802.2 Logical Link Control header */
	(void) fprintf(DbgFp, "\tHP Ext:    DXSAP:%x SXSAP:%x\n",
	               ntohs(rmp->hp_llc.dxsap), ntohs(rmp->hp_llc.sxsap));

	/*
	 *  Display information about RMP packet using type field to
	 *  determine what kind of packet this is.
	 */
	switch(rmp->r_type) {
		case RMP_BOOT_REQ:		/* boot request */
			(void) fprintf(DbgFp, "\tBoot Request:");
			GETWORD(rmp->r_brq.rmp_seqno, t);
			if (ntohs(rmp->r_brq.rmp_session) == RMP_PROBESID) {
				if (WORDZE(rmp->r_brq.rmp_seqno))
					fputs(" (Send Server ID)", DbgFp);
				else
					fprintf(DbgFp," (Send Filename #%u)",t);
			}
			(void) fputc('\n', DbgFp);
			(void) fprintf(DbgFp, BootFmt, rmp->r_brq.rmp_retcode,
			        t, ntohs(rmp->r_brq.rmp_session),
			        ntohs(rmp->r_brq.rmp_version));
			(void) fprintf(DbgFp, "\n\t\tMachine Type: ");
			for (i = 0; i < RMP_MACHLEN; i++)
				(void) fputc(rmp->r_brq.rmp_machtype[i], DbgFp);
			DspFlnm(rmp->r_brq.rmp_flnmsize, &rmp->r_brq.rmp_flnm);
			break;
		case RMP_BOOT_REPL:		/* boot reply */
			fprintf(DbgFp, "\tBoot Reply:\n");
			GETWORD(rmp->r_brpl.rmp_seqno, t);
			(void) fprintf(DbgFp, BootFmt, rmp->r_brpl.rmp_retcode,
			        t, ntohs(rmp->r_brpl.rmp_session),
			        ntohs(rmp->r_brpl.rmp_version));
			DspFlnm(rmp->r_brpl.rmp_flnmsize,&rmp->r_brpl.rmp_flnm);
			break;
		case RMP_READ_REQ:		/* read request */
			(void) fprintf(DbgFp, "\tRead Request:\n");
			GETWORD(rmp->r_rrq.rmp_offset, t);
			(void) fprintf(DbgFp, ReadFmt, rmp->r_rrq.rmp_retcode,
			        t, ntohs(rmp->r_rrq.rmp_session));
			(void) fprintf(DbgFp, "\t\tNoOfBytes: %u\n",
			        ntohs(rmp->r_rrq.rmp_size));
			break;
		case RMP_READ_REPL:		/* read reply */
			(void) fprintf(DbgFp, "\tRead Reply:\n");
			GETWORD(rmp->r_rrpl.rmp_offset, t);
			(void) fprintf(DbgFp, ReadFmt, rmp->r_rrpl.rmp_retcode,
			        t, ntohs(rmp->r_rrpl.rmp_session));
			(void) fprintf(DbgFp, "\t\tNoOfBytesSent: %zu\n",
			        rconn->rmplen - RMPREADSIZE(0));
			break;
		case RMP_BOOT_DONE:		/* boot complete */
			(void) fprintf(DbgFp, "\tBoot Complete:\n");
			(void) fprintf(DbgFp, "\t\tRetCode:%u SessID:%x\n",
			        rmp->r_done.rmp_retcode,
			        ntohs(rmp->r_done.rmp_session));
			break;
		default:			/* ??? */
			(void) fprintf(DbgFp, "\tUnknown Type:(%d)\n",
				rmp->r_type);
	}
	(void) fputc('\n', DbgFp);
	(void) fflush(DbgFp);

	(void) sigsetmask(omask);		/* reset old signal mask */
}


/*
**  GetEtherAddr -- convert an RMP (Ethernet) address into a string.
**
**	An RMP BOOT packet has been received.  Look at the type field
**	and process Boot Requests, Read Requests, and Boot Complete
**	packets.  Any other type will be dropped with a warning msg.
**
**	Parameters:
**		addr - array of RMP_ADDRLEN bytes.
**
**	Returns:
**		Pointer to static string representation of `addr'.
**
**	Side Effects:
**		None.
**
**	Warnings:
**		- The return value points to a static buffer; it must
**		  be copied if it's to be saved.
*/
char *
GetEtherAddr(u_int8_t *addr)
{
	static char Hex[] = "0123456789abcdef";
	static char etherstr[RMP_ADDRLEN*3];
	int i;
	char *cp;

	/*
	 *  For each byte in `addr', convert it to "<hexchar><hexchar>:".
	 *  The last byte does not get a trailing `:' appended.
	 */
	i = 0;
	cp = etherstr;
	for(;;) {
		*cp++ = Hex[*addr >> 4 & 0xf];
		*cp++ = Hex[*addr++ & 0xf];
		if (++i == RMP_ADDRLEN)
			break;
		*cp++ = ':';
	}
	*cp = '\0';

	return(etherstr);
}


/*
**  DispFlnm -- Print a string of bytes to DbgFp (often, a file name).
**
**	Parameters:
**		size - number of bytes to print.
**		flnm - address of first byte.
**
**	Returns:
**		Nothing.
**
**	Side Effects:
**		- Characters are sent to `DbgFp'.
*/
void
DspFlnm(u_int size, char *flnm)
{
	int i;

	(void) fprintf(DbgFp, "\n\t\tFile Name (%u): <", size);
	for (i = 0; i < size; i++)
		(void) fputc(*flnm++, DbgFp);
	(void) fputs(">\n", DbgFp);
}


/*
**  NewClient -- allocate memory for a new CLIENT.
**
**	Parameters:
**		addr - RMP (Ethernet) address of new client.
**
**	Returns:
**		Ptr to new CLIENT or NULL if we ran out of memory.
**
**	Side Effects:
**		- Memory will be malloc'd for the new CLIENT.
**		- If malloc() fails, a log message will be generated.
*/
CLIENT *
NewClient(u_int8_t *addr)
{
	CLIENT *ctmp;

	if ((ctmp = (CLIENT *) malloc(sizeof(CLIENT))) == NULL) {
		syslog(LOG_ERR, "NewClient: out of memory (%s)",
		       GetEtherAddr(addr));
		return(NULL);
	}

	memset(ctmp, 0, sizeof(CLIENT));
	memmove(&ctmp->addr[0], addr, RMP_ADDRLEN);
	return(ctmp);
}

/*
**  FreeClient -- free linked list of Clients.
**
**	Parameters:
**		None.
**
**	Returns:
**		Nothing.
**
**	Side Effects:
**		- All malloc'd memory associated with the linked list of
**		  CLIENTS will be free'd; `Clients' will be set to NULL.
**
**	Warnings:
**		- This routine must be called with SIGHUP blocked.
*/
void
FreeClients(void)
{
	CLIENT *ctmp;

	while (Clients != NULL) {
		ctmp = Clients;
		Clients = Clients->next;
		FreeClient(ctmp);
	}
}

/*
**  NewStr -- allocate memory for a character array.
**
**	Parameters:
**		str - null terminated character array.
**
**	Returns:
**		Ptr to new character array or NULL if we ran out of memory.
**
**	Side Effects:
**		- Memory will be malloc'd for the new character array.
**		- If malloc() fails, a log message will be generated.
*/
char *
NewStr(char *str)
{
	char *stmp;

	if ((stmp = (char *)malloc((unsigned) (strlen(str)+1))) == NULL) {
		syslog(LOG_ERR, "NewStr: out of memory (%s)", str);
		return(NULL);
	}

	(void) strcpy(stmp, str);
	return(stmp);
}

/*
**  To save time, NewConn and FreeConn maintain a cache of one RMPCONN
**  in `LastFree' (defined below).
*/

static RMPCONN *LastFree = NULL;

/*
**  NewConn -- allocate memory for a new RMPCONN connection.
**
**	Parameters:
**		rconn - initialization template for new connection.
**
**	Returns:
**		Ptr to new RMPCONN or NULL if we ran out of memory.
**
**	Side Effects:
**		- Memory may be malloc'd for the new RMPCONN (if not cached).
**		- If malloc() fails, a log message will be generated.
*/
RMPCONN *
NewConn(RMPCONN *rconn)
{
	RMPCONN *rtmp;

	if (LastFree == NULL) {		/* nothing cached; make a new one */
		if ((rtmp = (RMPCONN *) malloc(sizeof(RMPCONN))) == NULL) {
			syslog(LOG_ERR, "NewConn: out of memory (%s)",
			       EnetStr(rconn));
			return(NULL);
		}
	} else {			/* use the cached RMPCONN */
		rtmp = LastFree;
		LastFree = NULL;
	}

	/*
	 *  Copy template into `rtmp', init file descriptor to `-1' and
	 *  set ptr to next elem NULL.
	 */
	memmove((char *)rtmp, (char *)rconn, sizeof(RMPCONN));
	rtmp->bootfd = -1;
	rtmp->next = NULL;

	return(rtmp);
}

/*
**  FreeConn -- Free memory associated with an RMPCONN connection.
**
**	Parameters:
**		rtmp - ptr to RMPCONN to be free'd.
**
**	Returns:
**		Nothing.
**
**	Side Effects:
**		- Memory associated with `rtmp' may be free'd (or cached).
**		- File desc associated with `rtmp->bootfd' will be closed.
*/
void
FreeConn(RMPCONN *rtmp)
{
	/*
	 *  If the file descriptor is in use, close the file.
	 */
	if (rtmp->bootfd >= 0) {
		(void) close(rtmp->bootfd);
		rtmp->bootfd = -1;
	}

	if (LastFree == NULL)		/* cache for next time */
		rtmp = LastFree;
	else				/* already one cached; free this one */
		free((char *)rtmp);
}

/*
**  FreeConns -- free linked list of RMPCONN connections.
**
**	Parameters:
**		None.
**
**	Returns:
**		Nothing.
**
**	Side Effects:
**		- All malloc'd memory associated with the linked list of
**		  connections will be free'd; `RmpConns' will be set to NULL.
**		- If LastFree is != NULL, it too will be free'd & NULL'd.
**
**	Warnings:
**		- This routine must be called with SIGHUP blocked.
*/
void
FreeConns(void)
{
	RMPCONN *rtmp;

	while (RmpConns != NULL) {
		rtmp = RmpConns;
		RmpConns = RmpConns->next;
		FreeConn(rtmp);
	}

	if (LastFree != NULL) {
		free((char *)LastFree);
		LastFree = NULL;
	}
}

/*
**  AddConn -- Add a connection to the linked list of connections.
**
**	Parameters:
**		rconn - connection to be added.
**
**	Returns:
**		Nothing.
**
**	Side Effects:
**		- RmpConn will point to new connection.
**
**	Warnings:
**		- This routine must be called with SIGHUP blocked.
*/
void
AddConn(RMPCONN *rconn)
{
	if (RmpConns != NULL)
		rconn->next = RmpConns;
	RmpConns = rconn;
}

/*
**  FindConn -- Find a connection in the linked list of connections.
**
**	We use the RMP (Ethernet) address as the basis for determining
**	if this is the same connection.  According to the Remote Maint
**	Protocol, we can only have one connection with any machine.
**
**	Parameters:
**		rconn - connection to be found.
**
**	Returns:
**		Matching connection from linked list or NULL if not found.
**
**	Side Effects:
**		None.
**
**	Warnings:
**		- This routine must be called with SIGHUP blocked.
*/
RMPCONN *
FindConn(RMPCONN *rconn)
{
	RMPCONN *rtmp;

	for (rtmp = RmpConns; rtmp != NULL; rtmp = rtmp->next)
		if (bcmp((char *)&rconn->rmp.hp_hdr.saddr[0],
		         (char *)&rtmp->rmp.hp_hdr.saddr[0], RMP_ADDRLEN) == 0)
			break;

	return(rtmp);
}

/*
**  RemoveConn -- Remove a connection from the linked list of connections.
**
**	Parameters:
**		rconn - connection to be removed.
**
**	Returns:
**		Nothing.
**
**	Side Effects:
**		- If found, an RMPCONN will cease to exist and it will
**		  be removed from the linked list.
**
**	Warnings:
**		- This routine must be called with SIGHUP blocked.
*/
void
RemoveConn(RMPCONN *rconn)
{
	RMPCONN *thisrconn, *lastrconn;

	if (RmpConns == rconn) {		/* easy case */
		RmpConns = RmpConns->next;
		FreeConn(rconn);
	} else {				/* must traverse linked list */
		lastrconn = RmpConns;			/* set back ptr */
		thisrconn = lastrconn->next;		/* set current ptr */
		while (thisrconn != NULL) {
			if (rconn == thisrconn) {		/* found it */
				lastrconn->next = thisrconn->next;
				FreeConn(thisrconn);
				break;
			}
			lastrconn = thisrconn;
			thisrconn = thisrconn->next;
		}
	}
}
