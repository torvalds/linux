
/*
 * netgraph.h
 *
 * Copyright (c) 1996-1999 Whistle Communications, Inc.
 * All rights reserved.
 * 
 * Subject to the following obligations and disclaimer of warranty, use and
 * redistribution of this software, in source or object code forms, with or
 * without modifications are expressly permitted by Whistle Communications;
 * provided, however, that:
 * 1. Any and all reproductions of the source or object code must include the
 *    copyright notice above and the following disclaimer of warranties; and
 * 2. No rights are granted, in any manner or form, to use Whistle
 *    Communications, Inc. trademarks, including the mark "WHISTLE
 *    COMMUNICATIONS" on advertising, endorsements, or otherwise except as
 *    such appears in the above copyright notice or in the software.
 * 
 * THIS SOFTWARE IS BEING PROVIDED BY WHISTLE COMMUNICATIONS "AS IS", AND
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, WHISTLE COMMUNICATIONS MAKES NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING THIS SOFTWARE,
 * INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
 * WHISTLE COMMUNICATIONS DOES NOT WARRANT, GUARANTEE, OR MAKE ANY
 * REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS OF THE USE OF THIS
 * SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY, RELIABILITY OR OTHERWISE.
 * IN NO EVENT SHALL WHISTLE COMMUNICATIONS BE LIABLE FOR ANY DAMAGES
 * RESULTING FROM OR ARISING OUT OF ANY USE OF THIS SOFTWARE, INCLUDING
 * WITHOUT LIMITATION, ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES, LOSS OF USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF WHISTLE COMMUNICATIONS IS ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * Author: Archie Cobbs <archie@whistle.com>
 *
 * $FreeBSD$
 * $Whistle: netgraph.h,v 1.7 1999/01/20 00:57:23 archie Exp $
 */

#ifndef _NETGRAPH_H_
#define _NETGRAPH_H_

#include <sys/types.h>
#include <netgraph/ng_message.h>

__BEGIN_DECLS
int	NgMkSockNode(const char *, int *, int *);
int	NgNameNode(int, const char *, const char *, ...) __printflike(3, 4);
int	NgSendMsg(int, const char *, int, int, const void *, size_t);
int	NgSendAsciiMsg(int, const char *, const char *, ...) __printflike(3, 4);
int	NgSendReplyMsg(int, const char *,
		const struct ng_mesg *, const void *, size_t);
int	NgRecvMsg(int, struct ng_mesg *, size_t, char *);
int	NgAllocRecvMsg(int, struct ng_mesg **, char *);
int	NgRecvAsciiMsg(int, struct ng_mesg *, size_t, char *);
int	NgAllocRecvAsciiMsg(int, struct ng_mesg **, char *);
int	NgSendData(int, const char *, const u_char *, size_t);
int	NgRecvData(int, u_char *, size_t, char *);
int	NgAllocRecvData(int, u_char **, char *);
int	NgSetDebug(int);
void	NgSetErrLog(void (*)(const char *fmt, ...),
	    void (*)(const char *fmt, ...));
__END_DECLS

#endif

