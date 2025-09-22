/*
 * services/outbound_list.c - keep list of outbound serviced queries.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains functions to help a module keep track of the
 * queries it has outstanding to authoritative servers.
 */
#include "config.h"
#include <sys/time.h>
#include "services/outbound_list.h"
#include "services/outside_network.h"

void 
outbound_list_init(struct outbound_list* list)
{
	list->first = NULL;
}

void 
outbound_list_clear(struct outbound_list* list)
{
	struct outbound_entry *p, *np;
	p = list->first;
	while(p) {
		np = p->next;
		outnet_serviced_query_stop(p->qsent, p);
		/* in region, no free needed */
		p = np;
	}
	outbound_list_init(list);
}

void 
outbound_list_insert(struct outbound_list* list, struct outbound_entry* e)
{
	if(list->first)
		list->first->prev = e;
	e->next = list->first;
	e->prev = NULL;
	list->first = e;
}

void 
outbound_list_remove(struct outbound_list* list, struct outbound_entry* e)
{
	if(!e)
		return;
	outnet_serviced_query_stop(e->qsent, e);
	if(e->next)
		e->next->prev = e->prev;
	if(e->prev)
		e->prev->next = e->next;
	else	list->first = e->next;
	/* in region, no free needed */
}
