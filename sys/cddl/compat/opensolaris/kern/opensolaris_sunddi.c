/*-
 * Copyright (c) 2010 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/limits.h>
#include <sys/misc.h>
#include <sys/sunddi.h>
#include <sys/sysctl.h>

int
ddi_strtol(const char *str, char **nptr, int base, long *result)
{

	*result = strtol(str, nptr, base);
	return (0);
}

int
ddi_strtoul(const char *str, char **nptr, int base, unsigned long *result)
{

	if (str == hw_serial) {
		*result = prison0.pr_hostid;
		return (0);
	}

	*result = strtoul(str, nptr, base);
	return (0);
}

int
ddi_strtoull(const char *str, char **nptr, int base, unsigned long long *result)
{

	*result = (unsigned long long)strtouq(str, nptr, base);
	return (0);
}

int
ddi_strtoll(const char *str, char **nptr, int base, long long *result)
{

	*result = (long long)strtoq(str, nptr, base);
	return (0);
}

struct ddi_soft_state_item {
	int	 ssi_item;
	void	*ssi_data;
	LIST_ENTRY(ddi_soft_state_item) ssi_next;
};

struct ddi_soft_state {
	size_t		ss_size;
	kmutex_t	ss_lock;
	LIST_HEAD(, ddi_soft_state_item) ss_list;
};

static void *
ddi_get_soft_state_locked(struct ddi_soft_state *ss, int item)
{
	struct ddi_soft_state_item *itemp;

	ASSERT(MUTEX_HELD(&ss->ss_lock));

	LIST_FOREACH(itemp, &ss->ss_list, ssi_next) {
		if (itemp->ssi_item == item)
			return (itemp->ssi_data);
	}
	return (NULL);
}

void *
ddi_get_soft_state(void *state, int item)
{
	struct ddi_soft_state *ss = state;
	void *data;

	mutex_enter(&ss->ss_lock);
	data = ddi_get_soft_state_locked(ss, item);
	mutex_exit(&ss->ss_lock);
	return (data);
}

int
ddi_soft_state_zalloc(void *state, int item)
{
	struct ddi_soft_state *ss = state;
	struct ddi_soft_state_item *itemp;

	itemp = kmem_alloc(sizeof(*itemp), KM_SLEEP);
	itemp->ssi_item = item;
	itemp->ssi_data = kmem_zalloc(ss->ss_size, KM_SLEEP);

	mutex_enter(&ss->ss_lock);
	if (ddi_get_soft_state_locked(ss, item) != NULL) {
		mutex_exit(&ss->ss_lock);
		kmem_free(itemp->ssi_data, ss->ss_size);
		kmem_free(itemp, sizeof(*itemp));
		return (DDI_FAILURE);
	}
	LIST_INSERT_HEAD(&ss->ss_list, itemp, ssi_next);
	mutex_exit(&ss->ss_lock);
	return (DDI_SUCCESS);
}

static void
ddi_soft_state_free_locked(struct ddi_soft_state *ss, int item)
{
	struct ddi_soft_state_item *itemp;

	ASSERT(MUTEX_HELD(&ss->ss_lock));

	LIST_FOREACH(itemp, &ss->ss_list, ssi_next) {
		if (itemp->ssi_item == item)
			break;
	}
	if (itemp != NULL) {
		LIST_REMOVE(itemp, ssi_next);
		kmem_free(itemp->ssi_data, ss->ss_size);
		kmem_free(itemp, sizeof(*itemp));
	}
}

void
ddi_soft_state_free(void *state, int item)
{
	struct ddi_soft_state *ss = state;

	mutex_enter(&ss->ss_lock);
	ddi_soft_state_free_locked(ss, item);
	mutex_exit(&ss->ss_lock);
}

int
ddi_soft_state_init(void **statep, size_t size, size_t nitems __unused)
{
	struct ddi_soft_state *ss;

	ss = kmem_alloc(sizeof(*ss), KM_SLEEP);
	mutex_init(&ss->ss_lock, NULL, MUTEX_DEFAULT, NULL);
	ss->ss_size = size;
	LIST_INIT(&ss->ss_list);
	*statep = ss;
	return (0);
}

void
ddi_soft_state_fini(void **statep)
{
	struct ddi_soft_state *ss = *statep;
	struct ddi_soft_state_item *itemp;
	int item;

	mutex_enter(&ss->ss_lock);
	while ((itemp = LIST_FIRST(&ss->ss_list)) != NULL) {
		item = itemp->ssi_item;
		ddi_soft_state_free_locked(ss, item);
	}
	mutex_exit(&ss->ss_lock);
	mutex_destroy(&ss->ss_lock);
	kmem_free(ss, sizeof(*ss));

	*statep = NULL;
}
