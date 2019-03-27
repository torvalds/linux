/*-
 * Copyright (c) 2005 Michael Bushkov <bushman@rsu.ru>
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "debug.h"

static int
agent_cmp_func(const void *a1, const void *a2)
{
   	struct agent const *ap1 = *((struct agent const **)a1);
	struct agent const *ap2 = *((struct agent const **)a2);
	int res;

	res = strcmp(ap1->name, ap2->name);
	if (res == 0) {
		if (ap1->type == ap2->type)
			res = 0;
		else if (ap1->type < ap2->type)
			res = -1;
		else
			res = 1;
	}

	return (res);
}

struct agent_table *
init_agent_table(void)
{
   	struct agent_table	*retval;

	TRACE_IN(init_agent_table);
	retval = calloc(1, sizeof(*retval));
	assert(retval != NULL);

	TRACE_OUT(init_agent_table);
	return (retval);
}

void
register_agent(struct agent_table *at, struct agent *a)
{
	struct agent **new_agents;
    	size_t new_agents_num;

	TRACE_IN(register_agent);
	assert(at != NULL);
	assert(a != NULL);
	new_agents_num = at->agents_num + 1;
	new_agents = malloc(sizeof(*new_agents) *
		new_agents_num);
	assert(new_agents != NULL);
	memcpy(new_agents, at->agents, at->agents_num * sizeof(struct agent *));
	new_agents[new_agents_num - 1] = a;
	qsort(new_agents, new_agents_num, sizeof(struct agent *),
		agent_cmp_func);

	free(at->agents);
	at->agents = new_agents;
	at->agents_num = new_agents_num;
    	TRACE_OUT(register_agent);
}

struct agent *
find_agent(struct agent_table *at, const char *name, enum agent_type type)
{
	struct agent **res;
	struct agent model, *model_p;

	TRACE_IN(find_agent);
	model.name = (char *)name;
	model.type = type;
	model_p = &model;
	res = bsearch(&model_p, at->agents, at->agents_num,
		sizeof(struct agent *), agent_cmp_func);

	TRACE_OUT(find_agent);
	return ( res == NULL ? NULL : *res);
}

void
destroy_agent_table(struct agent_table *at)
{
    	size_t i;

	TRACE_IN(destroy_agent_table);
	assert(at != NULL);
	for (i = 0; i < at->agents_num; ++i) {
		free(at->agents[i]->name);
		free(at->agents[i]);
	}

	free(at->agents);
	free(at);
	TRACE_OUT(destroy_agent_table);
}
