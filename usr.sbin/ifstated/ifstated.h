/*	$OpenBSD: ifstated.h,v 1.19 2017/08/20 17:49:29 rob Exp $	*/

/*
 * Copyright (c) 2004 Ryan McBride
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/queue.h>

struct ifsd_expression;
TAILQ_HEAD(ifsd_expression_list, ifsd_expression);

struct ifsd_ifstate {
	TAILQ_ENTRY(ifsd_ifstate)	 entries;
	struct ifsd_expression_list	 expressions;
	int				 ifstate;
#define IFSD_LINKUNKNOWN	0
#define IFSD_LINKDOWN		1
#define IFSD_LINKUP		2
	int				 prevstate;
	u_int32_t			 refcount;
	char				 ifname[IFNAMSIZ];
};

struct ifsd_external {
	TAILQ_ENTRY(ifsd_external)	 entries;
	struct event			 ev;
	struct ifsd_expression_list	 expressions;
	char				*command;
	int				 prevstatus;
	u_int32_t			 frequency;
	u_int32_t			 refcount;
	time_t				 lastexec;
	pid_t				 pid;
};

struct ifsd_action;
TAILQ_HEAD(ifsd_action_list, ifsd_action);

struct ifsd_action {
	TAILQ_ENTRY(ifsd_action)	 entries;
	struct ifsd_action		*parent;
	union {
		char			*command;
		struct ifsd_state	*nextstate;
		char			*statename;
		struct {
			struct ifsd_action_list	 actions;
			struct ifsd_expression	*expression;
		} c;
	} act;
	u_int32_t			 type;
#define IFSD_ACTION_COMMAND		1
#define IFSD_ACTION_CHANGESTATE		2
#define IFSD_ACTION_CONDITION		3
};

struct ifsd_expression {
	TAILQ_ENTRY(ifsd_expression)	 entries;
	TAILQ_ENTRY(ifsd_expression)	 eval;
	struct ifsd_expression		*parent;
	struct ifsd_action		*action;
	struct ifsd_expression		*left;
	struct ifsd_expression		*right;
	union {
		struct ifsd_ifstate		*ifstate;
		struct ifsd_external		*external;
	} u;
	int				 depth;
	u_int32_t			 type;
#define IFSD_OPER_AND		1
#define IFSD_OPER_OR		2
#define IFSD_OPER_NOT		3
#define IFSD_OPER_EXTERNAL	4
#define IFSD_OPER_IFSTATE	5
	u_int8_t			 truth;
};

TAILQ_HEAD(ifsd_ifstate_list, ifsd_ifstate);
TAILQ_HEAD(ifsd_external_list, ifsd_external);

struct ifsd_state {
	struct event			 ev;
	struct ifsd_ifstate_list	 interface_states;
	struct ifsd_external_list	 external_tests;
	TAILQ_ENTRY(ifsd_state)		 entries;
	struct ifsd_action		*init;
	struct ifsd_action		*body;
	time_t				 entered;
	char				*name;
};

TAILQ_HEAD(ifsd_state_list, ifsd_state);

struct ifsd_config {
	struct ifsd_state		 initstate;
	struct ifsd_state_list		 states;
	struct ifsd_state		*curstate;
	struct ifsd_state		*nextstate;
	u_int32_t			 opts;
#define IFSD_OPT_VERBOSE	0x00000001
#define IFSD_OPT_VERBOSE2	0x00000002
#define IFSD_OPT_NOACTION	0x00000004
	int				 maxdepth;
};

enum	{ IFSD_EVTIMER_ADD, IFSD_EVTIMER_DEL };
struct ifsd_config *parse_config(char *, int);
int	cmdline_symset(char *);
void	clear_config(struct ifsd_config *);
