/*
 * services/outbound_list.h - keep list of outbound serviced queries.
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
#ifndef SERVICES_OUTBOUND_LIST_H
#define SERVICES_OUTBOUND_LIST_H
struct outbound_entry;
struct serviced_query;
struct module_qstate;

/**
 * The outbound list. This structure is part of the module specific query
 * state.
 */
struct outbound_list {
	/** The linked list of outbound query entries. */
	struct outbound_entry* first;
};

/**
 * Outbound list entry. A serviced query sent by a module processing the
 * query from the qstate. Double linked list to aid removal.
 */
struct outbound_entry {
	/** next in list */
	struct outbound_entry* next;
	/** prev in list */
	struct outbound_entry* prev;
	/** The query that was sent out */
	struct serviced_query* qsent;
	/** the module query state that sent it */
	struct module_qstate* qstate;
};

/**
 * Init the user allocated outbound list structure
 * @param list: the list structure.
 */
void outbound_list_init(struct outbound_list* list);

/**
 * Clear the user owner outbound list structure.
 * Deletes serviced queries.
 * @param list: the list structure. It is cleared, but the list struct itself
 * 	is callers responsibility to delete.
 */
void outbound_list_clear(struct outbound_list* list);

/**
 * Insert new entry into the list. Caller must allocate the entry with malloc.
 * qstate and qsent are set by caller.
 * @param list: the list to add to.
 * @param e: entry to add, it is only half initialised at call start, fully
 *	initialised at call end.
 */
void outbound_list_insert(struct outbound_list* list, 
	struct outbound_entry* e);

/**
 * Remove an entry from the list, and deletes it. 
 * Deletes serviced query in the entry.
 * @param list: the list to remove from.
 * @param e: the entry to remove.
 */
void outbound_list_remove(struct outbound_list* list, 
	struct outbound_entry* e);

#endif /* SERVICES_OUTBOUND_LIST_H */
