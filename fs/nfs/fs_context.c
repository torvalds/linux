// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/fs/nfs/fs_context.c
 *
 * Copyright (C) 1992 Rick Sladkey
 *
 * NFS mount handling.
 *
 * Split from fs/nfs/super.c by David Howells <dhowells@redhat.com>
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/parser.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/nfs4_mount.h>
#include "nfs.h"
#include "internal.h"

#define NFSDBG_FACILITY		NFSDBG_MOUNT

#if IS_ENABLED(CONFIG_NFS_V3)
#define NFS_DEFAULT_VERSION 3
#else
#define NFS_DEFAULT_VERSION 2
#endif

#define NFS_MAX_CONNECTIONS 16

enum {
	/* Mount options that take no arguments */
	Opt_soft, Opt_softerr, Opt_hard,
	Opt_posix, Opt_noposix,
	Opt_cto, Opt_nocto,
	Opt_ac, Opt_noac,
	Opt_lock, Opt_nolock,
	Opt_udp, Opt_tcp, Opt_rdma,
	Opt_acl, Opt_noacl,
	Opt_rdirplus, Opt_nordirplus,
	Opt_sharecache, Opt_nosharecache,
	Opt_resvport, Opt_noresvport,
	Opt_fscache, Opt_nofscache,
	Opt_migration, Opt_nomigration,

	/* Mount options that take integer arguments */
	Opt_port,
	Opt_rsize, Opt_wsize, Opt_bsize,
	Opt_timeo, Opt_retrans,
	Opt_acregmin, Opt_acregmax,
	Opt_acdirmin, Opt_acdirmax,
	Opt_actimeo,
	Opt_namelen,
	Opt_mountport,
	Opt_mountvers,
	Opt_minorversion,

	/* Mount options that take string arguments */
	Opt_nfsvers,
	Opt_sec, Opt_proto, Opt_mountproto, Opt_mounthost,
	Opt_addr, Opt_mountaddr, Opt_clientaddr,
	Opt_nconnect,
	Opt_lookupcache,
	Opt_fscache_uniq,
	Opt_local_lock,

	/* Special mount options */
	Opt_userspace, Opt_deprecated, Opt_sloppy,

	Opt_err
};

static const match_table_t nfs_mount_option_tokens = {
	{ Opt_userspace, "bg" },
	{ Opt_userspace, "fg" },
	{ Opt_userspace, "retry=%s" },

	{ Opt_sloppy, "sloppy" },

	{ Opt_soft, "soft" },
	{ Opt_softerr, "softerr" },
	{ Opt_hard, "hard" },
	{ Opt_deprecated, "intr" },
	{ Opt_deprecated, "nointr" },
	{ Opt_posix, "posix" },
	{ Opt_noposix, "noposix" },
	{ Opt_cto, "cto" },
	{ Opt_nocto, "nocto" },
	{ Opt_ac, "ac" },
	{ Opt_noac, "noac" },
	{ Opt_lock, "lock" },
	{ Opt_nolock, "nolock" },
	{ Opt_udp, "udp" },
	{ Opt_tcp, "tcp" },
	{ Opt_rdma, "rdma" },
	{ Opt_acl, "acl" },
	{ Opt_noacl, "noacl" },
	{ Opt_rdirplus, "rdirplus" },
	{ Opt_nordirplus, "nordirplus" },
	{ Opt_sharecache, "sharecache" },
	{ Opt_nosharecache, "nosharecache" },
	{ Opt_resvport, "resvport" },
	{ Opt_noresvport, "noresvport" },
	{ Opt_fscache, "fsc" },
	{ Opt_nofscache, "nofsc" },
	{ Opt_migration, "migration" },
	{ Opt_nomigration, "nomigration" },

	{ Opt_port, "port=%s" },
	{ Opt_rsize, "rsize=%s" },
	{ Opt_wsize, "wsize=%s" },
	{ Opt_bsize, "bsize=%s" },
	{ Opt_timeo, "timeo=%s" },
	{ Opt_retrans, "retrans=%s" },
	{ Opt_acregmin, "acregmin=%s" },
	{ Opt_acregmax, "acregmax=%s" },
	{ Opt_acdirmin, "acdirmin=%s" },
	{ Opt_acdirmax, "acdirmax=%s" },
	{ Opt_actimeo, "actimeo=%s" },
	{ Opt_namelen, "namlen=%s" },
	{ Opt_mountport, "mountport=%s" },
	{ Opt_mountvers, "mountvers=%s" },
	{ Opt_minorversion, "minorversion=%s" },

	{ Opt_nfsvers, "nfsvers=%s" },
	{ Opt_nfsvers, "vers=%s" },

	{ Opt_sec, "sec=%s" },
	{ Opt_proto, "proto=%s" },
	{ Opt_mountproto, "mountproto=%s" },
	{ Opt_addr, "addr=%s" },
	{ Opt_clientaddr, "clientaddr=%s" },
	{ Opt_mounthost, "mounthost=%s" },
	{ Opt_mountaddr, "mountaddr=%s" },

	{ Opt_nconnect, "nconnect=%s" },

	{ Opt_lookupcache, "lookupcache=%s" },
	{ Opt_fscache_uniq, "fsc=%s" },
	{ Opt_local_lock, "local_lock=%s" },

	/* The following needs to be listed after all other options */
	{ Opt_nfsvers, "v%s" },

	{ Opt_err, NULL }
};

enum {
	Opt_xprt_udp, Opt_xprt_udp6, Opt_xprt_tcp, Opt_xprt_tcp6, Opt_xprt_rdma,
	Opt_xprt_rdma6,

	Opt_xprt_err
};

static const match_table_t nfs_xprt_protocol_tokens = {
	{ Opt_xprt_udp, "udp" },
	{ Opt_xprt_udp6, "udp6" },
	{ Opt_xprt_tcp, "tcp" },
	{ Opt_xprt_tcp6, "tcp6" },
	{ Opt_xprt_rdma, "rdma" },
	{ Opt_xprt_rdma6, "rdma6" },

	{ Opt_xprt_err, NULL }
};

