/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Isilon Inc http://www.isilon.com/
 * Authors: Doug Rabson <dfr@rabson.org>
 * Developed with Red Inc: Alfred Perlstein <alfred@freebsd.org>
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

#ifndef _NLM_NLM_H_
#define _NLM_NLM_H_

#ifdef _KERNEL

#ifdef _SYS_MALLOC_H_
MALLOC_DECLARE(M_NLM);
#endif

/*
 * This value is added to host system IDs when recording NFS client
 * locks in the local lock manager.
 */
#define NLM_SYSID_CLIENT	0x1000000

struct nlm_host;
struct vnode;

extern struct timeval nlm_zero_tv;
extern int nlm_nsm_state;

/*
 * Make a struct netobj.
 */ 
extern void nlm_make_netobj(struct netobj *dst, caddr_t srt,
    size_t srcsize, struct malloc_type *type);

/*
 * Copy a struct netobj.
 */ 
extern void nlm_copy_netobj(struct netobj *dst, struct netobj *src,
    struct malloc_type *type);

/*
 * Search for an existing NLM host that matches the given name
 * (typically the caller_name element of an nlm4_lock).  If none is
 * found, create a new host. If 'addr' is non-NULL, record the remote
 * address of the host so that we can call it back for async
 * responses. If 'vers' is greater than zero then record the NLM
 * program version to use to communicate with this client. The host
 * reference count is incremented - the caller must call
 * nlm_host_release when it has finished using it.
 */
extern struct nlm_host *nlm_find_host_by_name(const char *name,
    const struct sockaddr *addr, rpcvers_t vers);

/*
 * Search for an existing NLM host that matches the given remote
 * address. If none is found, create a new host with the requested
 * address and remember 'vers' as the NLM protocol version to use for
 * that host. The host reference count is incremented - the caller
 * must call nlm_host_release when it has finished using it.
 */
extern struct nlm_host *nlm_find_host_by_addr(const struct sockaddr *addr,
    int vers);

/*
 * Register this NLM host with the local NSM so that we can be
 * notified if it reboots.
 */
extern void nlm_host_monitor(struct nlm_host *host, int state);

/*
 * Decrement the host reference count, freeing resources if the
 * reference count reaches zero.
 */
extern void nlm_host_release(struct nlm_host *host);

/*
 * Return an RPC client handle that can be used to talk to the NLM
 * running on the given host.
 */
extern CLIENT *nlm_host_get_rpc(struct nlm_host *host, bool_t isserver);

/*
 * Return the system ID for a host.
 */
extern int nlm_host_get_sysid(struct nlm_host *host);

/*
 * Return the remote NSM state value for a host.
 */
extern int nlm_host_get_state(struct nlm_host *host);

/*
 * When sending a blocking lock request, we need to track the request
 * in our waiting lock list. We add an entry to the waiting list
 * before we send the lock RPC so that we can cope with a granted
 * message arriving at any time. Call this function before sending the
 * lock rpc. If the lock succeeds, call nlm_deregister_wait_lock with
 * the handle this function returns, otherwise nlm_wait_lock. Both
 * will remove the entry from the waiting list.
 */
extern void *nlm_register_wait_lock(struct nlm4_lock *lock, struct vnode *vp);

/*
 * Deregister a blocking lock request. Call this if the lock succeeded
 * without blocking.
 */
extern void nlm_deregister_wait_lock(void *handle);

/*
 * Wait for a granted callback for a blocked lock request, waiting at
 * most timo ticks. If no granted message is received within the
 * timeout, return EWOULDBLOCK. If a signal interrupted the wait,
 * return EINTR - the caller must arrange to send a cancellation to
 * the server. In both cases, the request is removed from the waiting
 * list.
 */
extern int nlm_wait_lock(void *handle, int timo);

/*
 * Cancel any pending waits for this vnode - called on forcible unmounts.
 */
extern void nlm_cancel_wait(struct vnode *vp);

/*
 * Called when a host restarts.
 */
extern void nlm_sm_notify(nlm_sm_status *argp);

/*
 * Implementation for lock testing RPCs. If the request was handled
 * successfully and rpcp is non-NULL, *rpcp is set to an RPC client
 * handle which can be used to send an async rpc reply. Returns zero
 * if the request was handled, or a suitable unix error code
 * otherwise.
 */
extern int nlm_do_test(nlm4_testargs *argp, nlm4_testres *result,
    struct svc_req *rqstp, CLIENT **rpcp);

/*
 * Implementation for lock setting RPCs. If the request was handled
 * successfully and rpcp is non-NULL, *rpcp is set to an RPC client
 * handle which can be used to send an async rpc reply. Returns zero
 * if the request was handled, or a suitable unix error code
 * otherwise.
 */
extern int nlm_do_lock(nlm4_lockargs *argp, nlm4_res *result,
    struct svc_req *rqstp, bool_t monitor, CLIENT **rpcp); 

/*
 * Implementation for cancelling a pending lock request. If the
 * request was handled successfully and rpcp is non-NULL, *rpcp is set
 * to an RPC client handle which can be used to send an async rpc
 * reply. Returns zero if the request was handled, or a suitable unix
 * error code otherwise.
 */
extern int nlm_do_cancel(nlm4_cancargs *argp, nlm4_res *result,
    struct svc_req *rqstp, CLIENT **rpcp);

/*
 * Implementation for unlocking RPCs. If the request was handled
 * successfully and rpcp is non-NULL, *rpcp is set to an RPC client
 * handle which can be used to send an async rpc reply. Returns zero
 * if the request was handled, or a suitable unix error code
 * otherwise.
 */
extern int nlm_do_unlock(nlm4_unlockargs *argp, nlm4_res *result,
    struct svc_req *rqstp, CLIENT **rpcp);

/*
 * Implementation for granted RPCs. If the request was handled
 * successfully and rpcp is non-NULL, *rpcp is set to an RPC client
 * handle which can be used to send an async rpc reply. Returns zero
 * if the request was handled, or a suitable unix error code
 * otherwise.
 */
extern int nlm_do_granted(nlm4_testargs *argp, nlm4_res *result,
    struct svc_req *rqstp, CLIENT **rpcp);

/*
 * Implementation for the granted result RPC. The client may reject the granted
 * message, in which case we need to handle it appropriately.
 */
extern void nlm_do_granted_res(nlm4_res *argp, struct svc_req *rqstp);

/*
 * Free all locks associated with the hostname argp->name.
 */
extern void nlm_do_free_all(nlm4_notify *argp);

/*
 * Recover client lock state after a server reboot.
 */
extern void nlm_client_recovery(struct nlm_host *);

/*
 * Interface from NFS client code to the NLM.
 */
struct vop_advlock_args;
struct vop_reclaim_args;
extern int nlm_advlock(struct vop_advlock_args *ap);
extern int nlm_reclaim(struct vop_reclaim_args *ap);

/*
 * Acquire the next sysid for remote locks not handled by the NLM.
 */
extern uint32_t nlm_acquire_next_sysid(void);

#endif

#endif
