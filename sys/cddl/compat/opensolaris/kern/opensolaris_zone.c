/*-
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/jail.h>
#include <sys/osd.h>
#include <sys/priv.h>
#include <sys/zone.h>

static MALLOC_DEFINE(M_ZONES, "zones_data", "Zones data");

/*
 * Structure to record list of ZFS datasets exported to a zone.
 */
typedef struct zone_dataset {
	LIST_ENTRY(zone_dataset) zd_next;
	char	zd_dataset[0];
} zone_dataset_t;

LIST_HEAD(zone_dataset_head, zone_dataset);

static int zone_slot;

int
zone_dataset_attach(struct ucred *cred, const char *dataset, int jailid)
{
	struct zone_dataset_head *head;
	zone_dataset_t *zd, *zd2;
	struct prison *pr;
	int dofree, error;

	if ((error = priv_check_cred(cred, PRIV_ZFS_JAIL)) != 0)
		return (error);

	/* Allocate memory before we grab prison's mutex. */
	zd = malloc(sizeof(*zd) + strlen(dataset) + 1, M_ZONES, M_WAITOK);

	sx_slock(&allprison_lock);
	pr = prison_find(jailid);	/* Locks &pr->pr_mtx. */
	sx_sunlock(&allprison_lock);
	if (pr == NULL) {
		free(zd, M_ZONES);
		return (ENOENT);
	}

	head = osd_jail_get(pr, zone_slot);
	if (head != NULL) {
		dofree = 0;
		LIST_FOREACH(zd2, head, zd_next) {
			if (strcmp(dataset, zd2->zd_dataset) == 0) {
				free(zd, M_ZONES);
				error = EEXIST;
				goto end;
			}
		}
	} else {
		dofree = 1;
		prison_hold_locked(pr);
		mtx_unlock(&pr->pr_mtx);
		head = malloc(sizeof(*head), M_ZONES, M_WAITOK);
		LIST_INIT(head);
		mtx_lock(&pr->pr_mtx);
		error = osd_jail_set(pr, zone_slot, head);
		KASSERT(error == 0, ("osd_jail_set() failed (error=%d)", error));
	}
	strcpy(zd->zd_dataset, dataset);
	LIST_INSERT_HEAD(head, zd, zd_next);
end:
	if (dofree)
		prison_free_locked(pr);
	else
		mtx_unlock(&pr->pr_mtx);
	return (error);
}

int
zone_dataset_detach(struct ucred *cred, const char *dataset, int jailid)
{
	struct zone_dataset_head *head;
	zone_dataset_t *zd;
	struct prison *pr;
	int error;

	if ((error = priv_check_cred(cred, PRIV_ZFS_JAIL)) != 0)
		return (error);

	sx_slock(&allprison_lock);
	pr = prison_find(jailid);
	sx_sunlock(&allprison_lock);
	if (pr == NULL)
		return (ENOENT);
	head = osd_jail_get(pr, zone_slot);
	if (head == NULL) {
		error = ENOENT;
		goto end;
	}
	LIST_FOREACH(zd, head, zd_next) {
		if (strcmp(dataset, zd->zd_dataset) == 0)
			break;
	}
	if (zd == NULL)
		error = ENOENT;
	else {
		LIST_REMOVE(zd, zd_next);
		free(zd, M_ZONES);
		if (LIST_EMPTY(head))
			osd_jail_del(pr, zone_slot);
		error = 0;
	}
end:
	mtx_unlock(&pr->pr_mtx);
	return (error);
}

/*
 * Returns true if the named dataset is visible in the current zone.
 * The 'write' parameter is set to 1 if the dataset is also writable.
 */
int
zone_dataset_visible(const char *dataset, int *write)
{
	struct zone_dataset_head *head;
	zone_dataset_t *zd;
	struct prison *pr;
	size_t len;
	int ret = 0;

	if (dataset[0] == '\0')
		return (0);
	if (INGLOBALZONE(curthread)) {
		if (write != NULL)
			*write = 1;
		return (1);
	}
	pr = curthread->td_ucred->cr_prison;
	mtx_lock(&pr->pr_mtx);
	head = osd_jail_get(pr, zone_slot);
	if (head == NULL)
		goto end;

	/*
	 * Walk the list once, looking for datasets which match exactly, or
	 * specify a dataset underneath an exported dataset.  If found, return
	 * true and note that it is writable.
	 */
	LIST_FOREACH(zd, head, zd_next) {
		len = strlen(zd->zd_dataset);
		if (strlen(dataset) >= len &&
		    bcmp(dataset, zd->zd_dataset, len) == 0 &&
		    (dataset[len] == '\0' || dataset[len] == '/' ||
		    dataset[len] == '@')) {
			if (write)
				*write = 1;
			ret = 1;
			goto end;
		}
	}

	/*
	 * Walk the list a second time, searching for datasets which are parents
	 * of exported datasets.  These should be visible, but read-only.
	 *
	 * Note that we also have to support forms such as 'pool/dataset/', with
	 * a trailing slash.
	 */
	LIST_FOREACH(zd, head, zd_next) {
		len = strlen(dataset);
		if (dataset[len - 1] == '/')
			len--;	/* Ignore trailing slash */
		if (len < strlen(zd->zd_dataset) &&
		    bcmp(dataset, zd->zd_dataset, len) == 0 &&
		    zd->zd_dataset[len] == '/') {
			if (write)
				*write = 0;
			ret = 1;
			goto end;
		}
	}
end:
	mtx_unlock(&pr->pr_mtx);
	return (ret);
}

static void
zone_destroy(void *arg)
{
	struct zone_dataset_head *head;
	zone_dataset_t *zd;

	head = arg;
        while ((zd = LIST_FIRST(head)) != NULL) {
                LIST_REMOVE(zd, zd_next);
                free(zd, M_ZONES);
        }
        free(head, M_ZONES);
}

uint32_t
zone_get_hostid(void *ptr)
{

	KASSERT(ptr == NULL, ("only NULL pointer supported in %s", __func__));

	return ((uint32_t)curthread->td_ucred->cr_prison->pr_hostid);
}

static void
zone_sysinit(void *arg __unused)
{

	zone_slot = osd_jail_register(zone_destroy, NULL);
}

static void
zone_sysuninit(void *arg __unused)
{

	osd_jail_deregister(zone_slot);
}

SYSINIT(zone_sysinit, SI_SUB_DRIVERS, SI_ORDER_ANY, zone_sysinit, NULL);
SYSUNINIT(zone_sysuninit, SI_SUB_DRIVERS, SI_ORDER_ANY, zone_sysuninit, NULL);
