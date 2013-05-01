/*
 * Header for Bestcomm FEC tasks driver
 *
 *
 * Copyright (C) 2006-2007 Sylvain Munaut <tnt@246tNt.com>
 * Copyright (C) 2003-2004 MontaVista, Software, Inc.
 *                         ( by Dale Farnsworth <dfarnsworth@mvista.com> )
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef __BESTCOMM_FEC_H__
#define __BESTCOMM_FEC_H__


struct bcom_fec_bd {
	u32	status;
	u32	skb_pa;
};

#define BCOM_FEC_TX_BD_TFD	0x08000000ul	/* transmit frame done */
#define BCOM_FEC_TX_BD_TC	0x04000000ul	/* transmit CRC */
#define BCOM_FEC_TX_BD_ABC	0x02000000ul	/* append bad CRC */

#define BCOM_FEC_RX_BD_L	0x08000000ul	/* buffer is last in frame */
#define BCOM_FEC_RX_BD_BC	0x00800000ul	/* DA is broadcast */
#define BCOM_FEC_RX_BD_MC	0x00400000ul	/* DA is multicast and not broadcast */
#define BCOM_FEC_RX_BD_LG	0x00200000ul	/* Rx frame length violation */
#define BCOM_FEC_RX_BD_NO	0x00100000ul	/* Rx non-octet aligned frame */
#define BCOM_FEC_RX_BD_CR	0x00040000ul	/* Rx CRC error */
#define BCOM_FEC_RX_BD_OV	0x00020000ul	/* overrun */
#define BCOM_FEC_RX_BD_TR	0x00010000ul	/* Rx frame truncated */
#define BCOM_FEC_RX_BD_LEN_MASK	0x000007fful	/* mask for length of received frame */
#define BCOM_FEC_RX_BD_ERRORS	(BCOM_FEC_RX_BD_LG | BCOM_FEC_RX_BD_NO | \
		BCOM_FEC_RX_BD_CR | BCOM_FEC_RX_BD_OV | BCOM_FEC_RX_BD_TR)


extern struct bcom_task *
bcom_fec_rx_init(int queue_len, phys_addr_t fifo, int maxbufsize);

extern int
bcom_fec_rx_reset(struct bcom_task *tsk);

extern void
bcom_fec_rx_release(struct bcom_task *tsk);


extern struct bcom_task *
bcom_fec_tx_init(int queue_len, phys_addr_t fifo);

extern int
bcom_fec_tx_reset(struct bcom_task *tsk);

extern void
bcom_fec_tx_release(struct bcom_task *tsk);


#endif /* __BESTCOMM_FEC_H__ */

