/******************************************************************************
 * xenstore.c
 *
 * Low-level kernel interface to the XenStore.
 *
 * Copyright (C) 2005 Rusty Russell, IBM Corporation
 * Copyright (C) 2009,2010 Spectra Logic Corporation
 *
 * This file may be distributed separately from the Linux kernel, or
 * incorporated into other software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */


#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/syslog.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/kthread.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/uio.h>
#include <sys/unistd.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>

#include <machine/stdarg.h>

#include <xen/xen-os.h>
#include <xen/hypervisor.h>
#include <xen/xen_intr.h>

#include <xen/interface/hvm/params.h>
#include <xen/hvm.h>

#include <xen/xenstore/xenstorevar.h>
#include <xen/xenstore/xenstore_internal.h>

#include <vm/vm.h>
#include <vm/pmap.h>

/**
 * \file xenstore.c
 * \brief XenStore interface
 *
 * The XenStore interface is a simple storage system that is a means of
 * communicating state and configuration data between the Xen Domain 0
 * and the various guest domains.  All configuration data other than
 * a small amount of essential information required during the early
 * boot process of launching a Xen aware guest, is managed using the
 * XenStore.
 *
 * The XenStore is ASCII string based, and has a structure and semantics
 * similar to a filesystem.  There are files and directories, the directories
 * able to contain files or other directories.  The depth of the hierarchy
 * is only limited by the XenStore's maximum path length.
 *
 * The communication channel between the XenStore service and other
 * domains is via two, guest specific, ring buffers in a shared memory
 * area.  One ring buffer is used for communicating in each direction.
 * The grant table references for this shared memory are given to the
 * guest either via the xen_start_info structure for a fully para-
 * virtualized guest, or via HVM hypercalls for a hardware virtualized
 * guest.
 *
 * The XenStore communication relies on an event channel and thus
 * interrupts.  For this reason, the attachment of the XenStore
 * relies on an interrupt driven configuration hook to hold off
 * boot processing until communication with the XenStore service
 * can be established.
 *
 * Several Xen services depend on the XenStore, most notably the
 * XenBus used to discover and manage Xen devices.  These services
 * are implemented as NewBus child attachments to a bus exported
 * by this XenStore driver.
 */

static struct xs_watch *find_watch(const char *token);

MALLOC_DEFINE(M_XENSTORE, "xenstore", "XenStore data and results");

/**
 * Pointer to shared memory communication structures allowing us
 * to communicate with the XenStore service.
 *
 * When operating in full PV mode, this pointer is set early in kernel
 * startup from within xen_machdep.c.  In HVM mode, we use hypercalls
 * to get the guest frame number for the shared page and then map it
 * into kva.  See xs_init() for details.
 */
static struct xenstore_domain_interface *xen_store;

/*-------------------------- Private Data Structures ------------------------*/

/**
 * Structure capturing messages received from the XenStore service.
 */
struct xs_stored_msg {
	TAILQ_ENTRY(xs_stored_msg) list;

	struct xsd_sockmsg hdr;

	union {
		/* Queued replies. */
		struct {
			char *body;
		} reply;

		/* Queued watch events. */
		struct {
			struct xs_watch *handle;
			const char **vec;
			u_int vec_size;
		} watch;
	} u;
};
TAILQ_HEAD(xs_stored_msg_list, xs_stored_msg);

/**
 * Container for all XenStore related state.
 */
struct xs_softc {
	/** Newbus device for the XenStore. */
	device_t xs_dev;

	/**
	 * Lock serializing access to ring producer/consumer
	 * indexes.  Use of this lock guarantees that wakeups
	 * of blocking readers/writers are not missed due to
	 * races with the XenStore service.
	 */
	struct mtx ring_lock;

	/*
	 * Mutex used to insure exclusive access to the outgoing
	 * communication ring.  We use a lock type that can be
	 * held while sleeping so that xs_write() can block waiting
	 * for space in the ring to free up, without allowing another
	 * writer to come in and corrupt a partial message write.
	 */
	struct sx request_mutex;

	/**
	 * A list of replies to our requests.
	 *
	 * The reply list is filled by xs_rcv_thread().  It
	 * is consumed by the context that issued the request
	 * to which a reply is made.  The requester blocks in
	 * xs_read_reply().
	 *
	 * /note Only one requesting context can be active at a time.
	 *       This is guaranteed by the request_mutex and insures
	 *	 that the requester sees replies matching the order
	 *	 of its requests.
	 */
	struct xs_stored_msg_list reply_list;

	/** Lock protecting the reply list. */
	struct mtx reply_lock;

	/**
	 * List of registered watches.
	 */
	struct xs_watch_list  registered_watches;

	/** Lock protecting the registered watches list. */
	struct mtx registered_watches_lock;

	/**
	 * List of pending watch callback events.
	 */
	struct xs_stored_msg_list watch_events;

	/** Lock protecting the watch calback list. */
	struct mtx watch_events_lock;

	/**
	 * The processid of the xenwatch thread.
	 */
	pid_t xenwatch_pid;

	/**
	 * Sleepable mutex used to gate the execution of XenStore
	 * watch event callbacks.
	 *
	 * xenwatch_thread holds an exclusive lock on this mutex
	 * while delivering event callbacks, and xenstore_unregister_watch()
	 * uses an exclusive lock of this mutex to guarantee that no
	 * callbacks of the just unregistered watch are pending
	 * before returning to its caller.
	 */
	struct sx xenwatch_mutex;

	/**
	 * The HVM guest pseudo-physical frame number.  This is Xen's mapping
	 * of the true machine frame number into our "physical address space".
	 */
	unsigned long gpfn;

	/**
	 * The event channel for communicating with the
	 * XenStore service.
	 */
	int evtchn;

	/** Handle for XenStore interrupts. */
	xen_intr_handle_t xen_intr_handle;

