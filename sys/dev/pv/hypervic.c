/*-
 * Copyright (c) 2009-2016 Microsoft Corp.
 * Copyright (c) 2012 NetApp Inc.
 * Copyright (c) 2012 Citrix Inc.
 * Copyright (c) 2016 Mike Belopuhov <mike@esdenera.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * The OpenBSD port was done under funding by Esdenera Networks GmbH.
 */

#include <sys/param.h>

/* Hyperv requires locked atomic operations */
#ifndef MULTIPROCESSOR
#define _HYPERVMPATOMICS
#define MULTIPROCESSOR
#endif
#include <sys/atomic.h>
#ifdef _HYPERVMPATOMICS
#undef MULTIPROCESSOR
#undef _HYPERVMPATOMICS
#endif

#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/pool.h>
#include <sys/task.h>
#include <sys/sensors.h>

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/pv/pvvar.h>
#include <dev/pv/hypervreg.h>
#include <dev/pv/hypervvar.h>
#include <dev/pv/hypervicreg.h>

struct hv_ic_dev;

#define NKVPPOOLS			4
#define MAXPOOLENTS			1023

struct kvp_entry {
	int				kpe_index;
	uint32_t			kpe_valtype;
	uint8_t				kpe_key[HV_KVP_MAX_KEY_SIZE / 2];
	uint8_t				kpe_val[HV_KVP_MAX_VAL_SIZE / 2];
	TAILQ_ENTRY(kvp_entry)		kpe_entry;
};
TAILQ_HEAD(kvp_list, kvp_entry);

struct kvp_pool {
	struct kvp_list			kvp_entries;
	struct mutex			kvp_lock;
	u_int				kvp_index;
};

struct pool				kvp_entry_pool;

struct hv_kvp {
	struct kvp_pool			kvp_pool[NKVPPOOLS];
};

int	hv_heartbeat_attach(struct hv_ic_dev *);
void	hv_heartbeat(void *);
int	hv_kvp_attach(struct hv_ic_dev *);
void	hv_kvp(void *);
int	hv_kvop(void *, int, char *, char *, size_t);
int	hv_shutdown_attach(struct hv_ic_dev *);
void	hv_shutdown(void *);
int	hv_timesync_attach(struct hv_ic_dev *);
void	hv_timesync(void *);

static struct hv_ic_dev {
	const char		 *dv_name;
	const struct hv_guid	 *dv_type;
	int			(*dv_attach)(struct hv_ic_dev *);
	void			(*dv_handler)(void *);
	struct hv_channel	 *dv_ch;
	uint8_t			 *dv_buf;
	void			 *dv_priv;
} hv_ic_devs[] = {
	{
		"heartbeat",
		&hv_guid_heartbeat,
		hv_heartbeat_attach,
		hv_heartbeat
	},
	{
		"kvp",
		&hv_guid_kvp,
		hv_kvp_attach,
		hv_kvp
	},
	{
		"shutdown",
		&hv_guid_shutdown,
		hv_shutdown_attach,
		hv_shutdown
	},
	{
		"timesync",
		&hv_guid_timesync,
		hv_timesync_attach,
		hv_timesync
	}
};

static const struct {
	enum hv_kvp_pool		 poolidx;
	const char			*poolname;
	size_t				 poolnamelen;
} kvp_pools[] = {
	{ HV_KVP_POOL_EXTERNAL,		"External",	sizeof("External") },
	{ HV_KVP_POOL_GUEST,		"Guest",	sizeof("Guest")	},
	{ HV_KVP_POOL_AUTO,		"Auto",		sizeof("Auto") },
	{ HV_KVP_POOL_AUTO_EXTERNAL,	"Guest/Parameters",
	  sizeof("Guest/Parameters") }
};

static const struct {
	int				 keyidx;
	const char			*keyname;
	const char			*value;
} kvp_pool_auto[] = {
	{ 0, "FullyQualifiedDomainName",	hostname },
	{ 1, "IntegrationServicesVersion",	"6.6.6"	},
	{ 2, "NetworkAddressIPv4",		"127.0.0.1" },
	{ 3, "NetworkAddressIPv6",		"::1" },
	{ 4, "OSBuildNumber",			osversion },
	{ 5, "OSName",				ostype },
	{ 6, "OSMajorVersion",			"6" }, /* free commit for mike */
	{ 7, "OSMinorVersion",			&osrelease[2] },
	{ 8, "OSVersion",			osrelease },
#ifdef __amd64__ /* As specified in SYSTEM_INFO.wProcessorArchitecture */
	{ 9, "ProcessorArchitecture",		"9" }
#else
	{ 9, "ProcessorArchitecture",		"0" }
#endif
};

