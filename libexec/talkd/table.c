/*	$OpenBSD: table.c,v 1.20 2019/08/09 22:52:13 cheloha Exp $	*/

/*
 * Copyright (c) 1983 Regents of the University of California.
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
 */

/*
 * Routines to handle insertion, deletion, etc on the table
 * of requests kept by the daemon. Nothing fancy here, linear
 * search on a double-linked list. A time is kept with each
 * entry so that overly old invitations can be eliminated.
 *
 * Consider this a mis-guided attempt at modularity
 */
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <protocols/talkd.h>

#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "talkd.h"

#define MAX_ID 16000	/* << 2^15 so I don't have sign troubles */

struct	timeval tp;

typedef struct table_entry TABLE_ENTRY;

struct table_entry {
	CTL_MSG request;
	time_t	time;
	TAILQ_ENTRY(table_entry) list;
};
TAILQ_HEAD(, table_entry)	table;

static void	delete(TABLE_ENTRY *);

/*
 * Init the table
 */
void
init_table(void)
{
	TAILQ_INIT(&table);
}

/*
 * Look in the table for an invitation that matches the current
 * request looking for an invitation
 */
CTL_MSG *
find_match(CTL_MSG *request)
{
	TABLE_ENTRY *ptr, *next;
	time_t current_time;

	gettimeofday(&tp, NULL);
	current_time = tp.tv_sec;
	if (debug)
		print_request("find_match", request);
	for (ptr = TAILQ_FIRST(&table); ptr != NULL; ptr = next) {
		next = TAILQ_NEXT(ptr, list);
		if ((current_time - ptr->time) > MAX_LIFE) {
			/* the entry is too old */
			if (debug)
				print_request("deleting expired entry",
				    &ptr->request);
			delete(ptr);
			continue;
		}
		if (debug)
			print_request("", &ptr->request);
		if (ptr->request.type == LEAVE_INVITE &&
		    strcmp(request->l_name, ptr->request.r_name) == 0 &&
		    strcmp(request->r_name, ptr->request.l_name) == 0)
			return (&ptr->request);
	}
	if (debug)
		syslog(LOG_DEBUG, "find_match: not found");

	return (NULL);
}

/*
 * Look for an identical request, as opposed to a complimentary
 * one as find_match does
 */
CTL_MSG *
find_request(CTL_MSG *request)
{
	TABLE_ENTRY *ptr, *next;
	time_t current_time;

	gettimeofday(&tp, NULL);
	current_time = tp.tv_sec;
	/*
	 * See if this is a repeated message, and check for
	 * out of date entries in the table while we are it.
	 */
	if (debug)
		print_request("find_request", request);
	for (ptr = TAILQ_FIRST(&table); ptr != NULL; ptr = next) {
		next = TAILQ_NEXT(ptr, list);
		if ((current_time - ptr->time) > MAX_LIFE) {
			/* the entry is too old */
			if (debug)
				print_request("deleting expired entry",
				    &ptr->request);
			delete(ptr);
			continue;
		}
		if (debug)
			print_request("", &ptr->request);
		if (request->pid == ptr->request.pid &&
		    request->type == ptr->request.type &&
		    strcmp(request->r_name, ptr->request.r_name) == 0 &&
		    strcmp(request->l_name, ptr->request.l_name) == 0) {
			/* update the time if we 'touch' it */
			ptr->time = current_time;
			return (&ptr->request);
		}
	}
	return (NULL);
}

void
insert_table(CTL_MSG *request, CTL_RESPONSE *response)
{
	TABLE_ENTRY *ptr;
	time_t current_time;

	if (debug)
		print_request( "insert_table", request );
	gettimeofday(&tp, NULL);
	current_time = tp.tv_sec;
	request->id_num = new_id();
	response->id_num = htonl(request->id_num);
	/* insert a new entry into the top of the list */
	ptr = malloc(sizeof(TABLE_ENTRY));
	if (ptr == NULL) {
		syslog(LOG_ERR, "insert_table: Out of memory");
		_exit(1);
	}
	ptr->time = current_time;
	ptr->request = *request;
	TAILQ_INSERT_HEAD(&table, ptr, list);
}

/*
 * Generate a unique non-zero sequence number
 */
int
new_id(void)
{
	static int current_id = 0;

	current_id = (current_id + 1) % MAX_ID;
	/* 0 is reserved, helps to pick up bugs */
	if (current_id == 0)
		current_id = 1;
	return (current_id);
}

/*
 * Delete the invitation with id 'id_num'
 */
int
delete_invite(int id_num)
{
	TABLE_ENTRY *ptr;

	if (debug)
		syslog(LOG_DEBUG, "delete_invite(%d)", id_num);
	TAILQ_FOREACH(ptr, &table, list) {
		if (ptr->request.id_num == id_num)
			break;
		if (debug)
			print_request("", &ptr->request);
	}
	if (ptr != NULL) {
		delete(ptr);
		return (SUCCESS);
	}
	return (NOT_HERE);
}

/*
 * Classic delete from a double-linked list
 */
static void
delete(TABLE_ENTRY *ptr)
{

	if (debug)
		print_request("delete", &ptr->request);
	TAILQ_REMOVE(&table, ptr, list);
	free((char *)ptr);
}
