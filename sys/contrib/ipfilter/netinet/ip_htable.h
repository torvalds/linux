#ifndef __IP_HTABLE_H__
#define __IP_HTABLE_H__

#include "netinet/ip_lookup.h"

typedef	struct	iphtent_s	{
	struct	iphtent_s	*ipe_next, **ipe_pnext;
	struct	iphtent_s	*ipe_hnext, **ipe_phnext;
	struct	iphtent_s	*ipe_dnext, **ipe_pdnext;
	struct	iphtable_s	*ipe_owner;
	void		*ipe_ptr;
	i6addr_t	ipe_addr;
	i6addr_t	ipe_mask;
	U_QUAD_T	ipe_hits;
	U_QUAD_T	ipe_bytes;
	u_long		ipe_die;
	int		ipe_uid;
	int		ipe_ref;
	int		ipe_unit;
	char		ipe_family;
	char		ipe_xxx[3];
	union	{
		char	ipeu_char[16];
		u_long	ipeu_long;
		u_int	ipeu_int;
	} ipe_un;
} iphtent_t;

#define	ipe_value	ipe_un.ipeu_int
#define	ipe_group	ipe_un.ipeu_char

#define	IPE_V4_HASH_FN(a, m, s)	((((m) ^ (a)) - 1 - ((a) >> 8)) % (s))
#define	IPE_V6_HASH_FN(a, m, s)	(((((m)[0] ^ (a)[0]) - ((a)[0] >> 8)) + \
				  (((m)[1] & (a)[1]) - ((a)[1] >> 8)) + \
				  (((m)[2] & (a)[2]) - ((a)[2] >> 8)) + \
				  (((m)[3] & (a)[3]) - ((a)[3] >> 8))) % (s))

typedef	struct	iphtable_s	{
	ipfrwlock_t	iph_rwlock;
	struct	iphtable_s	*iph_next, **iph_pnext;
	struct	iphtent_s	**iph_table;
	struct	iphtent_s	*iph_list;
	struct	iphtent_s	**iph_tail;
#ifdef USE_INET6
	ipf_v6_masktab_t	iph_v6_masks;
#endif
	ipf_v4_masktab_t	iph_v4_masks;
	size_t	iph_size;		/* size of hash table */
	u_long	iph_seed;		/* hashing seed */
	u_32_t	iph_flags;
	u_int	iph_unit;		/* IPL_LOG* */
	u_int	iph_ref;
	u_int	iph_type;		/* lookup or group map  - IPHASH_* */
	u_int	iph_maskset[4];		/* netmasks in use */
	char	iph_name[FR_GROUPLEN];	/* hash table number */
} iphtable_t;

/* iph_type */
#define	IPHASH_LOOKUP	0
#define	IPHASH_GROUPMAP	1
#define	IPHASH_DELETE	2
#define	IPHASH_ANON	0x80000000


typedef	struct	iphtstat_s	{
	iphtable_t	*iphs_tables;
	u_long		iphs_numtables;
	u_long		iphs_numnodes;
	u_long		iphs_nomem;
	u_long		iphs_pad[16];
} iphtstat_t;


extern void *ipf_iphmfindgroup __P((ipf_main_softc_t *, void *, void *));
extern iphtable_t *ipf_htable_find __P((void *, int, char *));
extern ipf_lookup_t ipf_htable_backend;
#ifndef _KERNEL
extern	void	ipf_htable_dump __P((ipf_main_softc_t *, void *));
#endif

#endif /* __IP_HTABLE_H__ */
