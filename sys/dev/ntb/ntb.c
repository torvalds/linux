/*-
 * Copyright (c) 2016-2017 Alexander Motin <mav@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/rmlock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sysctl.h>

#include "ntb.h"

devclass_t ntb_hw_devclass;
SYSCTL_NODE(_hw, OID_AUTO, ntb, CTLFLAG_RW, 0, "NTB sysctls");

struct ntb_child {
	device_t	dev;
	int		function;
	int		enabled;
	int		mwoff;
	int		mwcnt;
	int		spadoff;
	int		spadcnt;
	int		dboff;
	int		dbcnt;
	uint64_t	dbmask;
	void		*ctx;
	const struct ntb_ctx_ops *ctx_ops;
	struct rmlock	ctx_lock;
	struct ntb_child *next;
};

int
ntb_register_device(device_t dev)
{
	struct ntb_child **cpp = device_get_softc(dev);
	struct ntb_child *nc;
	int i, mw, mwu, mwt, spad, spadu, spadt, db, dbu, dbt;
	char cfg[128] = "";
	char buf[32];
	char *n, *np, *c, *p, *name;

	mwu = 0;
	mwt = NTB_MW_COUNT(dev);
	spadu = 0;
	spadt = NTB_SPAD_COUNT(dev);
	dbu = 0;
	dbt = flsll(NTB_DB_VALID_MASK(dev));

	device_printf(dev, "%d memory windows, %d scratchpads, "
	    "%d doorbells\n", mwt, spadt, dbt);

	snprintf(buf, sizeof(buf), "hint.%s.%d.config", device_get_name(dev),
	    device_get_unit(dev));
	TUNABLE_STR_FETCH(buf, cfg, sizeof(cfg));
	n = cfg;
	i = 0;
	while ((c = strsep(&n, ",")) != NULL) {
		np = c;
		name = strsep(&np, ":");
		if (name != NULL && name[0] == 0)
			name = NULL;
		p = strsep(&np, ":");
		mw = (p && p[0] != 0) ? strtol(p, NULL, 10) : mwt - mwu;
		p = strsep(&np, ":");
		spad = (p && p[0] != 0) ? strtol(p, NULL, 10) : spadt - spadu;
		db = (np && np[0] != 0) ? strtol(np, NULL, 10) : dbt - dbu;

		if (mw > mwt - mwu || spad > spadt - spadu || db > dbt - dbu) {
			device_printf(dev, "Not enough resources for config\n");
			break;
		}

		nc = malloc(sizeof(*nc), M_DEVBUF, M_WAITOK | M_ZERO);
		nc->function = i;
		nc->mwoff = mwu;
		nc->mwcnt = mw;
		nc->spadoff = spadu;
		nc->spadcnt = spad;
		nc->dboff = dbu;
		nc->dbcnt = db;
		nc->dbmask = (db == 0) ? 0 : (0xffffffffffffffff >> (64 - db));
		rm_init(&nc->ctx_lock, "ntb ctx");
		nc->dev = device_add_child(dev, name, -1);
		if (nc->dev == NULL) {
			ntb_unregister_device(dev);
			return (ENOMEM);
		}
		device_set_ivars(nc->dev, nc);
		*cpp = nc;
		cpp = &nc->next;

		if (bootverbose) {
			device_printf(dev, "%d \"%s\":", i, name);
			if (mw > 0) {
				printf(" memory windows %d", mwu);
				if (mw > 1)
					printf("-%d", mwu + mw - 1);
			}
			if (spad > 0) {
				printf(" scratchpads %d", spadu);
				if (spad > 1)
					printf("-%d", spadu + spad - 1);
			}
			if (db > 0) {
				printf(" doorbells %d", dbu);
				if (db > 1)
					printf("-%d", dbu + db - 1);
			}
			printf("\n");
		}

		mwu += mw;
		spadu += spad;
		dbu += db;
		i++;
	}

	bus_generic_attach(dev);
	return (0);
}

int
ntb_unregister_device(device_t dev)
{
	struct ntb_child **cpp = device_get_softc(dev);
	struct ntb_child *nc;
	int error = 0;

	while ((nc = *cpp) != NULL) {
		*cpp = (*cpp)->next;
		error = device_delete_child(dev, nc->dev);
		if (error)
			break;
		rm_destroy(&nc->ctx_lock);
		free(nc, M_DEVBUF);
	}
	return (error);
}

int
ntb_child_location_str(device_t dev, device_t child, char *buf,
    size_t buflen)
{
	struct ntb_child *nc = device_get_ivars(child);

	snprintf(buf, buflen, "function=%d", nc->function);
	return (0);
}

int
ntb_print_child(device_t dev, device_t child)
{
	struct ntb_child *nc = device_get_ivars(child);
	int retval;

	retval = bus_print_child_header(dev, child);
	if (nc->mwcnt > 0) {
		printf(" mw %d", nc->mwoff);
		if (nc->mwcnt > 1)
			printf("-%d", nc->mwoff + nc->mwcnt - 1);
	}
	if (nc->spadcnt > 0) {
		printf(" spad %d", nc->spadoff);
		if (nc->spadcnt > 1)
			printf("-%d", nc->spadoff + nc->spadcnt - 1);
	}
	if (nc->dbcnt > 0) {
		printf(" db %d", nc->dboff);
		if (nc->dbcnt > 1)
			printf("-%d", nc->dboff + nc->dbcnt - 1);
	}
	retval += printf(" at function %d", nc->function);
	retval += bus_print_child_domain(dev, child);
	retval += bus_print_child_footer(dev, child);

	return (retval);
}

void
ntb_link_event(device_t dev)
{
	struct ntb_child **cpp = device_get_softc(dev);
	struct ntb_child *nc;
	struct rm_priotracker ctx_tracker;
	enum ntb_speed speed;
	enum ntb_width width;

	if (NTB_LINK_IS_UP(dev, &speed, &width)) {
		device_printf(dev, "Link is up (PCIe %d.x / x%d)\n",
		    (int)speed, (int)width);
	} else {
		device_printf(dev, "Link is down\n");
	}
	for (nc = *cpp; nc != NULL; nc = nc->next) {
		rm_rlock(&nc->ctx_lock, &ctx_tracker);
		if (nc->ctx_ops != NULL && nc->ctx_ops->link_event != NULL)
			nc->ctx_ops->link_event(nc->ctx);
		rm_runlock(&nc->ctx_lock, &ctx_tracker);
	}
}

void
ntb_db_event(device_t dev, uint32_t vec)
{
	struct ntb_child **cpp = device_get_softc(dev);
	struct ntb_child *nc;
	struct rm_priotracker ctx_tracker;

	for (nc = *cpp; nc != NULL; nc = nc->next) {
		rm_rlock(&nc->ctx_lock, &ctx_tracker);
		if (nc->ctx_ops != NULL && nc->ctx_ops->db_event != NULL)
			nc->ctx_ops->db_event(nc->ctx, vec);
		rm_runlock(&nc->ctx_lock, &ctx_tracker);
	}
}

bool
ntb_link_is_up(device_t ntb, enum ntb_speed *speed, enum ntb_width *width)
{

	return (NTB_LINK_IS_UP(device_get_parent(ntb), speed, width));
}

int
ntb_link_enable(device_t ntb, enum ntb_speed speed, enum ntb_width width)
{
	struct ntb_child *nc = device_get_ivars(ntb);
	struct ntb_child **cpp = device_get_softc(device_get_parent(nc->dev));
	struct ntb_child *nc1;

	for (nc1 = *cpp; nc1 != NULL; nc1 = nc1->next) {
		if (nc1->enabled) {
			nc->enabled = 1;
			return (0);
		}
	}
	nc->enabled = 1;
	return (NTB_LINK_ENABLE(device_get_parent(ntb), speed, width));
}

int
ntb_link_disable(device_t ntb)
{
	struct ntb_child *nc = device_get_ivars(ntb);
	struct ntb_child **cpp = device_get_softc(device_get_parent(nc->dev));
	struct ntb_child *nc1;

	if (!nc->enabled)
		return (0);
	nc->enabled = 0;
	for (nc1 = *cpp; nc1 != NULL; nc1 = nc1->next) {
		if (nc1->enabled)
			return (0);
	}
	return (NTB_LINK_DISABLE(device_get_parent(ntb)));
}

bool
ntb_link_enabled(device_t ntb)
{
	struct ntb_child *nc = device_get_ivars(ntb);

	return (nc->enabled && NTB_LINK_ENABLED(device_get_parent(ntb)));
}

int
ntb_set_ctx(device_t ntb, void *ctx, const struct ntb_ctx_ops *ctx_ops)
{
	struct ntb_child *nc = device_get_ivars(ntb);

	if (ctx == NULL || ctx_ops == NULL)
		return (EINVAL);

	rm_wlock(&nc->ctx_lock);
	if (nc->ctx_ops != NULL) {
		rm_wunlock(&nc->ctx_lock);
		return (EINVAL);
	}
	nc->ctx = ctx;
	nc->ctx_ops = ctx_ops;

	/*
	 * If applicaiton driver asks for link events, generate fake one now
	 * to let it update link state without races while we hold the lock.
	 */
	if (ctx_ops->link_event != NULL)
		ctx_ops->link_event(ctx);
	rm_wunlock(&nc->ctx_lock);

	return (0);
}