enum {
	Opt_sec_none, Opt_sec_sys,
	Opt_sec_krb5, Opt_sec_krb5i, Opt_sec_krb5p,
	Opt_sec_lkey, Opt_sec_lkeyi, Opt_sec_lkeyp,
	Opt_sec_spkm, Opt_sec_spkmi, Opt_sec_spkmp,

	Opt_sec_err
};

static const match_table_t nfs_secflavor_tokens = {
	{ Opt_sec_none, "none" },
	{ Opt_sec_none, "null" },
	{ Opt_sec_sys, "sys" },

	{ Opt_sec_krb5, "krb5" },
	{ Opt_sec_krb5i, "krb5i" },
	{ Opt_sec_krb5p, "krb5p" },

	{ Opt_sec_lkey, "lkey" },
	{ Opt_sec_lkeyi, "lkeyi" },
	{ Opt_sec_lkeyp, "lkeyp" },

	{ Opt_sec_spkm, "spkm3" },
	{ Opt_sec_spkmi, "spkm3i" },
	{ Opt_sec_spkmp, "spkm3p" },

	{ Opt_sec_err, NULL }
};

enum {
	Opt_lookupcache_all, Opt_lookupcache_positive,
	Opt_lookupcache_none,

	Opt_lookupcache_err
};

static match_table_t nfs_lookupcache_tokens = {
	{ Opt_lookupcache_all, "all" },
	{ Opt_lookupcache_positive, "pos" },
	{ Opt_lookupcache_positive, "positive" },
	{ Opt_lookupcache_none, "none" },

	{ Opt_lookupcache_err, NULL }
};

enum {
	Opt_local_lock_all, Opt_local_lock_flock, Opt_local_lock_posix,
	Opt_local_lock_none,

	Opt_local_lock_err
};

static match_table_t nfs_local_lock_tokens = {
	{ Opt_local_lock_all, "all" },
	{ Opt_local_lock_flock, "flock" },
	{ Opt_local_lock_posix, "posix" },
	{ Opt_local_lock_none, "none" },

	{ Opt_local_lock_err, NULL }
};

enum {
	Opt_vers_2, Opt_vers_3, Opt_vers_4, Opt_vers_4_0,
	Opt_vers_4_1, Opt_vers_4_2,

	Opt_vers_err
};

static match_table_t nfs_vers_tokens = {
	{ Opt_vers_2, "2" },
	{ Opt_vers_3, "3" },
	{ Opt_vers_4, "4" },
	{ Opt_vers_4_0, "4.0" },
	{ Opt_vers_4_1, "4.1" },
	{ Opt_vers_4_2, "4.2" },

	{ Opt_vers_err, NULL }
};

struct nfs_parsed_mount_data *nfs_alloc_parsed_mount_data(void)
{
	struct nfs_parsed_mount_data *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (data) {
		data->timeo		= NFS_UNSPEC_TIMEO;
		data->retrans		= NFS_UNSPEC_RETRANS;
		data->acregmin		= NFS_DEF_ACREGMIN;
		data->acregmax		= NFS_DEF_ACREGMAX;
		data->acdirmin		= NFS_DEF_ACDIRMIN;
		data->acdirmax		= NFS_DEF_ACDIRMAX;
		data->mount_server.port	= NFS_UNSPEC_PORT;
		data->nfs_server.port	= NFS_UNSPEC_PORT;
		data->nfs_server.protocol = XPRT_TRANSPORT_TCP;
		data->selected_flavor	= RPC_AUTH_MAXFLAVOR;
		data->minorversion	= 0;
		data->need_mount	= true;
		data->net		= current->nsproxy->net_ns;
		data->lsm_opts		= NULL;
	}
	return data;
}

void nfs_free_parsed_mount_data(struct nfs_parsed_mount_data *data)
{
	if (data) {
		kfree(data->client_address);
		kfree(data->mount_server.hostname);
		kfree(data->nfs_server.export_path);
		kfree(data->nfs_server.hostname);
		kfree(data->fscache_uniq);
		security_free_mnt_opts(&data->lsm_opts);
		kfree(data);
	}
}

/*
 * Sanity-check a server address provided by the mount command.
 *
 * Address family must be initialized, and address must not be
 * the ANY address for that family.
 */
static int nfs_verify_server_address(struct sockaddr *addr)
{
	switch (addr->sa_family) {
	case AF_INET: {
		struct sockaddr_in *sa = (struct sockaddr_in *)addr;
		return sa->sin_addr.s_addr != htonl(INADDR_ANY);
	}
	case AF_INET6: {
		struct in6_addr *sa = &((struct sockaddr_in6 *)addr)->sin6_addr;
		return !ipv6_addr_any(sa);
	}
	}

	dfprintk(MOUNT, "NFS: Invalid IP address specified\n");
	return 0;
}

/*
 * Sanity check the NFS transport protocol.
 *
 */
static void nfs_validate_transport_protocol(struct nfs_parsed_mount_data *mnt)
{
	switch (mnt->nfs_server.protocol) {
	case XPRT_TRANSPORT_UDP:
	case XPRT_TRANSPORT_TCP:
	case XPRT_TRANSPORT_RDMA:
		break;
	default:
		mnt->nfs_server.protocol = XPRT_TRANSPORT_TCP;
	}
}

/*
 * For text based NFSv2/v3 mounts, the mount protocol transport default
 * settings should depend upon the specified NFS transport.
 */
static void nfs_set_mount_transport_protocol(struct nfs_parsed_mount_data *mnt)
{
	nfs_validate_transport_protocol(mnt);

	if (mnt->mount_server.protocol == XPRT_TRANSPORT_UDP ||
	    mnt->mount_server.protocol == XPRT_TRANSPORT_TCP)
			return;
	switch (mnt->nfs_server.protocol) {
	case XPRT_TRANSPORT_UDP:
		mnt->mount_server.protocol = XPRT_TRANSPORT_UDP;
		break;
	case XPRT_TRANSPORT_TCP:
	case XPRT_TRANSPORT_RDMA:
		mnt->mount_server.protocol = XPRT_TRANSPORT_TCP;
	}
}

