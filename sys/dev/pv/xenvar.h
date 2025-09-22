/*	$OpenBSD: xenvar.h,v 1.51 2017/07/21 20:00:47 mikeb Exp $	*/

/*
 * Copyright (c) 2015 Mike Belopuhov
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

#ifndef _DEV_PV_XENVAR_H_
#define _DEV_PV_XENVAR_H_

static inline void
clear_bit(u_int b, volatile void *p)
{
	atomic_clearbits_int(((volatile u_int *)p) + (b >> 5), 1 << (b & 0x1f));
}

static inline void
set_bit(u_int b, volatile void *p)
{
	atomic_setbits_int(((volatile u_int *)p) + (b >> 5), 1 << (b & 0x1f));
}

static inline int
test_bit(u_int b, volatile void *p)
{
	return !!(((volatile u_int *)p)[b >> 5] & (1 << (b & 0x1f)));
}

#define XEN_MAX_NODE_LEN	64
#define XEN_MAX_BACKEND_LEN	128

struct xen_intsrc {
	SLIST_ENTRY(xen_intsrc)	 xi_entry;
	struct evcount		 xi_evcnt;
	evtchn_port_t		 xi_port;
	short			 xi_noclose;
	short			 xi_masked;
	struct refcnt		 xi_refcnt;
	struct task		 xi_task;
	struct taskq		*xi_taskq;
	void			(*xi_handler)(void *);
	void			*xi_ctx;
};

struct xen_gntent {
	grant_entry_t		*ge_table;
	grant_ref_t		 ge_start;
	short			 ge_reserved;
	short			 ge_next;
	short			 ge_free;
	struct mutex		 ge_lock;
};

struct xen_gntmap {
	grant_ref_t		 gm_ref;
	paddr_t			 gm_paddr;
};

struct xen_device {
	struct device		*dv_dev;
	char			 dv_unit[16];
	LIST_ENTRY(xen_device)	 dv_entry;
};
LIST_HEAD(xen_devices, xen_device);

struct xen_devlist {
	struct xen_softc	*dl_xen;
	char			 dl_node[XEN_MAX_NODE_LEN];
	struct task		 dl_task;
	struct xen_devices	 dl_devs;
	SLIST_ENTRY(xen_devlist) dl_entry;
};
SLIST_HEAD(xen_devlists, xen_devlist);

struct xen_softc {
	struct device		 sc_dev;
	uint32_t		 sc_base;
	void			*sc_hc;
	uint32_t		 sc_features;
#define  XENFEAT_CBVEC		(1<<8)

	bus_dma_tag_t		 sc_dmat;	/* parent dma tag */

	struct shared_info	*sc_ipg;	/* HYPERVISOR_shared_info */

	uint32_t		 sc_flags;
#define  XSF_CBVEC		  0x0001

	uint32_t		 sc_unplug;

	uint64_t		 sc_irq;	/* IDT vector number */
	SLIST_HEAD(, xen_intsrc) sc_intrs;
	struct mutex		 sc_islck;

	struct xen_gntent	*sc_gnt;	/* grant table entries */
	struct mutex		 sc_gntlck;
	int			 sc_gntcnt;	/* number of allocated frames */
	int			 sc_gntmax;	/* number of allotted frames */

	/*
	 * Xenstore
	 */
	struct xs_softc		*sc_xs;		/* xenstore softc */
	struct task		 sc_ctltsk;	/* control task */
	struct xen_devlists	 sc_devlists;	/* device lists heads */
};

extern struct xen_softc		*xen_sc;

struct xen_attach_args {
	char			 xa_name[16];
	char			 xa_node[XEN_MAX_NODE_LEN];
	char			 xa_backend[XEN_MAX_BACKEND_LEN];
	int			 xa_domid;
	bus_dma_tag_t		 xa_dmat;
};

/*
 *  Hypercalls
 */
#define XC_MEMORY		12
#define XC_OEVTCHN		16
#define XC_VERSION		17
#define XC_GNTTAB		20
#define XC_EVTCHN		32
#define XC_HVM			34

int	xen_hypercall(struct xen_softc *, int, int, ...);
int	xen_hypercallv(struct xen_softc *, int, int, ulong *);

/*
 *  Interrupts
 */
typedef uint32_t xen_intr_handle_t;

void	xen_intr(void);
void	xen_intr_ack(void);
void	xen_intr_signal(xen_intr_handle_t);
void	xen_intr_schedule(xen_intr_handle_t);
void	xen_intr_barrier(xen_intr_handle_t);
int	xen_intr_establish(evtchn_port_t, xen_intr_handle_t *, int,
	    void (*)(void *), void *, char *);
int	xen_intr_disestablish(xen_intr_handle_t);
void	xen_intr_enable(void);
void	xen_intr_mask(xen_intr_handle_t);
int	xen_intr_unmask(xen_intr_handle_t);

/*
 * Miscellaneous
 */
#define XEN_UNPLUG_NIC		0x0001	/* disable emul. NICs */
#define XEN_UNPLUG_IDE		0x0002	/* disable emul. primary IDE */
#define XEN_UNPLUG_IDESEC	0x0004	/* disable emul. secondary IDE */

void	xen_unplug_emulated(void *, int);

/*
 *  XenStore
 */
#define XS_LIST			0x01
#define XS_READ			0x02
#define XS_WATCH		0x04
#define XS_TOPEN		0x06
#define XS_TCLOSE		0x07
#define XS_WRITE		0x0b
#define XS_RM			0x0d
#define XS_EVENT		0x0f
#define XS_ERROR		0x10
#define XS_MAX			0x16

struct xs_transaction {
	uint32_t		 xst_id;
	void			*xst_cookie;
};

int	xs_cmd(struct xs_transaction *, int, const char *, struct iovec **,
	    int *);
void	xs_resfree(struct xs_transaction *, struct iovec *, int);
int	xs_watch(void *, const char *, const char *, struct task *,
	    void (*)(void *), void *);
int	xs_getnum(void *, const char *, const char *, unsigned long long *);
int	xs_setnum(void *, const char *, const char *, unsigned long long);
int	xs_getprop(void *, const char *, const char *, char *, int);
int	xs_setprop(void *, const char *, const char *, char *, int);
int	xs_kvop(void *, int, char *, char *, size_t);

#define XEN_STATE_UNKNOWN	"0"
#define XEN_STATE_INITIALIZING	"1"
#define XEN_STATE_INITWAIT	"2"
#define XEN_STATE_INITIALIZED	"3"
#define XEN_STATE_CONNECTED	"4"
#define XEN_STATE_CLOSING	"5"
#define XEN_STATE_CLOSED	"6"
#define XEN_STATE_RECONFIGURING	"7"
#define XEN_STATE_RECONFIGURED	"8"

int	xs_await_transition(void *, const char *, const char *,
	    const char *, int);

#endif	/* _DEV_PV_XENVAR_H_ */
