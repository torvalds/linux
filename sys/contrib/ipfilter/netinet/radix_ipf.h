/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */
#ifndef __RADIX_IPF_H__
#define	__RADIX_IPF_H__

#ifndef U_32_T
typedef unsigned int u_32_t;
# define	U_32_T	1
#endif

typedef struct ipf_rdx_mask {
	struct ipf_rdx_mask	*next;
	struct ipf_rdx_node	*node;
	u_32_t			*mask;
	int			maskbitcount;
} ipf_rdx_mask_t;

typedef struct ipf_rdx_node {
	struct ipf_rdx_node	*left;
	struct ipf_rdx_node	*right;
	struct ipf_rdx_node	*parent;
	struct ipf_rdx_node	*dupkey;
	struct ipf_rdx_mask	*masks;
	struct ipf_rdx_mask	*mymask;
	u_32_t			*addrkey;
	u_32_t			*maskkey;
	u_32_t			*addroff;
	u_32_t			*maskoff;
	u_32_t			lastmask;
	u_32_t			bitmask;
	int			offset;
	int			index;
	int			maskbitcount;
	int			root;
#ifdef RDX_DEBUG
	char			name[40];
#endif
} ipf_rdx_node_t;

struct ipf_rdx_head;

typedef	void		(* radix_walk_func_t)(ipf_rdx_node_t *, void *);
typedef	ipf_rdx_node_t	*(* idx_hamn_func_t)(struct ipf_rdx_head *,
					     addrfamily_t *, addrfamily_t *,
					     ipf_rdx_node_t *);
typedef	ipf_rdx_node_t	*(* idx_ham_func_t)(struct ipf_rdx_head *,
					    addrfamily_t *, addrfamily_t *);
typedef	ipf_rdx_node_t	*(* idx_ha_func_t)(struct ipf_rdx_head *,
					   addrfamily_t *);
typedef	void		(* idx_walk_func_t)(struct ipf_rdx_head *,
					    radix_walk_func_t, void *);

typedef struct ipf_rdx_head {
	ipf_rdx_node_t	*root;
	ipf_rdx_node_t	nodes[3];
	ipfmutex_t	lock;
	idx_hamn_func_t	addaddr;	/* add addr/mask to tree */
	idx_ham_func_t	deladdr;	/* delete addr/mask from tree */
	idx_ham_func_t	lookup;		/* look for specific addr/mask */
	idx_ha_func_t	matchaddr;	/* search tree for address match */
	idx_walk_func_t	walktree;	/* walk entire tree */
} ipf_rdx_head_t;

typedef struct radix_softc {
	u_char			*zeros;
	u_char			*ones;
} radix_softc_t;

#undef	RADIX_NODE_HEAD_LOCK
#undef	RADIX_NODE_HEAD_UNLOCK
#ifdef	_KERNEL
# define	RADIX_NODE_HEAD_LOCK(x)		MUTEX_ENTER(&(x)->lock)
# define	RADIX_NODE_HEAD_UNLOCK(x)	MUTEX_UNLOCK(&(x)->lock)
#else
# define	RADIX_NODE_HEAD_LOCK(x)
# define	RADIX_NODE_HEAD_UNLOCK(x)
#endif

extern	void	*ipf_rx_create __P((void));
extern	int	ipf_rx_init __P((void *));
extern	void	ipf_rx_destroy __P((void *));   
extern	int	ipf_rx_inithead __P((radix_softc_t *, ipf_rdx_head_t **));
extern	void	ipf_rx_freehead __P((ipf_rdx_head_t *));
extern	ipf_rdx_node_t *ipf_rx_addroute __P((ipf_rdx_head_t *,
					     addrfamily_t *, addrfamily_t *,
					     ipf_rdx_node_t *));
extern	ipf_rdx_node_t *ipf_rx_delete __P((ipf_rdx_head_t *, addrfamily_t *,
					   addrfamily_t *));
extern	void	ipf_rx_walktree __P((ipf_rdx_head_t *, radix_walk_func_t,
				     void *));

#endif /* __RADIX_IPF_H__ */
