#include <uapi/linux/netfilter/nf_osf.h>

/* Initial window size option state machine: multiple of mss, mtu or
 * plain numeric value. Can also be made as plain numeric value which
 * is not a multiple of specified value.
 */
enum nf_osf_window_size_options {
	OSF_WSS_PLAIN   = 0,
	OSF_WSS_MSS,
	OSF_WSS_MTU,
	OSF_WSS_MODULO,
	OSF_WSS_MAX,
};

enum osf_fmatch_states {
	/* Packet does not match the fingerprint */
	FMATCH_WRONG = 0,
	/* Packet matches the fingerprint */
	FMATCH_OK,
	/* Options do not match the fingerprint, but header does */
	FMATCH_OPT_WRONG,
};

bool nf_osf_match(const struct sk_buff *skb, u_int8_t family,
		  int hooknum, struct net_device *in, struct net_device *out,
		  const struct nf_osf_info *info, struct net *net,
		  const struct list_head *nf_osf_fingers);
