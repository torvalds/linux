#ifndef _IP_CONNTRACK_H323_H
#define _IP_CONNTRACK_H323_H

#ifdef __KERNEL__

#define RAS_PORT 1719
#define Q931_PORT 1720
#define H323_RTP_CHANNEL_MAX 4	/* Audio, video, FAX and other */

/* This structure exists only once per master */
struct ip_ct_h323_master {

	/* Original and NATed Q.931 or H.245 signal ports */
	u_int16_t sig_port[IP_CT_DIR_MAX];

	/* Original and NATed RTP ports */
	u_int16_t rtp_port[H323_RTP_CHANNEL_MAX][IP_CT_DIR_MAX];

	union {
		/* RAS connection timeout */
		u_int32_t timeout;

		/* Next TPKT length (for separate TPKT header and data) */
		u_int16_t tpkt_len[IP_CT_DIR_MAX];
	};
};

#endif

#endif