void
hv_attach_icdevs(struct hv_softc *sc)
{
	struct hv_ic_dev *dv;
	struct hv_channel *ch;
	int i, header = 0;

	for (i = 0; i < nitems(hv_ic_devs); i++) {
		dv = &hv_ic_devs[i];

		TAILQ_FOREACH(ch, &sc->sc_channels, ch_entry) {
			if (ch->ch_state != HV_CHANSTATE_OFFERED)
				continue;
			if (ch->ch_flags & CHF_MONITOR)
				continue;
			if (memcmp(dv->dv_type, &ch->ch_type,
			    sizeof(ch->ch_type)) == 0)
				break;
		}
		if (ch == NULL)
			continue;

		dv->dv_ch = ch;

		/*
		 * These services are not performance critical and
		 * do not need batched reading. Furthermore, some
		 * services such as KVP can only handle one message
		 * from the host at a time.
		 */
		dv->dv_ch->ch_flags &= ~CHF_BATCHED;

		if (dv->dv_attach && dv->dv_attach(dv) != 0)
			continue;

		if (hv_channel_open(ch, VMBUS_IC_BUFRINGSIZE, NULL, 0,
		    dv->dv_handler, dv)) {
			printf("%s: failed to open channel for %s\n",
			    sc->sc_dev.dv_xname, dv->dv_name);
			continue;
		}
		evcount_attach(&ch->ch_evcnt, dv->dv_name, &sc->sc_idtvec);

		if (!header) {
			printf("%s: %s", sc->sc_dev.dv_xname, dv->dv_name);
			header = 1;
		} else
			printf(", %s", dv->dv_name);
	}
	if (header)
		printf("\n");
}

static inline void
hv_ic_negotiate(struct vmbus_icmsg_hdr *hdr, uint32_t *rlen, uint32_t fwver,
    uint32_t msgver)
{
	struct vmbus_icmsg_negotiate *msg;
	uint16_t propmin, propmaj, chosenmaj, chosenmin;
	int i;

	msg = (struct vmbus_icmsg_negotiate *)hdr;

	chosenmaj = chosenmin = 0;
	for (i = 0; i < msg->ic_fwver_cnt; i++) {
		propmaj = VMBUS_ICVER_MAJOR(msg->ic_ver[i]);
		propmin = VMBUS_ICVER_MINOR(msg->ic_ver[i]);
		if (propmaj > chosenmaj &&
		    propmaj <= VMBUS_ICVER_MAJOR(fwver) &&
		    propmin >= chosenmin &&
		    propmin <= VMBUS_ICVER_MINOR(fwver)) {
			chosenmaj = propmaj;
			chosenmin = propmin;
		}
	}
	fwver = VMBUS_IC_VERSION(chosenmaj, chosenmin);

	chosenmaj = chosenmin = 0;
	for (; i < msg->ic_fwver_cnt + msg->ic_msgver_cnt; i++) {
		propmaj = VMBUS_ICVER_MAJOR(msg->ic_ver[i]);
		propmin = VMBUS_ICVER_MINOR(msg->ic_ver[i]);
		if (propmaj > chosenmaj &&
		    propmaj <= VMBUS_ICVER_MAJOR(msgver) &&
		    propmin >= chosenmin &&
		    propmin <= VMBUS_ICVER_MINOR(msgver)) {
			chosenmaj = propmaj;
			chosenmin = propmin;
		}
	}
	msgver = VMBUS_IC_VERSION(chosenmaj, chosenmin);

	msg->ic_fwver_cnt = 1;
	msg->ic_ver[0] = fwver;
	msg->ic_msgver_cnt = 1;
	msg->ic_ver[1] = msgver;
	hdr->ic_dsize = sizeof(*msg) + 2 * sizeof(uint32_t) -
	    sizeof(struct vmbus_icmsg_hdr);
	if (*rlen < sizeof(*msg) + 2 * sizeof(uint32_t))
		*rlen = sizeof(*msg) + 2 * sizeof(uint32_t);
}

int
hv_heartbeat_attach(struct hv_ic_dev *dv)
{
	struct hv_channel *ch = dv->dv_ch;
	struct hv_softc *sc = ch->ch_sc;

	dv->dv_buf = malloc(PAGE_SIZE, M_DEVBUF, M_ZERO |
	    (cold ? M_NOWAIT : M_WAITOK));
	if (dv->dv_buf == NULL) {
		printf("%s: failed to allocate receive buffer\n",
		    sc->sc_dev.dv_xname);
		return (-1);
	}
	return (0);
}

void
hv_heartbeat(void *arg)
{
	struct hv_ic_dev *dv = arg;
	struct hv_channel *ch = dv->dv_ch;
	struct hv_softc *sc = ch->ch_sc;
	struct vmbus_icmsg_hdr *hdr;
	struct vmbus_icmsg_heartbeat *msg;
	uint64_t rid;
	uint32_t rlen;
	int rv;

	rv = hv_channel_recv(ch, dv->dv_buf, PAGE_SIZE, &rlen, &rid, 0);
	if (rv || rlen == 0) {
		if (rv != EAGAIN)
			DPRINTF("%s: heartbeat rv=%d rlen=%u\n",
			    sc->sc_dev.dv_xname, rv, rlen);
		return;
	}
	if (rlen < sizeof(struct vmbus_icmsg_hdr)) {
		DPRINTF("%s: heartbeat short read rlen=%u\n",
			    sc->sc_dev.dv_xname, rlen);
		return;
	}
	hdr = (struct vmbus_icmsg_hdr *)dv->dv_buf;
	switch (hdr->ic_type) {
	case VMBUS_ICMSG_TYPE_NEGOTIATE:
		hv_ic_negotiate(hdr, &rlen, VMBUS_IC_VERSION(3, 0),
		    VMBUS_IC_VERSION(3, 0));
		break;
	case VMBUS_ICMSG_TYPE_HEARTBEAT:
		msg = (struct vmbus_icmsg_heartbeat *)hdr;
		msg->ic_seq += 1;
		break;
	default:
		printf("%s: unhandled heartbeat message type %u\n",
		    sc->sc_dev.dv_xname, hdr->ic_type);
		return;
	}
	hdr->ic_flags = VMBUS_ICMSG_FLAG_TRANSACTION | VMBUS_ICMSG_FLAG_RESPONSE;
	hv_channel_send(ch, dv->dv_buf, rlen, rid, VMBUS_CHANPKT_TYPE_INBAND, 0);
}

