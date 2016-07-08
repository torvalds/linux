/* ila.h - ILA Interface */

#ifndef _UAPI_LINUX_ILA_H
#define _UAPI_LINUX_ILA_H

/* NETLINK_GENERIC related info */
#define ILA_GENL_NAME		"ila"
#define ILA_GENL_VERSION	0x1

enum {
	ILA_ATTR_UNSPEC,
	ILA_ATTR_LOCATOR,			/* u64 */
	ILA_ATTR_IDENTIFIER,			/* u64 */
	ILA_ATTR_LOCATOR_MATCH,			/* u64 */
	ILA_ATTR_IFINDEX,			/* s32 */
	ILA_ATTR_DIR,				/* u32 */
	ILA_ATTR_PAD,
	ILA_ATTR_CSUM_MODE,			/* u8 */

	__ILA_ATTR_MAX,
};

#define ILA_ATTR_MAX		(__ILA_ATTR_MAX - 1)

enum {
	ILA_CMD_UNSPEC,
	ILA_CMD_ADD,
	ILA_CMD_DEL,
	ILA_CMD_GET,

	__ILA_CMD_MAX,
};

#define ILA_CMD_MAX	(__ILA_CMD_MAX - 1)

#define ILA_DIR_IN	(1 << 0)
#define ILA_DIR_OUT	(1 << 1)

enum {
	ILA_CSUM_ADJUST_TRANSPORT,
	ILA_CSUM_NEUTRAL_MAP,
	ILA_CSUM_NO_ACTION,
};

#endif /* _UAPI_LINUX_ILA_H */
