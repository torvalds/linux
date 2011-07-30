/*
 * Netlink message type permission tables, for user generated messages.
 *
 * Author: James Morris <jmorris@redhat.com>
 *
 * Copyright (C) 2004 Red Hat, Inc., James Morris <jmorris@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2,
 * as published by the Free Software Foundation.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if.h>
#include <linux/netfilter_ipv4/ip_queue.h>
#include <linux/inet_diag.h>
#include <linux/xfrm.h>
#include <linux/audit.h>

#include "flask.h"
#include "av_permissions.h"

struct nlmsg_perm {
	u16	nlmsg_type;
	u32	perm;
};

static struct nlmsg_perm nlmsg_route_perms[] =
{
	{ RTM_NEWLINK,		NETLINK_ROUTE_SOCKET__NLMSG_WRITE },
	{ RTM_DELLINK,		NETLINK_ROUTE_SOCKET__NLMSG_WRITE },
	{ RTM_GETLINK,		NETLINK_ROUTE_SOCKET__NLMSG_READ  },
	{ RTM_SETLINK,		NETLINK_ROUTE_SOCKET__NLMSG_WRITE },
	{ RTM_NEWADDR,		NETLINK_ROUTE_SOCKET__NLMSG_WRITE },
	{ RTM_DELADDR,		NETLINK_ROUTE_SOCKET__NLMSG_WRITE },
	{ RTM_GETADDR,		NETLINK_ROUTE_SOCKET__NLMSG_READ  },
	{ RTM_NEWROUTE,		NETLINK_ROUTE_SOCKET__NLMSG_WRITE },
	{ RTM_DELROUTE,		NETLINK_ROUTE_SOCKET__NLMSG_WRITE },
	{ RTM_GETROUTE,		NETLINK_ROUTE_SOCKET__NLMSG_READ  },
	{ RTM_NEWNEIGH,		NETLINK_ROUTE_SOCKET__NLMSG_WRITE },
	{ RTM_DELNEIGH,		NETLINK_ROUTE_SOCKET__NLMSG_WRITE },
	{ RTM_GETNEIGH,		NETLINK_ROUTE_SOCKET__NLMSG_READ  },
	{ RTM_NEWRULE,		NETLINK_ROUTE_SOCKET__NLMSG_WRITE },
	{ RTM_DELRULE,		NETLINK_ROUTE_SOCKET__NLMSG_WRITE },
	{ RTM_GETRULE,		NETLINK_ROUTE_SOCKET__NLMSG_READ  },
	{ RTM_NEWQDISC,		NETLINK_ROUTE_SOCKET__NLMSG_WRITE },
	{ RTM_DELQDISC,		NETLINK_ROUTE_SOCKET__NLMSG_WRITE },
	{ RTM_GETQDISC,		NETLINK_ROUTE_SOCKET__NLMSG_READ  },
	{ RTM_NEWTCLASS,	NETLINK_ROUTE_SOCKET__NLMSG_WRITE },
	{ RTM_DELTCLASS,	NETLINK_ROUTE_SOCKET__NLMSG_WRITE },
	{ RTM_GETTCLASS,	NETLINK_ROUTE_SOCKET__NLMSG_READ  },
	{ RTM_NEWTFILTER,	NETLINK_ROUTE_SOCKET__NLMSG_WRITE },
	{ RTM_DELTFILTER,	NETLINK_ROUTE_SOCKET__NLMSG_WRITE },
	{ RTM_GETTFILTER,	NETLINK_ROUTE_SOCKET__NLMSG_READ  },
	{ RTM_NEWACTION,	NETLINK_ROUTE_SOCKET__NLMSG_WRITE },
	{ RTM_DELACTION,	NETLINK_ROUTE_SOCKET__NLMSG_WRITE },
	{ RTM_GETACTION,	NETLINK_ROUTE_SOCKET__NLMSG_READ  },
	{ RTM_NEWPREFIX,	NETLINK_ROUTE_SOCKET__NLMSG_WRITE },
	{ RTM_GETMULTICAST,	NETLINK_ROUTE_SOCKET__NLMSG_READ  },
	{ RTM_GETANYCAST,	NETLINK_ROUTE_SOCKET__NLMSG_READ  },
	{ RTM_GETNEIGHTBL,	NETLINK_ROUTE_SOCKET__NLMSG_READ  },
	{ RTM_SETNEIGHTBL,	NETLINK_ROUTE_SOCKET__NLMSG_WRITE },
	{ RTM_NEWADDRLABEL,	NETLINK_ROUTE_SOCKET__NLMSG_WRITE },
	{ RTM_DELADDRLABEL,	NETLINK_ROUTE_SOCKET__NLMSG_WRITE },
	{ RTM_GETADDRLABEL,	NETLINK_ROUTE_SOCKET__NLMSG_READ  },
};

static struct nlmsg_perm nlmsg_firewall_perms[] =
{
	{ IPQM_MODE,		NETLINK_FIREWALL_SOCKET__NLMSG_WRITE },
	{ IPQM_VERDICT,		NETLINK_FIREWALL_SOCKET__NLMSG_WRITE },
};

static struct nlmsg_perm nlmsg_tcpdiag_perms[] =
{
	{ TCPDIAG_GETSOCK,	NETLINK_TCPDIAG_SOCKET__NLMSG_READ },
	{ DCCPDIAG_GETSOCK,	NETLINK_TCPDIAG_SOCKET__NLMSG_READ },
};

static struct nlmsg_perm nlmsg_xfrm_perms[] =
{
	{ XFRM_MSG_NEWSA,	NETLINK_XFRM_SOCKET__NLMSG_WRITE },
	{ XFRM_MSG_DELSA,	NETLINK_XFRM_SOCKET__NLMSG_WRITE },
	{ XFRM_MSG_GETSA,	NETLINK_XFRM_SOCKET__NLMSG_READ  },
	{ XFRM_MSG_NEWPOLICY,	NETLINK_XFRM_SOCKET__NLMSG_WRITE },
	{ XFRM_MSG_DELPOLICY,	NETLINK_XFRM_SOCKET__NLMSG_WRITE },
	{ XFRM_MSG_GETPOLICY,	NETLINK_XFRM_SOCKET__NLMSG_READ  },
	{ XFRM_MSG_ALLOCSPI,	NETLINK_XFRM_SOCKET__NLMSG_WRITE },
	{ XFRM_MSG_ACQUIRE,	NETLINK_XFRM_SOCKET__NLMSG_WRITE },
	{ XFRM_MSG_EXPIRE,	NETLINK_XFRM_SOCKET__NLMSG_WRITE },
	{ XFRM_MSG_UPDPOLICY,	NETLINK_XFRM_SOCKET__NLMSG_WRITE },
	{ XFRM_MSG_UPDSA,	NETLINK_XFRM_SOCKET__NLMSG_WRITE },
	{ XFRM_MSG_POLEXPIRE,	NETLINK_XFRM_SOCKET__NLMSG_WRITE },
	{ XFRM_MSG_FLUSHSA,	NETLINK_XFRM_SOCKET__NLMSG_WRITE },
	{ XFRM_MSG_FLUSHPOLICY,	NETLINK_XFRM_SOCKET__NLMSG_WRITE },
	{ XFRM_MSG_NEWAE,	NETLINK_XFRM_SOCKET__NLMSG_WRITE },
	{ XFRM_MSG_GETAE,	NETLINK_XFRM_SOCKET__NLMSG_READ  },
};

static struct nlmsg_perm nlmsg_audit_perms[] =
{
	{ AUDIT_GET,		NETLINK_AUDIT_SOCKET__NLMSG_READ     },
	{ AUDIT_SET,		NETLINK_AUDIT_SOCKET__NLMSG_WRITE    },
	{ AUDIT_LIST,		NETLINK_AUDIT_SOCKET__NLMSG_READPRIV },
	{ AUDIT_ADD,		NETLINK_AUDIT_SOCKET__NLMSG_WRITE    },
	{ AUDIT_DEL,		NETLINK_AUDIT_SOCKET__NLMSG_WRITE    },
	{ AUDIT_LIST_RULES,	NETLINK_AUDIT_SOCKET__NLMSG_READPRIV },
	{ AUDIT_ADD_RULE,	NETLINK_AUDIT_SOCKET__NLMSG_WRITE    },
	{ AUDIT_DEL_RULE,	NETLINK_AUDIT_SOCKET__NLMSG_WRITE    },
	{ AUDIT_USER,		NETLINK_AUDIT_SOCKET__NLMSG_RELAY    },
	{ AUDIT_SIGNAL_INFO,	NETLINK_AUDIT_SOCKET__NLMSG_READ     },
	{ AUDIT_TRIM,		NETLINK_AUDIT_SOCKET__NLMSG_WRITE    },
	{ AUDIT_MAKE_EQUIV,	NETLINK_AUDIT_SOCKET__NLMSG_WRITE    },
	{ AUDIT_TTY_GET,	NETLINK_AUDIT_SOCKET__NLMSG_READ     },
	{ AUDIT_TTY_SET,	NETLINK_AUDIT_SOCKET__NLMSG_TTY_AUDIT	},
};


static int nlmsg_perm(u16 nlmsg_type, u32 *perm, struct nlmsg_perm *tab, size_t tabsize)
{
	int i, err = -EINVAL;

	for (i = 0; i < tabsize/sizeof(struct nlmsg_perm); i++)
		if (nlmsg_type == tab[i].nlmsg_type) {
			*perm = tab[i].perm;
			err = 0;
			break;
		}

	return err;
}

int selinux_nlmsg_lookup(u16 sclass, u16 nlmsg_type, u32 *perm)
{
	int err = 0;

	switch (sclass) {
	case SECCLASS_NETLINK_ROUTE_SOCKET:
		err = nlmsg_perm(nlmsg_type, perm, nlmsg_route_perms,
				 sizeof(nlmsg_route_perms));
		break;

	case SECCLASS_NETLINK_FIREWALL_SOCKET:
	case SECCLASS_NETLINK_IP6FW_SOCKET:
		err = nlmsg_perm(nlmsg_type, perm, nlmsg_firewall_perms,
				 sizeof(nlmsg_firewall_perms));
		break;

	case SECCLASS_NETLINK_TCPDIAG_SOCKET:
		err = nlmsg_perm(nlmsg_type, perm, nlmsg_tcpdiag_perms,
				 sizeof(nlmsg_tcpdiag_perms));
		break;

	case SECCLASS_NETLINK_XFRM_SOCKET:
		err = nlmsg_perm(nlmsg_type, perm, nlmsg_xfrm_perms,
				 sizeof(nlmsg_xfrm_perms));
		break;

	case SECCLASS_NETLINK_AUDIT_SOCKET:
		if ((nlmsg_type >= AUDIT_FIRST_USER_MSG &&
		     nlmsg_type <= AUDIT_LAST_USER_MSG) ||
		    (nlmsg_type >= AUDIT_FIRST_USER_MSG2 &&
		     nlmsg_type <= AUDIT_LAST_USER_MSG2)) {
			*perm = NETLINK_AUDIT_SOCKET__NLMSG_RELAY;
		} else {
			err = nlmsg_perm(nlmsg_type, perm, nlmsg_audit_perms,
					 sizeof(nlmsg_audit_perms));
		}
		break;

	/* No messaging from userspace, or class unknown/unhandled */
	default:
		err = -ENOENT;
		break;
	}

	return err;
}
