/*
 * include/net/tipc/tipc.h: Main include file for TIPC users
 * 
 * Copyright (c) 2003-2006, Ericsson AB
 * Copyright (c) 2005,2010 Wind River Systems
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _NET_TIPC_H_
#define _NET_TIPC_H_

#ifdef __KERNEL__

#include <linux/tipc.h>
#include <linux/skbuff.h>

/* 
 * Native API
 */

/*
 * TIPC operating mode routines
 */

#define TIPC_NOT_RUNNING  0
#define TIPC_NODE_MODE    1
#define TIPC_NET_MODE     2

typedef void (*tipc_mode_event)(void *usr_handle, int mode, u32 addr);

int tipc_attach(unsigned int *userref, tipc_mode_event, void *usr_handle);

void tipc_detach(unsigned int userref);

/*
 * TIPC port manipulation routines
 */

typedef void (*tipc_msg_err_event) (void *usr_handle,
				    u32 portref,
				    struct sk_buff **buf,
				    unsigned char const *data,
				    unsigned int size,
				    int reason, 
				    struct tipc_portid const *attmpt_destid);

typedef void (*tipc_named_msg_err_event) (void *usr_handle,
					  u32 portref,
					  struct sk_buff **buf,
					  unsigned char const *data,
					  unsigned int size,
					  int reason, 
					  struct tipc_name_seq const *attmpt_dest);

typedef void (*tipc_conn_shutdown_event) (void *usr_handle,
					  u32 portref,
					  struct sk_buff **buf,
					  unsigned char const *data,
					  unsigned int size,
					  int reason);

typedef void (*tipc_msg_event) (void *usr_handle,
				u32 portref,
				struct sk_buff **buf,
				unsigned char const *data,
				unsigned int size,
				unsigned int importance, 
				struct tipc_portid const *origin);

typedef void (*tipc_named_msg_event) (void *usr_handle,
				      u32 portref,
				      struct sk_buff **buf,
				      unsigned char const *data,
				      unsigned int size,
				      unsigned int importance, 
				      struct tipc_portid const *orig,
				      struct tipc_name_seq const *dest);

typedef void (*tipc_conn_msg_event) (void *usr_handle,
				     u32 portref,
				     struct sk_buff **buf,
				     unsigned char const *data,
				     unsigned int size);

typedef void (*tipc_continue_event) (void *usr_handle, 
				     u32 portref);

int tipc_createport(unsigned int tipc_user, 
		    void *usr_handle, 
		    unsigned int importance, 
		    tipc_msg_err_event error_cb, 
		    tipc_named_msg_err_event named_error_cb, 
		    tipc_conn_shutdown_event conn_error_cb, 
		    tipc_msg_event message_cb, 
		    tipc_named_msg_event named_message_cb, 
		    tipc_conn_msg_event conn_message_cb, 
		    tipc_continue_event continue_event_cb,
		    u32 *portref);

int tipc_deleteport(u32 portref);

int tipc_ownidentity(u32 portref, struct tipc_portid *port);

int tipc_portimportance(u32 portref, unsigned int *importance);
int tipc_set_portimportance(u32 portref, unsigned int importance);

int tipc_portunreliable(u32 portref, unsigned int *isunreliable);
int tipc_set_portunreliable(u32 portref, unsigned int isunreliable);

int tipc_portunreturnable(u32 portref, unsigned int *isunreturnable);
int tipc_set_portunreturnable(u32 portref, unsigned int isunreturnable);

int tipc_publish(u32 portref, unsigned int scope, 
		 struct tipc_name_seq const *name_seq);
int tipc_withdraw(u32 portref, unsigned int scope,
		  struct tipc_name_seq const *name_seq);

int tipc_connect2port(u32 portref, struct tipc_portid const *port);

int tipc_disconnect(u32 portref);

int tipc_shutdown(u32 ref);

/*
 * TIPC messaging routines
 */

#define TIPC_PORT_IMPORTANCE 100	/* send using current port setting */


int tipc_send(u32 portref,
	      unsigned int num_sect,
	      struct iovec const *msg_sect);

int tipc_send2name(u32 portref, 
		   struct tipc_name const *name, 
		   u32 domain,
		   unsigned int num_sect,
		   struct iovec const *msg_sect);

int tipc_send2port(u32 portref,
		   struct tipc_portid const *dest,
		   unsigned int num_sect,
		   struct iovec const *msg_sect);

int tipc_send_buf2port(u32 portref,
		       struct tipc_portid const *dest,
		       struct sk_buff *buf,
		       unsigned int dsz);

int tipc_multicast(u32 portref, 
		   struct tipc_name_seq const *seq, 
		   u32 domain,	/* currently unused */
		   unsigned int section_count,
		   struct iovec const *msg);
#endif

#endif
