/*
 * Copyright (c) 2016 Mike Belopuhov <mike@esdenera.com>
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

#ifndef _DEV_PV_HYPERVVAR_H_
#define _DEV_PV_HYPERVVAR_H_

/* #define HYPERV_DEBUG */

#ifdef HYPERV_DEBUG
#define DPRINTF(x...)		printf(x)
#else
#define DPRINTF(x...)
#endif

struct hv_softc;

struct hv_msg {
	uint64_t			 msg_flags;
#define  MSGF_NOSLEEP			  0x0001
#define  MSGF_NOQUEUE			  0x0002
#define  MSGF_ORPHANED			  0x0004
	struct hypercall_postmsg_in	 msg_req; /* must be 8 byte aligned */
	void				*msg_rsp;
	size_t				 msg_rsplen;
	TAILQ_ENTRY(hv_msg)		 msg_entry;
};
TAILQ_HEAD(hv_queue, hv_msg);

struct hv_offer {
	struct vmbus_chanmsg_choffer	 co_chan;
	SIMPLEQ_ENTRY(hv_offer)		 co_entry;
};
SIMPLEQ_HEAD(hv_offers, hv_offer);

struct hv_ring_data {
	struct vmbus_bufring		*rd_ring;
	uint32_t			 rd_size;
	struct mutex			 rd_lock;
	uint32_t			 rd_prod;
	uint32_t			 rd_cons;
	uint32_t			 rd_dsize;
};

struct hv_channel {
	struct hv_softc			*ch_sc;

	int				 ch_state;
#define  HV_CHANSTATE_OFFERED		  1
#define  HV_CHANSTATE_OPENED		  2
#define  HV_CHANSTATE_CLOSING		  3
#define  HV_CHANSTATE_CLOSED		  4
	uint32_t			 ch_id;

	struct hv_guid			 ch_type;
	struct hv_guid			 ch_inst;
	char				 ch_ident[38];

	void				*ch_ring;
	uint32_t			 ch_ring_gpadl;
	u_long				 ch_ring_size;

	struct hv_ring_data		 ch_wrd;
	struct hv_ring_data		 ch_rrd;

	uint32_t			 ch_vcpu;

	void				(*ch_handler)(void *);
	void				 *ch_ctx;
	struct evcount			  ch_evcnt;
	struct taskq			*ch_taskq;
	struct task			 ch_task;

	uint32_t			 ch_flags;
#define  CHF_BATCHED			  0x0001
#define  CHF_MONITOR			  0x0002

	uint8_t				 ch_mgroup;
	uint8_t				 ch_mindex;
	struct hv_mon_param		 ch_monprm __attribute__((aligned(8)));

	TAILQ_ENTRY(hv_channel)		 ch_entry;
};
TAILQ_HEAD(hv_channels, hv_channel);

struct hv_attach_args {
	void				*aa_parent;
	bus_dma_tag_t			 aa_dmat;
	struct hv_guid			*aa_type;
	struct hv_guid			*aa_inst;
	char				*aa_ident;
	struct hv_channel		*aa_chan;
};

struct hv_dev {
	struct hv_attach_args		 dv_aa;
	SLIST_ENTRY(hv_dev)		 dv_entry;
};
SLIST_HEAD(hv_devices, hv_dev);

struct hv_softc {
	struct device			 sc_dev;
	struct pvbus_hv			*sc_pvbus;
	struct bus_dma_tag		*sc_dmat;

	void				*sc_hc;
	uint32_t			 sc_features;

	uint32_t			 sc_flags;
#define  HSF_CONNECTED			  0x0001
#define  HSF_OFFERS_DELIVERED		  0x0002

	int				 sc_idtvec;
	int				 sc_proto;

	/* CPU id to VCPU id mapping */
	uint32_t			 sc_vcpus[1];	/* XXX: per-cpu */
	/* Synthetic Interrupt Message Page (SIMP) */
	void				*sc_simp[1];	/* XXX: per-cpu */
	/* Synthetic Interrupt Event Flags Page (SIEFP) */
	void				*sc_siep[1];	/* XXX: per-cpu */

