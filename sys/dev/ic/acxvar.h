/*	$OpenBSD: acxvar.h,v 1.20 2024/05/29 01:11:53 jsg Exp $ */

/*
 * Copyright (c) 2006 Jonathan Gray <jsg@openbsd.org>
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

/*
 * Copyright (c) 2006 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _IF_ACXVAR_H
#define _IF_ACXVAR_H

#ifdef ACX_DEBUG
extern int acxdebug;
#define DPRINTF(x)      do { if (acxdebug) printf x; } while (0)
#define DPRINTFN(n,x)   do { if (acxdebug >= (n)) printf x; } while (0)
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

#define ACX_FRAME_HDRLEN	sizeof(struct ieee80211_frame)
#define ACX_MEMBLOCK_SIZE	256

#define ACX_TX_DESC_CNT		16
#define ACX_RX_DESC_CNT		16

#define ACX_TX_RING_SIZE	\
	(2 * ACX_TX_DESC_CNT * sizeof(struct acx_host_desc))
#define ACX_RX_RING_SIZE	\
	(ACX_RX_DESC_CNT * sizeof(struct acx_host_desc))

#define CSR_READ_1(sc, reg)					\
	bus_space_read_1((sc)->sc_mem1_bt, (sc)->sc_mem1_bh,	\
			 (sc)->chip_ioreg[(reg)])
#define CSR_READ_2(sc, reg)					\
	bus_space_read_2((sc)->sc_mem1_bt, (sc)->sc_mem1_bh,	\
			 (sc)->chip_ioreg[(reg)])
#define CSR_READ_4(sc, reg)					\
	bus_space_read_4((sc)->sc_mem1_bt, (sc)->sc_mem1_bh,	\
			 (sc)->chip_ioreg[(reg)])

#define CSR_WRITE_2(sc, reg, val)				\
	bus_space_write_2((sc)->sc_mem1_bt, (sc)->sc_mem1_bh,	\
			  (sc)->chip_ioreg[(reg)], val)
#define CSR_WRITE_4(sc, reg, val)				\
	bus_space_write_4((sc)->sc_mem1_bt, (sc)->sc_mem1_bh,	\
			  (sc)->chip_ioreg[(reg)], val)

#define CSR_SETB_2(sc, reg, b)		\
	CSR_WRITE_2((sc), (reg), CSR_READ_2((sc), (reg)) | (b))
#define CSR_CLRB_2(sc, reg, b)		\
	CSR_WRITE_2((sc), (reg), CSR_READ_2((sc), (reg)) & (~(b)))

#define DESC_WRITE_REGION_1(sc, off, d, dlen)				\
	bus_space_write_region_1((sc)->sc_mem2_bt, (sc)->sc_mem2_bh,	\
				 (off),	(const uint8_t *)(d), (dlen))

#define FW_TXDESC_SETFIELD_1(sc, mb, field, val)		\
	bus_space_write_1((sc)->sc_mem2_bt, (sc)->sc_mem2_bh,	\
	    (mb)->tb_fwdesc_ofs + offsetof(struct acx_fw_txdesc, field), (val))
#define FW_TXDESC_SETFIELD_2(sc, mb, field, val)		\
	bus_space_write_2((sc)->sc_mem2_bt, (sc)->sc_mem2_bh,	\
	    (mb)->tb_fwdesc_ofs + offsetof(struct acx_fw_txdesc, field), (val))
#define FW_TXDESC_SETFIELD_4(sc, mb, field, val)	\
	bus_space_write_4((sc)->sc_mem2_bt, (sc)->sc_mem2_bh,	\
	    (mb)->tb_fwdesc_ofs + offsetof(struct acx_fw_txdesc, field), (val))

#define FW_TXDESC_GETFIELD_1(sc, mb, field)			\
	bus_space_read_1((sc)->sc_mem2_bt, (sc)->sc_mem2_bh,	\
	    (mb)->tb_fwdesc_ofs + offsetof(struct acx_fw_txdesc, field))

/*
 * Firmware TX descriptor
 * Fields are little endian
 */
struct acx_fw_txdesc {
	uint32_t	f_tx_next_desc;	/* next acx_fw_txdesc phyaddr */
	uint32_t	f_tx_host_desc;	/* acx_host_desc phyaddr */
	uint32_t	f_tx_acx_ptr;
	uint32_t	f_tx_time;
	uint16_t	f_tx_len;
	uint16_t	f_tx_reserved;

	uint32_t	f_tx_dev_spec[4];

