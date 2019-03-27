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
 * $FreeBSD$
 */

#ifndef __NSCD_AGENT_H__
#define __NSCD_AGENT_H__

/*
 * Agents are used to perform the actual lookups from the caching daemon.
 * There are two types of daemons: for common requests and for multipart
 * requests.
 * All agents are stored in the agents table, which is the singleton.
 */

enum agent_type {
    COMMON_AGENT = 0,
    MULTIPART_AGENT = 1
};

struct agent {
   	char	*name;
    	enum agent_type type;
};

struct common_agent {
    	struct agent	parent;
	int (*lookup_func)(const char *, size_t, char **, size_t *);
};

struct multipart_agent {
    	struct agent	parent;
	void *(*mp_init_func)(void);
    	int (*mp_lookup_func)(char **, size_t *, void *);
	void (*mp_destroy_func)(void *);
};

struct agent_table {
   	struct agent	**agents;
	size_t		agents_num;
};

struct agent_table *init_agent_table(void);
void register_agent(struct agent_table *, struct agent *);
struct agent *find_agent(struct agent_table *, const char *, enum agent_type);
void destroy_agent_table(struct agent_table *);

#endif
