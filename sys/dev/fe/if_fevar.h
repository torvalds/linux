/*-
 * All Rights Reserved, Copyright (C) Fujitsu Limited 1995
 *
 * This software may be used, modified, copied, distributed, and sold, in
 * both source and binary form provided that the above copyright, these
 * terms and the following disclaimer are retained.  The name of the author
 * and/or the contributor may not be used to endorse or promote products
 * derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND THE CONTRIBUTOR ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR THE CONTRIBUTOR BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION.
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/* How many registers does an fe-supported adapter have at maximum?  */
#define MAXREGISTERS 32

/* Shouldn't these be defined somewhere else such as isa_device.h?  */
#define NO_IRQ		0

/* Flags for stability.  */
#define UNSTABLE_IRQ	0x01	/* IRQ setting may be incorrect.  */
#define UNSTABLE_MAC	0x02	/* Probed MAC address may be incorrect.  */
#define UNSTABLE_TYPE	0x04	/* Probed vendor/model may be incorrect.  */

/* Mapping between media bitmap (in fe_softc.mbitmap) and ifm_media.  */
#define MB_HA	0x0001
#define MB_HM	0x0002
#define MB_HT	0x0004
#define MB_H2	0x0008
#define MB_H5	0x0010
#define MB_HF	0x0020
#define MB_FT	0x0040

/* Card types. */
#define FE_TYPE_SSI		1
#define FE_TYPE_JLI		2
#define FE_TYPE_FMV		3
#define FE_TYPE_LNX		4
#define FE_TYPE_UBN		5
#define FE_TYPE_GWY		6
#define FE_TYPE_MBH		7
#define FE_TYPE_TDK		8
#define FE_TYPE_RE1000		9
#define FE_TYPE_CNET9NE		10
#define FE_TYPE_REX		11

/*
 * Data type for a multicast address filter on 8696x.
 */
struct fe_filter {
	u_char data [FE_FILTER_LEN];
};

/*
 * fe_softc: per line info and status
 */
struct fe_softc {

	/* Used by "common" codes.  */
	struct ifnet		*ifp;
	int			sc_unit;
	u_char			enaddr[6];

	/* Used by config codes.  */
	int			type;
	int			port_used;
	struct resource *	port_res;
	struct resource *	irq_res;
	void *			irq_handle;

	/* Set by probe() and not modified in later phases.  */
	char const * typestr;	/* printable name of the interface.  */
	u_short txb_size;	/* size of TX buffer, in bytes  */
	u_char proto_dlcr4;	/* DLCR4 prototype.  */
	u_char proto_dlcr5;	/* DLCR5 prototype.  */
	u_char proto_dlcr6;	/* DLCR6 prototype.  */
	u_char proto_dlcr7;	/* DLCR7 prototype.  */
	u_char proto_bmpr13;	/* BMPR13 prototype.  */
	u_char stability;	/* How stable is this?  */ 
	u_short priv_info;	/* info specific to a vendor/model.  */

	/* Vendor/model specific hooks.  */
	void (*init)(struct fe_softc *); /* Just before fe_init().  */
	void (*stop)(struct fe_softc *); /* Just after fe_stop().  */

	/* Transmission buffer management.  */
	u_short txb_free;	/* free bytes in TX buffer  */
	u_char txb_count;	/* number of packets in TX buffer  */
	u_char txb_sched;	/* number of scheduled packets  */

	/* Excessive collision counter (see fe_tint() for details.)  */
	u_char tx_excolls;	/* # of excessive collisions.  */

	/* Multicast address filter management.  */
	u_char filter_change;	/* MARs must be changed ASAP. */
	struct fe_filter filter;/* new filter value.  */

	/* Network management.  */
	struct ifmib_iso_8802_3 mibdata;

	/* Media information.  */
	struct ifmedia media;	/* used by if_media.  */
	u_short mbitmap;	/* bitmap for supported media; see bit2media */
	int defmedia;		/* default media  */
	void (* msel)(struct fe_softc *); /* media selector.  */

	struct mtx		lock;
	struct callout		timer;
	int			tx_timeout;
};

struct fe_simple_probe_struct {
	u_char port;	/* Offset from the base I/O address.  */
	u_char mask;	/* Bits to be checked.  */
	u_char bits;	/* Values to be compared against.  */
};

#define	FE_LOCK(sc)		mtx_lock(&(sc)->lock)
#define	FE_UNLOCK(sc)		mtx_unlock(&(sc)->lock)
#define	FE_ASSERT_LOCKED(sc)	mtx_assert(&(sc)->lock, MA_OWNED)

extern	devclass_t fe_devclass;

int	fe_attach(device_t);
int	fe_alloc_port(device_t, int);
int	fe_alloc_irq(device_t, int);
void	fe_release_resource(device_t);

int	fe_simple_probe(struct fe_softc const *,
			struct fe_simple_probe_struct const *);
int	fe_valid_Ether_p(u_char const *, unsigned);
void	fe_softc_defaults(struct fe_softc *);
void	fe_stop(struct fe_softc *sc);
void	fe_irq_failure(char const *, int, int, char const *);
void	fe_msel_965(struct fe_softc *);
void	fe_read_eeprom_jli(struct fe_softc *, u_char *);
void	fe_init_jli(struct fe_softc *);
void	fe_read_eeprom_ssi(struct fe_softc *, u_char *);
void	fe_read_eeprom_lnx(struct fe_softc *, u_char *);
void	fe_init_lnx(struct fe_softc *);
void	fe_init_ubn(struct fe_softc *);


#define	fe_inb(sc, port) \
	bus_read_1((sc)->port_res, (port))

#define	fe_outb(sc, port, value) \
	bus_write_1((sc)->port_res, (port), (value))

#define	fe_inw(sc, port) \
	bus_read_2((sc)->port_res, (port))

#define	fe_outw(sc, port, value) \
	bus_write_2((sc)->port_res, (port), (value))

#define	fe_insb(sc, port, addr, count) \
	bus_read_multi_1((sc)->port_res, (port), (addr), (count))

#define	fe_outsb(sc, port, addr, count) \
	bus_write_multi_1((sc)->port_res, (port), (addr), (count))

#define	fe_insw(sc, port, addr, count) \
	bus_read_multi_2((sc)->port_res, (port), (addr), (count))

#define	fe_outsw(sc, port, addr, count) \
	bus_write_multi_2((sc)->port_res, (port), (addr), (count))

#define fe_inblk(sc, port, addr, count) \
	bus_read_region_1((sc)->port_res, (port), (addr), (count))

#define fe_outblk(sc, port, addr, count) \
	bus_write_region_1((sc)->port_res, (port), (addr), (count))
