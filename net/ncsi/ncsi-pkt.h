/*
 * Copyright Gavin Shan, IBM Corporation 2016.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __NCSI_PKT_H__
#define __NCSI_PKT_H__

struct ncsi_pkt_hdr {
	unsigned char mc_id;        /* Management controller ID */
	unsigned char revision;     /* NCSI version - 0x01      */
	unsigned char reserved;     /* Reserved                 */
	unsigned char id;           /* Packet sequence number   */
	unsigned char type;         /* Packet type              */
	unsigned char channel;      /* Network controller ID    */
	__be16        length;       /* Payload length           */
	__be32        reserved1[2]; /* Reserved                 */
};

struct ncsi_cmd_pkt_hdr {
	struct ncsi_pkt_hdr common; /* Common NCSI packet header */
};

/* NCSI common command packet */
struct ncsi_cmd_pkt {
	struct ncsi_cmd_pkt_hdr cmd;      /* Command header */
	__be32                  checksum; /* Checksum       */
	unsigned char           pad[26];
};

/* Select Package */
struct ncsi_cmd_sp_pkt {
	struct ncsi_cmd_pkt_hdr cmd;            /* Command header */
	unsigned char           reserved[3];    /* Reserved       */
	unsigned char           hw_arbitration; /* HW arbitration */
	__be32                  checksum;       /* Checksum       */
	unsigned char           pad[22];
};

/* Disable Channel */
struct ncsi_cmd_dc_pkt {
	struct ncsi_cmd_pkt_hdr cmd;         /* Command header  */
	unsigned char           reserved[3]; /* Reserved        */
	unsigned char           ald;         /* Allow link down */
	__be32                  checksum;    /* Checksum        */
	unsigned char           pad[22];
};

/* Reset Channel */
struct ncsi_cmd_rc_pkt {
	struct ncsi_cmd_pkt_hdr cmd;      /* Command header */
	__be32                  reserved; /* Reserved       */
	__be32                  checksum; /* Checksum       */
	unsigned char           pad[22];
};

/* AEN Enable */
struct ncsi_cmd_ae_pkt {
	struct ncsi_cmd_pkt_hdr cmd;         /* Command header   */
	unsigned char           reserved[3]; /* Reserved         */
	unsigned char           mc_id;       /* MC ID            */
	__be32                  mode;        /* AEN working mode */
	__be32                  checksum;    /* Checksum         */
	unsigned char           pad[18];
};

/* Set Link */
struct ncsi_cmd_sl_pkt {
	struct ncsi_cmd_pkt_hdr cmd;      /* Command header    */
	__be32                  mode;     /* Link working mode */
	__be32                  oem_mode; /* OEM link mode     */
	__be32                  checksum; /* Checksum          */
	unsigned char           pad[18];
};

/* Set VLAN Filter */
struct ncsi_cmd_svf_pkt {
	struct ncsi_cmd_pkt_hdr cmd;       /* Command header    */
	__be16                  reserved;  /* Reserved          */
	__be16                  vlan;      /* VLAN ID           */
	__be16                  reserved1; /* Reserved          */
	unsigned char           index;     /* VLAN table index  */
	unsigned char           enable;    /* Enable or disable */
	__be32                  checksum;  /* Checksum          */
	unsigned char           pad[14];
};

/* Enable VLAN */
struct ncsi_cmd_ev_pkt {
	struct ncsi_cmd_pkt_hdr cmd;         /* Command header   */
	unsigned char           reserved[3]; /* Reserved         */
	unsigned char           mode;        /* VLAN filter mode */
	__be32                  checksum;    /* Checksum         */
	unsigned char           pad[22];
};

/* Set MAC Address */
struct ncsi_cmd_sma_pkt {
	struct ncsi_cmd_pkt_hdr cmd;      /* Command header          */
	unsigned char           mac[6];   /* MAC address             */
	unsigned char           index;    /* MAC table index         */
	unsigned char           at_e;     /* Addr type and operation */
	__be32                  checksum; /* Checksum                */
	unsigned char           pad[18];
};