/*
 * Add 'flavor' to 'auth_info' if not already present.
 * Returns true if 'flavor' ends up in the list, false otherwise
 */
static bool nfs_auth_info_add(struct nfs_auth_info *auth_info,
			      rpc_authflavor_t flavor)
{
	unsigned int i;
	unsigned int max_flavor_len = ARRAY_SIZE(auth_info->flavors);

	/* make sure this flavor isn't already in the list */
	for (i = 0; i < auth_info->flavor_len; i++) {
		if (flavor == auth_info->flavors[i])
			return true;
	}

	if (auth_info->flavor_len + 1 >= max_flavor_len) {
		dfprintk(MOUNT, "NFS: too many sec= flavors\n");
		return false;
	}

	auth_info->flavors[auth_info->flavor_len++] = flavor;
	return true;
}

/*
 * Parse the value of the 'sec=' option.
 */
static int nfs_parse_security_flavors(char *value,
				      struct nfs_parsed_mount_data *mnt)
{
	substring_t args[MAX_OPT_ARGS];
	rpc_authflavor_t pseudoflavor;
	char *p;

	dfprintk(MOUNT, "NFS: parsing sec=%s option\n", value);

	while ((p = strsep(&value, ":")) != NULL) {
		switch (match_token(p, nfs_secflavor_tokens, args)) {
		case Opt_sec_none:
			pseudoflavor = RPC_AUTH_NULL;
			break;
		case Opt_sec_sys:
			pseudoflavor = RPC_AUTH_UNIX;
			break;
		case Opt_sec_krb5:
			pseudoflavor = RPC_AUTH_GSS_KRB5;
			break;
		case Opt_sec_krb5i:
			pseudoflavor = RPC_AUTH_GSS_KRB5I;
			break;
		case Opt_sec_krb5p:
			pseudoflavor = RPC_AUTH_GSS_KRB5P;
			break;
		case Opt_sec_lkey:
			pseudoflavor = RPC_AUTH_GSS_LKEY;
			break;
		case Opt_sec_lkeyi:
			pseudoflavor = RPC_AUTH_GSS_LKEYI;
			break;
		case Opt_sec_lkeyp:
			pseudoflavor = RPC_AUTH_GSS_LKEYP;
			break;
		case Opt_sec_spkm:
			pseudoflavor = RPC_AUTH_GSS_SPKM;
			break;
		case Opt_sec_spkmi:
			pseudoflavor = RPC_AUTH_GSS_SPKMI;
			break;
		case Opt_sec_spkmp:
			pseudoflavor = RPC_AUTH_GSS_SPKMP;
			break;
		default:
			dfprintk(MOUNT,
				 "NFS: sec= option '%s' not recognized\n", p);
			return 0;
		}

		if (!nfs_auth_info_add(&mnt->auth_info, pseudoflavor))
			return 0;
	}

	return 1;
}

static int nfs_parse_version_string(char *string,
		struct nfs_parsed_mount_data *mnt,
		substring_t *args)
{
	mnt->flags &= ~NFS_MOUNT_VER3;
	switch (match_token(string, nfs_vers_tokens, args)) {
	case Opt_vers_2:
		mnt->version = 2;
		break;
	case Opt_vers_3:
		mnt->flags |= NFS_MOUNT_VER3;
		mnt->version = 3;
		break;
	case Opt_vers_4:
		/* Backward compatibility option. In future,
		 * the mount program should always supply
		 * a NFSv4 minor version number.
		 */
		mnt->version = 4;
		break;
	case Opt_vers_4_0:
		mnt->version = 4;
		mnt->minorversion = 0;
		break;
	case Opt_vers_4_1:
		mnt->version = 4;
		mnt->minorversion = 1;
		break;
	case Opt_vers_4_2:
		mnt->version = 4;
		mnt->minorversion = 2;
		break;
	default:
		return 0;
	}
	return 1;
}

static int nfs_get_option_str(substring_t args[], char **option)
{
	kfree(*option);
	*option = match_strdup(args);
	return !*option;
}

static int nfs_get_option_ul(substring_t args[], unsigned long *option)
{
	int rc;
	char *string;

	string = match_strdup(args);
	if (string == NULL)
		return -ENOMEM;
	rc = kstrtoul(string, 10, option);
	kfree(string);

	return rc;
}

static int nfs_get_option_ul_bound(substring_t args[], unsigned long *option,
		unsigned long l_bound, unsigned long u_bound)
{
	int ret;

	ret = nfs_get_option_ul(args, option);
	if (ret != 0)
		return ret;
	if (*option < l_bound || *option > u_bound)
		return -ERANGE;
	return 0;
}

/*
 * Error-check and convert a string of mount options from user space into
 * a data structure.  The whole mount string is processed; bad options are
 * skipped as they are encountered.  If there were no errors, return 1;
 * otherwise return 0 (zero).
 */
