/*-
 * Copyright (c) 2015-2016 Andriy Voskoboinyk <avos@FreeBSD.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/bus.h>
#include <sys/endian.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/rtwn/if_rtwnvar.h>

#include <dev/rtwn/if_rtwn_task.h>


static void
rtwn_cmdq_cb(void *arg, int pending)
{
	struct rtwn_softc *sc = arg;
	struct rtwn_cmdq *item;

	/*
	 * Device must be powered on (via rtwn_power_on())
	 * before any command may be sent.
	 */
	RTWN_LOCK(sc);
	if (!(sc->sc_flags & RTWN_RUNNING)) {
		RTWN_UNLOCK(sc);
		return;
	}

	RTWN_CMDQ_LOCK(sc);
	while (sc->cmdq[sc->cmdq_first].func != NULL) {
		item = &sc->cmdq[sc->cmdq_first];
		sc->cmdq_first = (sc->cmdq_first + 1) % RTWN_CMDQ_SIZE;
		RTWN_CMDQ_UNLOCK(sc);

		item->func(sc, &item->data);

		RTWN_CMDQ_LOCK(sc);
		memset(item, 0, sizeof (*item));
	}
	RTWN_CMDQ_UNLOCK(sc);
	RTWN_UNLOCK(sc);
}

void
rtwn_cmdq_init(struct rtwn_softc *sc)
{
	RTWN_CMDQ_LOCK_INIT(sc);
	TASK_INIT(&sc->cmdq_task, 0, rtwn_cmdq_cb, sc);
}

void
rtwn_cmdq_destroy(struct rtwn_softc *sc)
{
	if (RTWN_CMDQ_LOCK_INITIALIZED(sc))
		RTWN_CMDQ_LOCK_DESTROY(sc);
}

int
rtwn_cmd_sleepable(struct rtwn_softc *sc, const void *ptr, size_t len,
    CMD_FUNC_PROTO)
{
	struct ieee80211com *ic = &sc->sc_ic;

	KASSERT(len <= sizeof(union sec_param), ("buffer overflow"));

	RTWN_CMDQ_LOCK(sc);
	if (sc->sc_detached) {
		RTWN_CMDQ_UNLOCK(sc);
		return (ESHUTDOWN);
	}

	if (sc->cmdq[sc->cmdq_last].func != NULL) {
		device_printf(sc->sc_dev, "%s: cmdq overflow\n", __func__);
		RTWN_CMDQ_UNLOCK(sc);

		return (EAGAIN);
	}

	if (ptr != NULL)
		memcpy(&sc->cmdq[sc->cmdq_last].data, ptr, len);
	sc->cmdq[sc->cmdq_last].func = func;
	sc->cmdq_last = (sc->cmdq_last + 1) % RTWN_CMDQ_SIZE;
	RTWN_CMDQ_UNLOCK(sc);

	ieee80211_runtask(ic, &sc->cmdq_task);

	return (0);
}