static void
hv_shutdown_task(void *arg)
{
	struct hv_softc *sc = arg;
	pvbus_shutdown(&sc->sc_dev);
}

int
hv_shutdown_attach(struct hv_ic_dev *dv)
{
	struct hv_channel *ch = dv->dv_ch;
	struct hv_softc *sc = ch->ch_sc;

	dv->dv_buf = malloc(PAGE_SIZE, M_DEVBUF, M_ZERO |
	    (cold ? M_NOWAIT : M_WAITOK));
	if (dv->dv_buf == NULL) {
		printf("%s: failed to allocate receive buffer\n",
		    sc->sc_dev.dv_xname);
		return (-1);
	}

	task_set(&sc->sc_sdtask, hv_shutdown_task, sc);

	return (0);
}

void
hv_shutdown(void *arg)
{
	struct hv_ic_dev *dv = arg;
	struct hv_channel *ch = dv->dv_ch;
	struct hv_softc *sc = ch->ch_sc;
	struct vmbus_icmsg_hdr *hdr;
	struct vmbus_icmsg_shutdown *msg;
	uint64_t rid;
	uint32_t rlen;
	int rv, shutdown = 0;

	rv = hv_channel_recv(ch, dv->dv_buf, PAGE_SIZE, &rlen, &rid, 0);
	if (rv || rlen == 0) {
		if (rv != EAGAIN)
			DPRINTF("%s: shutdown rv=%d rlen=%u\n",
			    sc->sc_dev.dv_xname, rv, rlen);
		return;
	}
	if (rlen < sizeof(struct vmbus_icmsg_hdr)) {
		DPRINTF("%s: shutdown short read rlen=%u\n",
			    sc->sc_dev.dv_xname, rlen);
		return;
	}
	hdr = (struct vmbus_icmsg_hdr *)dv->dv_buf;
	switch (hdr->ic_type) {
	case VMBUS_ICMSG_TYPE_NEGOTIATE:
		hv_ic_negotiate(hdr, &rlen, VMBUS_IC_VERSION(3, 0),
		    VMBUS_IC_VERSION(3, 0));
		break;
	case VMBUS_ICMSG_TYPE_SHUTDOWN:
		msg = (struct vmbus_icmsg_shutdown *)hdr;
		if (msg->ic_haltflags == 0 || msg->ic_haltflags == 1) {
			shutdown = 1;
			hdr->ic_status = VMBUS_ICMSG_STATUS_OK;
		} else
			hdr->ic_status = VMBUS_ICMSG_STATUS_FAIL;
		break;
	default:
		printf("%s: unhandled shutdown message type %u\n",
		    sc->sc_dev.dv_xname, hdr->ic_type);
		return;
	}

	hdr->ic_flags = VMBUS_ICMSG_FLAG_TRANSACTION | VMBUS_ICMSG_FLAG_RESPONSE;
	hv_channel_send(ch, dv->dv_buf, rlen, rid, VMBUS_CHANPKT_TYPE_INBAND, 0);

	if (shutdown)
		task_add(systq, &sc->sc_sdtask);
}

int
hv_timesync_attach(struct hv_ic_dev *dv)
{
	struct hv_channel *ch = dv->dv_ch;
	struct hv_softc *sc = ch->ch_sc;

	dv->dv_buf = malloc(PAGE_SIZE, M_DEVBUF, M_ZERO |
	    (cold ? M_NOWAIT : M_WAITOK));
	if (dv->dv_buf == NULL) {
		printf("%s: failed to allocate receive buffer\n",
		    sc->sc_dev.dv_xname);
		return (-1);
	}

	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	sc->sc_sensor.type = SENSOR_TIMEDELTA;
	sc->sc_sensor.status = SENSOR_S_UNKNOWN;

	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor);
	sensordev_install(&sc->sc_sensordev);

	return (0);
}

void
hv_timesync(void *arg)
{
	struct hv_ic_dev *dv = arg;
	struct hv_channel *ch = dv->dv_ch;
	struct hv_softc *sc = ch->ch_sc;
	struct vmbus_icmsg_hdr *hdr;
	struct vmbus_icmsg_timesync *msg;
	struct timespec guest, host, diff;
	uint64_t tns;
	uint64_t rid;
	uint32_t rlen;
	int rv;

	rv = hv_channel_recv(ch, dv->dv_buf, PAGE_SIZE, &rlen, &rid, 0);
	if (rv || rlen == 0) {
		if (rv != EAGAIN)
			DPRINTF("%s: timesync rv=%d rlen=%u\n",
			    sc->sc_dev.dv_xname, rv, rlen);
		return;
	}
	if (rlen < sizeof(struct vmbus_icmsg_hdr)) {
		DPRINTF("%s: timesync short read rlen=%u\n",
			    sc->sc_dev.dv_xname, rlen);
		return;
	}
	hdr = (struct vmbus_icmsg_hdr *)dv->dv_buf;
	switch (hdr->ic_type) {
	case VMBUS_ICMSG_TYPE_NEGOTIATE:
		hv_ic_negotiate(hdr, &rlen, VMBUS_IC_VERSION(3, 0),
		    VMBUS_IC_VERSION(3, 0));
		break;
	case VMBUS_ICMSG_TYPE_TIMESYNC:
		msg = (struct vmbus_icmsg_timesync *)hdr;
		if (msg->ic_tsflags == VMBUS_ICMSG_TS_FLAG_SAMPLE) {
			microtime(&sc->sc_sensor.tv);
			nanotime(&guest);
			tns = (msg->ic_hvtime - 116444736000000000LL) * 100;
			host.tv_sec = tns / 1000000000LL;
			host.tv_nsec = tns % 1000000000LL;
			timespecsub(&guest, &host, &diff);
			sc->sc_sensor.value = (int64_t)diff.tv_sec *
			    1000000000LL + diff.tv_nsec;
			sc->sc_sensor.status = SENSOR_S_OK;
		}
		break;
	default:
		printf("%s: unhandled timesync message type %u\n",
		    sc->sc_dev.dv_xname, hdr->ic_type);
		return;
	}

	hdr->ic_flags = VMBUS_ICMSG_FLAG_TRANSACTION | VMBUS_ICMSG_FLAG_RESPONSE;
	hv_channel_send(ch, dv->dv_buf, rlen, rid, VMBUS_CHANPKT_TYPE_INBAND, 0);
}

