/*	$OpenBSD: connect.c,v 1.8 2016/01/07 21:37:53 mestre Exp $	*/
/*	$NetBSD: connect.c,v 1.3 1997/10/11 08:13:40 lukem Exp $	*/
/*
 * Copyright (c) 1983-2003, Regents of the University of California.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are 
 * met:
 * 
 * + Redistributions of source code must retain the above copyright 
 *   notice, this list of conditions and the following disclaimer.
 * + Redistributions in binary form must reproduce the above copyright 
 *   notice, this list of conditions and the following disclaimer in the 
 *   documentation and/or other materials provided with the distribution.
 * + Neither the name of the University of California, San Francisco nor 
 *   the names of its contributors may be used to endorse or promote 
 *   products derived from this software without specific prior written 
 *   permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS 
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED 
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A 
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <string.h>
#include <unistd.h>

#include "hunt.h"
#include "client.h"

void
do_connect(char *name, u_int8_t team, u_int32_t enter_status)
{
	u_int32_t	uid;
	u_int32_t	mode;
	char *		Ttyname;
	char		buf[NAMELEN];

	if (Send_message != NULL)
		mode = C_MESSAGE;
	else if (Am_monitor)
		mode = C_MONITOR;
	else
		mode = C_PLAYER;

	Ttyname = ttyname(STDOUT_FILENO);
	if (Ttyname == NULL)
		Ttyname = "not a tty";
	memset(buf, '\0', sizeof buf);
	(void) strlcpy(buf, Ttyname, sizeof buf);

	uid = htonl(getuid());
	enter_status = htonl(enter_status);
	mode = htonl(mode);

	(void) write(Socket, &uid, sizeof uid);
	(void) write(Socket, name, NAMELEN);
	(void) write(Socket, &team, sizeof team);
	(void) write(Socket, &enter_status, sizeof enter_status);
	(void) write(Socket, buf, NAMELEN);
	(void) write(Socket, &mode, sizeof mode);
}
