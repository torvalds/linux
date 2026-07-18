/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DOS share management for lockd.
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LOCKD_SHARE_H
#define _LOCKD_SHARE_H

/* Synthetic svid for lockowner lookup during share operations */
#define LOCKD_SHARE_SVID	(~(u32)0)

/*
 * DOS share for a specific file
 */
struct lockd_share {
	struct lockd_share *	s_next;		/* linked list */
	struct nlm_host *	s_host;		/* client host */
	struct nlm_file *	s_file;		/* shared file */
	struct xdr_netobj	s_owner;	/* owner handle */
	u32			s_access;	/* access mode */
	u32			s_mode;		/* deny mode */
};

__be32	nlmsvc_share_file(struct nlm_host *host, struct nlm_file *file,
			  struct xdr_netobj *oh, u32 access, u32 mode);
__be32	nlmsvc_unshare_file(struct nlm_host *host, struct nlm_file *file,
			    struct xdr_netobj *oh);
void	nlmsvc_traverse_shares(struct nlm_host *, struct nlm_file *,
					       nlm_host_match_fn_t);

#endif /* _LOCKD_SHARE_H */
