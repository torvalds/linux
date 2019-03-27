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
 *	from: @(#)rmpproto.c	8.1 (Berkeley) 6/4/93
 *
 * From: Utah Hdr: rmpproto.c 3.1 92/07/06
 * Author: Jeff Forys, University of Utah CSS
 */

#ifndef lint
#if 0
static const char sccsid[] = "@(#)rmpproto.c	8.1 (Berkeley) 6/4/93";
#endif
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/param.h>
#include <sys/time.h>
#include <netinet/in.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include "defs.h"

/*
**  ProcessPacket -- determine packet type and do what's required.
**
**	An RMP BOOT packet has been received.  Look at the type field
**	and process Boot Requests, Read Requests, and Boot Complete
**	packets.  Any other type will be dropped with a warning msg.
**
**	Parameters:
**		rconn - the new connection
**		client - list of files available to this host
**
**	Returns:
**		Nothing.
**
**	Side Effects:
**		- If this is a valid boot request, it will be added to
**		  the linked list of outstanding requests (RmpConns).
**		- If this is a valid boot complete, its associated
**		  entry in RmpConns will be deleted.
**		- Also, unless we run out of memory, a reply will be
**		  sent to the host that sent the packet.
*/
void
ProcessPacket(RMPCONN *rconn, CLIENT *client)
{
	struct rmp_packet *rmp;
	RMPCONN *rconnout;

	rmp = &rconn->rmp;		/* cache pointer to RMP packet */

	switch(rmp->r_type) {		/* do what we came here to do */
		case RMP_BOOT_REQ:		/* boot request */
			if ((rconnout = NewConn(rconn)) == NULL)
				return;

			/*
			 *  If the Session ID is 0xffff, this is a "probe"
			 *  packet and we do not want to add the connection
			 *  to the linked list of active connections.  There
			 *  are two types of probe packets, if the Sequence
			 *  Number is 0 they want to know our host name, o/w
			 *  they want the name of the file associated with
			 *  the number spec'd by the Sequence Number.
			 *
			 *  If this is an actual boot request, open the file
			 *  and send a reply.  If SendBootRepl() does not
			 *  return 0, add the connection to the linked list
			 *  of active connections, otherwise delete it since
			 *  an error was encountered.
			 */
			if (ntohs(rmp->r_brq.rmp_session) == RMP_PROBESID) {
				if (WORDZE(rmp->r_brq.rmp_seqno))
					(void) SendServerID(rconnout);
				else
					(void) SendFileNo(rmp, rconnout,
					                  client? client->files:
					                          BootFiles);
				FreeConn(rconnout);
			} else {
				if (SendBootRepl(rmp, rconnout,
				    client? client->files: BootFiles))
					AddConn(rconnout);
				else
					FreeConn(rconnout);
			}
			break;

		case RMP_BOOT_REPL:		/* boot reply (not valid) */
			syslog(LOG_WARNING, "%s: sent a boot reply",
			       EnetStr(rconn));
			break;

		case RMP_READ_REQ:		/* read request */
			/*
			 *  Send a portion of the boot file.
			 */
			(void) SendReadRepl(rconn);
			break;

		case RMP_READ_REPL:		/* read reply (not valid) */
			syslog(LOG_WARNING, "%s: sent a read reply",
			       EnetStr(rconn));
			break;

		case RMP_BOOT_DONE:		/* boot complete */
			/*
			 *  Remove the entry from the linked list of active
			 *  connections.
			 */
			(void) BootDone(rconn);
			break;

		default:			/* unknown RMP packet type */
			syslog(LOG_WARNING, "%s: unknown packet type (%u)",
			       EnetStr(rconn), rmp->r_type);
	}
}

/*
**  SendServerID -- send our host name to who ever requested it.
**
**	Parameters:
**		rconn - the reply packet to be formatted.
**
**	Returns:
**		1 on success, 0 on failure.
**
**	Side Effects:
**		none.
*/
int
SendServerID(RMPCONN *rconn)
{
	struct rmp_packet *rpl;
	char *src, *dst;
	u_int8_t *size;

	rpl = &rconn->rmp;			/* cache ptr to RMP packet */

	/*
	 *  Set up assorted fields in reply packet.
	 */
	rpl->r_brpl.rmp_type = RMP_BOOT_REPL;
	rpl->r_brpl.rmp_retcode = RMP_E_OKAY;
	ZEROWORD(rpl->r_brpl.rmp_seqno);
	rpl->r_brpl.rmp_session = 0;
	rpl->r_brpl.rmp_version = htons(RMP_VERSION);

	size = &rpl->r_brpl.rmp_flnmsize;	/* ptr to length of host name */

	/*
	 *  Copy our host name into the reply packet incrementing the
	 *  length as we go.  Stop at RMP_HOSTLEN or the first dot.
	 */
	src = MyHost;
	dst = (char *) &rpl->r_brpl.rmp_flnm;
	for (*size = 0; *size < RMP_HOSTLEN; (*size)++) {
		if (*src == '.' || *src == '\0')
			break;
		*dst++ = *src++;
	}

	rconn->rmplen = RMPBOOTSIZE(*size);	/* set packet length */

	return(SendPacket(rconn));		/* send packet */
}

/*
**  SendFileNo -- send the name of a bootable file to the requester.
**
**	Parameters:
**		req - RMP BOOT packet containing the request.
**		rconn - the reply packet to be formatted.
**		filelist - list of files available to the requester.
**
**	Returns:
**		1 on success, 0 on failure.
**
**	Side Effects:
**		none.
*/
int
SendFileNo(struct rmp_packet *req, RMPCONN *rconn, char *filelist[])
{
	struct rmp_packet *rpl;
	char *src, *dst;
	u_int8_t *size;
	int i;

	GETWORD(req->r_brpl.rmp_seqno, i);	/* SeqNo is really FileNo */
	rpl = &rconn->rmp;			/* cache ptr to RMP packet */

	/*
	 *  Set up assorted fields in reply packet.
	 */
	rpl->r_brpl.rmp_type = RMP_BOOT_REPL;
	PUTWORD(i, rpl->r_brpl.rmp_seqno);
	i--;
	rpl->r_brpl.rmp_session = 0;
	rpl->r_brpl.rmp_version = htons(RMP_VERSION);

	size = &rpl->r_brpl.rmp_flnmsize;	/* ptr to length of filename */
	*size = 0;				/* init length to zero */

	/*
	 *  Copy the file name into the reply packet incrementing the
	 *  length as we go.  Stop at end of string or when RMPBOOTDATA
	 *  characters have been copied.  Also, set return code to
	 *  indicate success or "no more files".
	 */
	if (i < C_MAXFILE && filelist[i] != NULL) {
		src = filelist[i];
		dst = (char *)&rpl->r_brpl.rmp_flnm;
		for (; *src && *size < RMPBOOTDATA; (*size)++) {
			if (*src == '\0')
				break;
			*dst++ = *src++;
		}
		rpl->r_brpl.rmp_retcode = RMP_E_OKAY;
	} else
		rpl->r_brpl.rmp_retcode = RMP_E_NODFLT;

	rconn->rmplen = RMPBOOTSIZE(*size);	/* set packet length */

	return(SendPacket(rconn));		/* send packet */
}

/*
**  SendBootRepl -- open boot file and respond to boot request.
**
**	Parameters:
**		req - RMP BOOT packet containing the request.
**		rconn - the reply packet to be formatted.
**		filelist - list of files available to the requester.
**
**	Returns:
**		1 on success, 0 on failure.
**
**	Side Effects:
**		none.
*/
int
SendBootRepl(struct rmp_packet *req, RMPCONN *rconn, char *filelist[])
{
	int retval;
	char *filename, filepath[RMPBOOTDATA+1];
	RMPCONN *oldconn;
	struct rmp_packet *rpl;
	char *src, *dst1, *dst2;
	u_int8_t i;

	/*
	 *  If another connection already exists, delete it since we
	 *  are obviously starting again.
	 */
	if ((oldconn = FindConn(rconn)) != NULL) {
		syslog(LOG_WARNING, "%s: dropping existing connection",
		       EnetStr(oldconn));
		RemoveConn(oldconn);
	}

	rpl = &rconn->rmp;			/* cache ptr to RMP packet */

	/*
	 *  Set up assorted fields in reply packet.
	 */
	rpl->r_brpl.rmp_type = RMP_BOOT_REPL;
	COPYWORD(req->r_brq.rmp_seqno, rpl->r_brpl.rmp_seqno);
	rpl->r_brpl.rmp_session = htons(GenSessID());
	rpl->r_brpl.rmp_version = htons(RMP_VERSION);
	rpl->r_brpl.rmp_flnmsize = req->r_brq.rmp_flnmsize;

	/*
	 *  Copy file name to `filepath' string, and into reply packet.
	 */
	src = &req->r_brq.rmp_flnm;
	dst1 = filepath;
	dst2 = &rpl->r_brpl.rmp_flnm;
	for (i = 0; i < req->r_brq.rmp_flnmsize; i++)
		*dst1++ = *dst2++ = *src++;
	*dst1 = '\0';

	/*
	 *  If we are booting HP-UX machines, their secondary loader will
	 *  ask for files like "/hp-ux".  As a security measure, we do not
	 *  allow boot files to lay outside the boot directory (unless they
	 *  are purposely link'd out.  So, make `filename' become the path-
	 *  stripped file name and spoof the client into thinking that it
	 *  really got what it wanted.
	 */
	filename = (filename = strrchr(filepath,'/'))? ++filename: filepath;

	/*
	 *  Check that this is a valid boot file name.
	 */
	for (i = 0; i < C_MAXFILE && filelist[i] != NULL; i++)
		if (STREQN(filename, filelist[i]))
			goto match;

	/*
	 *  Invalid boot file name, set error and send reply packet.
	 */
	rpl->r_brpl.rmp_retcode = RMP_E_NOFILE;
	retval = 0;
	goto sendpkt;

match:
	/*
	 *  This is a valid boot file.  Open the file and save the file
	 *  descriptor associated with this connection and set success
	 *  indication.  If the file couldnt be opened, set error:
	 *  	"no such file or dir" - RMP_E_NOFILE
	 *	"file table overflow" - RMP_E_BUSY
	 *	"too many open files" - RMP_E_BUSY
	 *	anything else         - RMP_E_OPENFILE
	 */
	if ((rconn->bootfd = open(filename, O_RDONLY, 0600)) < 0) {
		rpl->r_brpl.rmp_retcode = (errno == ENOENT)? RMP_E_NOFILE:
			(errno == EMFILE || errno == ENFILE)? RMP_E_BUSY:
			RMP_E_OPENFILE;
		retval = 0;
	} else {
		rpl->r_brpl.rmp_retcode = RMP_E_OKAY;
		retval = 1;
	}

sendpkt:
	syslog(LOG_INFO, "%s: request to boot %s (%s)",
	       EnetStr(rconn), filename, retval? "granted": "denied");

	rconn->rmplen = RMPBOOTSIZE(rpl->r_brpl.rmp_flnmsize);

	return (retval & SendPacket(rconn));
}

/*
**  SendReadRepl -- send a portion of the boot file to the requester.
**
**	Parameters:
**		rconn - the reply packet to be formatted.
**
**	Returns:
**		1 on success, 0 on failure.
**
**	Side Effects:
**		none.
*/
int
SendReadRepl(RMPCONN *rconn)
{
	int retval = 0;
	RMPCONN *oldconn;
	struct rmp_packet *rpl, *req;
	int size = 0;
	int madeconn = 0;

	/*
	 *  Find the old connection.  If one doesn't exist, create one only
	 *  to return the error code.
	 */
	if ((oldconn = FindConn(rconn)) == NULL) {
		if ((oldconn = NewConn(rconn)) == NULL)
			return(0);
		syslog(LOG_ERR, "SendReadRepl: no active connection (%s)",
		       EnetStr(rconn));
		madeconn++;
	}

	req = &rconn->rmp;		/* cache ptr to request packet */
	rpl = &oldconn->rmp;		/* cache ptr to reply packet */

	if (madeconn) {			/* no active connection above; abort */
		rpl->r_rrpl.rmp_retcode = RMP_E_ABORT;
		retval = 1;
		goto sendpkt;
	}

	/*
	 *  Make sure Session ID's match.
	 */
	if (ntohs(req->r_rrq.rmp_session) !=
	    ((rpl->r_type == RMP_BOOT_REPL)? ntohs(rpl->r_brpl.rmp_session):
	                                     ntohs(rpl->r_rrpl.rmp_session))) {
		syslog(LOG_ERR, "SendReadRepl: bad session id (%s)",
		       EnetStr(rconn));
		rpl->r_rrpl.rmp_retcode = RMP_E_BADSID;
		retval = 1;
		goto sendpkt;
	}

	/*
	 *  If the requester asks for more data than we can fit,
	 *  silently clamp the request size down to RMPREADDATA.
	 *
	 *  N.B. I do not know if this is "legal", however it seems
	 *  to work.  This is necessary for bpfwrite() on machines
	 *  with MCLBYTES less than 1514.
	 */
	if (ntohs(req->r_rrq.rmp_size) > RMPREADDATA)
		req->r_rrq.rmp_size = htons(RMPREADDATA);

	/*
	 *  Position read head on file according to info in request packet.
	 */
	GETWORD(req->r_rrq.rmp_offset, size);
	if (lseek(oldconn->bootfd, (off_t)size, SEEK_SET) < 0) {
		syslog(LOG_ERR, "SendReadRepl: lseek: %m (%s)",
		       EnetStr(rconn));
		rpl->r_rrpl.rmp_retcode = RMP_E_ABORT;
		retval = 1;
		goto sendpkt;
	}

	/*
	 *  Read data directly into reply packet.
	 */
	if ((size = read(oldconn->bootfd, &rpl->r_rrpl.rmp_data,
	                 (int) ntohs(req->r_rrq.rmp_size))) <= 0) {
		if (size < 0) {
			syslog(LOG_ERR, "SendReadRepl: read: %m (%s)",
			       EnetStr(rconn));
			rpl->r_rrpl.rmp_retcode = RMP_E_ABORT;
		} else {
			rpl->r_rrpl.rmp_retcode = RMP_E_EOF;
		}
		retval = 1;
		goto sendpkt;
	}

	/*
	 *  Set success indication.
	 */
	rpl->r_rrpl.rmp_retcode = RMP_E_OKAY;

sendpkt:
	/*
	 *  Set up assorted fields in reply packet.
	 */
	rpl->r_rrpl.rmp_type = RMP_READ_REPL;
	COPYWORD(req->r_rrq.rmp_offset, rpl->r_rrpl.rmp_offset);
	rpl->r_rrpl.rmp_session = req->r_rrq.rmp_session;

	oldconn->rmplen = RMPREADSIZE(size);	/* set size of packet */

	retval &= SendPacket(oldconn);		/* send packet */

	if (madeconn)				/* clean up after ourself */
		FreeConn(oldconn);

	return (retval);
}

/*
**  BootDone -- free up memory allocated for a connection.
**
**	Parameters:
**		rconn - incoming boot complete packet.
**
**	Returns:
**		1 on success, 0 on failure.
**
**	Side Effects:
**		none.
*/
int
BootDone(RMPCONN *rconn)
{
	RMPCONN *oldconn;
	struct rmp_packet *rpl;

	/*
	 *  If we can't find the connection, ignore the request.
	 */
	if ((oldconn = FindConn(rconn)) == NULL) {
		syslog(LOG_ERR, "BootDone: no existing connection (%s)",
		       EnetStr(rconn));
		return(0);
	}

	rpl = &oldconn->rmp;			/* cache ptr to RMP packet */

	/*
	 *  Make sure Session ID's match.
	 */
	if (ntohs(rconn->rmp.r_rrq.rmp_session) !=
	    ((rpl->r_type == RMP_BOOT_REPL)? ntohs(rpl->r_brpl.rmp_session):
	                                    ntohs(rpl->r_rrpl.rmp_session))) {
		syslog(LOG_ERR, "BootDone: bad session id (%s)",
		       EnetStr(rconn));
		return(0);
	}

	RemoveConn(oldconn);			/* remove connection */

	syslog(LOG_INFO, "%s: boot complete", EnetStr(rconn));

	return(1);
}

/*
**  SendPacket -- send an RMP packet to a remote host.
**
**	Parameters:
**		rconn - packet to be sent.
**
**	Returns:
**		1 on success, 0 on failure.
**
**	Side Effects:
**		none.
*/
int
SendPacket(RMPCONN *rconn)
{
	/*
	 *  Set Ethernet Destination address to Source (BPF and the enet
	 *  driver will take care of getting our source address set).
	 */
	memmove((char *)&rconn->rmp.hp_hdr.daddr[0],
	        (char *)&rconn->rmp.hp_hdr.saddr[0], RMP_ADDRLEN);
	rconn->rmp.hp_hdr.len = htons(rconn->rmplen - sizeof(struct hp_hdr));

	/*
	 *  Reverse 802.2/HP Extended Source & Destination Access Pts.
	 */
	rconn->rmp.hp_llc.dxsap = htons(HPEXT_SXSAP);
	rconn->rmp.hp_llc.sxsap = htons(HPEXT_DXSAP);

	/*
	 *  Last time this connection was active.
	 */
	(void)gettimeofday(&rconn->tstamp, NULL);

	if (DbgFp != NULL)			/* display packet */
		DispPkt(rconn,DIR_SENT);

	/*
	 *  Send RMP packet to remote host.
	 */
	return(BpfWrite(rconn));
}
