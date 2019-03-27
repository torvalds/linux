/******************************************************************************
 * xenstorevar.h
 *
 * Method declarations and structures for accessing the XenStore.h
 *
 * Copyright (C) 2005 Rusty Russell, IBM Corporation
 * Copyright (C) 2005 XenSource Ltd.
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
 *
 * $FreeBSD$
 */

#ifndef _XEN_XENSTORE_XENSTOREVAR_H
#define _XEN_XENSTORE_XENSTOREVAR_H

#include <sys/queue.h>
#include <sys/bus.h>
#include <sys/eventhandler.h>
#include <sys/malloc.h>
#include <sys/sbuf.h>

#include <machine/stdarg.h>

#include <xen/xen-os.h>
#include <xen/interface/grant_table.h>
#include <xen/interface/io/xenbus.h>
#include <xen/interface/io/xs_wire.h>

#include "xenbus_if.h"

/* XenStore allocations including XenStore data returned to clients. */
MALLOC_DECLARE(M_XENSTORE);

struct xs_watch;

typedef	void (xs_watch_cb_t)(struct xs_watch *, const char **vec,
    unsigned int len);

/* Register callback to watch subtree (node) in the XenStore. */
struct xs_watch
{
	LIST_ENTRY(xs_watch) list;

	/* Path being watched. */
	char *node;

	/* Callback (executed in a process context with no locks held). */
	xs_watch_cb_t *callback;

	/* Callback client data untouched by the XenStore watch mechanism. */
	uintptr_t callback_data;
};
LIST_HEAD(xs_watch_list, xs_watch);

typedef int (*xs_event_handler_t)(void *);

struct xs_transaction
{
	uint32_t id;
};

#define XST_NIL ((struct xs_transaction) { 0 })

/**
 * Check if Xenstore is initialized.
 *
 * \return  True if initialized, false otherwise.
 */
bool xs_initialized(void);

/**
 * Return xenstore event channel port.
 *
 * \return event channel port.
 */
evtchn_port_t xs_evtchn(void);

/**
 * Return xenstore page physical memory address.
 *
 * \return xenstore page physical address.
 */
vm_paddr_t xs_address(void);

/**
 * Fetch the contents of a directory in the XenStore.
 *
 * \param t       The XenStore transaction covering this request.
 * \param dir     The dirname of the path to read.
 * \param node    The basename of the path to read.
 * \param num     The returned number of directory entries.
 * \param result  An array of directory entry strings.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 *
 * \note The results buffer is malloced and should be free'd by the
 *       caller with 'free(*result, M_XENSTORE)'.
 */
int xs_directory(struct xs_transaction t, const char *dir,
    const char *node, unsigned int *num, const char ***result);

/**
 * Determine if a path exists in the XenStore.
 *
 * \param t       The XenStore transaction covering this request.
 * \param dir     The dirname of the path to read.
 * \param node    The basename of the path to read.
 *
 * \retval 1  The path exists.
 * \retval 0  The path does not exist or an error occurred attempting
 *            to make that determination.
 */
int xs_exists(struct xs_transaction t, const char *dir, const char *node);

/**
 * Get the contents of a single "file".  Returns the contents in
 * *result which should be freed with free(*result, M_XENSTORE) after
 * use.  The length of the value in bytes is returned in *len.
 *
 * \param t       The XenStore transaction covering this request.
 * \param dir     The dirname of the file to read.
 * \param node    The basename of the file to read.
 * \param len     The amount of data read.
 * \param result  The returned contents from this file.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 *
 * \note The results buffer is malloced and should be free'd by the
 *       caller with 'free(*result, M_XENSTORE)'.
 */
int xs_read(struct xs_transaction t, const char *dir,
    const char *node, unsigned int *len, void **result);

/**
 * Write to a single file.
 *
 * \param t       The XenStore transaction covering this request.
 * \param dir     The dirname of the file to write.
 * \param node    The basename of the file to write.
 * \param string  The NUL terminated string of data to write.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 */
int xs_write(struct xs_transaction t, const char *dir,
    const char *node, const char *string);

/**
 * Create a new directory.
 *
 * \param t       The XenStore transaction covering this request.
 * \param dir     The dirname of the directory to create.
 * \param node    The basename of the directory to create.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 */
int xs_mkdir(struct xs_transaction t, const char *dir,
    const char *node);

/**
 * Remove a file or directory (directories must be empty).
 *
 * \param t       The XenStore transaction covering this request.
 * \param dir     The dirname of the directory to remove.
 * \param node    The basename of the directory to remove.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 */
int xs_rm(struct xs_transaction t, const char *dir, const char *node);

/**
 * Destroy a tree of files rooted at dir/node.
 *
 * \param t       The XenStore transaction covering this request.
 * \param dir     The dirname of the directory to remove.
 * \param node    The basename of the directory to remove.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 */
