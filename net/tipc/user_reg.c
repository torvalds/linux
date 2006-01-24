/*
 * net/tipc/user_reg.c: TIPC user registry code
 * 
 * Copyright (c) 2000-2006, Ericsson AB
 * Copyright (c) 2004-2005, Wind River Systems
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

#include "core.h"
#include "user_reg.h"

/*
 * TIPC user registry keeps track of users of the tipc_port interface.
 *
 * The registry utilizes an array of "TIPC user" entries; 
 * a user's ID is the index of their associated array entry.
 * Array entry 0 is not used, so userid 0 is not valid;
 * TIPC sometimes uses this value to denote an anonymous user.
 * The list of free entries is initially chained from last entry to entry 1.
 */

/**
 * struct tipc_user - registered TIPC user info
 * @next: index of next free registry entry (or -1 for an allocated entry)
 * @callback: ptr to routine to call when TIPC mode changes (NULL if none)
 * @usr_handle: user-defined value passed to callback routine 
 * @ports: list of user ports owned by the user
 */

struct tipc_user {
	int next;
	tipc_mode_event callback;
	void *usr_handle;
	struct list_head ports;
};

#define MAX_USERID 64
#define USER_LIST_SIZE ((MAX_USERID + 1) * sizeof(struct tipc_user))

static struct tipc_user *users = 0;
static u32 next_free_user = MAX_USERID + 1;
static spinlock_t reg_lock = SPIN_LOCK_UNLOCKED;

/**
 * reg_init - create TIPC user registry (but don't activate it)
 * 
 * If registry has been pre-initialized it is left "as is".
 * NOTE: This routine may be called when TIPC is inactive.
 */

static int reg_init(void)
{
	u32 i;
	
	spin_lock_bh(&reg_lock);
	if (!users) {
		users = (struct tipc_user *)kmalloc(USER_LIST_SIZE, GFP_ATOMIC);
		if (users) {
			memset(users, 0, USER_LIST_SIZE);
			for (i = 1; i <= MAX_USERID; i++) {
				users[i].next = i - 1;
			}
			next_free_user = MAX_USERID;
		}
	}
	spin_unlock_bh(&reg_lock);
	return users ? TIPC_OK : -ENOMEM;
}

/**
 * reg_callback - inform TIPC user about current operating mode
 */

static void reg_callback(struct tipc_user *user_ptr)
{
	tipc_mode_event cb;
	void *arg;

	spin_lock_bh(&reg_lock);
	cb = user_ptr->callback;
	arg = user_ptr->usr_handle;
	spin_unlock_bh(&reg_lock);

	if (cb)
		cb(arg, tipc_mode, tipc_own_addr);
}

/**
 * tipc_reg_start - activate TIPC user registry
 */

int tipc_reg_start(void)
{
	u32 u;
	int res;

	if ((res = reg_init()))
		return res;

	for (u = 1; u <= MAX_USERID; u++) {
		if (users[u].callback)
			tipc_k_signal((Handler)reg_callback,
				      (unsigned long)&users[u]);
	}
	return TIPC_OK;
}

/**
 * tipc_reg_stop - shut down & delete TIPC user registry
 */

void tipc_reg_stop(void)
{               
	int id;

	if (!users)
		return;

	for (id = 1; id <= MAX_USERID; id++) {
		if (users[id].callback)
			reg_callback(&users[id]);
	}
	kfree(users);
	users = 0;
}

/**
 * tipc_attach - register a TIPC user
 *
 * NOTE: This routine may be called when TIPC is inactive.
 */

int tipc_attach(u32 *userid, tipc_mode_event cb, void *usr_handle)
{
	struct tipc_user *user_ptr;

	if ((tipc_mode == TIPC_NOT_RUNNING) && !cb)
		return -ENOPROTOOPT;
	if (!users)
		reg_init();

	spin_lock_bh(&reg_lock);
	if (!next_free_user) {
		spin_unlock_bh(&reg_lock);
		return -EBUSY;
	}
	user_ptr = &users[next_free_user];
	*userid = next_free_user;
	next_free_user = user_ptr->next;
	user_ptr->next = -1; 
	spin_unlock_bh(&reg_lock);

	user_ptr->callback = cb;
	user_ptr->usr_handle = usr_handle;
	INIT_LIST_HEAD(&user_ptr->ports);
	atomic_inc(&tipc_user_count);
	
	if (cb && (tipc_mode != TIPC_NOT_RUNNING))
		tipc_k_signal((Handler)reg_callback, (unsigned long)user_ptr);
	return TIPC_OK;
}

/**
 * tipc_detach - deregister a TIPC user
 */

void tipc_detach(u32 userid)
{
	struct tipc_user *user_ptr;
	struct list_head ports_temp;
	struct user_port *up_ptr, *temp_up_ptr;

	if ((userid == 0) || (userid > MAX_USERID))
		return;

	spin_lock_bh(&reg_lock);
	if ((!users) || (users[userid].next >= 0)) {
		spin_unlock_bh(&reg_lock);
		return;
	}

	user_ptr = &users[userid];
        user_ptr->callback = NULL;              
	INIT_LIST_HEAD(&ports_temp);
        list_splice(&user_ptr->ports, &ports_temp);
	user_ptr->next = next_free_user;
	next_free_user = userid;
	spin_unlock_bh(&reg_lock);

	atomic_dec(&tipc_user_count);

        list_for_each_entry_safe(up_ptr, temp_up_ptr, &ports_temp, uport_list) {
		tipc_deleteport(up_ptr->ref);
	}
}

/**
 * tipc_reg_add_port - register a user's driver port
 */

int tipc_reg_add_port(struct user_port *up_ptr)
{
	struct tipc_user *user_ptr;

	if (up_ptr->user_ref == 0)
		return TIPC_OK;
	if (up_ptr->user_ref > MAX_USERID)
		return -EINVAL;
	if ((tipc_mode == TIPC_NOT_RUNNING) || !users )
		return -ENOPROTOOPT;

	spin_lock_bh(&reg_lock);
	user_ptr = &users[up_ptr->user_ref];
	list_add(&up_ptr->uport_list, &user_ptr->ports);
	spin_unlock_bh(&reg_lock);
	return TIPC_OK;
}

/**
 * tipc_reg_remove_port - deregister a user's driver port
 */

int tipc_reg_remove_port(struct user_port *up_ptr)
{
	if (up_ptr->user_ref == 0)
		return TIPC_OK;
	if (up_ptr->user_ref > MAX_USERID)
		return -EINVAL;
	if (!users )
		return -ENOPROTOOPT;

	spin_lock_bh(&reg_lock);
	list_del_init(&up_ptr->uport_list);
	spin_unlock_bh(&reg_lock);
	return TIPC_OK;
}

