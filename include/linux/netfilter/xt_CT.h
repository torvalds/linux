#ifndef _XT_CT_H
#define _XT_CT_H

#define XT_CT_NOTRACK	0x1

struct xt_ct_target_info {
	u_int16_t	flags;
	u_int16_t	zone;
	u_int32_t	ct_events;
	u_int32_t	exp_events;
	char		helper[16];

	/* Used internally by the kernel */
	struct nf_conn	*ct __attribute__((aligned(8)));
};

#endif /* _XT_CT_H */