int xs_rm_tree(struct xs_transaction t, const char *dir,
    const char *node);

/**
 * Start a transaction.
 *
 * Changes by others will not be seen during the lifetime of this
 * transaction, and changes will not be visible to others until it
 * is committed (xs_transaction_end).
 *
 * \param t  The returned transaction.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 */
int xs_transaction_start(struct xs_transaction *t);

/**
 * End a transaction.
 *
 * \param t      The transaction to end/commit.
 * \param abort  If non-zero, the transaction is discarded
 * 		 instead of committed.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 */
int xs_transaction_end(struct xs_transaction t, int abort);

/*
 * Single file read and scanf parsing of the result.
 *
 * \param t           The XenStore transaction covering this request.
 * \param dir         The dirname of the path to read.
 * \param node        The basename of the path to read.
 * \param scancountp  The number of input values assigned (i.e. the result
 *      	      of scanf).
 * \param fmt         Scanf format string followed by a variable number of
 *                    scanf input arguments.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 */
int xs_scanf(struct xs_transaction t,
    const char *dir, const char *node, int *scancountp, const char *fmt, ...)
    __attribute__((format(scanf, 5, 6)));

/**
 * Printf formatted write to a XenStore file.
 *
 * \param t     The XenStore transaction covering this request.
 * \param dir   The dirname of the path to read.
 * \param node  The basename of the path to read.
 * \param fmt   Printf format string followed by a variable number of
 *              printf arguments.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of write failure.
 */
int xs_printf(struct xs_transaction t, const char *dir,
    const char *node, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

/**
 * va_list version of xenbus_printf().
 *
 * \param t     The XenStore transaction covering this request.
 * \param dir   The dirname of the path to read.
 * \param node  The basename of the path to read.
 * \param fmt   Printf format string.
 * \param ap    Va_list of printf arguments.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of write failure.
 */
int xs_vprintf(struct xs_transaction t, const char *dir,
    const char *node, const char *fmt, va_list ap);

/**
 * Multi-file read within a single directory and scanf parsing of
 * the results.
 *
 * \param t    The XenStore transaction covering this request.
 * \param dir  The dirname of the paths to read.
 * \param ...  A variable number of argument triples specifying
 *             the file name, scanf-style format string, and
 *             output variable (pointer to storage of the results).
 *             The last triple in the call must be terminated
 *             will a final NULL argument.  A NULL format string
 *             will cause the entire contents of the given file
 *             to be assigned as a NUL terminated, M_XENSTORE heap
 *             backed, string to the output parameter of that tuple.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of read failure.
 *
 * Example:
 *         char protocol_abi[64];
 *         uint32_t ring_ref;
 *         char *dev_type;
 *         int error;
 *
 *         error = xenbus_gather(XBT_NIL, xenbus_get_node(dev),
 *             "ring-ref", "%" PRIu32, &ring_ref,
 *             "protocol", "%63s", protocol_abi,
 *             "device-type", NULL, &dev_type,
 *             NULL);
 *
 *         ...
 *
 *         free(dev_type, M_XENSTORE);
 */
int xs_gather(struct xs_transaction t, const char *dir, ...);

/**
 * Register a XenStore watch.
 *
 * XenStore watches allow a client to be notified via a callback (embedded
 * within the watch object) of changes to an object in the XenStore.
 *
 * \param watch  An xs_watch struct with it's node and callback fields
 *               properly initialized.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of write failure.  EEXIST errors from the XenStore
 *          are supressed, allowing multiple, physically different,
 *          xenbus_watch objects, to watch the same path in the XenStore.
 */
int xs_register_watch(struct xs_watch *watch);
 
/**
 * Unregister a XenStore watch.
 *
 * \param watch  An xs_watch object previously used in a successful call
 *		 to xs_register_watch().
 *
 * The xs_watch object's node field is not altered by this call.
 * It is the caller's responsibility to properly dispose of both the
 * watch object and the data pointed to by watch->node.
 */
void xs_unregister_watch(struct xs_watch *watch);

/**
 * Allocate and return an sbuf containing the XenStore path string
 * <dir>/<name>.  If name is the NUL string, the returned sbuf contains
 * the path string <dir>.
 *
 * \param dir	The NUL terminated directory prefix for new path.
 * \param name  The NUL terminated basename for the new path.
 *
 * \return  A buffer containing the joined path.
 */
struct sbuf *xs_join(const char *, const char *);

/**
 * Lock the xenstore request mutex.
 */
void xs_lock(void);

/**
 * Unlock the xenstore request mutex.
 */
void xs_unlock(void);

#endif /* _XEN_XENSTORE_XENSTOREVAR_H */