	/**
	 * Interrupt driven config hook allowing us to defer
	 * attaching children until interrupts (and thus communication
	 * with the XenStore service) are available.
	 */
	struct intr_config_hook xs_attachcb;

	/**
	 * Xenstore is a user-space process that usually runs in Dom0,
	 * so if this domain is booting as Dom0, xenstore wont we accessible,
	 * and we have to defer the initialization of xenstore related
	 * devices to later (when xenstore is started).
	 */
	bool initialized;

	/**
	 * Task to run when xenstore is initialized (Dom0 only), will
	 * take care of attaching xenstore related devices.
	 */
	struct task xs_late_init;
};

/*-------------------------------- Global Data ------------------------------*/
static struct xs_softc xs;

/*------------------------- Private Utility Functions -----------------------*/

/**
 * Count and optionally record pointers to a number of NUL terminated
 * strings in a buffer.
 *
 * \param strings  A pointer to a contiguous buffer of NUL terminated strings.
 * \param dest	   An array to store pointers to each string found in strings.
 * \param len	   The length of the buffer pointed to by strings.
 *
 * \return  A count of the number of strings found.
 */
static u_int
extract_strings(const char *strings, const char **dest, u_int len)
{
	u_int num;
	const char *p;

	for (p = strings, num = 0; p < strings + len; p += strlen(p) + 1) {
		if (dest != NULL)
			*dest++ = p;
		num++;
	}

	return (num);
}

/**
 * Convert a contiguous buffer containing a series of NUL terminated
 * strings into an array of pointers to strings.
 *
 * The returned pointer references the array of string pointers which
 * is followed by the storage for the string data.  It is the client's
 * responsibility to free this storage.
 *
 * The storage addressed by strings is free'd prior to split returning.
 *
 * \param strings  A pointer to a contiguous buffer of NUL terminated strings.
 * \param len	   The length of the buffer pointed to by strings.
 * \param num	   The number of strings found and returned in the strings
 *                 array.
 *
 * \return  An array of pointers to the strings found in the input buffer.
 */
static const char **
split(char *strings, u_int len, u_int *num)
{
	const char **ret;

	/* Protect against unterminated buffers. */
	if (len > 0)
		strings[len - 1] = '\0';

	/* Count the strings. */
	*num = extract_strings(strings, /*dest*/NULL, len);

	/* Transfer to one big alloc for easy freeing by the caller. */
	ret = malloc(*num * sizeof(char *) + len, M_XENSTORE, M_WAITOK);
	memcpy(&ret[*num], strings, len);
	free(strings, M_XENSTORE);

	/* Extract pointers to newly allocated array. */
	strings = (char *)&ret[*num];
	(void)extract_strings(strings, /*dest*/ret, len);

	return (ret);
}

/*------------------------- Public Utility Functions -------------------------*/
/*------- API comments for these methods can be found in xenstorevar.h -------*/
struct sbuf *
xs_join(const char *dir, const char *name)
{
	struct sbuf *sb;

	sb = sbuf_new_auto();
	sbuf_cat(sb, dir);
	if (name[0] != '\0') {
		sbuf_putc(sb, '/');
		sbuf_cat(sb, name);
	}
	sbuf_finish(sb);

	return (sb);
}

/*-------------------- Low Level Communication Management --------------------*/
/**
 * Interrupt handler for the XenStore event channel.
 *
 * XenStore reads and writes block on "xen_store" for buffer
 * space.  Wakeup any blocking operations when the XenStore
 * service has modified the queues.
 */
static void
xs_intr(void * arg __unused /*__attribute__((unused))*/)
{

	/* If xenstore has not been initialized, initialize it now */
	if (!xs.initialized) {
		xs.initialized = true;
		/*
		 * Since this task is probing and attaching devices we
		 * have to hold the Giant lock.
		 */
		taskqueue_enqueue(taskqueue_swi_giant, &xs.xs_late_init);
	}

	/*
	 * Hold ring lock across wakeup so that clients
	 * cannot miss a wakeup.
	 */
	mtx_lock(&xs.ring_lock);
	wakeup(xen_store);
	mtx_unlock(&xs.ring_lock);
}

/**
 * Verify that the indexes for a ring are valid.
 *
 * The difference between the producer and consumer cannot
 * exceed the size of the ring.
 *
 * \param cons  The consumer index for the ring to test.
 * \param prod  The producer index for the ring to test.
 *
 * \retval 1  If indexes are in range.
 * \retval 0  If the indexes are out of range.
 */
static int
xs_check_indexes(XENSTORE_RING_IDX cons, XENSTORE_RING_IDX prod)
{

	return ((prod - cons) <= XENSTORE_RING_SIZE);
}

/**
 * Return a pointer to, and the length of, the contiguous
 * free region available for output in a ring buffer.
 *
 * \param cons  The consumer index for the ring.
 * \param prod  The producer index for the ring.
 * \param buf   The base address of the ring's storage.
 * \param len   The amount of contiguous storage available.
 *
 * \return  A pointer to the start location of the free region.
 */
static void *
xs_get_output_chunk(XENSTORE_RING_IDX cons, XENSTORE_RING_IDX prod,
    char *buf, uint32_t *len)
{

	*len = XENSTORE_RING_SIZE - MASK_XENSTORE_IDX(prod);
	if ((XENSTORE_RING_SIZE - (prod - cons)) < *len)
		*len = XENSTORE_RING_SIZE - (prod - cons);
	return (buf + MASK_XENSTORE_IDX(prod));
}

/**
 * Return a pointer to, and the length of, the contiguous
 * data available to read from a ring buffer.
 *
 * \param cons  The consumer index for the ring.
 * \param prod  The producer index for the ring.
 * \param buf   The base address of the ring's storage.
 * \param len   The amount of contiguous data available to read.
 *
 * \return  A pointer to the start location of the available data.
 */