static inline int
copyout_utf16le(void *dst, const void *src, size_t dlen, size_t slen)
{
	const uint8_t *sp = src;
	uint8_t *dp = dst;
	int i, j;

	KASSERT(dlen >= slen * 2);

	for (i = j = 0; i < slen; i++, j += 2) {
		dp[j] = sp[i];
		dp[j + 1] = '\0';
	}
	return (j);
}

static inline int
copyin_utf16le(void *dst, const void *src, size_t dlen, size_t slen)
{
	const uint8_t *sp = src;
	uint8_t *dp = dst;
	int i, j;

	KASSERT(dlen >= slen / 2);

	for (i = j = 0; i < slen; i += 2, j++)
		dp[j] = sp[i];
	return (j);
}

static inline int
keycmp_utf16le(const uint8_t *key, const uint8_t *ukey, size_t ukeylen)
{
	int i, j;

	for (i = j = 0; i < ukeylen; i += 2, j++) {
		if (key[j] != ukey[i])
			return (key[j] > ukey[i] ?
			    key[j] - ukey[i] :
			    ukey[i] - key[j]);
	}
	return (0);
}

static void
kvp_pool_init(struct kvp_pool *kvpl)
{
	TAILQ_INIT(&kvpl->kvp_entries);
	mtx_init(&kvpl->kvp_lock, IPL_NET);
	kvpl->kvp_index = 0;
}

static int
kvp_pool_insert(struct kvp_pool *kvpl, const char *key, const char *val,
    uint32_t vallen, uint32_t valtype)
{
	struct kvp_entry *kpe;
	int keylen = strlen(key);

	if (keylen > HV_KVP_MAX_KEY_SIZE / 2)
		return (ERANGE);

	mtx_enter(&kvpl->kvp_lock);

	TAILQ_FOREACH(kpe, &kvpl->kvp_entries, kpe_entry) {
		if (strcmp(kpe->kpe_key, key) == 0) {
			mtx_leave(&kvpl->kvp_lock);
			return (EEXIST);
		}
	}

	kpe = pool_get(&kvp_entry_pool, PR_ZERO | PR_NOWAIT);
	if (kpe == NULL) {
		mtx_leave(&kvpl->kvp_lock);
		return (ENOMEM);
	}

	strlcpy(kpe->kpe_key, key, HV_KVP_MAX_KEY_SIZE / 2);

	if ((kpe->kpe_valtype = valtype) == HV_KVP_REG_SZ)
		strlcpy(kpe->kpe_val, val, HV_KVP_MAX_KEY_SIZE / 2);
	else
		memcpy(kpe->kpe_val, val, vallen);

	kpe->kpe_index = kvpl->kvp_index++ & MAXPOOLENTS;

	TAILQ_INSERT_TAIL(&kvpl->kvp_entries, kpe, kpe_entry);

	mtx_leave(&kvpl->kvp_lock);

	return (0);
}

static int
kvp_pool_update(struct kvp_pool *kvpl, const char *key, const char *val,
    uint32_t vallen, uint32_t valtype)
{
	struct kvp_entry *kpe;
	int keylen = strlen(key);

	if (keylen > HV_KVP_MAX_KEY_SIZE / 2)
		return (ERANGE);

	mtx_enter(&kvpl->kvp_lock);

	TAILQ_FOREACH(kpe, &kvpl->kvp_entries, kpe_entry) {
		if (strcmp(kpe->kpe_key, key) == 0)
			break;
	}
	if (kpe == NULL) {
		mtx_leave(&kvpl->kvp_lock);
		return (ENOENT);
	}

	if ((kpe->kpe_valtype = valtype) == HV_KVP_REG_SZ)
		strlcpy(kpe->kpe_val, val, HV_KVP_MAX_KEY_SIZE / 2);
	else
		memcpy(kpe->kpe_val, val, vallen);

	mtx_leave(&kvpl->kvp_lock);

	return (0);
}

