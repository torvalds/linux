/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 *
 * $FreeBSD$
 */

/* This file defines helper functions */

#ifndef	_VMCI_KERNEL_IF_H_
#define	_VMCI_KERNEL_IF_H_

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/sema.h>

#include "vmci_defs.h"

#define VMCI_MEMORY_NORMAL		0x0
#define VMCI_MEMORY_ATOMIC		0x1

#define vmci_list(_l)			LIST_HEAD(, _l)
#define vmci_list_item(_l)		LIST_ENTRY(_l)
#define vmci_list_init(_l)		LIST_INIT(_l)
#define vmci_list_empty(_l)		LIST_EMPTY(_l)
#define vmci_list_first(_l)		LIST_FIRST(_l)
#define vmci_list_next(e, f)		LIST_NEXT(e, f)
#define vmci_list_insert(_l, _e, n)	LIST_INSERT_HEAD(_l, _e, n)
#define vmci_list_remove(_e, n)		LIST_REMOVE(_e, n)
#define vmci_list_scan(v, _l, n)	LIST_FOREACH(v, _l, n)
#define vmci_list_scan_safe(_e, _l, n, t)				\
	LIST_FOREACH_SAFE(_e, _l, n, t)
#define vmci_list_swap(_l1, _l2, t, f)	LIST_SWAP(_l1, _l2, t, f)

typedef unsigned short int vmci_io_port;
typedef int vmci_io_handle;

void	vmci_read_port_bytes(vmci_io_handle handle, vmci_io_port port,
	    uint8_t *buffer, size_t buffer_length);

typedef struct mtx vmci_lock;
int	vmci_init_lock(vmci_lock *lock, char *name);
void	vmci_cleanup_lock(vmci_lock *lock);
void	vmci_grab_lock(vmci_lock *lock);
void	vmci_release_lock(vmci_lock *lock);
void	vmci_grab_lock_bh(vmci_lock *lock);
void	vmci_release_lock_bh(vmci_lock *lock);

void	*vmci_alloc_kernel_mem(size_t size, int flags);
void	vmci_free_kernel_mem(void *ptr, size_t size);

typedef struct sema vmci_event;
typedef int (*vmci_event_release_cb)(void *client_data);
void	vmci_create_event(vmci_event *event);
void	vmci_destroy_event(vmci_event *event);
void	vmci_signal_event(vmci_event *event);
void	vmci_wait_on_event(vmci_event *event, vmci_event_release_cb release_cb,
	    void *client_data);
bool	vmci_wait_on_event_interruptible(vmci_event *event,
	    vmci_event_release_cb release_cb, void *client_data);

typedef void (vmci_work_fn)(void *data);
bool	vmci_can_schedule_delayed_work(void);
int	vmci_schedule_delayed_work(vmci_work_fn *work_fn, void *data);
void	vmci_delayed_work_cb(void *context, int data);

typedef struct mtx vmci_mutex;
int	vmci_mutex_init(vmci_mutex *mutex, char *name);
void	vmci_mutex_destroy(vmci_mutex *mutex);
void	vmci_mutex_acquire(vmci_mutex *mutex);
void	vmci_mutex_release(vmci_mutex *mutex);

void	*vmci_alloc_queue(uint64_t size, uint32_t flags);
void	vmci_free_queue(void *q, uint64_t size);

typedef PPN *vmci_ppn_list;
struct ppn_set {
	uint64_t	num_produce_pages;
	uint64_t	num_consume_pages;
	vmci_ppn_list	produce_ppns;
	vmci_ppn_list	consume_ppns;
	bool		initialized;
};

int	vmci_alloc_ppn_set(void *produce_q, uint64_t num_produce_pages,
	    void *consume_q, uint64_t num_consume_pages,
	    struct ppn_set *ppn_set);
void	vmci_free_ppn_set(struct ppn_set *ppn_set);
int	vmci_populate_ppn_list(uint8_t *call_buf, const struct ppn_set *ppnset);

#endif /* !_VMCI_KERNEL_IF_H_ */