static const void *
xs_get_input_chunk(XENSTORE_RING_IDX cons, XENSTORE_RING_IDX prod,
    const char *buf, uint32_t *len)
{

	*len = XENSTORE_RING_SIZE - MASK_XENSTORE_IDX(cons);
	if ((prod - cons) < *len)
		*len = prod - cons;
	return (buf + MASK_XENSTORE_IDX(cons));
}

/**
 * Transmit data to the XenStore service.
 *
 * \param tdata  A pointer to the contiguous data to send.
 * \param len    The amount of data to send.
 *
 * \return  On success 0, otherwise an errno value indicating the
 *          cause of failure.
 *
 * \invariant  Called from thread context.
 * \invariant  The buffer pointed to by tdata is at least len bytes
 *             in length.
 * \invariant  xs.request_mutex exclusively locked.
 */
static int
xs_write_store(const void *tdata, unsigned len)
{
	XENSTORE_RING_IDX cons, prod;
	const char *data = (const char *)tdata;
	int error;

	sx_assert(&xs.request_mutex, SX_XLOCKED);
	while (len != 0) {
		void *dst;
		u_int avail;

		/* Hold lock so we can't miss wakeups should we block. */
		mtx_lock(&xs.ring_lock);
		cons = xen_store->req_cons;
		prod = xen_store->req_prod;
		if ((prod - cons) == XENSTORE_RING_SIZE) {
			/*
			 * Output ring is full. Wait for a ring event.
			 *
			 * Note that the events from both queues
			 * are combined, so being woken does not
			 * guarantee that data exist in the read
			 * ring.
			 *
			 * To simplify error recovery and the retry,
			 * we specify PDROP so our lock is *not* held
			 * when msleep returns.
			 */
			error = msleep(xen_store, &xs.ring_lock, PCATCH|PDROP,
			     "xbwrite", /*timeout*/0);
			if (error && error != EWOULDBLOCK)
				return (error);

			/* Try again. */
			continue;
		}
		mtx_unlock(&xs.ring_lock);

		/* Verify queue sanity. */
		if (!xs_check_indexes(cons, prod)) {
			xen_store->req_cons = xen_store->req_prod = 0;
			return (EIO);
		}

		dst = xs_get_output_chunk(cons, prod, xen_store->req, &avail);
		if (avail > len)
			avail = len;

		memcpy(dst, data, avail);
		data += avail;
		len -= avail;

		/*
		 * The store to the producer index, which indicates
		 * to the other side that new data has arrived, must
		 * be visible only after our copy of the data into the
		 * ring has completed.
		 */
		wmb();
		xen_store->req_prod += avail;

		/*
		 * xen_intr_signal() implies mb(). The other side will see
		 * the change to req_prod at the time of the interrupt.
		 */
		xen_intr_signal(xs.xen_intr_handle);
	}

	return (0);
}

/**
 * Receive data from the XenStore service.
 *
 * \param tdata  A pointer to the contiguous buffer to receive the data.
 * \param len    The amount of data to receive.
 *
 * \return  On success 0, otherwise an errno value indicating the
 *          cause of failure.
 *
 * \invariant  Called from thread context.
 * \invariant  The buffer pointed to by tdata is at least len bytes
 *             in length.
 *
 * \note xs_read does not perform any internal locking to guarantee
 *       serial access to the incoming ring buffer.  However, there
 *	 is only one context processing reads: xs_rcv_thread().
 */
static int
xs_read_store(void *tdata, unsigned len)
{
	XENSTORE_RING_IDX cons, prod;
	char *data = (char *)tdata;
	int error;

	while (len != 0) {
		u_int avail;
		const char *src;

		/* Hold lock so we can't miss wakeups should we block. */
		mtx_lock(&xs.ring_lock);
		cons = xen_store->rsp_cons;
		prod = xen_store->rsp_prod;
		if (cons == prod) {
			/*
			 * Nothing to read. Wait for a ring event.
			 *
			 * Note that the events from both queues
			 * are combined, so being woken does not
			 * guarantee that data exist in the read
			 * ring.
			 *
			 * To simplify error recovery and the retry,
			 * we specify PDROP so our lock is *not* held
			 * when msleep returns.
			 */
			error = msleep(xen_store, &xs.ring_lock, PCATCH|PDROP,
			    "xbread", /*timeout*/0);
			if (error && error != EWOULDBLOCK)
				return (error);
			continue;
		}
		mtx_unlock(&xs.ring_lock);

		/* Verify queue sanity. */
		if (!xs_check_indexes(cons, prod)) {
			xen_store->rsp_cons = xen_store->rsp_prod = 0;
			return (EIO);
		}

		src = xs_get_input_chunk(cons, prod, xen_store->rsp, &avail);
		if (avail > len)
			avail = len;

		/*
		 * Insure the data we read is related to the indexes
		 * we read above.
		 */
		rmb();

		memcpy(data, src, avail);
		data += avail;
		len -= avail;

		/*
		 * Insure that the producer of this ring does not see
		 * the ring space as free until after we have copied it
		 * out.
		 */
		mb();
		xen_store->rsp_cons += avail;

		/*
		 * xen_intr_signal() implies mb(). The producer will see
		 * the updated consumer index when the event is delivered.
		 */
		xen_intr_signal(xs.xen_intr_handle);
	}

	return (0);
}

/*----------------------- Received Message Processing ------------------------*/
/**
 * Block reading the next message from the XenStore service and
 * process the result.
 *
 * \param type  The returned type of the XenStore message received.
 *
 * \return  0 on success.  Otherwise an errno value indicating the
 *          type of failure encountered.
 */