int nfs_parse_mount_options(char *raw, struct nfs_parsed_mount_data *mnt)
{
	char *p, *string;
	int rc, sloppy = 0, invalid_option = 0;
	unsigned short protofamily = AF_UNSPEC;
	unsigned short mountfamily = AF_UNSPEC;

	if (!raw) {
		dfprintk(MOUNT, "NFS: mount options string was NULL.\n");
		return 1;
	}
	dfprintk(MOUNT, "NFS: nfs mount opts='%s'\n", raw);

	rc = security_sb_eat_lsm_opts(raw, &mnt->lsm_opts);
	if (rc)
		goto out_security_failure;

	while ((p = strsep(&raw, ",")) != NULL) {
		substring_t args[MAX_OPT_ARGS];
		unsigned long option;
		int token;

		if (!*p)
			continue;

		dfprintk(MOUNT, "NFS:   parsing nfs mount option '%s'\n", p);

		token = match_token(p, nfs_mount_option_tokens, args);
		switch (token) {

		/*
		 * boolean options:  foo/nofoo
		 */
		case Opt_soft:
			mnt->flags |= NFS_MOUNT_SOFT;
			mnt->flags &= ~NFS_MOUNT_SOFTERR;
			break;
		case Opt_softerr:
			mnt->flags |= NFS_MOUNT_SOFTERR;
			mnt->flags &= ~NFS_MOUNT_SOFT;
			break;
		case Opt_hard:
			mnt->flags &= ~(NFS_MOUNT_SOFT|NFS_MOUNT_SOFTERR);
			break;
		case Opt_posix:
			mnt->flags |= NFS_MOUNT_POSIX;
			break;
		case Opt_noposix:
			mnt->flags &= ~NFS_MOUNT_POSIX;
			break;
		case Opt_cto:
			mnt->flags &= ~NFS_MOUNT_NOCTO;
			break;
		case Opt_nocto:
			mnt->flags |= NFS_MOUNT_NOCTO;
			break;
		case Opt_ac:
			mnt->flags &= ~NFS_MOUNT_NOAC;
			break;
		case Opt_noac:
			mnt->flags |= NFS_MOUNT_NOAC;
			break;
		case Opt_lock:
			mnt->flags &= ~NFS_MOUNT_NONLM;
			mnt->flags &= ~(NFS_MOUNT_LOCAL_FLOCK |
					NFS_MOUNT_LOCAL_FCNTL);
			break;
		case Opt_nolock:
			mnt->flags |= NFS_MOUNT_NONLM;
			mnt->flags |= (NFS_MOUNT_LOCAL_FLOCK |
				       NFS_MOUNT_LOCAL_FCNTL);
			break;
		case Opt_udp:
			mnt->flags &= ~NFS_MOUNT_TCP;
			mnt->nfs_server.protocol = XPRT_TRANSPORT_UDP;
			break;
		case Opt_tcp:
			mnt->flags |= NFS_MOUNT_TCP;
			mnt->nfs_server.protocol = XPRT_TRANSPORT_TCP;
			break;
		case Opt_rdma:
			mnt->flags |= NFS_MOUNT_TCP; /* for side protocols */
			mnt->nfs_server.protocol = XPRT_TRANSPORT_RDMA;
			xprt_load_transport(p);
			break;
		case Opt_acl:
			mnt->flags &= ~NFS_MOUNT_NOACL;
			break;
		case Opt_noacl:
			mnt->flags |= NFS_MOUNT_NOACL;
			break;
		case Opt_rdirplus:
			mnt->flags &= ~NFS_MOUNT_NORDIRPLUS;
			break;
		case Opt_nordirplus:
			mnt->flags |= NFS_MOUNT_NORDIRPLUS;
			break;
		case Opt_sharecache:
			mnt->flags &= ~NFS_MOUNT_UNSHARED;
			break;
		case Opt_nosharecache:
			mnt->flags |= NFS_MOUNT_UNSHARED;
			break;
		case Opt_resvport:
			mnt->flags &= ~NFS_MOUNT_NORESVPORT;
			break;
		case Opt_noresvport:
			mnt->flags |= NFS_MOUNT_NORESVPORT;
			break;
		case Opt_fscache:
			mnt->options |= NFS_OPTION_FSCACHE;
			kfree(mnt->fscache_uniq);
			mnt->fscache_uniq = NULL;
			break;
		case Opt_nofscache:
			mnt->options &= ~NFS_OPTION_FSCACHE;
			kfree(mnt->fscache_uniq);
			mnt->fscache_uniq = NULL;
			break;
		case Opt_migration:
			mnt->options |= NFS_OPTION_MIGRATION;
			break;
		case Opt_nomigration:
			mnt->options &= ~NFS_OPTION_MIGRATION;
			break;

		/*
		 * options that take numeric values
		 */
		case Opt_port:
			if (nfs_get_option_ul(args, &option) ||
			    option > USHRT_MAX)
				goto out_invalid_value;
			mnt->nfs_server.port = option;
			break;
		case Opt_rsize:
			if (nfs_get_option_ul(args, &option))
				goto out_invalid_value;
			mnt->rsize = option;
			break;
		case Opt_wsize:
			if (nfs_get_option_ul(args, &option))
				goto out_invalid_value;
			mnt->wsize = option;
			break;
		case Opt_bsize:
			if (nfs_get_option_ul(args, &option))
				goto out_invalid_value;
			mnt->bsize = option;
			break;
		case Opt_timeo:
			if (nfs_get_option_ul_bound(args, &option, 1, INT_MAX))
				goto out_invalid_value;
			mnt->timeo = option;
			break;
		case Opt_retrans:
			if (nfs_get_option_ul_bound(args, &option, 0, INT_MAX))
				goto out_invalid_value;
			mnt->retrans = option;
			break;
		case Opt_acregmin:
			if (nfs_get_option_ul(args, &option))
				goto out_invalid_value;
			mnt->acregmin = option;
			break;
		case Opt_acregmax:
			if (nfs_get_option_ul(args, &option))
				goto out_invalid_value;
			mnt->acregmax = option;
			break;
		case Opt_acdirmin:
			if (nfs_get_option_ul(args, &option))
				goto out_invalid_value;
			mnt->acdirmin = option;
			break;
		case Opt_acdirmax:
			if (nfs_get_option_ul(args, &option))
				goto out_invalid_value;
			mnt->acdirmax = option;
			break;
		case Opt_actimeo:
			if (nfs_get_option_ul(args, &option))
				goto out_invalid_value;
			mnt->acregmin = mnt->acregmax =
			mnt->acdirmin = mnt->acdirmax = option;
			break;
		case Opt_namelen:
			if (nfs_get_option_ul(args, &option))
				goto out_invalid_value;
			mnt->namlen = option;
			break;
		case Opt_mountport:
			if (nfs_get_option_ul(args, &option) ||
			    option > USHRT_MAX)
				goto out_invalid_value;
			mnt->mount_server.port = option;
			break;
		case Opt_mountvers:
			if (nfs_get_option_ul(args, &option) ||
			    option < NFS_MNT_VERSION ||
			    option > NFS_MNT3_VERSION)
				goto out_invalid_value;
			mnt->mount_server.version = option;
			break;
		case Opt_minorversion:
			if (nfs_get_option_ul(args, &option))
				goto out_invalid_value;
			if (option > NFS4_MAX_MINOR_VERSION)
				goto out_invalid_value;
			mnt->minorversion = option;
			break;

		/*
		 * options that take text values
		 */
		case Opt_nfsvers:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			rc = nfs_parse_version_string(string, mnt, args);
			kfree(string);
			if (!rc)
				goto out_invalid_value;
			break;
		case Opt_sec:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			rc = nfs_parse_security_flavors(string, mnt);
			kfree(string);
			if (!rc) {
				dfprintk(MOUNT, "NFS:   unrecognized "
						"security flavor\n");
				return 0;
			}
			break;
		case Opt_proto:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			token = match_token(string,
					    nfs_xprt_protocol_tokens, args);

			protofamily = AF_INET;
			switch (token) {
			case Opt_xprt_udp6:
				protofamily = AF_INET6;
				/* fall through */
			case Opt_xprt_udp:
				mnt->flags &= ~NFS_MOUNT_TCP;
				mnt->nfs_server.protocol = XPRT_TRANSPORT_UDP;
				break;
			case Opt_xprt_tcp6:
				protofamily = AF_INET6;
				/* fall through */
			case Opt_xprt_tcp:
				mnt->flags |= NFS_MOUNT_TCP;
				mnt->nfs_server.protocol = XPRT_TRANSPORT_TCP;
				break;
			case Opt_xprt_rdma6:
				protofamily = AF_INET6;
				/* fall through */
			case Opt_xprt_rdma:
				/* vector side protocols to TCP */
				mnt->flags |= NFS_MOUNT_TCP;
				mnt->nfs_server.protocol = XPRT_TRANSPORT_RDMA;
				xprt_load_transport(string);
				break;
			default:
				dfprintk(MOUNT, "NFS:   unrecognized "
						"transport protocol\n");
				kfree(string);
				return 0;
			}
			kfree(string);
			break;
		case Opt_mountproto:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			token = match_token(string,
					    nfs_xprt_protocol_tokens, args);
			kfree(string);

			mountfamily = AF_INET;
			switch (token) {
			case Opt_xprt_udp6:
				mountfamily = AF_INET6;
				/* fall through */
			case Opt_xprt_udp:
				mnt->mount_server.protocol = XPRT_TRANSPORT_UDP;
				break;
			case Opt_xprt_tcp6:
				mountfamily = AF_INET6;
				/* fall through */
			case Opt_xprt_tcp:
				mnt->mount_server.protocol = XPRT_TRANSPORT_TCP;
				break;
			case Opt_xprt_rdma: /* not used for side protocols */
			default:
				dfprintk(MOUNT, "NFS:   unrecognized "
						"transport protocol\n");
				return 0;
			}
			break;
		case Opt_addr:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			mnt->nfs_server.addrlen =
				rpc_pton(mnt->net, string, strlen(string),
					(struct sockaddr *)
					&mnt->nfs_server.address,
					sizeof(mnt->nfs_server.address));
			kfree(string);
			if (mnt->nfs_server.addrlen == 0)
				goto out_invalid_address;
			break;
		case Opt_clientaddr:
			if (nfs_get_option_str(args, &mnt->client_address))
				goto out_nomem;
			break;
		case Opt_mounthost:
			if (nfs_get_option_str(args,
					       &mnt->mount_server.hostname))
				goto out_nomem;
			break;
		case Opt_mountaddr:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			mnt->mount_server.addrlen =
				rpc_pton(mnt->net, string, strlen(string),
					(struct sockaddr *)
					&mnt->mount_server.address,
					sizeof(mnt->mount_server.address));
			kfree(string);
			if (mnt->mount_server.addrlen == 0)
				goto out_invalid_address;
			break;
		case Opt_nconnect:
			if (nfs_get_option_ul_bound(args, &option, 1, NFS_MAX_CONNECTIONS))
				goto out_invalid_value;
			mnt->nfs_server.nconnect = option;
			break;
		case Opt_lookupcache:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			token = match_token(string,
					nfs_lookupcache_tokens, args);
			kfree(string);
			switch (token) {
				case Opt_lookupcache_all:
					mnt->flags &= ~(NFS_MOUNT_LOOKUP_CACHE_NONEG|NFS_MOUNT_LOOKUP_CACHE_NONE);
					break;
				case Opt_lookupcache_positive:
					mnt->flags &= ~NFS_MOUNT_LOOKUP_CACHE_NONE;
					mnt->flags |= NFS_MOUNT_LOOKUP_CACHE_NONEG;
					break;
				case Opt_lookupcache_none:
					mnt->flags |= NFS_MOUNT_LOOKUP_CACHE_NONEG|NFS_MOUNT_LOOKUP_CACHE_NONE;
					break;
				default:
					dfprintk(MOUNT, "NFS:   invalid "
							"lookupcache argument\n");
					return 0;
			}
			break;
		case Opt_fscache_uniq:
			if (nfs_get_option_str(args, &mnt->fscache_uniq))
				goto out_nomem;
			mnt->options |= NFS_OPTION_FSCACHE;
			break;
		case Opt_local_lock:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;
			token = match_token(string, nfs_local_lock_tokens,
					args);
			kfree(string);
			switch (token) {
			case Opt_local_lock_all:
				mnt->flags |= (NFS_MOUNT_LOCAL_FLOCK |
					       NFS_MOUNT_LOCAL_FCNTL);
				break;
			case Opt_local_lock_flock:
				mnt->flags |= NFS_MOUNT_LOCAL_FLOCK;
				break;
			case Opt_local_lock_posix:
				mnt->flags |= NFS_MOUNT_LOCAL_FCNTL;
				break;
			case Opt_local_lock_none:
				mnt->flags &= ~(NFS_MOUNT_LOCAL_FLOCK |
						NFS_MOUNT_LOCAL_FCNTL);
				break;
			default:
				dfprintk(MOUNT, "NFS:	invalid	"
						"local_lock argument\n");
				return 0;
			}
			break;

		/*
		 * Special options
		 */
		case Opt_sloppy:
			sloppy = 1;
			dfprintk(MOUNT, "NFS:   relaxing parsing rules\n");
			break;
		case Opt_userspace:
		case Opt_deprecated:
			dfprintk(MOUNT, "NFS:   ignoring mount option "
					"'%s'\n", p);
			break;

		default:
			invalid_option = 1;
			dfprintk(MOUNT, "NFS:   unrecognized mount option "
					"'%s'\n", p);
		}
	}

	if (!sloppy && invalid_option)
		return 0;

	if (mnt->minorversion && mnt->version != 4)
		goto out_minorversion_mismatch;

	if (mnt->options & NFS_OPTION_MIGRATION &&
	    (mnt->version != 4 || mnt->minorversion != 0))
		goto out_migration_misuse;

	/*
	 * verify that any proto=/mountproto= options match the address
	 * families in the addr=/mountaddr= options.
	 */
	if (protofamily != AF_UNSPEC &&
	    protofamily != mnt->nfs_server.address.ss_family)
		goto out_proto_mismatch;

	if (mountfamily != AF_UNSPEC) {
		if (mnt->mount_server.addrlen) {
			if (mountfamily != mnt->mount_server.address.ss_family)
				goto out_mountproto_mismatch;
		} else {
			if (mountfamily != mnt->nfs_server.address.ss_family)
				goto out_mountproto_mismatch;
		}
	}

	return 1;

out_mountproto_mismatch:
	printk(KERN_INFO "NFS: mount server address does not match mountproto= "
			 "option\n");
	return 0;
out_proto_mismatch:
	printk(KERN_INFO "NFS: server address does not match proto= option\n");
	return 0;
out_invalid_address:
	printk(KERN_INFO "NFS: bad IP address specified: %s\n", p);
	return 0;
out_invalid_value:
	printk(KERN_INFO "NFS: bad mount option value specified: %s\n", p);
	return 0;
out_minorversion_mismatch:
	printk(KERN_INFO "NFS: mount option vers=%u does not support "
			 "minorversion=%u\n", mnt->version, mnt->minorversion);
	return 0;
out_migration_misuse:
	printk(KERN_INFO
		"NFS: 'migration' not supported for this NFS version\n");
	return 0;
out_nomem:
	printk(KERN_INFO "NFS: not enough memory to parse option\n");
	return 0;
out_security_failure:
	printk(KERN_INFO "NFS: security options invalid: %d\n", rc);
	return 0;
}

