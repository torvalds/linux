/*
 * xenstore_dev.c
 * 
 * Driver giving user-space access to the kernel's connection to the
 * XenStore service.
 * 
 * Copyright (c) 2005, Christian Limpach
 * Copyright (c) 2005, Rusty Russell, IBM Corporation
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

#include <sys/types.h>
#include <sys/cdefs.h>
#include <sys/errno.h>
#include <sys/uio.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/module.h>
#include <sys/selinfo.h>
#include <sys/poll.h>

#include <xen/xen-os.h>

#include <xen/hypervisor.h>
#include <xen/xenstore/xenstorevar.h>
#include <xen/xenstore/xenstore_internal.h>

struct xs_dev_transaction {
	LIST_ENTRY(xs_dev_transaction) list;
	struct xs_transaction handle;
};

struct xs_dev_watch {
	LIST_ENTRY(xs_dev_watch) list;
	struct xs_watch watch;
	char *token;
	struct xs_dev_data *user;
};

struct xs_dev_data {
	/* In-progress transaction. */
	LIST_HEAD(, xs_dev_transaction) transactions;

	/* Active watches. */
	LIST_HEAD(, xs_dev_watch) watches;

	/* Partial request. */
	unsigned int len;
	union {
		struct xsd_sockmsg msg;
		char buffer[PAGE_SIZE];
	} u;

	/* Response queue. */
#define MASK_READ_IDX(idx) ((idx)&(PAGE_SIZE-1))
	char read_buffer[PAGE_SIZE];
	unsigned int read_cons, read_prod;

	/* Serializes writes to the read buffer. */
	struct mtx lock;

	/* Polling structure (for reads only ATM). */
	struct selinfo ev_rsel;
};

static void
xs_queue_reply(struct xs_dev_data *u, const char *data, unsigned int len)
{
	unsigned int i;

	for (i = 0; i < len; i++, u->read_prod++)
		u->read_buffer[MASK_READ_IDX(u->read_prod)] = data[i];

	KASSERT((u->read_prod - u->read_cons) <= sizeof(u->read_buffer),
	    ("xenstore reply too big"));

	wakeup(u);
	selwakeup(&u->ev_rsel);
}

static const char *
xs_dev_error_to_string(int error)
{
	unsigned int i;

	for (i = 0; i < nitems(xsd_errors); i++)
		if (xsd_errors[i].errnum == error)
			return (xsd_errors[i].errstring);

	return (NULL);
}

static void
xs_dev_return_error(struct xs_dev_data *u, int error, int req_id, int tx_id)
{
	struct xsd_sockmsg msg;
	const char *payload;

	msg.type = XS_ERROR;
	msg.req_id = req_id;
	msg.tx_id = tx_id;
	payload = NULL;


	payload = xs_dev_error_to_string(error);
	if (payload == NULL)
		payload = xs_dev_error_to_string(EINVAL);
	KASSERT(payload != NULL, ("Unable to find string for EINVAL errno"));

	msg.len = strlen(payload) + 1;

	mtx_lock(&u->lock);
	xs_queue_reply(u, (char *)&msg, sizeof(msg));
	xs_queue_reply(u, payload, msg.len);
	mtx_unlock(&u->lock);
}

static int
xs_dev_watch_message_parse_string(const char **p, const char *end,
    const char **string_r)
{
	const char *nul;

	nul = memchr(*p, 0, end - *p);
	if (!nul)
		return (EINVAL);

	*string_r = *p;
	*p = nul+1;

	return (0);
}

static int
xs_dev_watch_message_parse(const struct xsd_sockmsg *msg, const char **path_r,
    const char **token_r)
{
	const char *p, *end;
	int error;

	p = (const char *)msg + sizeof(*msg);
	end = p + msg->len;
	KASSERT(p <= end, ("payload overflow"));

	error = xs_dev_watch_message_parse_string(&p, end, path_r);
	if (error)
		return (error);
	error = xs_dev_watch_message_parse_string(&p, end, token_r);
	if (error)
		return (error);

	return (0);
}

static struct xs_dev_watch *
xs_dev_find_watch(struct xs_dev_data *u, const char *token)
{
	struct xs_dev_watch *watch;

	LIST_FOREACH(watch, &u->watches, list)
		if (strcmp(watch->token, token) == 0)
			return (watch);

	return (NULL);
}

static void
xs_dev_watch_cb(struct xs_watch *watch, const char **vec, unsigned int len)
{
	struct xs_dev_watch *dwatch;
	struct xsd_sockmsg msg;
	char *payload;

	dwatch = (struct xs_dev_watch *)watch->callback_data;
	msg.type = XS_WATCH_EVENT;
	msg.req_id = msg.tx_id = 0;
	msg.len = strlen(vec[XS_WATCH_PATH]) + strlen(dwatch->token) + 2;

	payload = malloc(msg.len, M_XENSTORE, M_WAITOK);
	strcpy(payload, vec[XS_WATCH_PATH]);
	strcpy(&payload[strlen(vec[XS_WATCH_PATH]) + 1], dwatch->token);
	mtx_lock(&dwatch->user->lock);
	xs_queue_reply(dwatch->user, (char *)&msg, sizeof(msg));
	xs_queue_reply(dwatch->user, payload, msg.len);
	mtx_unlock(&dwatch->user->lock);
	free(payload, M_XENSTORE);
}