static int
xs_process_msg(enum xsd_sockmsg_type *type)
{
	struct xs_stored_msg *msg;
	char *body;
	int error;

	msg = malloc(sizeof(*msg), M_XENSTORE, M_WAITOK);
	error = xs_read_store(&msg->hdr, sizeof(msg->hdr));
	if (error) {
		free(msg, M_XENSTORE);
		return (error);
	}

	body = malloc(msg->hdr.len + 1, M_XENSTORE, M_WAITOK);
	error = xs_read_store(body, msg->hdr.len);
	if (error) {
		free(body, M_XENSTORE);
		free(msg, M_XENSTORE);
		return (error);
	}
	body[msg->hdr.len] = '\0';

	*type = msg->hdr.type;
	if (msg->hdr.type == XS_WATCH_EVENT) {
		msg->u.watch.vec = split(body, msg->hdr.len,
		    &msg->u.watch.vec_size);

		mtx_lock(&xs.registered_watches_lock);
		msg->u.watch.handle = find_watch(
		    msg->u.watch.vec[XS_WATCH_TOKEN]);
		if (msg->u.watch.handle != NULL) {
			mtx_lock(&xs.watch_events_lock);
			TAILQ_INSERT_TAIL(&xs.watch_events, msg, list);
			wakeup(&xs.watch_events);
			mtx_unlock(&xs.watch_events_lock);
		} else {
			free(msg->u.watch.vec, M_XENSTORE);
			free(msg, M_XENSTORE);
		}
		mtx_unlock(&xs.registered_watches_lock);
	} else {
		msg->u.reply.body = body;
		mtx_lock(&xs.reply_lock);
		TAILQ_INSERT_TAIL(&xs.reply_list, msg, list);
		wakeup(&xs.reply_list);
		mtx_unlock(&xs.reply_lock);
	}

	return (0);
}

/**
 * Thread body of the XenStore receive thread.
 *
 * This thread blocks waiting for data from the XenStore service
 * and processes and received messages.
 */
static void
xs_rcv_thread(void *arg __unused)
{
	int error;
	enum xsd_sockmsg_type type;

	for (;;) {
		error = xs_process_msg(&type);
		if (error)
			printf("XENSTORE error %d while reading message\n",
			    error);
	}
}

/*---------------- XenStore Message Request/Reply Processing -----------------*/
#define xsd_error_count	(sizeof(xsd_errors) / sizeof(xsd_errors[0]))

/**
 * Convert a XenStore error string into an errno number.
 *
 * \param errorstring  The error string to convert.
 *
 * \return  The errno best matching the input string.
 *
 * \note Unknown error strings are converted to EINVAL.
 */
static int
xs_get_error(const char *errorstring)
{
	u_int i;

	for (i = 0; i < xsd_error_count; i++) {
		if (!strcmp(errorstring, xsd_errors[i].errstring))
			return (xsd_errors[i].errnum);
	}
	log(LOG_WARNING, "XENSTORE xen store gave: unknown error %s",
	    errorstring);
	return (EINVAL);
}

/**
 * Block waiting for a reply to a message request.
 *
 * \param type	  The returned type of the reply.
 * \param len	  The returned body length of the reply.
 * \param result  The returned body of the reply.
 *
 * \return  0 on success.  Otherwise an errno indicating the
 *          cause of failure.
 */
static int
xs_read_reply(enum xsd_sockmsg_type *type, u_int *len, void **result)
{
	struct xs_stored_msg *msg;
	char *body;
	int error;

	mtx_lock(&xs.reply_lock);
	while (TAILQ_EMPTY(&xs.reply_list)) {
		error = mtx_sleep(&xs.reply_list, &xs.reply_lock, 0, "xswait",
		    hz/10);
		if (error && error != EWOULDBLOCK) {
			mtx_unlock(&xs.reply_lock);
			return (error);
		}
	}
	msg = TAILQ_FIRST(&xs.reply_list);
	TAILQ_REMOVE(&xs.reply_list, msg, list);
	mtx_unlock(&xs.reply_lock);

	*type = msg->hdr.type;
	if (len)
		*len = msg->hdr.len;
	body = msg->u.reply.body;

	free(msg, M_XENSTORE);
	*result = body;
	return (0);
}

/**
 * Pass-thru interface for XenStore access by userland processes
 * via the XenStore device.
 *
 * Reply type and length data are returned by overwriting these
 * fields in the passed in request message.
 *
 * \param msg	  A properly formatted message to transmit to
 *		  the XenStore service.
 * \param result  The returned body of the reply.
 *
 * \return  0 on success.  Otherwise an errno indicating the cause
 *          of failure.
 *
 * \note The returned result is provided in malloced storage and thus
 *       must be free'd by the caller with 'free(result, M_XENSTORE);
 */
int
xs_dev_request_and_reply(struct xsd_sockmsg *msg, void **result)
{
	uint32_t request_type;
	int error;

	request_type = msg->type;

	sx_xlock(&xs.request_mutex);
	if ((error = xs_write_store(msg, sizeof(*msg) + msg->len)) == 0)
		error = xs_read_reply(&msg->type, &msg->len, result);
	sx_xunlock(&xs.request_mutex);

	return (error);
}

/**
 * Send a message with an optionally muti-part body to the XenStore service.
 *
 * \param t              The transaction to use for this request.
 * \param request_type   The type of message to send.
 * \param iovec          Pointers to the body sections of the request.
 * \param num_vecs       The number of body sections in the request.
 * \param len            The returned length of the reply.
 * \param result         The returned body of the reply.
 *
 * \return  0 on success.  Otherwise an errno indicating
 *          the cause of failure.
 *
 * \note The returned result is provided in malloced storage and thus
 *       must be free'd by the caller with 'free(*result, M_XENSTORE);
 */