/*
 * Split "dev_name" into "hostname:export_path".
 *
 * The leftmost colon demarks the split between the server's hostname
 * and the export path.  If the hostname starts with a left square
 * bracket, then it may contain colons.
 *
 * Note: caller frees hostname and export path, even on error.
 */
static int nfs_parse_devname(const char *dev_name,
			     char **hostname, size_t maxnamlen,
			     char **export_path, size_t maxpathlen)
{
	size_t len;
	char *end;

	if (unlikely(!dev_name || !*dev_name)) {
		dfprintk(MOUNT, "NFS: device name not specified\n");
		return -EINVAL;
	}

	/* Is the host name protected with square brakcets? */
	if (*dev_name == '[') {
		end = strchr(++dev_name, ']');
		if (end == NULL || end[1] != ':')
			goto out_bad_devname;

		len = end - dev_name;
		end++;
	} else {
		char *comma;

		end = strchr(dev_name, ':');
		if (end == NULL)
			goto out_bad_devname;
		len = end - dev_name;

		/* kill possible hostname list: not supported */
		comma = strchr(dev_name, ',');
		if (comma != NULL && comma < end)
			len = comma - dev_name;
	}

	if (len > maxnamlen)
		goto out_hostname;

	/* N.B. caller will free nfs_server.hostname in all cases */
	*hostname = kstrndup(dev_name, len, GFP_KERNEL);
	if (*hostname == NULL)
		goto out_nomem;
	len = strlen(++end);
	if (len > maxpathlen)
		goto out_path;
	*export_path = kstrndup(end, len, GFP_KERNEL);
	if (!*export_path)
		goto out_nomem;

	dfprintk(MOUNT, "NFS: MNTPATH: '%s'\n", *export_path);
	return 0;

out_bad_devname:
	dfprintk(MOUNT, "NFS: device name not in host:path format\n");
	return -EINVAL;

out_nomem:
	dfprintk(MOUNT, "NFS: not enough memory to parse device name\n");
	return -ENOMEM;

out_hostname:
	dfprintk(MOUNT, "NFS: server hostname too long\n");
	return -ENAMETOOLONG;

out_path:
	dfprintk(MOUNT, "NFS: export pathname too long\n");
	return -ENAMETOOLONG;
}