static int
kvp_pool_import(struct kvp_pool *kvpl, const char *key, uint32_t keylen,
    const char *val, uint32_t vallen, uint32_t valtype)
{
	struct kvp_entry *kpe;

	if (keylen > HV_KVP_MAX_KEY_SIZE ||
	    vallen > HV_KVP_MAX_VAL_SIZE)
		return (ERANGE);

	mtx_enter(&kvpl->kvp_lock);

	TAILQ_FOREACH(kpe, &kvpl->kvp_entries, kpe_entry) {
		if (keycmp_utf16le(kpe->kpe_key, key, keylen) == 0)
			break;
	}
	if (kpe == NULL) {
		kpe = pool_get(&kvp_entry_pool, PR_ZERO | PR_NOWAIT);
		if (kpe == NULL) {
			mtx_leave(&kvpl->kvp_lock);
			return (ENOMEM);
		}

		copyin_utf16le(kpe->kpe_key, key, HV_KVP_MAX_KEY_SIZE / 2,
		    keylen);

		kpe->kpe_index = kvpl->kvp_index++ & MAXPOOLENTS;

		TAILQ_INSERT_TAIL(&kvpl->kvp_entries, kpe, kpe_entry);
	}

	copyin_utf16le(kpe->kpe_val, val, HV_KVP_MAX_VAL_SIZE / 2, vallen);
	kpe->kpe_valtype = valtype;

	mtx_leave(&kvpl->kvp_lock);

	return (0);
}

static int
kvp_pool_export(struct kvp_pool *kvpl, uint32_t index, char *key,
    uint32_t *keylen, char *val, uint32_t *vallen, uint32_t *valtype)
{
	struct kvp_entry *kpe;

	mtx_enter(&kvpl->kvp_lock);

	TAILQ_FOREACH(kpe, &kvpl->kvp_entries, kpe_entry) {
		if (kpe->kpe_index == index)
			break;
	}
	if (kpe == NULL) {
		mtx_leave(&kvpl->kvp_lock);
		return (ENOENT);
	}

	*keylen = copyout_utf16le(key, kpe->kpe_key, HV_KVP_MAX_KEY_SIZE,
	    strlen(kpe->kpe_key) + 1);
	*vallen = copyout_utf16le(val, kpe->kpe_val, HV_KVP_MAX_VAL_SIZE,
	    strlen(kpe->kpe_val) + 1);
	*valtype = kpe->kpe_valtype;

	mtx_leave(&kvpl->kvp_lock);

	return (0);
}

static int
kvp_pool_remove(struct kvp_pool *kvpl, const char *key, uint32_t keylen)
{
	struct kvp_entry *kpe;

	mtx_enter(&kvpl->kvp_lock);

	TAILQ_FOREACH(kpe, &kvpl->kvp_entries, kpe_entry) {
		if (keycmp_utf16le(kpe->kpe_key, key, keylen) == 0)
			break;
	}
	if (kpe == NULL) {
		mtx_leave(&kvpl->kvp_lock);
		return (ENOENT);
	}

	TAILQ_REMOVE(&kvpl->kvp_entries, kpe, kpe_entry);

	mtx_leave(&kvpl->kvp_lock);

	pool_put(&kvp_entry_pool, kpe);

	return (0);
}

static int
kvp_pool_extract(struct kvp_pool *kvpl, const char *key, char *val,
    uint32_t vallen)
{
	struct kvp_entry *kpe;

	if (vallen < HV_KVP_MAX_VAL_SIZE / 2)
		return (ERANGE);

	mtx_enter(&kvpl->kvp_lock);

	TAILQ_FOREACH(kpe, &kvpl->kvp_entries, kpe_entry) {
		if (strcmp(kpe->kpe_key, key) == 0)
			break;
	}
	if (kpe == NULL) {
		mtx_leave(&kvpl->kvp_lock);
		return (ENOENT);
	}

	switch (kpe->kpe_valtype) {
	case HV_KVP_REG_SZ:
		strlcpy(val, kpe->kpe_val, HV_KVP_MAX_VAL_SIZE / 2);
		break;
	case HV_KVP_REG_U32:
		snprintf(val, HV_KVP_MAX_VAL_SIZE / 2, "%u",
		    *(uint32_t *)kpe->kpe_val);
		break;
	case HV_KVP_REG_U64:
		snprintf(val, HV_KVP_MAX_VAL_SIZE / 2, "%llu",
		    *(uint64_t *)kpe->kpe_val);
		break;
	}

	mtx_leave(&kvpl->kvp_lock);

	return (0);
}

static int
kvp_pool_keys(struct kvp_pool *kvpl, int next, char *key, size_t *keylen)
{
	struct kvp_entry *kpe;
	int iter = 0;

	mtx_enter(&kvpl->kvp_lock);

	TAILQ_FOREACH(kpe, &kvpl->kvp_entries, kpe_entry) {
		if (iter++ < next)
			continue;
		*keylen = strlen(kpe->kpe_key) + 1;
		strlcpy(key, kpe->kpe_key, *keylen);

		mtx_leave(&kvpl->kvp_lock);

		return (0);
	}

	mtx_leave(&kvpl->kvp_lock);

	return (-1);
}