static int
xs_talkv(struct xs_transaction t, enum xsd_sockmsg_type request_type,
    const struct iovec *iovec, u_int num_vecs, u_int *len, void **result)
{
	struct xsd_sockmsg msg;
	void *ret = NULL;
	u_int i;
	int error;

	msg.tx_id = t.id;
	msg.req_id = 0;
	msg.type = request_type;
	msg.len = 0;
	for (i = 0; i < num_vecs; i++)
		msg.len += iovec[i].iov_len;

	sx_xlock(&xs.request_mutex);
	error = xs_write_store(&msg, sizeof(msg));
	if (error) {
		printf("xs_talkv failed %d\n", error);
		goto error_lock_held;
	}

	for (i = 0; i < num_vecs; i++) {
		error = xs_write_store(iovec[i].iov_base, iovec[i].iov_len);
		if (error) {
			printf("xs_talkv failed %d\n", error);
			goto error_lock_held;
		}
	}

	error = xs_read_reply(&msg.type, len, &ret);

error_lock_held:
	sx_xunlock(&xs.request_mutex);
	if (error)
		return (error);

	if (msg.type == XS_ERROR) {
		error = xs_get_error(ret);
		free(ret, M_XENSTORE);
		return (error);
	}

	/* Reply is either error or an echo of our request message type. */
	KASSERT(msg.type == request_type, ("bad xenstore message type"));

	if (result)
		*result = ret;
	else
		free(ret, M_XENSTORE);

	return (0);
}

/**
 * Wrapper for xs_talkv allowing easy transmission of a message with
 * a single, contiguous, message body.
 *
 * \param t              The transaction to use for this request.
 * \param request_type   The type of message to send.
 * \param body           The body of the request.
 * \param len            The returned length of the reply.
 * \param result         The returned body of the reply.
 *
 * \return  0 on success.  Otherwise an errno indicating
 *          the cause of failure.
 *
 * \note The returned result is provided in malloced storage and thus
 *       must be free'd by the caller with 'free(*result, M_XENSTORE);
 */
static int
xs_single(struct xs_transaction t, enum xsd_sockmsg_type request_type,
    const char *body, u_int *len, void **result)
{
	struct iovec iovec;

	iovec.iov_base = (void *)(uintptr_t)body;
	iovec.iov_len = strlen(body) + 1;

	return (xs_talkv(t, request_type, &iovec, 1, len, result));
}

/*------------------------- XenStore Watch Support ---------------------------*/
/**
 * Transmit a watch request to the XenStore service.
 *
 * \param path    The path in the XenStore to watch.
 * \param tocken  A unique identifier for this watch.
 *
 * \return  0 on success.  Otherwise an errno indicating the
 *          cause of failure.
 */
static int
xs_watch(const char *path, const char *token)
{
	struct iovec iov[2];

	iov[0].iov_base = (void *)(uintptr_t) path;
	iov[0].iov_len = strlen(path) + 1;
	iov[1].iov_base = (void *)(uintptr_t) token;
	iov[1].iov_len = strlen(token) + 1;

	return (xs_talkv(XST_NIL, XS_WATCH, iov, 2, NULL, NULL));
}

/**
 * Transmit an uwatch request to the XenStore service.
 *
 * \param path    The path in the XenStore to watch.
 * \param tocken  A unique identifier for this watch.
 *
 * \return  0 on success.  Otherwise an errno indicating the
 *          cause of failure.
 */
static int
xs_unwatch(const char *path, const char *token)
{
	struct iovec iov[2];

	iov[0].iov_base = (void *)(uintptr_t) path;
	iov[0].iov_len = strlen(path) + 1;
	iov[1].iov_base = (void *)(uintptr_t) token;
	iov[1].iov_len = strlen(token) + 1;

	return (xs_talkv(XST_NIL, XS_UNWATCH, iov, 2, NULL, NULL));
}

/**
 * Convert from watch token (unique identifier) to the associated
 * internal tracking structure for this watch.
 *
 * \param tocken  The unique identifier for the watch to find.
 *
 * \return  A pointer to the found watch structure or NULL.
 */
static struct xs_watch *
find_watch(const char *token)
{
	struct xs_watch *i, *cmp;

	cmp = (void *)strtoul(token, NULL, 16);

	LIST_FOREACH(i, &xs.registered_watches, list)
		if (i == cmp)
			return (i);

	return (NULL);
}

/**
 * Thread body of the XenStore watch event dispatch thread.
 */
static void
xenwatch_thread(void *unused)
{
	struct xs_stored_msg *msg;

	for (;;) {

		mtx_lock(&xs.watch_events_lock);
		while (TAILQ_EMPTY(&xs.watch_events))
			mtx_sleep(&xs.watch_events,
			    &xs.watch_events_lock,
			    PWAIT | PCATCH, "waitev", hz/10);

		mtx_unlock(&xs.watch_events_lock);
		sx_xlock(&xs.xenwatch_mutex);

		mtx_lock(&xs.watch_events_lock);
		msg = TAILQ_FIRST(&xs.watch_events);
		if (msg)
			TAILQ_REMOVE(&xs.watch_events, msg, list);
		mtx_unlock(&xs.watch_events_lock);

		if (msg != NULL) {
			/*
			 * XXX There are messages coming in with a NULL
			 * XXX callback.  This deserves further investigation;
			 * XXX the workaround here simply prevents the kernel
			 * XXX from panic'ing on startup.
			 */
			if (msg->u.watch.handle->callback != NULL)
				msg->u.watch.handle->callback(
					msg->u.watch.handle,
					(const char **)msg->u.watch.vec,
					msg->u.watch.vec_size);
			free(msg->u.watch.vec, M_XENSTORE);
			free(msg, M_XENSTORE);
		}

		sx_xunlock(&xs.xenwatch_mutex);
	}
}

/*----------- XenStore Configuration, Initialization, and Control ------------*/
/**
 * Setup communication channels with the XenStore service.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 */
