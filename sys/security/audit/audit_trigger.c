/*-
 * Copyright (c) 2005 Wayne J. Salamon
 * All rights reserved.
 *
 * This software was developed by Wayne Salamon for the TrustedBSD Project.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <sys/uio.h>

#include <security/audit/audit.h>
#include <security/audit/audit_private.h>

/*
 * Structures and operations to support the basic character special device
 * used to communicate with userland.  /dev/audit reliably delivers one-byte
 * messages to a listening application (or discards them if there is no
 * listening application).
 *
 * Currently, select/poll are not supported on the trigger device.
 */
struct trigger_info {
	unsigned int			trigger;
	TAILQ_ENTRY(trigger_info)	list;
};

static MALLOC_DEFINE(M_AUDITTRIGGER, "audit_trigger", "Audit trigger events");
static struct cdev *audit_dev;
static int audit_isopen = 0;
static TAILQ_HEAD(, trigger_info) trigger_list;
static struct mtx audit_trigger_mtx;

static int
audit_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	int error;

	/* Only one process may open the device at a time. */
	mtx_lock(&audit_trigger_mtx);
	if (!audit_isopen) {
		error = 0;
		audit_isopen = 1;
	} else
		error = EBUSY;
	mtx_unlock(&audit_trigger_mtx);

	return (error);
}

static int
audit_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	struct trigger_info *ti;

	/* Flush the queue of pending trigger events. */
	mtx_lock(&audit_trigger_mtx);
	audit_isopen = 0;
	while (!TAILQ_EMPTY(&trigger_list)) {
		ti = TAILQ_FIRST(&trigger_list);
		TAILQ_REMOVE(&trigger_list, ti, list);
		free(ti, M_AUDITTRIGGER);
	}
	mtx_unlock(&audit_trigger_mtx);

	return (0);
}

static int
audit_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	int error = 0;
	struct trigger_info *ti = NULL;

	mtx_lock(&audit_trigger_mtx);
	while (TAILQ_EMPTY(&trigger_list)) {
		error = msleep(&trigger_list, &audit_trigger_mtx,
		    PSOCK | PCATCH, "auditd", 0);
		if (error)
			break;
	}
	if (!error) {
		ti = TAILQ_FIRST(&trigger_list);
		TAILQ_REMOVE(&trigger_list, ti, list);
	}
	mtx_unlock(&audit_trigger_mtx);
	if (!error) {
		error = uiomove(&ti->trigger, sizeof(ti->trigger), uio);
		free(ti, M_AUDITTRIGGER);
	}
	return (error);
}

static int
audit_write(struct cdev *dev, struct uio *uio, int ioflag)
{

	/* Communication is kernel->userspace only. */
	return (EOPNOTSUPP);
}

int
audit_send_trigger(unsigned int trigger)
{
	struct trigger_info *ti;

	ti = malloc(sizeof *ti, M_AUDITTRIGGER, M_WAITOK);
	mtx_lock(&audit_trigger_mtx);
	if (!audit_isopen) {
		/* If nobody's listening, we ain't talking. */
		mtx_unlock(&audit_trigger_mtx);
		free(ti, M_AUDITTRIGGER);
		return (ENODEV);
	}
	ti->trigger = trigger;
	TAILQ_INSERT_TAIL(&trigger_list, ti, list);
	wakeup(&trigger_list);
	mtx_unlock(&audit_trigger_mtx);
	return (0);
}

static struct cdevsw audit_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	audit_open,
	.d_close =	audit_close,
	.d_read =	audit_read,
	.d_write =	audit_write,
	.d_name =	"audit"
};

void
audit_trigger_init(void)
{

	TAILQ_INIT(&trigger_list);
	mtx_init(&audit_trigger_mtx, "audit_trigger_mtx", NULL, MTX_DEF);
}

static void
audit_trigger_cdev_init(void *unused)
{

	/* Create the special device file. */
	audit_dev = make_dev(&audit_cdevsw, 0, UID_ROOT, GID_KMEM, 0600,
	    AUDITDEV_FILENAME);
}

SYSINIT(audit_trigger_cdev_init, SI_SUB_DRIVERS, SI_ORDER_MIDDLE,
    audit_trigger_cdev_init, NULL);