void *
ntb_get_ctx(device_t ntb, const struct ntb_ctx_ops **ctx_ops)
{
	struct ntb_child *nc = device_get_ivars(ntb);

	KASSERT(nc->ctx != NULL && nc->ctx_ops != NULL, ("bogus"));
	if (ctx_ops != NULL)
		*ctx_ops = nc->ctx_ops;
	return (nc->ctx);
}

void
ntb_clear_ctx(device_t ntb)
{
	struct ntb_child *nc = device_get_ivars(ntb);

	rm_wlock(&nc->ctx_lock);
	nc->ctx = NULL;
	nc->ctx_ops = NULL;
	rm_wunlock(&nc->ctx_lock);
}

uint8_t
ntb_mw_count(device_t ntb)
{
	struct ntb_child *nc = device_get_ivars(ntb);

	return (nc->mwcnt);
}

int
ntb_mw_get_range(device_t ntb, unsigned mw_idx, vm_paddr_t *base,
    caddr_t *vbase, size_t *size, size_t *align, size_t *align_size,
    bus_addr_t *plimit)
{
	struct ntb_child *nc = device_get_ivars(ntb);

	return (NTB_MW_GET_RANGE(device_get_parent(ntb), mw_idx + nc->mwoff,
	    base, vbase, size, align, align_size, plimit));
}