static int
xs_init_comms(void)
{
	int error;

	if (xen_store->rsp_prod != xen_store->rsp_cons) {
		log(LOG_WARNING, "XENSTORE response ring is not quiescent "
		    "(%08x:%08x): fixing up\n",
		    xen_store->rsp_cons, xen_store->rsp_prod);
		xen_store->rsp_cons = xen_store->rsp_prod;
	}

	xen_intr_unbind(&xs.xen_intr_handle);

	error = xen_intr_bind_local_port(xs.xs_dev, xs.evtchn,
	    /*filter*/NULL, xs_intr, /*arg*/NULL, INTR_TYPE_NET|INTR_MPSAFE,
	    &xs.xen_intr_handle);
	if (error) {
		log(LOG_WARNING, "XENSTORE request irq failed %i\n", error);
		return (error);
	}

	return (0);
}

/*------------------ Private Device Attachment Functions  --------------------*/
static void
xs_identify(driver_t *driver, device_t parent)
{

	BUS_ADD_CHILD(parent, 0, "xenstore", 0);
}

/**
 * Probe for the existence of the XenStore.
 *
 * \param dev
 */
static int 
xs_probe(device_t dev)
{
	/*
	 * We are either operating within a PV kernel or being probed
	 * as the child of the successfully attached xenpci device.
	 * Thus we are in a Xen environment and there will be a XenStore.
	 * Unconditionally return success.
	 */
	device_set_desc(dev, "XenStore");
	return (BUS_PROBE_NOWILDCARD);
}

static void
xs_attach_deferred(void *arg)
{

	bus_generic_probe(xs.xs_dev);
	bus_generic_attach(xs.xs_dev);

	config_intrhook_disestablish(&xs.xs_attachcb);
}

static void
xs_attach_late(void *arg, int pending)
{

	KASSERT((pending == 1), ("xs late attach queued several times"));
	bus_generic_probe(xs.xs_dev);
	bus_generic_attach(xs.xs_dev);
}

/**
 * Attach to the XenStore.
 *
 * This routine also prepares for the probe/attach of drivers that rely
 * on the XenStore.  
 */
static int
xs_attach(device_t dev)
{
	int error;

	/* Allow us to get device_t from softc and vice-versa. */
	xs.xs_dev = dev;
	device_set_softc(dev, &xs);

	/* Initialize the interface to xenstore. */
	struct proc *p;

	xs.initialized = false;
	xs.evtchn = xen_get_xenstore_evtchn();
	if (xs.evtchn == 0) {
		struct evtchn_alloc_unbound alloc_unbound;

		/* Allocate a local event channel for xenstore */
		alloc_unbound.dom = DOMID_SELF;
		alloc_unbound.remote_dom = DOMID_SELF;
		error = HYPERVISOR_event_channel_op(
		    EVTCHNOP_alloc_unbound, &alloc_unbound);
		if (error != 0)
			panic(
			   "unable to alloc event channel for Dom0: %d",
			    error);

		xs.evtchn = alloc_unbound.port;

		/* Allocate memory for the xs shared ring */
		xen_store = malloc(PAGE_SIZE, M_XENSTORE, M_WAITOK | M_ZERO);
		xs.gpfn = atop(pmap_kextract((vm_offset_t)xen_store));
	} else {
		xs.gpfn = xen_get_xenstore_mfn();
		xen_store = pmap_mapdev_attr(ptoa(xs.gpfn), PAGE_SIZE,
		    PAT_WRITE_BACK);
		xs.initialized = true;
	}

	TAILQ_INIT(&xs.reply_list);
	TAILQ_INIT(&xs.watch_events);

	mtx_init(&xs.ring_lock, "ring lock", NULL, MTX_DEF);
	mtx_init(&xs.reply_lock, "reply lock", NULL, MTX_DEF);
	sx_init(&xs.xenwatch_mutex, "xenwatch");
	sx_init(&xs.request_mutex, "xenstore request");
	mtx_init(&xs.registered_watches_lock, "watches", NULL, MTX_DEF);
	mtx_init(&xs.watch_events_lock, "watch events", NULL, MTX_DEF);

	/* Initialize the shared memory rings to talk to xenstored */
	error = xs_init_comms();
	if (error)
		return (error);

	error = kproc_create(xenwatch_thread, NULL, &p, RFHIGHPID,
	    0, "xenwatch");
	if (error)
		return (error);
	xs.xenwatch_pid = p->p_pid;

	error = kproc_create(xs_rcv_thread, NULL, NULL,
	    RFHIGHPID, 0, "xenstore_rcv");

	xs.xs_attachcb.ich_func = xs_attach_deferred;
	xs.xs_attachcb.ich_arg = NULL;
	if (xs.initialized) {
		config_intrhook_establish(&xs.xs_attachcb);
	} else {
		TASK_INIT(&xs.xs_late_init, 0, xs_attach_late, NULL);
	}

	return (error);
}

/**
 * Prepare for suspension of this VM by halting XenStore access after
 * all transactions and individual requests have completed.
 */
static int
xs_suspend(device_t dev)
{
	int error;

	/* Suspend child Xen devices. */
	error = bus_generic_suspend(dev);
	if (error != 0)
		return (error);

	sx_xlock(&xs.request_mutex);

	return (0);
}

/**
 * Resume XenStore operations after this VM is resumed.
 */
static int
xs_resume(device_t dev __unused)
{
	struct xs_watch *watch;
	char token[sizeof(watch) * 2 + 1];

	xs_init_comms();

	sx_xunlock(&xs.request_mutex);

	/*
	 * NB: since xenstore childs have not been resumed yet, there's
	 * no need to hold any watch mutex. Having clients try to add or
	 * remove watches at this point (before xenstore is resumed) is
	 * clearly a violantion of the resume order.
	 */
	LIST_FOREACH(watch, &xs.registered_watches, list) {
		sprintf(token, "%lX", (long)watch);
		xs_watch(watch->node, token);
	}

	/* Resume child Xen devices. */
	bus_generic_resume(dev);

	return (0);
}

