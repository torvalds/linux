#ifndef __LINUX_GENERIC_NETLINK_H
#define __LINUX_GENERIC_NETLINK_H

#include <linux/types.h>
#include <linux/netlink.h>

#define GENL_NAMSIZ	16	/* length of family name */

#define GENL_MIN_ID	NLMSG_MIN_TYPE
#define GENL_MAX_ID	1023

struct genlmsghdr {
	__u8	cmd;
	__u8	version;
	__u16	reserved;
};

#define GENL_HDRLEN	NLMSG_ALIGN(sizeof(struct genlmsghdr))

#define GENL_ADMIN_PERM		0x01
#define GENL_CMD_CAP_DO		0x02
#define GENL_CMD_CAP_DUMP	0x04
#define GENL_CMD_CAP_HASPOL	0x08

/*
 * List of reserved static generic netlink identifiers:
 */
#define GENL_ID_GENERATE	0
#define GENL_ID_CTRL		NLMSG_MIN_TYPE

/**************************************************************************
 * Controller
 **************************************************************************/

enum {
	CTRL_CMD_UNSPEC,
	CTRL_CMD_NEWFAMILY,
	CTRL_CMD_DELFAMILY,
	CTRL_CMD_GETFAMILY,
	CTRL_CMD_NEWOPS,
	CTRL_CMD_DELOPS,
	CTRL_CMD_GETOPS,
	CTRL_CMD_NEWMCAST_GRP,
	CTRL_CMD_DELMCAST_GRP,
	CTRL_CMD_GETMCAST_GRP, /* unused */
	__CTRL_CMD_MAX,
};

#define CTRL_CMD_MAX (__CTRL_CMD_MAX - 1)

enum {
	CTRL_ATTR_UNSPEC,
	CTRL_ATTR_FAMILY_ID,
	CTRL_ATTR_FAMILY_NAME,
	CTRL_ATTR_VERSION,
	CTRL_ATTR_HDRSIZE,
	CTRL_ATTR_MAXATTR,
	CTRL_ATTR_OPS,
	CTRL_ATTR_MCAST_GROUPS,
	__CTRL_ATTR_MAX,
};

#define CTRL_ATTR_MAX (__CTRL_ATTR_MAX - 1)

enum {
	CTRL_ATTR_OP_UNSPEC,
	CTRL_ATTR_OP_ID,
	CTRL_ATTR_OP_FLAGS,
	__CTRL_ATTR_OP_MAX,
};

#define CTRL_ATTR_OP_MAX (__CTRL_ATTR_OP_MAX - 1)

enum {
	CTRL_ATTR_MCAST_GRP_UNSPEC,
	CTRL_ATTR_MCAST_GRP_NAME,
	CTRL_ATTR_MCAST_GRP_ID,
	__CTRL_ATTR_MCAST_GRP_MAX,
};

#define CTRL_ATTR_MCAST_GRP_MAX (__CTRL_ATTR_MCAST_GRP_MAX - 1)

#ifdef __KERNEL__

/* All generic netlink requests are serialized by a global lock.  */
extern void genl_lock(void);
extern void genl_unlock(void);
#ifdef CONFIG_PROVE_LOCKING
extern int lockdep_genl_is_held(void);
#endif

/**
 * rcu_dereference_genl - rcu_dereference with debug checking
 * @p: The pointer to read, prior to dereferencing
 *
 * Do an rcu_dereference(p), but check caller either holds rcu_read_lock()
 * or genl mutex. Note : Please prefer genl_dereference() or rcu_dereference()
 */
#define rcu_dereference_genl(p)					\
	rcu_dereference_check(p, lockdep_genl_is_held())

/**
 * genl_dereference - fetch RCU pointer when updates are prevented by genl mutex
 * @p: The pointer to read, prior to dereferencing
 *
 * Return the value of the specified RCU-protected pointer, but omit
 * both the smp_read_barrier_depends() and the ACCESS_ONCE(), because
 * caller holds genl mutex.
 */
#define genl_dereference(p)					\
	rcu_dereference_protected(p, lockdep_genl_is_held())

#endif /* __KERNEL__ */

#endif	/* __LINUX_GENERIC_NETLINK_H */
