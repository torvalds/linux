/*********************************************************************
 *                
 * Filename:      ircomm_param.h
 * Version:       1.0
 * Description:   Parameter handling for the IrCOMM protocol
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Mon Jun  7 08:47:28 1999
 * Modified at:   Wed Aug 25 13:46:33 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1999 Dag Brattli, All Rights Reserved.
 *     
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License 
 *     along with this program; if not, write to the Free Software 
 *     Foundation, Inc., 59 Temple Place, Suite 330, Boston, 
 *     MA 02111-1307 USA
 *     
 ********************************************************************/

#ifndef IRCOMM_PARAMS_H
#define IRCOMM_PARAMS_H

#include <net/irda/parameters.h>

/* Parameters common to all service types */
#define IRCOMM_SERVICE_TYPE     0x00
#define IRCOMM_PORT_TYPE        0x01 /* Only used in LM-IAS */
#define IRCOMM_PORT_NAME        0x02 /* Only used in LM-IAS */

/* Parameters for both 3 wire and 9 wire */
#define IRCOMM_DATA_RATE        0x10
#define IRCOMM_DATA_FORMAT      0x11
#define IRCOMM_FLOW_CONTROL     0x12
#define IRCOMM_XON_XOFF         0x13
#define IRCOMM_ENQ_ACK          0x14
#define IRCOMM_LINE_STATUS      0x15
#define IRCOMM_BREAK            0x16

/* Parameters for 9 wire */
#define IRCOMM_DTE              0x20
#define IRCOMM_DCE              0x21
#define IRCOMM_POLL             0x22

/* Service type (details) */
#define IRCOMM_3_WIRE_RAW       0x01
#define IRCOMM_3_WIRE           0x02
#define IRCOMM_9_WIRE           0x04
#define IRCOMM_CENTRONICS       0x08

/* Port type (details) */
#define IRCOMM_SERIAL           0x00
#define IRCOMM_PARALLEL         0x01

/* Data format (details) */
#define IRCOMM_WSIZE_5          0x00
#define IRCOMM_WSIZE_6          0x01
#define IRCOMM_WSIZE_7          0x02
#define IRCOMM_WSIZE_8          0x03

#define IRCOMM_1_STOP_BIT       0x00
#define IRCOMM_2_STOP_BIT       0x04 /* 1.5 if char len 5 */

#define IRCOMM_PARITY_DISABLE   0x00
#define IRCOMM_PARITY_ENABLE    0x08

#define IRCOMM_PARITY_ODD       0x00
#define IRCOMM_PARITY_EVEN      0x10
#define IRCOMM_PARITY_MARK      0x20
#define IRCOMM_PARITY_SPACE     0x30

/* Flow control */
#define IRCOMM_XON_XOFF_IN      0x01
#define IRCOMM_XON_XOFF_OUT     0x02
#define IRCOMM_RTS_CTS_IN       0x04
#define IRCOMM_RTS_CTS_OUT      0x08
#define IRCOMM_DSR_DTR_IN       0x10
#define IRCOMM_DSR_DTR_OUT      0x20
#define IRCOMM_ENQ_ACK_IN       0x40
#define IRCOMM_ENQ_ACK_OUT      0x80

/* Line status */
#define IRCOMM_OVERRUN_ERROR    0x02
#define IRCOMM_PARITY_ERROR     0x04
#define IRCOMM_FRAMING_ERROR    0x08

/* DTE (Data terminal equipment) line settings */
#define IRCOMM_DELTA_DTR        0x01
#define IRCOMM_DELTA_RTS        0x02
#define IRCOMM_DTR              0x04
#define IRCOMM_RTS              0x08

/* DCE (Data communications equipment) line settings */
#define IRCOMM_DELTA_CTS        0x01  /* Clear to send has changed */
#define IRCOMM_DELTA_DSR        0x02  /* Data set ready has changed */
#define IRCOMM_DELTA_RI         0x04  /* Ring indicator has changed */
#define IRCOMM_DELTA_CD         0x08  /* Carrier detect has changed */
#define IRCOMM_CTS              0x10  /* Clear to send is high */
#define IRCOMM_DSR              0x20  /* Data set ready is high */
#define IRCOMM_RI               0x40  /* Ring indicator is high */
#define IRCOMM_CD               0x80  /* Carrier detect is high */
#define IRCOMM_DCE_DELTA_ANY    0x0f

/*
 * Parameter state
 */
struct ircomm_params {
	/* General control params */
	__u8  service_type;
	__u8  port_type;
	char  port_name[32];

	/* Control params for 3- and 9-wire service type */
	__u32 data_rate;         /* Data rate in bps */
	__u8  data_format;
	__u8  flow_control;
	char  xonxoff[2];
	char  enqack[2];
	__u8  line_status;
	__u8  _break;

	__u8  null_modem;

	/* Control params for 9-wire service type */
	__u8 dte;
	__u8 dce;
	__u8 poll;

	/* Control params for Centronics service type */
};

struct ircomm_tty_cb; /* Forward decl. */

int ircomm_param_request(struct ircomm_tty_cb *self, __u8 pi, int flush);

extern pi_param_info_t ircomm_param_info;

#endif /* IRCOMM_PARAMS_H */