int
ntb_mw_set_trans(device_t ntb, unsigned mw_idx, bus_addr_t addr, size_t size)
{
	struct ntb_child *nc = device_get_ivars(ntb);

	return (NTB_MW_SET_TRANS(device_get_parent(ntb), mw_idx + nc->mwoff,
	    addr, size));
}

int
ntb_mw_clear_trans(device_t ntb, unsigned mw_idx)
{
	struct ntb_child *nc = device_get_ivars(ntb);

	return (NTB_MW_CLEAR_TRANS(device_get_parent(ntb), mw_idx + nc->mwoff));
}

int
ntb_mw_get_wc(device_t ntb, unsigned mw_idx, vm_memattr_t *mode)
{
	struct ntb_child *nc = device_get_ivars(ntb);

	return (NTB_MW_GET_WC(device_get_parent(ntb), mw_idx + nc->mwoff, mode));
}

int
ntb_mw_set_wc(device_t ntb, unsigned mw_idx, vm_memattr_t mode)
{
	struct ntb_child *nc = device_get_ivars(ntb);

	return (NTB_MW_SET_WC(device_get_parent(ntb), mw_idx + nc->mwoff, mode));
}

uint8_t
ntb_spad_count(device_t ntb)
{
	struct ntb_child *nc = device_get_ivars(ntb);

	return (nc->spadcnt);
}

