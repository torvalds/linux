/* $OpenBSD: isakmp_cfg.h,v 1.5 2004/05/23 18:17:56 hshoexer Exp $	 */

/*
 * Copyright (c) 2001 Niklas Hallqvist.  All rights reserved.
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
 */

/*
 * This code was written under funding by Gatespace
 * (http://www.gatespace.com/).
 */

#ifndef _ISAKMP_CFG_H_
#define _ISAKMP_CFG_H_

#include <sys/queue.h>

struct isakmp_cfg_attr {
	LIST_ENTRY(isakmp_cfg_attr) link;
	u_int16_t       type;
	u_int8_t        attr_used;
	/* 8 bits just to be well-aligned.  */
	u_int8_t        ignore;
	size_t          length;
	void           *value;
};

struct message;

extern int      (*isakmp_cfg_initiator[])(struct message *);
extern int      (*isakmp_cfg_responder[])(struct message *);
extern int16_t  script_transaction[];

#endif				/* _ISAKMP_CFG_H_ */