/*-------------------- Private Device Attachment Data  -----------------------*/
static device_method_t xenstore_methods[] = { 
	/* Device interface */ 
	DEVMETHOD(device_identify,	xs_identify),
	DEVMETHOD(device_probe,         xs_probe), 
	DEVMETHOD(device_attach,        xs_attach), 
	DEVMETHOD(device_detach,        bus_generic_detach), 
	DEVMETHOD(device_shutdown,      bus_generic_shutdown), 
	DEVMETHOD(device_suspend,       xs_suspend), 
	DEVMETHOD(device_resume,        xs_resume), 
 
	/* Bus interface */ 
	DEVMETHOD(bus_add_child,        bus_generic_add_child),
	DEVMETHOD(bus_alloc_resource,   bus_generic_alloc_resource),
	DEVMETHOD(bus_release_resource, bus_generic_release_resource),
	DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),

	DEVMETHOD_END
}; 

DEFINE_CLASS_0(xenstore, xenstore_driver, xenstore_methods, 0);
static devclass_t xenstore_devclass; 
 
DRIVER_MODULE(xenstore, xenpv, xenstore_driver, xenstore_devclass, 0, 0);

/*------------------------------- Sysctl Data --------------------------------*/
/* XXX Shouldn't the node be somewhere else? */
SYSCTL_NODE(_dev, OID_AUTO, xen, CTLFLAG_RD, NULL, "Xen");
SYSCTL_INT(_dev_xen, OID_AUTO, xsd_port, CTLFLAG_RD, &xs.evtchn, 0, "");
SYSCTL_ULONG(_dev_xen, OID_AUTO, xsd_kva, CTLFLAG_RD, (u_long *) &xen_store, 0, "");

/*-------------------------------- Public API --------------------------------*/
/*------- API comments for these methods can be found in xenstorevar.h -------*/
bool
xs_initialized(void)
{

	return (xs.initialized);
}

evtchn_port_t
xs_evtchn(void)
{

    return (xs.evtchn);
}

vm_paddr_t
xs_address(void)
{

    return (ptoa(xs.gpfn));
}

int
xs_directory(struct xs_transaction t, const char *dir, const char *node,
    u_int *num, const char ***result)
{
	struct sbuf *path;
	char *strings;
	u_int len = 0;
	int error;

	path = xs_join(dir, node);
	error = xs_single(t, XS_DIRECTORY, sbuf_data(path), &len,
	    (void **)&strings);
	sbuf_delete(path);
	if (error)
		return (error);

	*result = split(strings, len, num);

	return (0);
}

int
xs_exists(struct xs_transaction t, const char *dir, const char *node)
{
	const char **d;
	int error, dir_n;

	error = xs_directory(t, dir, node, &dir_n, &d);
	if (error)
		return (0);
	free(d, M_XENSTORE);
	return (1);
}

int
xs_read(struct xs_transaction t, const char *dir, const char *node,
    u_int *len, void **result)
{
	struct sbuf *path;
	void *ret;
	int error;

	path = xs_join(dir, node);
	error = xs_single(t, XS_READ, sbuf_data(path), len, &ret);
	sbuf_delete(path);
	if (error)
		return (error);
	*result = ret;
	return (0);
}

int
xs_write(struct xs_transaction t, const char *dir, const char *node,
    const char *string)
{
	struct sbuf *path;
	struct iovec iovec[2];
	int error;

	path = xs_join(dir, node);

	iovec[0].iov_base = (void *)(uintptr_t) sbuf_data(path);
	iovec[0].iov_len = sbuf_len(path) + 1;
	iovec[1].iov_base = (void *)(uintptr_t) string;
	iovec[1].iov_len = strlen(string);

	error = xs_talkv(t, XS_WRITE, iovec, 2, NULL, NULL);
	sbuf_delete(path);

	return (error);
}

int
xs_mkdir(struct xs_transaction t, const char *dir, const char *node)
{
	struct sbuf *path;
	int ret;

	path = xs_join(dir, node);
	ret = xs_single(t, XS_MKDIR, sbuf_data(path), NULL, NULL);
	sbuf_delete(path);

	return (ret);
}

int
xs_rm(struct xs_transaction t, const char *dir, const char *node)
{
	struct sbuf *path;
	int ret;

	path = xs_join(dir, node);
	ret = xs_single(t, XS_RM, sbuf_data(path), NULL, NULL);
	sbuf_delete(path);

	return (ret);
}

int
xs_rm_tree(struct xs_transaction xbt, const char *base, const char *node)
{
	struct xs_transaction local_xbt;
	struct sbuf *root_path_sbuf;
	struct sbuf *cur_path_sbuf;
	char *root_path;
	char *cur_path;
	const char **dir;
	int error;

retry:
	root_path_sbuf = xs_join(base, node);
	cur_path_sbuf  = xs_join(base, node);
	root_path      = sbuf_data(root_path_sbuf);
	cur_path       = sbuf_data(cur_path_sbuf);
	dir            = NULL;
	local_xbt.id   = 0;

	if (xbt.id == 0) {
		error = xs_transaction_start(&local_xbt);
		if (error != 0)
			goto out;
		xbt = local_xbt;
	}

	while (1) {
		u_int count;
		u_int i;

		error = xs_directory(xbt, cur_path, "", &count, &dir);
		if (error)
			goto out;

		for (i = 0; i < count; i++) {
			error = xs_rm(xbt, cur_path, dir[i]);
			if (error == ENOTEMPTY) {
				struct sbuf *push_dir;

				/*
				 * Descend to clear out this sub directory.
				 * We'll return to cur_dir once push_dir
				 * is empty.
				 */
				push_dir = xs_join(cur_path, dir[i]);
				sbuf_delete(cur_path_sbuf);
				cur_path_sbuf = push_dir;
				cur_path = sbuf_data(cur_path_sbuf);
				break;
			} else if (error != 0) {
				goto out;
			}
		}

		free(dir, M_XENSTORE);
		dir = NULL;

		if (i == count) {
			char *last_slash;

			/* Directory is empty.  It is now safe to remove. */
			error = xs_rm(xbt, cur_path, "");
			if (error != 0)
				goto out;

			if (!strcmp(cur_path, root_path))
				break;

			/* Return to processing the parent directory. */
			last_slash = strrchr(cur_path, '/');
			KASSERT(last_slash != NULL,
				("xs_rm_tree: mangled path %s", cur_path));
			*last_slash = '\0';
		}
	}

out:
	sbuf_delete(cur_path_sbuf);
	sbuf_delete(root_path_sbuf);
	if (dir != NULL)
		free(dir, M_XENSTORE);

	if (local_xbt.id != 0) {
		int terror;

		terror = xs_transaction_end(local_xbt, /*abort*/error != 0);
		xbt.id = 0;
		if (terror == EAGAIN && error == 0)
			goto retry;
	}
	return (error);
}

