#ifndef _UAPI_NFNL_ACCT_H_
#define _UAPI_NFNL_ACCT_H_

#ifndef NFACCT_NAME_MAX
#define NFACCT_NAME_MAX		32
#endif

enum nfnl_acct_msg_types {
	NFNL_MSG_ACCT_NEW,
	NFNL_MSG_ACCT_GET,
	NFNL_MSG_ACCT_GET_CTRZERO,
	NFNL_MSG_ACCT_DEL,
	NFNL_MSG_ACCT_OVERQUOTA,
	NFNL_MSG_ACCT_MAX
};

enum nfnl_acct_flags {
	NFACCT_F_QUOTA_PKTS	= (1 << 0),
	NFACCT_F_QUOTA_BYTES	= (1 << 1),
	NFACCT_F_OVERQUOTA	= (1 << 2), /* can't be set from userspace */
};

enum nfnl_acct_type {
	NFACCT_UNSPEC,
	NFACCT_NAME,
	NFACCT_PKTS,
	NFACCT_BYTES,
	NFACCT_USE,
	NFACCT_FLAGS,
	NFACCT_QUOTA,
	__NFACCT_MAX
};
#define NFACCT_MAX (__NFACCT_MAX - 1)


#endif /* _UAPI_NFNL_ACCT_H_ */
