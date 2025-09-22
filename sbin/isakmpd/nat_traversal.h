/*	$OpenBSD: nat_traversal.h,v 1.4 2005/07/25 15:03:47 hshoexer Exp $	*/

/*
 * Copyright (c) 2004 Håkan Olsson.  All rights reserved.
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

#ifndef _NAT_TRAVERSAL_H_
#define _NAT_TRAVERSAL_H_

#define VID_DRAFT_V2	0
#define VID_DRAFT_V2_N	1
#define VID_DRAFT_V3	2
#define VID_RFC3947	3

struct nat_t_cap {
	int		 id;
	u_int32_t	 flags;
	const char	*text;
	char		*hash;
	size_t		 hashsize;
};

/*
 * Set if -T is given on the command line to disable NAT-T support.
 */
extern int	disable_nat_t;

void	nat_t_init(void);
int	nat_t_add_vendor_payloads(struct message *);
void	nat_t_check_vendor_payload(struct message *, struct payload *);
int	nat_t_exchange_add_nat_d(struct message *);
int	nat_t_exchange_check_nat_d(struct message *);
void	nat_t_setup_keepalive(struct sa *);

#endif /* _NAT_TRAVERSAL_H_ */