	uint8_t		f_tx_ctrl;	/* see DESC_CTRL_ */
	uint8_t		f_tx_ctrl2;
	uint8_t		f_tx_error;	/* see DESC_ERR_ */
	uint8_t		f_tx_ack_fail;
	uint8_t		f_tx_rts_fail;
	uint8_t		f_tx_rts_ok;

	/* XXX should be moved to chip specific file */
	union {
		struct {
			uint8_t		rate100;	/* acx100 tx rate */
			uint8_t		queue_ctrl;
		} __packed r1;
		struct {
			uint16_t	rate111;	/* acx111 tx rate */
		} __packed r2;
	} u;
#define f_tx_rate100	u.r1.rate100
#define f_tx_queue_ctrl	u.r1.queue_ctrl
#define f_tx_rate111	u.r2.rate111
	uint32_t	f_tx_queue_info;
} __packed;

/*
 * Firmware RX descriptor
 * Fields are little endian
 */
struct acx_fw_rxdesc {
	uint32_t	f_rx_next_desc;	/* next acx_fw_rxdesc phyaddr */
	uint32_t	f_rx_host_desc;	/* acx_host_desc phyaddr */
	uint32_t	f_rx_acx_ptr;
	uint32_t	f_rx_time;
	uint16_t	f_rx_len;
	uint16_t	f_rx_wep_len;
	uint32_t	f_rx_wep_ofs;

	uint8_t		f_rx_dev_spec[16];

	uint8_t		f_rx_ctrl;	/* see DESC_CTRL_ */
	uint8_t		f_rx_rate;
	uint8_t		f_rx_error;
	uint8_t		f_rx_snr;	/* signal noise ratio */
	uint8_t		f_rx_level;
	uint8_t		f_rx_queue_ctrl;
	uint16_t	f_rx_unknown0;
	uint32_t	f_rx_unknown1;
} __packed;

/*
 * Host TX/RX descriptor
 * Fields are little endian
 */
struct acx_host_desc {
	uint32_t	h_data_paddr;	/* data phyaddr */
	uint16_t	h_data_ofs;
	uint16_t	h_reserved;
	uint16_t	h_ctrl;		/* see DESC_CTRL_ */
	uint16_t	h_data_len;	/* data length */
	uint32_t	h_next_desc;	/* next acx_host_desc phyaddr */
	uint32_t	h_pnext;
	uint32_t	h_status;	/* see DESC_STATUS_ */
} __packed;

#define DESC_STATUS_FULL		0x80000000

#define DESC_CTRL_SHORT_PREAMBLE	0x01
#define DESC_CTRL_FIRST_FRAG		0x02
#define DESC_CTRL_AUTODMA		0x04
#define DESC_CTRL_RECLAIM		0x08
#define DESC_CTRL_HOSTDONE		0x20	/* host finished buf proc */
#define DESC_CTRL_ACXDONE		0x40	/* chip finished buf proc */
#define DESC_CTRL_HOSTOWN		0x80	/* host controls desc */

#define DESC_ERR_OTHER_FRAG		0x01
#define DESC_ERR_ABORT			0x02
#define DESC_ERR_PARAM			0x04
#define DESC_ERR_NO_WEPKEY		0x08
#define DESC_ERR_MSDU_TIMEOUT		0x10
#define DESC_ERR_EXCESSIVE_RETRY	0x20
#define DESC_ERR_BUF_OVERFLOW		0x40
#define DESC_ERR_DMA			0x80

/*
 * Extra header in receiving buffer
 * Fields are little endian
 */
struct acx_rxbuf_hdr {
	uint16_t	rbh_len;	/* ACX_RXBUG_LEN_MASK part is len */
	uint8_t		rbh_memblk_cnt;
	uint8_t		rbh_status;
	uint8_t		rbh_stat_baseband; /* see ACX_RXBUF_STAT_ */
	uint8_t		rbh_plcp;
	uint8_t		rbh_level;	/* signal level */
	uint8_t		rbh_snr;	/* signal noise ratio */
	uint32_t	rbh_time;	/* recv timestamp */

	/*
	 * XXX may have 4~8 byte here which
	 * depends on firmware version
	 */
} __packed;

#define ACX_RXBUF_LEN_MASK	0xfff
#define ACX_RXBUF_STAT_LNA	0x80	/* low noise amplifier */

struct acx_ring_data {
	struct acx_host_desc	*rx_ring;
	bus_dma_segment_t	rx_ring_seg;
	bus_dmamap_t		rx_ring_dmamap;
	uint32_t		rx_ring_paddr;

