/*-
 * Copyright (c) 2016 Stanislav Galabov.
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
 * $FreeBSD$
 */

#ifndef	__MTKSWITCHVAR_H__
#define	__MTKSWITCHVAR_H__

typedef enum {
	MTK_SWITCH_NONE,
	MTK_SWITCH_RT3050,
	MTK_SWITCH_RT3352,
	MTK_SWITCH_RT5350,
	MTK_SWITCH_MT7620,
	MTK_SWITCH_MT7621,
	MTK_SWITCH_MT7628,
} mtk_switch_type;

#define	MTK_IS_SWITCH(_sc, _type)		\
	    (!!((_sc)->sc_switchtype == MTK_SWITCH_ ## _type))

#define	MTKSWITCH_MAX_PORTS	7
#define MTKSWITCH_MAX_PHYS	7
#define	MTKSWITCH_CPU_PORT	6

#define	MTKSWITCH_LINK_UP	(1<<0)
#define	MTKSWITCH_SPEED_MASK	(3<<1)
#define	MTKSWITCH_SPEED_10	(0<<1)
#define MTKSWITCH_SPEED_100	(1<<1)
#define	MTKSWITCH_SPEED_1000	(2<<1)
#define	MTKSWITCH_DUPLEX	(1<<3)
#define MTKSWITCH_TXFLOW	(1<<4)
#define MTKSWITCH_RXFLOW	(1<<5)

struct mtkswitch_softc {
	struct mtx	sc_mtx;
	device_t	sc_dev;
	struct resource *sc_res;
	int		numphys;
	uint32_t	phymap;
	int		numports;
	uint32_t	portmap;
	int		cpuport;
	uint32_t	valid_vlans;
	mtk_switch_type	sc_switchtype;
	char		*ifname[MTKSWITCH_MAX_PHYS];
	device_t	miibus[MTKSWITCH_MAX_PHYS];
	struct ifnet	*ifp[MTKSWITCH_MAX_PHYS];
	struct callout	callout_tick;
	etherswitch_info_t info;

	uint32_t	vlan_mode;

	struct {
		/* Global setup */
		int (* mtkswitch_reset) (struct mtkswitch_softc *);
		int (* mtkswitch_hw_setup) (struct mtkswitch_softc *);
		int (* mtkswitch_hw_global_setup) (struct mtkswitch_softc *);

		/* Port functions */
		void (* mtkswitch_port_init) (struct mtkswitch_softc *, int);
		uint32_t (* mtkswitch_get_port_status)
		    (struct mtkswitch_softc *, int);

		/* ATU functions */
		int (* mtkswitch_atu_flush) (struct mtkswitch_softc *);

		/* VLAN functions */
		int (* mtkswitch_port_vlan_setup) (struct mtkswitch_softc *,
		    etherswitch_port_t *);
		int (* mtkswitch_port_vlan_get) (struct mtkswitch_softc *,
		    etherswitch_port_t *);
		void (* mtkswitch_vlan_init_hw) (struct mtkswitch_softc *);
		int (* mtkswitch_vlan_getvgroup) (struct mtkswitch_softc *,
		    etherswitch_vlangroup_t *);
		int (* mtkswitch_vlan_setvgroup) (struct mtkswitch_softc *,
		    etherswitch_vlangroup_t *);
		int (* mtkswitch_vlan_get_pvid) (struct mtkswitch_softc *,
		    int, int *);
		int (* mtkswitch_vlan_set_pvid) (struct mtkswitch_softc *,
		    int, int);

		/* PHY functions */
		int (* mtkswitch_phy_read) (device_t, int, int);
		int (* mtkswitch_phy_write) (device_t, int, int, int);

		/* Register functions */
		int (* mtkswitch_reg_read) (device_t, int);
		int (* mtkswitch_reg_write) (device_t, int, int);

		/* Internal register access functions */
		uint32_t (* mtkswitch_read) (struct mtkswitch_softc *, int);
		uint32_t (* mtkswitch_write) (struct mtkswitch_softc *, int,
		    uint32_t);
	} hal;
};

#define	MTKSWITCH_LOCK(_sc)			\
	    mtx_lock(&(_sc)->sc_mtx)
#define	MTKSWITCH_UNLOCK(_sc)			\
	    mtx_unlock(&(_sc)->sc_mtx)
#define	MTKSWITCH_LOCK_ASSERT(_sc, _what)	\
	    mtx_assert(&(_sc)->sc_mtx, (_what))
#define	MTKSWITCH_TRYLOCK(_sc)			\
	    mtx_trylock(&(_sc)->sc_mtx)

#define	MTKSWITCH_READ(_sc, _reg)		\
	    bus_read_4((_sc)->sc_res, (_reg))
#define MTKSWITCH_WRITE(_sc, _reg, _val)	\
	    bus_write_4((_sc)->sc_res, (_reg), (_val))
#define	MTKSWITCH_MOD(_sc, _reg, _clr, _set)	\
	    MTKSWITCH_WRITE((_sc), (_reg),	\
	        ((MTKSWITCH_READ((_sc), (_reg)) & ~(_clr)) | (_set))

#define	MTKSWITCH_REG32(addr)	((addr) & ~(0x3))
#define	MTKSWITCH_IS_HI16(addr)	(((addr) & 0x3) > 0x1)
#define	MTKSWITCH_HI16(x)	(((x) >> 16) & 0xffff)
#define	MTKSWITCH_LO16(x)	((x) & 0xffff)
#define	MTKSWITCH_TO_HI16(x)	(((x) & 0xffff) << 16)
#define	MTKSWITCH_TO_LO16(x)	((x) & 0xffff)
#define	MTKSWITCH_HI16_MSK	0xffff0000
#define MTKSWITCH_LO16_MSK	0x0000ffff

#if defined(DEBUG)
#define	DPRINTF(dev, args...)	device_printf(dev, args)
#define	DEVERR(dev, err, fmt, args...)	do {	\
	    if (err != 0) device_printf(dev, fmt, err, args);	\
	} while (0)
#define	DEBUG_INCRVAR(var)		do {	\
	    var++;				\
	} while (0)
#else
#define	DPRINTF(dev, args...)
#define	DEVERR(dev, err, fmt, args...)
#define	DEBUG_INCRVAR(var)
#endif

extern void mtk_attach_switch_rt3050(struct mtkswitch_softc *);
extern void mtk_attach_switch_mt7620(struct mtkswitch_softc *);

#endif	/* __MTKSWITCHVAR_H__ */