/*
 * Validate the NFS2/NFS3 mount data
 * - fills in the mount root filehandle
 *
 * For option strings, user space handles the following behaviors:
 *
 * + DNS: mapping server host name to IP address ("addr=" option)
 *
 * + failure mode: how to behave if a mount request can't be handled
 *   immediately ("fg/bg" option)
 *
 * + retry: how often to retry a mount request ("retry=" option)
 *
 * + breaking back: trying proto=udp after proto=tcp, v2 after v3,
 *   mountproto=tcp after mountproto=udp, and so on
 */
static int nfs23_validate_mount_data(void *options,
				     struct nfs_parsed_mount_data *args,
				     struct nfs_fh *mntfh,
				     const char *dev_name)
{
	struct nfs_mount_data *data = (struct nfs_mount_data *)options;
	struct sockaddr *sap = (struct sockaddr *)&args->nfs_server.address;
	int extra_flags = NFS_MOUNT_LEGACY_INTERFACE;

	if (data == NULL)
		goto out_no_data;

	args->version = NFS_DEFAULT_VERSION;
	switch (data->version) {
	case 1:
		data->namlen = 0; /* fall through */
	case 2:
		data->bsize = 0; /* fall through */
	case 3:
		if (data->flags & NFS_MOUNT_VER3)
			goto out_no_v3;
		data->root.size = NFS2_FHSIZE;
		memcpy(data->root.data, data->old_root.data, NFS2_FHSIZE);
		/* Turn off security negotiation */
		extra_flags |= NFS_MOUNT_SECFLAVOUR;
		/* fall through */
	case 4:
		if (data->flags & NFS_MOUNT_SECFLAVOUR)
			goto out_no_sec;
		/* fall through */
	case 5:
		memset(data->context, 0, sizeof(data->context));
		/* fall through */
	case 6:
		if (data->flags & NFS_MOUNT_VER3) {
			if (data->root.size > NFS3_FHSIZE || data->root.size == 0)
				goto out_invalid_fh;
			mntfh->size = data->root.size;
			args->version = 3;
		} else {
			mntfh->size = NFS2_FHSIZE;
			args->version = 2;
		}


		memcpy(mntfh->data, data->root.data, mntfh->size);
		if (mntfh->size < sizeof(mntfh->data))
			memset(mntfh->data + mntfh->size, 0,
			       sizeof(mntfh->data) - mntfh->size);

		/*
		 * Translate to nfs_parsed_mount_data, which nfs_fill_super
		 * can deal with.
		 */
		args->flags		= data->flags & NFS_MOUNT_FLAGMASK;
		args->flags		|= extra_flags;
		args->rsize		= data->rsize;
		args->wsize		= data->wsize;
		args->timeo		= data->timeo;
		args->retrans		= data->retrans;
		args->acregmin		= data->acregmin;
		args->acregmax		= data->acregmax;
		args->acdirmin		= data->acdirmin;
		args->acdirmax		= data->acdirmax;
		args->need_mount	= false;

		memcpy(sap, &data->addr, sizeof(data->addr));
		args->nfs_server.addrlen = sizeof(data->addr);
		args->nfs_server.port = ntohs(data->addr.sin_port);
		if (sap->sa_family != AF_INET ||
		    !nfs_verify_server_address(sap))
			goto out_no_address;

		if (!(data->flags & NFS_MOUNT_TCP))
			args->nfs_server.protocol = XPRT_TRANSPORT_UDP;
		/* N.B. caller will free nfs_server.hostname in all cases */
		args->nfs_server.hostname = kstrdup(data->hostname, GFP_KERNEL);
		args->namlen		= data->namlen;
		args->bsize		= data->bsize;

		if (data->flags & NFS_MOUNT_SECFLAVOUR)
			args->selected_flavor = data->pseudoflavor;
		else
			args->selected_flavor = RPC_AUTH_UNIX;
		if (!args->nfs_server.hostname)
			goto out_nomem;

		if (!(data->flags & NFS_MOUNT_NONLM))
			args->flags &= ~(NFS_MOUNT_LOCAL_FLOCK|
					 NFS_MOUNT_LOCAL_FCNTL);
		else
			args->flags |= (NFS_MOUNT_LOCAL_FLOCK|
					NFS_MOUNT_LOCAL_FCNTL);
		/*
		 * The legacy version 6 binary mount data from userspace has a
		 * field used only to transport selinux information into the
		 * the kernel.  To continue to support that functionality we
		 * have a touch of selinux knowledge here in the NFS code. The
		 * userspace code converted context=blah to just blah so we are
		 * converting back to the full string selinux understands.
		 */
		if (data->context[0]){
#ifdef CONFIG_SECURITY_SELINUX
			int rc;
			data->context[NFS_MAX_CONTEXT_LEN] = '\0';
			rc = security_add_mnt_opt("context", data->context,
					strlen(data->context), &args->lsm_opts);
			if (rc)
				return rc;
#else
			return -EINVAL;
#endif
		}

		break;
	default:
		return NFS_TEXT_DATA;
	}

	return 0;

out_no_data:
	dfprintk(MOUNT, "NFS: mount program didn't pass any mount data\n");
	return -EINVAL;

out_no_v3:
	dfprintk(MOUNT, "NFS: nfs_mount_data version %d does not support v3\n",
		 data->version);
	return -EINVAL;

out_no_sec:
	dfprintk(MOUNT, "NFS: nfs_mount_data version supports only AUTH_SYS\n");
	return -EINVAL;

out_nomem:
	dfprintk(MOUNT, "NFS: not enough memory to handle mount options\n");
	return -ENOMEM;

out_no_address:
	dfprintk(MOUNT, "NFS: mount program didn't pass remote address\n");
	return -EINVAL;

out_invalid_fh:
	dfprintk(MOUNT, "NFS: invalid root filehandle\n");
	return -EINVAL;
}