void
ntb_spad_clear(device_t ntb)
{
	struct ntb_child *nc = device_get_ivars(ntb);
	unsigned i;

	for (i = 0; i < nc->spadcnt; i++)
		NTB_SPAD_WRITE(device_get_parent(ntb), i + nc->spadoff, 0);
}

int
ntb_spad_write(device_t ntb, unsigned int idx, uint32_t val)
{
	struct ntb_child *nc = device_get_ivars(ntb);

	return (NTB_SPAD_WRITE(device_get_parent(ntb), idx + nc->spadoff, val));
}

int
ntb_spad_read(device_t ntb, unsigned int idx, uint32_t *val)
{
	struct ntb_child *nc = device_get_ivars(ntb);

	return (NTB_SPAD_READ(device_get_parent(ntb), idx + nc->spadoff, val));
}

int
ntb_peer_spad_write(device_t ntb, unsigned int idx, uint32_t val)
{
	struct ntb_child *nc = device_get_ivars(ntb);

	return (NTB_PEER_SPAD_WRITE(device_get_parent(ntb), idx + nc->spadoff,
	    val));
}

int
ntb_peer_spad_read(device_t ntb, unsigned int idx, uint32_t *val)
{
	struct ntb_child *nc = device_get_ivars(ntb);

	return (NTB_PEER_SPAD_READ(device_get_parent(ntb), idx + nc->spadoff,
	    val));
}

uint64_t
ntb_db_valid_mask(device_t ntb)
{
	struct ntb_child *nc = device_get_ivars(ntb);

	return (nc->dbmask);
}

int
ntb_db_vector_count(device_t ntb)
{

	return (NTB_DB_VECTOR_COUNT(device_get_parent(ntb)));
}

uint64_t
ntb_db_vector_mask(device_t ntb, uint32_t vector)
{
	struct ntb_child *nc = device_get_ivars(ntb);

	return ((NTB_DB_VECTOR_MASK(device_get_parent(ntb), vector)
	    >> nc->dboff) & nc->dbmask);
}

int
ntb_peer_db_addr(device_t ntb, bus_addr_t *db_addr, vm_size_t *db_size)
{

	return (NTB_PEER_DB_ADDR(device_get_parent(ntb), db_addr, db_size));
}

void
ntb_db_clear(device_t ntb, uint64_t bits)
{
	struct ntb_child *nc = device_get_ivars(ntb);

	return (NTB_DB_CLEAR(device_get_parent(ntb), bits << nc->dboff));
}

void
ntb_db_clear_mask(device_t ntb, uint64_t bits)
{
	struct ntb_child *nc = device_get_ivars(ntb);

	return (NTB_DB_CLEAR_MASK(device_get_parent(ntb), bits << nc->dboff));
}

uint64_t
ntb_db_read(device_t ntb)
{
	struct ntb_child *nc = device_get_ivars(ntb);

	return ((NTB_DB_READ(device_get_parent(ntb)) >> nc->dboff)
	    & nc->dbmask);
}

void
ntb_db_set_mask(device_t ntb, uint64_t bits)
{
	struct ntb_child *nc = device_get_ivars(ntb);

	return (NTB_DB_SET_MASK(device_get_parent(ntb), bits << nc->dboff));
}

void
ntb_peer_db_set(device_t ntb, uint64_t bits)
{
	struct ntb_child *nc = device_get_ivars(ntb);

	return (NTB_PEER_DB_SET(device_get_parent(ntb), bits << nc->dboff));
}

MODULE_VERSION(ntb, 1);