int
xs_transaction_start(struct xs_transaction *t)
{
	char *id_str;
	int error;

	error = xs_single(XST_NIL, XS_TRANSACTION_START, "", NULL,
	    (void **)&id_str);
	if (error == 0) {
		t->id = strtoul(id_str, NULL, 0);
		free(id_str, M_XENSTORE);
	}
	return (error);
}

int
xs_transaction_end(struct xs_transaction t, int abort)
{
	char abortstr[2];

	if (abort)
		strcpy(abortstr, "F");
	else
		strcpy(abortstr, "T");

	return (xs_single(t, XS_TRANSACTION_END, abortstr, NULL, NULL));
}

int
xs_scanf(struct xs_transaction t, const char *dir, const char *node,
     int *scancountp, const char *fmt, ...)
{
	va_list ap;
	int error, ns;
	char *val;

	error = xs_read(t, dir, node, NULL, (void **) &val);
	if (error)
		return (error);

	va_start(ap, fmt);
	ns = vsscanf(val, fmt, ap);
	va_end(ap);
	free(val, M_XENSTORE);
	/* Distinctive errno. */
	if (ns == 0)
		return (ERANGE);
	if (scancountp)
		*scancountp = ns;
	return (0);
}

int
xs_vprintf(struct xs_transaction t,
    const char *dir, const char *node, const char *fmt, va_list ap)
{
	struct sbuf *sb;
	int error;

	sb = sbuf_new_auto();
	sbuf_vprintf(sb, fmt, ap);
	sbuf_finish(sb);
	error = xs_write(t, dir, node, sbuf_data(sb));
	sbuf_delete(sb);

	return (error);
}

int
xs_printf(struct xs_transaction t, const char *dir, const char *node,
     const char *fmt, ...)
{
	va_list ap;
	int error;

	va_start(ap, fmt);
	error = xs_vprintf(t, dir, node, fmt, ap);
	va_end(ap);

	return (error);
}

int
xs_gather(struct xs_transaction t, const char *dir, ...)
{
	va_list ap;
	const char *name;
	int error;

	va_start(ap, dir);
	error = 0;
	while (error == 0 && (name = va_arg(ap, char *)) != NULL) {
		const char *fmt = va_arg(ap, char *);
		void *result = va_arg(ap, void *);
		char *p;

		error = xs_read(t, dir, name, NULL, (void **) &p);
		if (error)
			break;

		if (fmt) {
			if (sscanf(p, fmt, result) == 0)
				error = EINVAL;
			free(p, M_XENSTORE);
		} else
			*(char **)result = p;
	}
	va_end(ap);

	return (error);
}

int
xs_register_watch(struct xs_watch *watch)
{
	/* Pointer in ascii is the token. */
	char token[sizeof(watch) * 2 + 1];
	int error;

	sprintf(token, "%lX", (long)watch);

	mtx_lock(&xs.registered_watches_lock);
	KASSERT(find_watch(token) == NULL, ("watch already registered"));
	LIST_INSERT_HEAD(&xs.registered_watches, watch, list);
	mtx_unlock(&xs.registered_watches_lock);

	error = xs_watch(watch->node, token);

	/* Ignore errors due to multiple registration. */
	if (error == EEXIST)
		error = 0;

	if (error != 0) {
		mtx_lock(&xs.registered_watches_lock);
		LIST_REMOVE(watch, list);
		mtx_unlock(&xs.registered_watches_lock);
	}

	return (error);
}

void
xs_unregister_watch(struct xs_watch *watch)
{
	struct xs_stored_msg *msg, *tmp;
	char token[sizeof(watch) * 2 + 1];
	int error;

	sprintf(token, "%lX", (long)watch);

	mtx_lock(&xs.registered_watches_lock);
	if (find_watch(token) == NULL) {
		mtx_unlock(&xs.registered_watches_lock);
		return;
	}
	LIST_REMOVE(watch, list);
	mtx_unlock(&xs.registered_watches_lock);

	error = xs_unwatch(watch->node, token);
	if (error)
		log(LOG_WARNING, "XENSTORE Failed to release watch %s: %i\n",
		    watch->node, error);

	/* Cancel pending watch events. */
	mtx_lock(&xs.watch_events_lock);
	TAILQ_FOREACH_SAFE(msg, &xs.watch_events, list, tmp) {
		if (msg->u.watch.handle != watch)
			continue;
		TAILQ_REMOVE(&xs.watch_events, msg, list);
		free(msg->u.watch.vec, M_XENSTORE);
		free(msg, M_XENSTORE);
	}
	mtx_unlock(&xs.watch_events_lock);

	/* Flush any currently-executing callback, unless we are it. :-) */
	if (curproc->p_pid != xs.xenwatch_pid) {
		sx_xlock(&xs.xenwatch_mutex);
		sx_xunlock(&xs.xenwatch_mutex);
	}
}

void
xs_lock(void)
{

	sx_xlock(&xs.request_mutex);
	return;
}

void
xs_unlock(void)
{

	sx_xunlock(&xs.request_mutex);
	return;
}