#if IS_ENABLED(CONFIG_NFS_V4)

static void nfs4_validate_mount_flags(struct nfs_parsed_mount_data *args)
{
	args->flags &= ~(NFS_MOUNT_NONLM|NFS_MOUNT_NOACL|NFS_MOUNT_VER3|
			 NFS_MOUNT_LOCAL_FLOCK|NFS_MOUNT_LOCAL_FCNTL);
}

/*
 * Validate NFSv4 mount options
 */
static int nfs4_validate_mount_data(void *options,
				    struct nfs_parsed_mount_data *args,
				    const char *dev_name)
{
	struct sockaddr *sap = (struct sockaddr *)&args->nfs_server.address;
	struct nfs4_mount_data *data = (struct nfs4_mount_data *)options;
	char *c;

	if (data == NULL)
		goto out_no_data;

	args->version = 4;

	switch (data->version) {
	case 1:
		if (data->host_addrlen > sizeof(args->nfs_server.address))
			goto out_no_address;
		if (data->host_addrlen == 0)
			goto out_no_address;
		args->nfs_server.addrlen = data->host_addrlen;
		if (copy_from_user(sap, data->host_addr, data->host_addrlen))
			return -EFAULT;
		if (!nfs_verify_server_address(sap))
			goto out_no_address;
		args->nfs_server.port = ntohs(((struct sockaddr_in *)sap)->sin_port);

		if (data->auth_flavourlen) {
			rpc_authflavor_t pseudoflavor;
			if (data->auth_flavourlen > 1)
				goto out_inval_auth;
			if (copy_from_user(&pseudoflavor,
					   data->auth_flavours,
					   sizeof(pseudoflavor)))
				return -EFAULT;
			args->selected_flavor = pseudoflavor;
		} else
			args->selected_flavor = RPC_AUTH_UNIX;

		c = strndup_user(data->hostname.data, NFS4_MAXNAMLEN);
		if (IS_ERR(c))
			return PTR_ERR(c);
		args->nfs_server.hostname = c;

		c = strndup_user(data->mnt_path.data, NFS4_MAXPATHLEN);
		if (IS_ERR(c))
			return PTR_ERR(c);
		args->nfs_server.export_path = c;
		dfprintk(MOUNT, "NFS: MNTPATH: '%s'\n", c);

		c = strndup_user(data->client_addr.data, 16);
		if (IS_ERR(c))
			return PTR_ERR(c);
		args->client_address = c;

		/*
		 * Translate to nfs_parsed_mount_data, which nfs4_fill_super
		 * can deal with.
		 */

		args->flags	= data->flags & NFS4_MOUNT_FLAGMASK;
		args->rsize	= data->rsize;
		args->wsize	= data->wsize;
		args->timeo	= data->timeo;
		args->retrans	= data->retrans;
		args->acregmin	= data->acregmin;
		args->acregmax	= data->acregmax;
		args->acdirmin	= data->acdirmin;
		args->acdirmax	= data->acdirmax;
		args->nfs_server.protocol = data->proto;
		nfs_validate_transport_protocol(args);
		if (args->nfs_server.protocol == XPRT_TRANSPORT_UDP)
			goto out_invalid_transport_udp;

		break;
	default:
		return NFS_TEXT_DATA;
	}

	return 0;

out_no_data:
	dfprintk(MOUNT, "NFS4: mount program didn't pass any mount data\n");
	return -EINVAL;

out_inval_auth:
	dfprintk(MOUNT, "NFS4: Invalid number of RPC auth flavours %d\n",
		 data->auth_flavourlen);
	return -EINVAL;

out_no_address:
	dfprintk(MOUNT, "NFS4: mount program didn't pass remote address\n");
	return -EINVAL;

out_invalid_transport_udp:
	dfprintk(MOUNT, "NFSv4: Unsupported transport protocol udp\n");
	return -EINVAL;
}