static struct xs_dev_transaction *
xs_dev_find_transaction(struct xs_dev_data *u, uint32_t tx_id)
{
	struct xs_dev_transaction *trans;

	LIST_FOREACH(trans, &u->transactions, list)
		if (trans->handle.id == tx_id)
			return (trans);

	return (NULL);
}

static int 
xs_dev_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	int error;
	struct xs_dev_data *u;

	error = devfs_get_cdevpriv((void **)&u);
	if (error != 0)
		return (error);

	while (u->read_prod == u->read_cons) {
		error = tsleep(u, PCATCH, "xsdread", hz/10);
		if (error && error != EWOULDBLOCK)
			return (error);
	}

	while (uio->uio_resid > 0) {
		if (u->read_cons == u->read_prod)
			break;
		error = uiomove(&u->read_buffer[MASK_READ_IDX(u->read_cons)],
		    1, uio);
		if (error)
			return (error);
		u->read_cons++;
	}
	return (0);
}

static int 
xs_dev_write(struct cdev *dev, struct uio *uio, int ioflag)
{
	int error;
	const char *wpath, *wtoken;
	struct xs_dev_data *u;
	struct xs_dev_transaction *trans;
	struct xs_dev_watch *watch;
	void *reply;
	static const char *ok = "OK";
	int len = uio->uio_resid;

	error = devfs_get_cdevpriv((void **)&u);
	if (error != 0)
		return (error);

	if ((len + u->len) > sizeof(u->u.buffer))
		return (EINVAL);

	error = uiomove(u->u.buffer + u->len, len, uio);
	if (error)
		return (error);

	u->len += len;
	if (u->len < (sizeof(u->u.msg) + u->u.msg.len))
		return (0);

	switch (u->u.msg.type) {
	case XS_TRANSACTION_START:
	case XS_TRANSACTION_END:
	case XS_DIRECTORY:
	case XS_READ:
	case XS_GET_PERMS:
	case XS_RELEASE:
	case XS_GET_DOMAIN_PATH:
	case XS_WRITE:
	case XS_MKDIR:
	case XS_RM:
	case XS_SET_PERMS:
		/* Check that this transaction id is not hijacked. */
		if (u->u.msg.tx_id != 0 &&
		    xs_dev_find_transaction(u, u->u.msg.tx_id) == NULL) {
			error = EINVAL;
			break;
		}
		error = xs_dev_request_and_reply(&u->u.msg, &reply);
		if (!error) {
			if (u->u.msg.type == XS_TRANSACTION_START) {
				trans = malloc(sizeof(*trans), M_XENSTORE,
				    M_WAITOK);
				trans->handle.id = strtoul(reply, NULL, 0);
				LIST_INSERT_HEAD(&u->transactions, trans, list);
			} else if (u->u.msg.type == XS_TRANSACTION_END) {
				trans = xs_dev_find_transaction(u,
				    u->u.msg.tx_id);
				KASSERT(trans != NULL,
				    ("Unable to find transaction"));
				LIST_REMOVE(trans, list);
				free(trans, M_XENSTORE);
			}
			mtx_lock(&u->lock);
			xs_queue_reply(u, (char *)&u->u.msg, sizeof(u->u.msg));
			xs_queue_reply(u, (char *)reply, u->u.msg.len);
			mtx_unlock(&u->lock);
			free(reply, M_XENSTORE);
		}
		break;
	case XS_WATCH:
		u->u.msg.tx_id = 0;
		error = xs_dev_watch_message_parse(&u->u.msg, &wpath, &wtoken);
		if (error)
			break;
		if (xs_dev_find_watch(u, wtoken) != NULL) {
			error = EINVAL;
			break;
		}

		watch = malloc(sizeof(*watch), M_XENSTORE, M_WAITOK);
		watch->watch.node = strdup(wpath, M_XENSTORE);
		watch->watch.callback = xs_dev_watch_cb;
		watch->watch.callback_data = (uintptr_t)watch;
		watch->token = strdup(wtoken, M_XENSTORE);
		watch->user = u;

		error = xs_register_watch(&watch->watch);
		if (error != 0) {
			free(watch->token, M_XENSTORE);
			free(watch->watch.node, M_XENSTORE);
			free(watch, M_XENSTORE);
			break;
		}

		LIST_INSERT_HEAD(&u->watches, watch, list);
		u->u.msg.len = sizeof(ok);
		mtx_lock(&u->lock);
		xs_queue_reply(u, (char *)&u->u.msg, sizeof(u->u.msg));
		xs_queue_reply(u, ok, sizeof(ok));
		mtx_unlock(&u->lock);
		break;
	case XS_UNWATCH:
		u->u.msg.tx_id = 0;
		error = xs_dev_watch_message_parse(&u->u.msg, &wpath, &wtoken);
		if (error)
			break;
		watch = xs_dev_find_watch(u, wtoken);
		if (watch == NULL) {
			error = EINVAL;
			break;
		}

		LIST_REMOVE(watch, list);
		xs_unregister_watch(&watch->watch);
		free(watch->watch.node, M_XENSTORE);
		free(watch->token, M_XENSTORE);
		free(watch, M_XENSTORE);
		u->u.msg.len = sizeof(ok);
		mtx_lock(&u->lock);
		xs_queue_reply(u, (char *)&u->u.msg, sizeof(u->u.msg));
		xs_queue_reply(u, ok, sizeof(ok));
		mtx_unlock(&u->lock);
		break;
	default:
		error = EINVAL;
		break;
	}

	if (error != 0)
		xs_dev_return_error(u, error, u->u.msg.req_id, u->u.msg.tx_id);

	/* Reset the write buffer. */
	u->len = 0;

	return (0);
}