	struct acx_host_desc	*tx_ring;
	bus_dma_segment_t	tx_ring_seg;
	bus_dmamap_t		tx_ring_dmamap;
	uint32_t		tx_ring_paddr;
};

struct acx_txbuf {
	struct mbuf		*tb_mbuf;
	bus_dmamap_t		tb_mbuf_dmamap;

	struct acx_host_desc	*tb_desc1;
	struct acx_host_desc	*tb_desc2;

	uint32_t		tb_fwdesc_ofs;

	/*
	 * Used by tx rate updating
	 */
	struct acx_node		*tb_node;	/* remote node */
	int			tb_rate;	/* current tx rate */
};

struct acx_rxbuf {
	struct mbuf		*rb_mbuf;
	bus_dmamap_t		rb_mbuf_dmamap;

	struct acx_host_desc	*rb_desc;
};

struct acx_buf_data {
	struct acx_rxbuf	rx_buf[ACX_RX_DESC_CNT];
	struct acx_txbuf	tx_buf[ACX_TX_DESC_CNT];
	bus_dmamap_t		mbuf_tmp_dmamap;

	int			rx_scan_start;

	int			tx_free_start;
	int			tx_used_start;
	int			tx_used_count;
};

struct acx_node {
	struct ieee80211_node		ni;	/* must be first */
	struct ieee80211_amrr_node	amn;
};

struct acx_config {
	uint8_t	antenna;
	uint8_t	regdom;
	uint8_t	cca_mode;	/* acx100 */
	uint8_t	ed_thresh;	/* acx100 */
};

struct acx_stats {
	uint64_t	err_oth_frag;	/* XXX error in other frag?? */
	uint64_t	err_abort;	/* tx abortion */
	uint64_t	err_param;	/* tx desc contains invalid param */
	uint64_t	err_no_wepkey;	/* no WEP key exists */
	uint64_t	err_msdu_timeout; /* MSDU timed out */
	uint64_t	err_ex_retry;	/* excessive tx retry */
	uint64_t	err_buf_oflow;	/* buffer overflow */
	uint64_t	err_dma;	/* DMA error */
	uint64_t	err_unkn;	/* XXX unknown error */
};

#define ACX_RX_RADIOTAP_PRESENT						\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL) |				\
	 (1 << IEEE80211_RADIOTAP_RSSI))

struct acx_rx_radiotap_hdr {
	struct ieee80211_radiotap_header	wr_ihdr;
	uint8_t					wr_flags;
	uint16_t				wr_chan_freq;
	uint16_t				wr_chan_flags;
	uint8_t					wr_rssi;
	uint8_t					wr_max_rssi;
} __packed;

#define ACX_TX_RADIOTAP_PRESENT						\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_RATE) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL))				\

struct acx_tx_radiotap_hdr {
	struct ieee80211_radiotap_header	wt_ihdr;
	uint8_t					wt_flags;
	uint8_t					wt_rate;
	uint16_t				wt_chan_freq;
	uint16_t				wt_chan_flags;
} __packed;

struct acx_softc {
	/*
	 * sc_xxx are filled in by common code
	 * chip_xxx are filled in by chip specific code
	 */
	struct device		sc_dev;
	struct ieee80211com	sc_ic;

	struct timeout		sc_chanscan_timer;
	uint32_t		sc_flags;	/* see ACX_FLAG_ */

	uint32_t		sc_firmware_ver;
	uint32_t		sc_hardware_id;

	bus_dma_tag_t		sc_dmat;

	struct ieee80211_amrr	amrr;
	struct timeout		amrr_ch;

	/*
	 * MMIO 1
	 */
	bus_space_tag_t		sc_mem1_bt;
	bus_space_handle_t	sc_mem1_bh;
	int			chip_mem1_rid;

	/*
	 * MMIO 2
	 */
	bus_space_tag_t		sc_mem2_bt;
	bus_space_handle_t	sc_mem2_bh;
	int			chip_mem2_rid;

	int			(*sc_enable)(struct acx_softc *);
	void			(*sc_disable)(struct acx_softc *);
	void			(*sc_power)(struct acx_softc *, int);

	uint32_t		sc_cmd;		/* cmd reg (MMIO 2) */
	uint32_t		sc_cmd_param;	/* cmd param reg (MMIO 2) */
	uint32_t		sc_info;	/* unused */
	uint32_t		sc_info_param;	/* unused */

