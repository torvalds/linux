/* atmsap.h - ATM Service Access Point addressing definitions */

/* Written 1995-1999 by Werner Almesberger, EPFL LRC/ICA */


#ifndef _LINUX_ATMSAP_H
#define _LINUX_ATMSAP_H

#include <linux/atmapi.h>

/*
 * BEGIN_xx and END_xx markers are used for automatic generation of
 * documentation. Do not change them.
 */


/*
 * Layer 2 protocol identifiers
 */

/* BEGIN_L2 */
#define ATM_L2_NONE	0	/* L2 not specified */
#define ATM_L2_ISO1745  0x01	/* Basic mode ISO 1745 */
#define ATM_L2_Q291	0x02	/* ITU-T Q.291 (Rec. I.441) */
#define ATM_L2_X25_LL	0x06	/* ITU-T X.25, link layer */
#define ATM_L2_X25_ML	0x07	/* ITU-T X.25, multilink */
#define ATM_L2_LAPB	0x08	/* Extended LAPB, half-duplex (Rec. T.71) */
#define ATM_L2_HDLC_ARM	0x09	/* HDLC ARM (ISO/IEC 4335) */
#define ATM_L2_HDLC_NRM	0x0a	/* HDLC NRM (ISO/IEC 4335) */
#define ATM_L2_HDLC_ABM	0x0b	/* HDLC ABM (ISO/IEC 4335) */
#define ATM_L2_ISO8802	0x0c	/* LAN LLC (ISO/IEC 8802/2) */
#define ATM_L2_X75	0x0d	/* ITU-T X.75, SLP */
#define ATM_L2_Q922	0x0e	/* ITU-T Q.922 */
#define ATM_L2_USER	0x10	/* user-specified */
#define ATM_L2_ISO7776	0x11	/* ISO 7776 DTE-DTE */
/* END_L2 */


/*
 * Layer 3 protocol identifiers
 */

/* BEGIN_L3 */
#define ATM_L3_NONE	0	/* L3 not specified */
#define ATM_L3_X25	0x06	/* ITU-T X.25, packet layer */
#define ATM_L3_ISO8208	0x07	/* ISO/IEC 8208 */
#define ATM_L3_X223	0x08	/* ITU-T X.223 | ISO/IEC 8878 */
#define ATM_L3_ISO8473	0x09	/* ITU-T X.233 | ISO/IEC 8473 */
#define ATM_L3_T70	0x0a	/* ITU-T T.70 minimum network layer */
#define ATM_L3_TR9577	0x0b	/* ISO/IEC TR 9577 */
#define ATM_L3_H310	0x0c	/* ITU-T Recommendation H.310 */
#define ATM_L3_H321	0x0d	/* ITU-T Recommendation H.321 */
#define ATM_L3_USER	0x10	/* user-specified */
/* END_L3 */


/*
 * High layer identifiers
 */

/* BEGIN_HL */
#define ATM_HL_NONE	0	/* HL not specified */
#define ATM_HL_ISO	0x01	/* ISO */
#define ATM_HL_USER	0x02	/* user-specific */
#define ATM_HL_HLP	0x03	/* high layer profile - UNI 3.0 only */
#define ATM_HL_VENDOR	0x04	/* vendor-specific application identifier */
/* END_HL */


/*
 * ITU-T coded mode of operation
 */

/* BEGIN_IMD */
#define ATM_IMD_NONE	 0	/* mode not specified */
#define ATM_IMD_NORMAL	 1	/* normal mode of operation */
#define ATM_IMD_EXTENDED 2	/* extended mode of operation */
/* END_IMD */

/*
 * H.310 code points
 */

#define ATM_TT_NONE	0	/* terminal type not specified */
#define ATM_TT_RX	1	/* receive only */
#define ATM_TT_TX	2	/* send only */
#define ATM_TT_RXTX	3	/* receive and send */

#define ATM_MC_NONE	0	/* no multiplexing */
#define ATM_MC_TS	1	/* transport stream (TS) */
#define ATM_MC_TS_FEC	2	/* transport stream with forward error corr. */
#define ATM_MC_PS	3	/* program stream (PS) */
#define ATM_MC_PS_FEC	4	/* program stream with forward error corr. */
#define ATM_MC_H221	5	/* ITU-T Rec. H.221 */

/*
 * SAP structures
 */

#define ATM_MAX_HLI	8	/* maximum high-layer information length */


struct atm_blli {
    unsigned char l2_proto;	/* layer 2 protocol */
    union {
	struct {
	    unsigned char mode;	/* mode of operation (ATM_IMD_xxx), 0 if */
				/* absent */
	    unsigned char window; /* window size (k), 1-127 (0 to omit) */
	} itu;			/* ITU-T encoding */
	unsigned char user;	/* user-specified l2 information */
    } l2;
    unsigned char l3_proto;	/* layer 3 protocol */
    union {
	struct {
	    unsigned char mode;	/* mode of operation (ATM_IMD_xxx), 0 if */
				/* absent */
	    unsigned char def_size; /* default packet size (log2), 4-12 (0 to */
				    /* omit) */
	    unsigned char window;/* packet window size, 1-127 (0 to omit) */
	} itu;			/* ITU-T encoding */
	unsigned char user;	/* user specified l3 information */
	struct {		      /* if l3_proto = ATM_L3_H310 */
	    unsigned char term_type;  /* terminal type */
	    unsigned char fw_mpx_cap; /* forward multiplexing capability */
				      /* only if term_type != ATM_TT_NONE */
	    unsigned char bw_mpx_cap; /* backward multiplexing capability */
				      /* only if term_type != ATM_TT_NONE */
	} h310;
	struct {		  /* if l3_proto = ATM_L3_TR9577 */
	    unsigned char ipi;	  /* initial protocol id */
	    unsigned char snap[5];/* IEEE 802.1 SNAP identifier */
				  /* (only if ipi == NLPID_IEEE802_1_SNAP) */
	} tr9577;
    } l3;
} __ATM_API_ALIGN;


struct atm_bhli {
    unsigned char hl_type;	/* high layer information type */
    unsigned char hl_length;	/* length (only if hl_type == ATM_HL_USER || */
				/* hl_type == ATM_HL_ISO) */
    unsigned char hl_info[ATM_MAX_HLI];/* high layer information */
};


#define ATM_MAX_BLLI	3		/* maximum number of BLLI elements */


struct atm_sap {
	struct atm_bhli bhli;		/* local SAP, high-layer information */
	struct atm_blli blli[ATM_MAX_BLLI] __ATM_API_ALIGN;
					/* local SAP, low-layer info */
};


static __inline__ int blli_in_use(struct atm_blli blli)
{
	return blli.l2_proto || blli.l3_proto;
}

#endif