int
hv_kvp_attach(struct hv_ic_dev *dv)
{
	struct hv_channel *ch = dv->dv_ch;
	struct hv_softc *sc = ch->ch_sc;
	struct hv_kvp *kvp;
	int i;

	dv->dv_buf = malloc(2 * PAGE_SIZE, M_DEVBUF, M_ZERO |
	    (cold ? M_NOWAIT : M_WAITOK));
	if (dv->dv_buf == NULL) {
		printf("%s: failed to allocate receive buffer\n",
		    sc->sc_dev.dv_xname);
		return (-1);
	}

	dv->dv_priv = malloc(sizeof(struct hv_kvp), M_DEVBUF, M_ZERO |
	    (cold ? M_NOWAIT : M_WAITOK));
	if (dv->dv_priv == NULL) {
		free(dv->dv_buf, M_DEVBUF, 2 * PAGE_SIZE);
		printf("%s: failed to allocate KVP private data\n",
		    sc->sc_dev.dv_xname);
		return (-1);
	}
	kvp = dv->dv_priv;

	pool_init(&kvp_entry_pool, sizeof(struct kvp_entry), 0, IPL_NET, 0,
	    "hvkvpl", NULL);

	for (i = 0; i < NKVPPOOLS; i++)
		kvp_pool_init(&kvp->kvp_pool[i]);

	/* Initialize 'Auto' pool */
	for (i = 0; i < nitems(kvp_pool_auto); i++) {
		if (kvp_pool_insert(&kvp->kvp_pool[HV_KVP_POOL_AUTO],
		    kvp_pool_auto[i].keyname, kvp_pool_auto[i].value,
		    strlen(kvp_pool_auto[i].value), HV_KVP_REG_SZ))
			DPRINTF("%s: failed to insert into 'Auto' pool\n",
			    sc->sc_dev.dv_xname);
	}

	sc->sc_pvbus->hv_kvop = hv_kvop;
	sc->sc_pvbus->hv_arg = dv;

	return (0);
}

static int
nibble(int ch)
{
	if (ch >= '0' && ch <= '9')
		return (ch - '0');
	if (ch >= 'A' && ch <= 'F')
		return (10 + ch - 'A');
	if (ch >= 'a' && ch <= 'f')
		return (10 + ch - 'a');
	return (-1);
}

static int
kvp_get_ip_info(struct hv_kvp *kvp, const uint8_t *mac, uint8_t *family,
    uint8_t *addr, uint8_t *netmask, size_t addrlen)
{
	struct ifnet *ifp;
	struct ifaddr *ifa, *ifa6, *ifa6ll;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6, sa6;
	uint8_t	enaddr[ETHER_ADDR_LEN];
	uint8_t ipaddr[INET6_ADDRSTRLEN];
	int i, j, lo, hi, s, af;

	/* Convert from the UTF-16LE string format to binary */
	for (i = 0, j = 0; j < ETHER_ADDR_LEN; i += 6) {
		if ((hi = nibble(mac[i])) == -1 ||
		    (lo = nibble(mac[i+2])) == -1)
			return (-1);
		enaddr[j++] = hi << 4 | lo;
	}

	switch (*family) {
	case ADDR_FAMILY_NONE:
		af = AF_UNSPEC;
		break;
	case ADDR_FAMILY_IPV4:
		af = AF_INET;
		break;
	case ADDR_FAMILY_IPV6:
		af = AF_INET6;
		break;
	default:
		return (-1);
	}

	KERNEL_LOCK();
	s = splnet();

	TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
		if (!memcmp(LLADDR(ifp->if_sadl), enaddr, ETHER_ADDR_LEN))
			break;
	}
	if (ifp == NULL) {
		splx(s);
		KERNEL_UNLOCK();
		return (-1);
	}

	ifa6 = ifa6ll = NULL;

	/* Try to find a best matching address, preferring IPv4 */
	TAILQ_FOREACH(ifa, &ifp->if_addrlist, ifa_list) {
		/*
		 * First IPv4 address is always a best match unless
		 * we were asked for an IPv6 address.
		 */
		if ((af == AF_INET || af == AF_UNSPEC) &&
		    (ifa->ifa_addr->sa_family == AF_INET)) {
			af = AF_INET;
			goto found;
		}
		if ((af == AF_INET6 || af == AF_UNSPEC) &&
		    (ifa->ifa_addr->sa_family == AF_INET6)) {
			if (!IN6_IS_ADDR_LINKLOCAL(
			    &satosin6(ifa->ifa_addr)->sin6_addr)) {
				/* Done if we're looking for an IPv6 address */
				if (af == AF_INET6)
					goto found;
				/* Stick to the first one */
				if (ifa6 == NULL)
					ifa6 = ifa;
			} else	/* Pick the last one */
				ifa6ll = ifa;
		}
	}
	/* If we haven't found any IPv4 or IPv6 direct matches... */
	if (ifa == NULL) {
		/* ... try the last global IPv6 address... */
		if (ifa6 != NULL)
			ifa = ifa6;
		/* ... or the last link-local...  */
		else if (ifa6ll != NULL)
			ifa = ifa6ll;
		else {
			splx(s);
			KERNEL_UNLOCK();
			return (-1);
		}
	}
 found:
	switch (af) {
	case AF_INET:
		sin = satosin(ifa->ifa_addr);
		inet_ntop(AF_INET, &sin->sin_addr, ipaddr, sizeof(ipaddr));
		copyout_utf16le(addr, ipaddr, addrlen, INET_ADDRSTRLEN);

		sin = satosin(ifa->ifa_netmask);
		inet_ntop(AF_INET, &sin->sin_addr, ipaddr, sizeof(ipaddr));
		copyout_utf16le(netmask, ipaddr, addrlen, INET_ADDRSTRLEN);

		*family = ADDR_FAMILY_IPV4;
		break;
	case AF_UNSPEC:
	case AF_INET6:
		sin6 = satosin6(ifa->ifa_addr);
		if (IN6_IS_SCOPE_EMBED(&sin6->sin6_addr)) {
			sa6 = *satosin6(ifa->ifa_addr);
			sa6.sin6_addr.s6_addr16[1] = 0;
			sin6 = &sa6;
		}
		inet_ntop(AF_INET6, &sin6->sin6_addr, ipaddr, sizeof(ipaddr));
		copyout_utf16le(addr, ipaddr, addrlen, INET6_ADDRSTRLEN);

		sin6 = satosin6(ifa->ifa_netmask);
		inet_ntop(AF_INET6, &sin6->sin6_addr, ipaddr, sizeof(ipaddr));
		copyout_utf16le(netmask, ipaddr, addrlen, INET6_ADDRSTRLEN);

		*family = ADDR_FAMILY_IPV6;
		break;
	}

	splx(s);
	KERNEL_UNLOCK();

	return (0);
}

