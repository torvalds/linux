/*
 * TCP Vegas congestion control interface
 */
#ifndef __TCP_VEGAS_H
#define __TCP_VEGAS_H 1

/* Vegas variables */
struct vegas {
	u32	beg_snd_nxt;	/* right edge during last RTT */
	u32	beg_snd_una;	/* left edge  during last RTT */
	u32	beg_snd_cwnd;	/* saves the size of the cwnd */
	u8	doing_vegas_now;/* if true, do vegas for this RTT */
	u16	cntRTT;		/* # of RTTs measured within last RTT */
	u32	minRTT;		/* min of RTTs measured within last RTT (in usec) */
	u32	baseRTT;	/* the min of all Vegas RTT measurements seen (in usec) */
};

void tcp_vegas_init(struct sock *sk);
void tcp_vegas_state(struct sock *sk, u8 ca_state);
void tcp_vegas_pkts_acked(struct sock *sk, u32 cnt, s32 rtt_us);
void tcp_vegas_cwnd_event(struct sock *sk, enum tcp_ca_event event);
size_t tcp_vegas_get_info(struct sock *sk, u32 ext, int *attr,
			  union tcp_cc_info *info);

#endif	/* __TCP_VEGAS_H */