	const uint16_t		*chip_ioreg;	/* reg map (MMIO 1) */

	/*
	 * NOTE:
	 * chip_intr_enable is not necessarily same as
	 * ~chip_intr_disable
	 */
	uint16_t		chip_intr_enable;
	uint16_t		chip_intr_disable;

	int			chip_hw_crypt;
	uint16_t		chip_gpio_pled;	/* power led */
	uint16_t		chip_chan_flags; /* see IEEE80211_CHAN_ */
	uint16_t		chip_txdesc1_len;
	int			chip_rxbuf_exhdr; /* based on fw ver */
	uint32_t		chip_ee_eaddr_ofs;
	enum ieee80211_phymode	chip_phymode;	/* see IEEE80211_MODE_ */
	uint8_t			chip_fw_txdesc_ctrl;

	uint8_t			sc_eeprom_ver;	/* unused */
	uint8_t			sc_form_factor;	/* unused */
	uint8_t			sc_radio_type;	/* see ACX_RADIO_TYPE_ */

	struct acx_ring_data	sc_ring_data;
	struct acx_buf_data	sc_buf_data;

	struct acx_stats	sc_stats;	/* statistics */

	/*
	 * Per interface sysctl variables
	 */
	int			sc_txtimer;
	int			sc_long_retry_limit;
	int			sc_short_retry_limit;
	int			sc_msdu_lifetime;

	int			(*sc_newstate)
				(struct ieee80211com *,
				 enum ieee80211_state, int);

	int			(*chip_init)		/* non-NULL */
				(struct acx_softc *);

	int			(*chip_set_wepkey)
				(struct acx_softc *,
				 struct ieee80211_key *, int);

	int			(*chip_read_config)
				(struct acx_softc *, struct acx_config *);

	int			(*chip_write_config)
				(struct acx_softc *, struct acx_config *);

	void			(*chip_set_fw_txdesc_rate) /* non-NULL */
				(struct acx_softc *, struct acx_txbuf *, int);

	void			(*chip_set_bss_join_param) /* non-NULL */
				(struct acx_softc *, void *, int);

	void			(*chip_proc_wep_rxbuf)
				(struct acx_softc *, struct mbuf *, int *);

#if NBPFILTER > 0
	caddr_t			sc_drvbpf;

	union {
		struct acx_rx_radiotap_hdr th;
		uint8_t pad[64];
	}			sc_rxtapu;
#define sc_rxtap		sc_rxtapu.th
	int			sc_rxtap_len;

	union {
		struct acx_tx_radiotap_hdr th;
		uint8_t pad[64];
	}			sc_txtapu;
#define sc_txtap		sc_txtapu.th
	int			sc_txtap_len;
#endif
};

#define ACX_FLAG_FW_LOADED	0x01
#define ACX_FLAG_ACX111		0x02

#define ACX_RADIO_TYPE_MAXIM	0x0d
#define ACX_RADIO_TYPE_RFMD	0x11
#define ACX_RADIO_TYPE_RALINK	0x15
#define ACX_RADIO_TYPE_RADIA	0x16
#define ACX_RADIO_TYPE_UNKN17	0x17
#define ACX_RADIO_TYPE_UNKN19	0x19

#define ACX_RADIO_RSSI_MAXIM	120	/* 100dB */
#define ACX_RADIO_RSSI_RFMD	215	/* 215dB */
#define ACX_RADIO_RSSI_RALINK	0	/* XXX unknown yet */
#define ACX_RADIO_RSSI_RADIA	78	/* 78db */
#define ACX_RADIO_RSSI_UNKN	0	/* unknown radio */

extern int				acx_beacon_intvl;

void	acx100_set_param(struct acx_softc *);
void	acx111_set_param(struct acx_softc *);

int	acx_init_tmplt_ordered(struct acx_softc *);
void	acx_write_phyreg(struct acx_softc *, uint32_t, uint8_t);

int	acx_set_tmplt(struct acx_softc *, uint16_t, void *, uint16_t);
int	acx_get_conf(struct acx_softc *, uint16_t, void *, uint16_t);
int	acx_set_conf(struct acx_softc *, uint16_t, void *, uint16_t);
int	acx_exec_command(struct acx_softc *, uint16_t, void *, uint16_t,
	    void *, uint16_t);
int	acx_attach(struct acx_softc *);
int	acx_detach(void *);
int	acx_intr(void *);

#endif	/* !_IF_ACXVAR_H */
