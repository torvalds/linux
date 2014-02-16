/*
 * net/tipc/handler.c: TIPC signal handling
 *
 * Copyright (c) 2000-2006, Ericsson AB
 * Copyright (c) 2005, Wind River Systems
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

struct queue_item {
	struct list_head next_signal;
	void (*handler) (unsigned long);
	unsigned long data;
};

static struct kmem_cache *tipc_queue_item_cache;
static struct list_head signal_queue_head;
static DEFINE_SPINLOCK(qitem_lock);
static int handler_enabled __read_mostly;

static void process_signal_queue(unsigned long dummy);

static DECLARE_TASKLET_DISABLED(tipc_tasklet, process_signal_queue, 0);


unsigned int tipc_k_signal(Handler routine, unsigned long argument)
{
	struct queue_item *item;

	spin_lock_bh(&qitem_lock);
	if (!handler_enabled) {
		pr_err("Signal request ignored by handler\n");
		spin_unlock_bh(&qitem_lock);
		return -ENOPROTOOPT;
	}

	item = kmem_cache_alloc(tipc_queue_item_cache, GFP_ATOMIC);
	if (!item) {
		pr_err("Signal queue out of memory\n");
		spin_unlock_bh(&qitem_lock);
		return -ENOMEM;
	}
	item->handler = routine;
	item->data = argument;
	list_add_tail(&item->next_signal, &signal_queue_head);
	spin_unlock_bh(&qitem_lock);
	tasklet_schedule(&tipc_tasklet);
	return 0;
}

static void process_signal_queue(unsigned long dummy)
{
	struct queue_item *__volatile__ item;
	struct list_head *l, *n;

	spin_lock_bh(&qitem_lock);
	list_for_each_safe(l, n, &signal_queue_head) {
		item = list_entry(l, struct queue_item, next_signal);
		list_del(&item->next_signal);
		spin_unlock_bh(&qitem_lock);
		item->handler(item->data);
		spin_lock_bh(&qitem_lock);
		kmem_cache_free(tipc_queue_item_cache, item);
	}
	spin_unlock_bh(&qitem_lock);
}

int tipc_handler_start(void)
{
	tipc_queue_item_cache =
		kmem_cache_create("tipc_queue_items", sizeof(struct queue_item),
				  0, SLAB_HWCACHE_ALIGN, NULL);
	if (!tipc_queue_item_cache)
		return -ENOMEM;

	INIT_LIST_HEAD(&signal_queue_head);
	tasklet_enable(&tipc_tasklet);
	handler_enabled = 1;
	return 0;
}

void tipc_handler_stop(void)
{
	struct list_head *l, *n;
	struct queue_item *item;

	spin_lock_bh(&qitem_lock);
	if (!handler_enabled) {
		spin_unlock_bh(&qitem_lock);
		return;
	}
	handler_enabled = 0;
	spin_unlock_bh(&qitem_lock);

	tasklet_kill(&tipc_tasklet);

	spin_lock_bh(&qitem_lock);
	list_for_each_safe(l, n, &signal_queue_head) {
		item = list_entry(l, struct queue_item, next_signal);
		list_del(&item->next_signal);
		kmem_cache_free(tipc_queue_item_cache, item);
	}
	spin_unlock_bh(&qitem_lock);

	kmem_cache_destroy(tipc_queue_item_cache);
}
