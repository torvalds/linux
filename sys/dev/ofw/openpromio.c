/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Jake Burkholder.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/ioccom.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/openpromio.h>

/*
 * This provides a Solaris compatible character device interface to
 * Open Firmware.  It exists entirely for compatibility with software
 * like X11, and only the features that are actually needed for that
 * are implemented.  The interface sucks too much to actually use,
 * new code should use the /dev/openfirm device.
 */

static d_open_t openprom_open;
static d_close_t openprom_close;
static d_ioctl_t openprom_ioctl;

static int openprom_modevent(module_t mode, int type, void *data);
static int openprom_node_valid(phandle_t node);
static int openprom_node_search(phandle_t root, phandle_t node);

static struct cdevsw openprom_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_NEEDGIANT,
	.d_open =	openprom_open,
	.d_close =	openprom_close,
	.d_ioctl =	openprom_ioctl,
	.d_name =	"openprom",
};

static int openprom_is_open;
static struct cdev *openprom_dev;
static phandle_t openprom_node;

static int
openprom_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{

	if (openprom_is_open != 0)
		return (EBUSY);
	openprom_is_open = 1;
	return (0);
}

static int
openprom_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{

	openprom_is_open = 0;
	return (0);
}

static int
openprom_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flags,
    struct thread *td)
{
	struct openpromio *oprom;
	phandle_t node;
	uint32_t len;
	size_t done;
	int proplen;
	char *prop;
	char *buf;
	int error;

	if ((flags & FREAD) == 0)
		return (EPERM);

	prop = buf = NULL;
	error = 0;
	switch (cmd) {
	case OPROMCHILD:
	case OPROMNEXT:
		if (data == NULL || *(void **)data == NULL)
			return (EINVAL);
		oprom = *(void **)data;
		error = copyin(&oprom->oprom_size, &len, sizeof(len));
		if (error != 0)
			break;
		if (len != sizeof(node)) {
			error = EINVAL;
			break;
		}
		error = copyin(&oprom->oprom_array, &node, sizeof(node));
		if (error != 0)
			break;
		error = openprom_node_valid(node);
		if (error != 0)
			break;
		switch (cmd) {
		case OPROMCHILD:
			node = OF_child(node);
			break;
		case OPROMNEXT:
			node = OF_peer(node);
			break;
		}
		error = copyout(&node, &oprom->oprom_array, sizeof(node));
		if (error != 0)
			break;
		openprom_node = node;
		break;
	case OPROMGETPROP:
	case OPROMNXTPROP:
		if (data == NULL || *(void **)data == NULL)
			return (EINVAL);
		oprom = *(void **)data;
		error = copyin(&oprom->oprom_size, &len, sizeof(len));
		if (error != 0)
			break;
		if (len > OPROMMAXPARAM) {
			error = EINVAL;
			break;
		}
		prop = malloc(len, M_TEMP, M_WAITOK | M_ZERO);
		error = copyinstr(&oprom->oprom_array, prop, len, &done);
		if (error != 0)
			break;
		buf = malloc(OPROMMAXPARAM, M_TEMP, M_WAITOK | M_ZERO);
		node = openprom_node;
		switch (cmd) {
		case OPROMGETPROP:
			proplen = OF_getproplen(node, prop);
			if (proplen > OPROMMAXPARAM) {
				error = EINVAL;
				break;
			}
			error = OF_getprop(node, prop, buf, proplen);
			break;
		case OPROMNXTPROP:
			error = OF_nextprop(node, prop, buf, OPROMMAXPARAM);
			proplen = strlen(buf);
			break;
		}
		if (error != -1) {
			error = copyout(&proplen, &oprom->oprom_size,
			    sizeof(proplen));
			if (error == 0)
				error = copyout(buf, &oprom->oprom_array,
				    proplen + 1);
		} else
			error = EINVAL;
		break;
	default:
		error = ENOIOCTL;
		break;
	}

	if (prop != NULL)
		free(prop, M_TEMP);
	if (buf != NULL)
		free(buf, M_TEMP);

	return (error);
}

static int
openprom_node_valid(phandle_t node)
{

	if (node == 0)
		return (0);
	return (openprom_node_search(OF_peer(0), node));
}

static int
openprom_node_search(phandle_t root, phandle_t node)
{
	phandle_t child;

	if (root == node)
		return (0);
	for (child = OF_child(root); child != 0 && child != -1;
	    child = OF_peer(child))
		if (openprom_node_search(child, node) == 0)
			return (0);
	return (EINVAL);
}

static int
openprom_modevent(module_t mode, int type, void *data)
{

	switch (type) {
	case MOD_LOAD:
		openprom_dev = make_dev(&openprom_cdevsw, 0, UID_ROOT,
		    GID_WHEEL, 0600, "openprom");
		return (0);
	case MOD_UNLOAD:
		destroy_dev(openprom_dev);
		return (0);
	default:
		return (EOPNOTSUPP);
	}
}

DEV_MODULE(openprom, openprom_modevent, NULL);