	/* Channel port events page */
	void				*sc_events;
	u_long				*sc_wevents;	/* Write events */
	u_long				*sc_revents;	/* Read events */

	/* Monitor pages for parent<->child notifications */
	struct vmbus_mnf		*sc_monitor[2];

	struct hv_queue	 		 sc_reqs;	/* Request queue */
	struct mutex			 sc_reqlck;
	struct hv_queue	 		 sc_rsps;	/* Response queue */
	struct mutex			 sc_rsplck;

	struct hv_offers		 sc_offers;
	struct mutex			 sc_offerlck;

	struct hv_channels		 sc_channels;
	struct mutex			 sc_channelck;

	volatile uint32_t		 sc_handle;

	struct hv_devices		 sc_devs;
	struct mutex			 sc_devlck;

	struct task			 sc_sdtask;	/* shutdown */

	struct ksensordev		 sc_sensordev;
	struct ksensor			 sc_sensor;
};

static __inline void
clear_bit(u_int b, volatile void *p)
{
	atomic_clearbits_int(((volatile u_int *)p) + (b >> 5), 1 << (b & 0x1f));
}

static __inline void
set_bit(u_int b, volatile void *p)
{
	atomic_setbits_int(((volatile u_int *)p) + (b >> 5), 1 << (b & 0x1f));
}

static __inline int
test_bit(u_int b, volatile void *p)
{
	return !!(((volatile u_int *)p)[b >> 5] & (1 << (b & 0x1f)));
}

extern const struct hv_guid hv_guid_network;
extern const struct hv_guid hv_guid_ide;
extern const struct hv_guid hv_guid_scsi;
extern const struct hv_guid hv_guid_shutdown;
extern const struct hv_guid hv_guid_timesync;
extern const struct hv_guid hv_guid_heartbeat;
extern const struct hv_guid hv_guid_kvp;
#ifdef HYPERV_DEBUG
extern const struct hv_guid hv_guid_vss;
extern const struct hv_guid hv_guid_dynmem;
extern const struct hv_guid hv_guid_mouse;
extern const struct hv_guid hv_guid_kbd;
extern const struct hv_guid hv_guid_video;
extern const struct hv_guid hv_guid_fc;
extern const struct hv_guid hv_guid_fcopy;
extern const struct hv_guid hv_guid_pcie;
extern const struct hv_guid hv_guid_netdir;
extern const struct hv_guid hv_guid_rdesktop;
extern const struct hv_guid hv_guid_avma1;
extern const struct hv_guid hv_guid_avma2;
extern const struct hv_guid hv_guid_avma3;
extern const struct hv_guid hv_guid_avma4;
#endif	/* HYPERV_DEBUG */

int	hv_handle_alloc(struct hv_channel *, void *, uint32_t, uint32_t *);
void	hv_handle_free(struct hv_channel *, uint32_t);
int	hv_channel_open(struct hv_channel *, size_t, void *, size_t,
	    void (*)(void *), void *);
int	hv_channel_close(struct hv_channel *);
int	hv_channel_setdeferred(struct hv_channel *, const char *);
void	hv_channel_schedule(struct hv_channel *);
void	hv_evcount_attach(struct hv_channel *, const char *);
int	hv_channel_send(struct hv_channel *, void *, uint32_t, uint64_t,
	    int, uint32_t);
int	hv_channel_send_sgl(struct hv_channel *, struct vmbus_gpa *,
	    uint32_t, void *, uint32_t, uint64_t);
int	hv_channel_send_prpl(struct hv_channel *, struct vmbus_gpa_range *,
	    uint32_t, void *, uint32_t, uint64_t);
int	hv_channel_recv(struct hv_channel *, void *, uint32_t, uint32_t *,
	    uint64_t *, int);

#endif	/* _DEV_PV_HYPERVVAR_H_ */