static int
xs_dev_poll(struct cdev *dev, int events, struct thread *td)
{
	struct xs_dev_data *u;
	int error, mask;

	error = devfs_get_cdevpriv((void **)&u);
	if (error != 0)
		return (POLLERR);

	/* we can always write */
	mask = events & (POLLOUT | POLLWRNORM);

	if (events & (POLLIN | POLLRDNORM)) {
		if (u->read_cons != u->read_prod) {
			mask |= events & (POLLIN | POLLRDNORM);
		} else {
			/* Record that someone is waiting */
			selrecord(td, &u->ev_rsel);
		}
	}

	return (mask);
}

static void
xs_dev_dtor(void *arg)
{
	struct xs_dev_data *u = arg;
	struct xs_dev_transaction *trans, *tmpt;
	struct xs_dev_watch *watch, *tmpw;

	seldrain(&u->ev_rsel);

	LIST_FOREACH_SAFE(trans, &u->transactions, list, tmpt) {
		xs_transaction_end(trans->handle, 1);
		LIST_REMOVE(trans, list);
		free(trans, M_XENSTORE);
	}

	LIST_FOREACH_SAFE(watch, &u->watches, list, tmpw) {
		LIST_REMOVE(watch, list);
		xs_unregister_watch(&watch->watch);
		free(watch->watch.node, M_XENSTORE);
		free(watch->token, M_XENSTORE);
		free(watch, M_XENSTORE);
	}
	mtx_destroy(&u->lock);

	free(u, M_XENSTORE);
}

static int
xs_dev_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct xs_dev_data *u;
	int error;

	u = malloc(sizeof(*u), M_XENSTORE, M_WAITOK|M_ZERO);
	mtx_init(&u->lock, "xsdev_lock", NULL, MTX_DEF);
	LIST_INIT(&u->transactions);
	LIST_INIT(&u->watches);
	error = devfs_set_cdevpriv(u, xs_dev_dtor);
	if (error != 0)
		free(u, M_XENSTORE);

	return (error);
}

static struct cdevsw xs_dev_cdevsw = {
	.d_version = D_VERSION,	
	.d_read = xs_dev_read,
	.d_write = xs_dev_write,
	.d_open = xs_dev_open,
	.d_poll = xs_dev_poll,
	.d_name = "xs_dev",
};

/*------------------ Private Device Attachment Functions  --------------------*/
/**
 * \brief Identify instances of this device type in the system.
 *
 * \param driver  The driver performing this identify action.
 * \param parent  The NewBus parent device for any devices this method adds.
 */
static void
xs_dev_identify(driver_t *driver __unused, device_t parent)
{
	/*
	 * A single device instance for our driver is always present
	 * in a system operating under Xen.
	 */
	BUS_ADD_CHILD(parent, 0, driver->name, 0);
}

/**
 * \brief Probe for the existence of the Xenstore device
 *
 * \param dev  NewBus device_t for this instance.
 *
 * \return  Always returns 0 indicating success.
 */
static int 
xs_dev_probe(device_t dev)
{

	device_set_desc(dev, "Xenstore user-space device");
	return (0);
}

/**
 * \brief Attach the Xenstore device.
 *
 * \param dev  NewBus device_t for this instance.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 */
static int
xs_dev_attach(device_t dev)
{
	struct cdev *xs_cdev;

	xs_cdev = make_dev_credf(MAKEDEV_ETERNAL, &xs_dev_cdevsw, 0, NULL,
	    UID_ROOT, GID_WHEEL, 0400, "xen/xenstore");
	if (xs_cdev == NULL)
		return (EINVAL);

	return (0);
}

/*-------------------- Private Device Attachment Data  -----------------------*/
static device_method_t xs_dev_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	xs_dev_identify),
	DEVMETHOD(device_probe,         xs_dev_probe),
	DEVMETHOD(device_attach,        xs_dev_attach),

	DEVMETHOD_END
};

DEFINE_CLASS_0(xs_dev, xs_dev_driver, xs_dev_methods, 0);
devclass_t xs_dev_devclass;

DRIVER_MODULE(xs_dev, xenstore, xs_dev_driver, xs_dev_devclass,
    NULL, NULL);