int nfs_validate_mount_data(struct file_system_type *fs_type,
			    void *options,
			    struct nfs_parsed_mount_data *args,
			    struct nfs_fh *mntfh,
			    const char *dev_name)
{
	if (fs_type == &nfs_fs_type)
		return nfs23_validate_mount_data(options, args, mntfh, dev_name);
	return nfs4_validate_mount_data(options, args, dev_name);
}
#else
int nfs_validate_mount_data(struct file_system_type *fs_type,
			    void *options,
			    struct nfs_parsed_mount_data *args,
			    struct nfs_fh *mntfh,
			    const char *dev_name)
{
	return nfs23_validate_mount_data(options, args, mntfh, dev_name);
}
#endif

int nfs_validate_text_mount_data(void *options,
				 struct nfs_parsed_mount_data *args,
				 const char *dev_name)
{
	int port = 0;
	int max_namelen = PAGE_SIZE;
	int max_pathlen = NFS_MAXPATHLEN;
	struct sockaddr *sap = (struct sockaddr *)&args->nfs_server.address;

	if (nfs_parse_mount_options((char *)options, args) == 0)
		return -EINVAL;

	if (!nfs_verify_server_address(sap))
		goto out_no_address;

	if (args->version == 4) {
#if IS_ENABLED(CONFIG_NFS_V4)
		if (args->nfs_server.protocol == XPRT_TRANSPORT_RDMA)
			port = NFS_RDMA_PORT;
		else
			port = NFS_PORT;
		max_namelen = NFS4_MAXNAMLEN;
		max_pathlen = NFS4_MAXPATHLEN;
		nfs_validate_transport_protocol(args);
		if (args->nfs_server.protocol == XPRT_TRANSPORT_UDP)
			goto out_invalid_transport_udp;
		nfs4_validate_mount_flags(args);
#else
		goto out_v4_not_compiled;
#endif /* CONFIG_NFS_V4 */
	} else {
		nfs_set_mount_transport_protocol(args);
		if (args->nfs_server.protocol == XPRT_TRANSPORT_RDMA)
			port = NFS_RDMA_PORT;
	}

	nfs_set_port(sap, &args->nfs_server.port, port);

	return nfs_parse_devname(dev_name,
				   &args->nfs_server.hostname,
				   max_namelen,
				   &args->nfs_server.export_path,
				   max_pathlen);

#if !IS_ENABLED(CONFIG_NFS_V4)
out_v4_not_compiled:
	dfprintk(MOUNT, "NFS: NFSv4 is not compiled into kernel\n");
	return -EPROTONOSUPPORT;
#else
out_invalid_transport_udp:
	dfprintk(MOUNT, "NFSv4: Unsupported transport protocol udp\n");
	return -EINVAL;
#endif /* !CONFIG_NFS_V4 */

out_no_address:
	dfprintk(MOUNT, "NFS: mount program didn't pass remote address\n");
	return -EINVAL;
}