/* Enable Broadcast Filter */
struct ncsi_cmd_ebf_pkt {
	struct ncsi_cmd_pkt_hdr cmd;      /* Command header */
	__be32                  mode;     /* Filter mode    */
	__be32                  checksum; /* Checksum       */
	unsigned char           pad[22];
};

/* Enable Global Multicast Filter */
struct ncsi_cmd_egmf_pkt {
	struct ncsi_cmd_pkt_hdr cmd;      /* Command header */
	__be32                  mode;     /* Global MC mode */
	__be32                  checksum; /* Checksum       */
	unsigned char           pad[22];
};

/* Set NCSI Flow Control */
struct ncsi_cmd_snfc_pkt {
	struct ncsi_cmd_pkt_hdr cmd;         /* Command header    */
	unsigned char           reserved[3]; /* Reserved          */
	unsigned char           mode;        /* Flow control mode */
	__be32                  checksum;    /* Checksum          */
	unsigned char           pad[22];
};

/* NCSI packet revision */
#define NCSI_PKT_REVISION	0x01

/* NCSI packet commands */
#define NCSI_PKT_CMD_CIS	0x00 /* Clear Initial State              */
#define NCSI_PKT_CMD_SP		0x01 /* Select Package                   */
#define NCSI_PKT_CMD_DP		0x02 /* Deselect Package                 */
#define NCSI_PKT_CMD_EC		0x03 /* Enable Channel                   */
#define NCSI_PKT_CMD_DC		0x04 /* Disable Channel                  */
#define NCSI_PKT_CMD_RC		0x05 /* Reset Channel                    */
#define NCSI_PKT_CMD_ECNT	0x06 /* Enable Channel Network Tx        */
#define NCSI_PKT_CMD_DCNT	0x07 /* Disable Channel Network Tx       */
#define NCSI_PKT_CMD_AE		0x08 /* AEN Enable                       */
#define NCSI_PKT_CMD_SL		0x09 /* Set Link                         */
#define NCSI_PKT_CMD_GLS	0x0a /* Get Link                         */
#define NCSI_PKT_CMD_SVF	0x0b /* Set VLAN Filter                  */
#define NCSI_PKT_CMD_EV		0x0c /* Enable VLAN                      */
#define NCSI_PKT_CMD_DV		0x0d /* Disable VLAN                     */
#define NCSI_PKT_CMD_SMA	0x0e /* Set MAC address                  */
#define NCSI_PKT_CMD_EBF	0x10 /* Enable Broadcast Filter          */
#define NCSI_PKT_CMD_DBF	0x11 /* Disable Broadcast Filter         */
#define NCSI_PKT_CMD_EGMF	0x12 /* Enable Global Multicast Filter   */
#define NCSI_PKT_CMD_DGMF	0x13 /* Disable Global Multicast Filter  */
#define NCSI_PKT_CMD_SNFC	0x14 /* Set NCSI Flow Control            */
#define NCSI_PKT_CMD_GVI	0x15 /* Get Version ID                   */
#define NCSI_PKT_CMD_GC		0x16 /* Get Capabilities                 */
#define NCSI_PKT_CMD_GP		0x17 /* Get Parameters                   */
#define NCSI_PKT_CMD_GCPS	0x18 /* Get Controller Packet Statistics */
#define NCSI_PKT_CMD_GNS	0x19 /* Get NCSI Statistics              */
#define NCSI_PKT_CMD_GNPTS	0x1a /* Get NCSI Pass-throu Statistics   */
#define NCSI_PKT_CMD_GPS	0x1b /* Get package status               */
#define NCSI_PKT_CMD_OEM	0x50 /* OEM                              */
#define NCSI_PKT_CMD_PLDM	0x51 /* PLDM request over NCSI over RBT  */
#define NCSI_PKT_CMD_GPUUID	0x52 /* Get package UUID                 */

#endif /* __NCSI_PKT_H__ */
