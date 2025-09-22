/*	$OpenBSD: if_etherbridge.h,v 1.5 2024/11/04 00:13:15 jsg Exp $ */

/*
 * Copyright (c) 2018, 2021 David Gwynne <dlg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _NET_ETHERBRIDGE_H_
#define _NET_ETHERBRIDGE_H_

#define ETHERBRIDGE_TABLE_BITS		8
#define ETHERBRIDGE_TABLE_SIZE		(1U << ETHERBRIDGE_TABLE_BITS)
#define ETHERBRIDGE_TABLE_MASK		(ETHERBRIDGE_TABLE_SIZE - 1)

struct etherbridge_ops {
	int	 (*eb_op_port_eq)(void *, void *, void *);
	void	*(*eb_op_port_take)(void *, void *);
	void	 (*eb_op_port_rele)(void *, void *);
	size_t	 (*eb_op_port_ifname)(void *, char *, size_t, void *);
	void	 (*eb_op_port_sa)(void *, struct sockaddr_storage *, void *);
};

struct etherbridge;

struct eb_entry {
	SMR_TAILQ_ENTRY(eb_entry)	 ebe_lentry;
	union {
		RBT_ENTRY(eb_entry)	 _ebe_tentry;
		TAILQ_ENTRY(eb_entry)	 _ebe_qentry;
	}				 _ebe_entries;
#define ebe_tentry	_ebe_entries._ebe_tentry
#define ebe_qentry	_ebe_entries._ebe_qentry

	uint64_t			 ebe_addr;
	void				*ebe_port;
	unsigned int			 ebe_type;
#define EBE_DYNAMIC				0x0
#define EBE_STATIC				0x1
#define EBE_DEAD				0xdead
	time_t				 ebe_age;

	struct etherbridge		*ebe_etherbridge;
	struct smr_entry		 ebe_smr_entry;
};

SMR_TAILQ_HEAD(eb_list, eb_entry);
RBT_HEAD(eb_tree, eb_entry);
TAILQ_HEAD(eb_queue, eb_entry);

struct etherbridge {
	const char			*eb_name;
	const struct etherbridge_ops	*eb_ops;
	void				*eb_cookie;

	struct mutex			 eb_lock;
	unsigned int			 eb_num;
	unsigned int			 eb_max;
	int				 eb_max_age; /* seconds */
	struct timeout			 eb_tmo_age;

	struct eb_list			*eb_table;
	struct eb_tree			 eb_tree;

};

int	 etherbridge_init(struct etherbridge *, const char *,
	     const struct etherbridge_ops *, void *);
int	 etherbridge_up(struct etherbridge *);
int	 etherbridge_down(struct etherbridge *);
void	 etherbridge_destroy(struct etherbridge *);

void	 etherbridge_map(struct etherbridge *, void *, uint64_t);
void	 etherbridge_map_ea(struct etherbridge *, void *,
	     const struct ether_addr *);
void	*etherbridge_resolve(struct etherbridge *, uint64_t);
void	*etherbridge_resolve_ea(struct etherbridge *,
	     const struct ether_addr *);
void	 etherbridge_detach_port(struct etherbridge *, void *);

/* ioctl support */
int	 etherbridge_set_max(struct etherbridge *, struct ifbrparam *);
int	 etherbridge_get_max(struct etherbridge *, struct ifbrparam *);
int	 etherbridge_set_tmo(struct etherbridge *, struct ifbrparam *);
int	 etherbridge_get_tmo(struct etherbridge *, struct ifbrparam *);
int	 etherbridge_rtfind(struct etherbridge *, struct ifbaconf *);
int	 etherbridge_add_addr(struct etherbridge *, void *,
	     const struct ether_addr *, unsigned int);
int	 etherbridge_del_addr(struct etherbridge *, const struct ether_addr *);
void	 etherbridge_flush(struct etherbridge *, uint32_t);

#endif /* _NET_ETHERBRIDGE_H_ */
