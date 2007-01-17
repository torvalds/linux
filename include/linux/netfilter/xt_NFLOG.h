#ifndef _XT_NFLOG_TARGET
#define _XT_NFLOG_TARGET

#define XT_NFLOG_DEFAULT_GROUP		0x1
#define XT_NFLOG_DEFAULT_THRESHOLD	1

#define XT_NFLOG_MASK			0x0

struct xt_nflog_info {
	u_int32_t	len;
	u_int16_t	group;
	u_int16_t	threshold;
	u_int16_t	flags;
	u_int16_t	pad;
	char		prefix[64];
};

#endif /* _XT_NFLOG_TARGET */