static void
hv_kvp_process(struct hv_kvp *kvp, struct vmbus_icmsg_kvp *msg)
{
	union hv_kvp_hdr *kvh = &msg->ic_kvh;
	union hv_kvp_msg *kvm = &msg->ic_kvm;

	switch (kvh->kvh_op) {
	case HV_KVP_OP_SET:
		if (kvh->kvh_pool == HV_KVP_POOL_AUTO_EXTERNAL &&
		    kvp_pool_import(&kvp->kvp_pool[HV_KVP_POOL_AUTO_EXTERNAL],
		    kvm->kvm_val.kvm_key, kvm->kvm_val.kvm_keylen,
		    kvm->kvm_val.kvm_val, kvm->kvm_val.kvm_vallen,
		    kvm->kvm_val.kvm_valtype)) {
			DPRINTF("%s: failed to import into 'Guest/Parameters'"
			    " pool\n", __func__);
			kvh->kvh_err = HV_KVP_S_CONT;
		} else if (kvh->kvh_pool == HV_KVP_POOL_EXTERNAL &&
		    kvp_pool_import(&kvp->kvp_pool[HV_KVP_POOL_EXTERNAL],
		    kvm->kvm_val.kvm_key, kvm->kvm_val.kvm_keylen,
		    kvm->kvm_val.kvm_val, kvm->kvm_val.kvm_vallen,
		    kvm->kvm_val.kvm_valtype)) {
			DPRINTF("%s: failed to import into 'External' pool\n",
			    __func__);
			kvh->kvh_err = HV_KVP_S_CONT;
		} else if (kvh->kvh_pool != HV_KVP_POOL_AUTO_EXTERNAL &&
		    kvh->kvh_pool != HV_KVP_POOL_EXTERNAL) {
			kvh->kvh_err = HV_KVP_S_CONT;
		} else
			kvh->kvh_err = HV_KVP_S_OK;
		break;
	case HV_KVP_OP_DELETE:
		if (kvh->kvh_pool != HV_KVP_POOL_EXTERNAL ||
		    kvp_pool_remove(&kvp->kvp_pool[HV_KVP_POOL_EXTERNAL],
		    kvm->kvm_del.kvm_key, kvm->kvm_del.kvm_keylen)) {
			DPRINTF("%s: failed to remove from 'External' pool\n",
			    __func__);
			kvh->kvh_err = HV_KVP_S_CONT;
		} else
			kvh->kvh_err = HV_KVP_S_OK;
		break;
	case HV_KVP_OP_ENUMERATE:
		if (kvh->kvh_pool == HV_KVP_POOL_AUTO &&
		    kvp_pool_export(&kvp->kvp_pool[HV_KVP_POOL_AUTO],
		    kvm->kvm_enum.kvm_index, kvm->kvm_enum.kvm_key,
		    &kvm->kvm_enum.kvm_keylen, kvm->kvm_enum.kvm_val,
		    &kvm->kvm_enum.kvm_vallen, &kvm->kvm_enum.kvm_valtype))
			kvh->kvh_err = HV_KVP_S_CONT;
		else if (kvh->kvh_pool == HV_KVP_POOL_GUEST &&
		    kvp_pool_export(&kvp->kvp_pool[HV_KVP_POOL_GUEST],
		    kvm->kvm_enum.kvm_index, kvm->kvm_enum.kvm_key,
		    &kvm->kvm_enum.kvm_keylen, kvm->kvm_enum.kvm_val,
		    &kvm->kvm_enum.kvm_vallen, &kvm->kvm_enum.kvm_valtype))
			kvh->kvh_err = HV_KVP_S_CONT;
		else
			kvh->kvh_err = HV_KVP_S_OK;
		break;
	case HV_KVP_OP_GET_IP_INFO:
		if (VMBUS_ICVER_MAJOR(msg->ic_hdr.ic_msgver) <= 4) {
			struct vmbus_icmsg_kvp_addr *amsg;
			struct hv_kvp_msg_addr *kva;

			amsg = (struct vmbus_icmsg_kvp_addr *)msg;
			kva = &amsg->ic_kvm;

			if (kvp_get_ip_info(kvp, kva->kvm_mac,
			    &kva->kvm_family, kva->kvm_addr,
			    kva->kvm_netmask, sizeof(kva->kvm_addr)))
				kvh->kvh_err = HV_KVP_S_CONT;
			else
				kvh->kvh_err = HV_KVP_S_OK;
		} else {
			DPRINTF("KVP GET_IP_INFO fw %u.%u msg %u.%u dsize=%u\n",
			    VMBUS_ICVER_MAJOR(msg->ic_hdr.ic_fwver),
			    VMBUS_ICVER_MINOR(msg->ic_hdr.ic_fwver),
			    VMBUS_ICVER_MAJOR(msg->ic_hdr.ic_msgver),
			    VMBUS_ICVER_MINOR(msg->ic_hdr.ic_msgver),
			    msg->ic_hdr.ic_dsize);
			kvh->kvh_err = HV_KVP_S_CONT;
		}
		break;
	default:
		DPRINTF("KVP message op %u pool %u\n", kvh->kvh_op,
		    kvh->kvh_pool);
		kvh->kvh_err = HV_KVP_S_CONT;
	}
}

