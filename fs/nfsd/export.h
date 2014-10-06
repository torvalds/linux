/*
 * Copyright (C) 1995-1997 Olaf Kirch <okir@monad.swb.de>
 */
#ifndef NFSD_EXPORT_H
#define NFSD_EXPORT_H

#include <linux/sunrpc/cache.h>
#include <uapi/linux/nfsd/export.h>

struct knfsd_fh;
struct svc_fh;
struct svc_rqst;

/*
 * FS Locations
 */

#define MAX_FS_LOCATIONS	128

struct nfsd4_fs_location {
	char *hosts; /* colon separated list of hosts */
	char *path;  /* slash separated list of path components */
};

struct nfsd4_fs_locations {
	uint32_t locations_count;
	struct nfsd4_fs_location *locations;
/* If we're not actually serving this data ourselves (only providing a
 * list of replicas that do serve it) then we set "migrated": */
	int migrated;
};

/*
 * We keep an array of pseudoflavors with the export, in order from most
 * to least preferred.  For the foreseeable future, we don't expect more
 * than the eight pseudoflavors null, unix, krb5, krb5i, krb5p, skpm3,
 * spkm3i, and spkm3p (and using all 8 at once should be rare).
 */
#define MAX_SECINFO_LIST	8
#define EX_UUID_LEN		16

struct exp_flavor_info {
	u32	pseudoflavor;
	u32	flags;
};

struct svc_export {
	struct cache_head	h;
	struct auth_domain *	ex_client;
	int			ex_flags;
	struct path		ex_path;
	kuid_t			ex_anon_uid;
	kgid_t			ex_anon_gid;
	int			ex_fsid;
	unsigned char *		ex_uuid; /* 16 byte fsid */
	struct nfsd4_fs_locations ex_fslocs;
	uint32_t		ex_nflavors;
	struct exp_flavor_info	ex_flavors[MAX_SECINFO_LIST];
	struct cache_detail	*cd;
};

/* an "export key" (expkey) maps a filehandlefragement to an
 * svc_export for a given client.  There can be several per export,
 * for the different fsid types.
 */
struct svc_expkey {
	struct cache_head	h;

	struct auth_domain *	ek_client;
	int			ek_fsidtype;
	u32			ek_fsid[6];

	struct path		ek_path;
};

#define EX_ISSYNC(exp)		(!((exp)->ex_flags & NFSEXP_ASYNC))
#define EX_NOHIDE(exp)		((exp)->ex_flags & NFSEXP_NOHIDE)
#define EX_WGATHER(exp)		((exp)->ex_flags & NFSEXP_GATHERED_WRITES)

int nfsexp_flags(struct svc_rqst *rqstp, struct svc_export *exp);
__be32 check_nfsd_access(struct svc_export *exp, struct svc_rqst *rqstp);

/*
 * Function declarations
 */
int			nfsd_export_init(struct net *);
void			nfsd_export_shutdown(struct net *);
void			nfsd_export_flush(struct net *);
struct svc_export *	rqst_exp_get_by_name(struct svc_rqst *,
					     struct path *);
struct svc_export *	rqst_exp_parent(struct svc_rqst *,
					struct path *);
struct svc_export *	rqst_find_fsidzero_export(struct svc_rqst *);
int			exp_rootfh(struct net *, struct auth_domain *,
					char *path, struct knfsd_fh *, int maxsize);
__be32			exp_pseudoroot(struct svc_rqst *, struct svc_fh *);
__be32			nfserrno(int errno);

static inline void exp_put(struct svc_export *exp)
{
	cache_put(&exp->h, exp->cd);
}

static inline struct svc_export *exp_get(struct svc_export *exp)
{
	cache_get(&exp->h);
	return exp;
}
struct svc_export * rqst_exp_find(struct svc_rqst *, int, u32 *);

#endif /* NFSD_EXPORT_H */