void
hv_kvp(void *arg)
{
	struct hv_ic_dev *dv = arg;
	struct hv_channel *ch = dv->dv_ch;
	struct hv_softc *sc = ch->ch_sc;
	struct hv_kvp *kvp = dv->dv_priv;
	struct vmbus_icmsg_hdr *hdr;
	uint64_t rid;
	uint32_t fwver, msgver, rlen;
	int rv;

	for (;;) {
		rv = hv_channel_recv(ch, dv->dv_buf, 2 * PAGE_SIZE,
		    &rlen, &rid, 0);
		if (rv || rlen == 0) {
			if (rv != EAGAIN)
				DPRINTF("%s: kvp rv=%d rlen=%u\n",
				    sc->sc_dev.dv_xname, rv, rlen);
			return;
		}
		if (rlen < sizeof(struct vmbus_icmsg_hdr)) {
			DPRINTF("%s: kvp short read rlen=%u\n",
			    sc->sc_dev.dv_xname, rlen);
			return;
		}
		hdr = (struct vmbus_icmsg_hdr *)dv->dv_buf;
		switch (hdr->ic_type) {
		case VMBUS_ICMSG_TYPE_NEGOTIATE:
			switch (sc->sc_proto) {
			case VMBUS_VERSION_WS2008:
				fwver = VMBUS_IC_VERSION(1, 0);
				msgver = VMBUS_IC_VERSION(1, 0);
				break;
			case VMBUS_VERSION_WIN7:
				fwver = VMBUS_IC_VERSION(3, 0);
				msgver = VMBUS_IC_VERSION(3, 0);
				break;
			default:
				fwver = VMBUS_IC_VERSION(3, 0);
				msgver = VMBUS_IC_VERSION(4, 0);
			}
			hv_ic_negotiate(hdr, &rlen, fwver, msgver);
			break;
		case VMBUS_ICMSG_TYPE_KVP:
			if (hdr->ic_dsize >= sizeof(union hv_kvp_hdr))
				hv_kvp_process(kvp,
				    (struct vmbus_icmsg_kvp *)hdr);
			else
				printf("%s: message too short: %u\n",
				    sc->sc_dev.dv_xname, hdr->ic_dsize);
			break;
		default:
			printf("%s: unhandled kvp message type %u\n",
			    sc->sc_dev.dv_xname, hdr->ic_type);
			continue;
		}
		hdr->ic_flags = VMBUS_ICMSG_FLAG_TRANSACTION |
		    VMBUS_ICMSG_FLAG_RESPONSE;
		hv_channel_send(ch, dv->dv_buf, rlen, rid,
		    VMBUS_CHANPKT_TYPE_INBAND, 0);
	}
}

static int
kvp_poolname(char **key)
{
	char *p;
	int i, rv = -1;

	if ((p = strrchr(*key, '/')) == NULL)
		return (rv);
	*p = '\0';
	for (i = 0; i < nitems(kvp_pools); i++) {
		if (strncasecmp(*key, kvp_pools[i].poolname,
		    kvp_pools[i].poolnamelen) == 0) {
			rv = kvp_pools[i].poolidx;
			break;
		}
	}
	if (rv >= 0)
		*key = ++p;
	return (rv);
}

int
hv_kvop(void *arg, int op, char *key, char *val, size_t vallen)
{
	struct hv_ic_dev *dv = arg;
	struct hv_kvp *kvp = dv->dv_priv;
	struct kvp_pool *kvpl;
	int next, pool, error = 0;
	char *vp = val;
	size_t keylen;

	pool = kvp_poolname(&key);
	if (pool == -1)
		return (EINVAL);

	kvpl = &kvp->kvp_pool[pool];
	if (strlen(key) == 0) {
		for (next = 0; next < MAXPOOLENTS; next++) {
			if (val + vallen < vp + HV_KVP_MAX_KEY_SIZE / 2)
				return (ERANGE);
			if (kvp_pool_keys(kvpl, next, vp, &keylen))
				goto out;
			if (strlcat(val, "\n", vallen) >= vallen)
				return (ERANGE);
			vp += keylen;
		}
 out:
		if (vp > val)
			*(vp - 1) = '\0';
		return (0);
	}

	if (op == PVBUS_KVWRITE) {
		if (pool == HV_KVP_POOL_AUTO)
			error = kvp_pool_update(kvpl, key, val, vallen,
			    HV_KVP_REG_SZ);
		else if (pool == HV_KVP_POOL_GUEST)
			error = kvp_pool_insert(kvpl, key, val, vallen,
			    HV_KVP_REG_SZ);
		else
			error = EINVAL;
	} else
		error = kvp_pool_extract(kvpl, key, val, vallen);

	return (error);
}
