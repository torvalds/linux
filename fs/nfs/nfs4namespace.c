/*
 * linux/fs/nfs/nfs4namespace.c
 *
 * Copyright (C) 2005 Trond Myklebust <Trond.Myklebust@netapp.com>
 * - Modified by David Howells <dhowells@redhat.com>
 *
 * NFSv4 namespace
 */

#include <linux/dcache.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/addr.h>
#include <linux/vfs.h>
#include <linux/inet.h>
#include "internal.h"
#include "nfs4_fs.h"
#include "dns_resolve.h"

#define NFSDBG_FACILITY		NFSDBG_VFS

/*
 * Convert the NFSv4 pathname components into a standard posix path.
 *
 * Note that the resulting string will be placed at the end of the buffer
 */
static inline char *nfs4_pathname_string(const struct nfs4_pathname *pathname,
					 char *buffer, ssize_t buflen)
{
	char *end = buffer + buflen;
	int n;

	*--end = '\0';
	buflen--;

	n = pathname->ncomponents;
	while (--n >= 0) {
		const struct nfs4_string *component = &pathname->components[n];
		buflen -= component->len + 1;
		if (buflen < 0)
			goto Elong;
		end -= component->len;
		memcpy(end, component->data, component->len);
		*--end = '/';
	}
	return end;
Elong:
	return ERR_PTR(-ENAMETOOLONG);
}

/*
 * return the path component of "<server>:<path>"
 *  nfspath - the "<server>:<path>" string
 *  end - one past the last char that could contain "<server>:"
 * returns NULL on failure
 */
static char *nfs_path_component(const char *nfspath, const char *end)
{
	char *p;

	if (*nfspath == '[') {
		/* parse [] escaped IPv6 addrs */
		p = strchr(nfspath, ']');
		if (p != NULL && ++p < end && *p == ':')
			return p + 1;
	} else {
		/* otherwise split on first colon */
		p = strchr(nfspath, ':');
		if (p != NULL && p < end)
			return p + 1;
	}
	return NULL;
}

/*
 * Determine the mount path as a string
 */
static char *nfs4_path(struct dentry *dentry, char *buffer, ssize_t buflen)
{
	char *limit;
	char *path = nfs_path(&limit, dentry, buffer, buflen,
			      NFS_PATH_CANONICAL);
	if (!IS_ERR(path)) {
		char *path_component = nfs_path_component(path, limit);
		if (path_component)
			return path_component;
	}
	return path;
}

/*
 * Check that fs_locations::fs_root [RFC3530 6.3] is a prefix for what we
 * believe to be the server path to this dentry
 */
static int nfs4_validate_fspath(struct dentry *dentry,
				const struct nfs4_fs_locations *locations,
				char *page, char *page2)
{
	const char *path, *fs_path;

	path = nfs4_path(dentry, page, PAGE_SIZE);
	if (IS_ERR(path))
		return PTR_ERR(path);

	fs_path = nfs4_pathname_string(&locations->fs_path, page2, PAGE_SIZE);
	if (IS_ERR(fs_path))
		return PTR_ERR(fs_path);

	if (strncmp(path, fs_path, strlen(fs_path)) != 0) {
		dprintk("%s: path %s does not begin with fsroot %s\n",
			__func__, path, fs_path);
		return -ENOENT;
	}

	return 0;
}

static size_t nfs_parse_server_name(char *string, size_t len,
		struct sockaddr *sa, size_t salen, struct net *net)
{
	ssize_t ret;

	ret = rpc_pton(net, string, len, sa, salen);
	if (ret == 0) {
		ret = nfs_dns_resolve_name(net, string, len, sa, salen);
		if (ret < 0)
			ret = 0;
	}
	return ret;
}

/**
 * nfs_find_best_sec - Find a security mechanism supported locally
 * @server: NFS server struct
 * @flavors: List of security tuples returned by SECINFO procedure
 *
 * Return an rpc client that uses the first security mechanism in
 * "flavors" that is locally supported.  The "flavors" array
 * is searched in the order returned from the server, per RFC 3530
 * recommendation and each flavor is checked for membership in the
 * sec= mount option list if it exists.
 *
 * Return -EPERM if no matching flavor is found in the array.
 *
 * Please call rpc_shutdown_client() when you are done with this rpc client.
 *
 */
static struct rpc_clnt *nfs_find_best_sec(struct rpc_clnt *clnt,
					  struct nfs_server *server,
					  struct nfs4_secinfo_flavors *flavors)
{
	rpc_authflavor_t pflavor;
	struct nfs4_secinfo4 *secinfo;
	unsigned int i;

	for (i = 0; i < flavors->num_flavors; i++) {
		secinfo = &flavors->flavors[i];

		switch (secinfo->flavor) {
		case RPC_AUTH_NULL:
		case RPC_AUTH_UNIX:
		case RPC_AUTH_GSS:
			pflavor = rpcauth_get_pseudoflavor(secinfo->flavor,
							&secinfo->flavor_info);
			/* does the pseudoflavor match a sec= mount opt? */
			if (pflavor != RPC_AUTH_MAXFLAVOR &&
			    nfs_auth_info_match(&server->auth_info, pflavor)) {
				struct rpc_clnt *new;
				struct rpc_cred *cred;

				/* Cloning creates an rpc_auth for the flavor */
				new = rpc_clone_client_set_auth(clnt, pflavor);
				if (IS_ERR(new))
					continue;
				/**
				* Check that the user actually can use the
				* flavor. This is mostly for RPC_AUTH_GSS
				* where cr_init obtains a gss context
				*/
				cred = rpcauth_lookupcred(new->cl_auth, 0);
				if (IS_ERR(cred)) {
					rpc_shutdown_client(new);
					continue;
				}
				put_rpccred(cred);
				return new;
			}
		}
	}
	return ERR_PTR(-EPERM);
}

/**
 * nfs4_negotiate_security - in response to an NFS4ERR_WRONGSEC on lookup,
 * return an rpc_clnt that uses the best available security flavor with
 * respect to the secinfo flavor list and the sec= mount options.
 *
 * @clnt: RPC client to clone
 * @inode: directory inode
 * @name: lookup name
 *
 * Please call rpc_shutdown_client() when you are done with this rpc client.
 */
struct rpc_clnt *
nfs4_negotiate_security(struct rpc_clnt *clnt, struct inode *inode,
					struct qstr *name)
{
	struct page *page;
	struct nfs4_secinfo_flavors *flavors;
	struct rpc_clnt *new;
	int err;

	page = alloc_page(GFP_KERNEL);
	if (!page)
		return ERR_PTR(-ENOMEM);

	flavors = page_address(page);

	err = nfs4_proc_secinfo(inode, name, flavors);
	if (err < 0) {
		new = ERR_PTR(err);
		goto out;
	}

	new = nfs_find_best_sec(clnt, NFS_SERVER(inode), flavors);

out:
	put_page(page);
	return new;
}

static struct vfsmount *try_location(struct nfs_clone_mount *mountdata,
				     char *page, char *page2,
				     const struct nfs4_fs_location *location)
{
	const size_t addr_bufsize = sizeof(struct sockaddr_storage);
	struct net *net = rpc_net_ns(NFS_SB(mountdata->sb)->client);
	struct vfsmount *mnt = ERR_PTR(-ENOENT);
	char *mnt_path;
	unsigned int maxbuflen;
	unsigned int s;

	mnt_path = nfs4_pathname_string(&location->rootpath, page2, PAGE_SIZE);
	if (IS_ERR(mnt_path))
		return ERR_CAST(mnt_path);
	mountdata->mnt_path = mnt_path;
	maxbuflen = mnt_path - 1 - page2;

	mountdata->addr = kmalloc(addr_bufsize, GFP_KERNEL);
	if (mountdata->addr == NULL)
		return ERR_PTR(-ENOMEM);

	for (s = 0; s < location->nservers; s++) {
		const struct nfs4_string *buf = &location->servers[s];

		if (buf->len <= 0 || buf->len >= maxbuflen)
			continue;

		if (memchr(buf->data, IPV6_SCOPE_DELIMITER, buf->len))
			continue;

		mountdata->addrlen = nfs_parse_server_name(buf->data, buf->len,
				mountdata->addr, addr_bufsize, net);
		if (mountdata->addrlen == 0)
			continue;

		rpc_set_port(mountdata->addr, NFS_PORT);

		memcpy(page2, buf->data, buf->len);
		page2[buf->len] = '\0';
		mountdata->hostname = page2;

		snprintf(page, PAGE_SIZE, "%s:%s",
				mountdata->hostname,
				mountdata->mnt_path);

		mnt = vfs_kern_mount(&nfs4_referral_fs_type, 0, page, mountdata);
		if (!IS_ERR(mnt))
			break;
	}
	kfree(mountdata->addr);
	return mnt;
}

/**
 * nfs_follow_referral - set up mountpoint when hitting a referral on moved error
 * @dentry - parent directory
 * @locations - array of NFSv4 server location information
 *
 */
static struct vfsmount *nfs_follow_referral(struct dentry *dentry,
					    const struct nfs4_fs_locations *locations)
{
	struct vfsmount *mnt = ERR_PTR(-ENOENT);
	struct nfs_clone_mount mountdata = {
		.sb = dentry->d_sb,
		.dentry = dentry,
		.authflavor = NFS_SB(dentry->d_sb)->client->cl_auth->au_flavor,
	};
	char *page = NULL, *page2 = NULL;
	int loc, error;

	if (locations == NULL || locations->nlocations <= 0)
		goto out;

	dprintk("%s: referral at %pd2\n", __func__, dentry);

	page = (char *) __get_free_page(GFP_USER);
	if (!page)
		goto out;

	page2 = (char *) __get_free_page(GFP_USER);
	if (!page2)
		goto out;

	/* Ensure fs path is a prefix of current dentry path */
	error = nfs4_validate_fspath(dentry, locations, page, page2);
	if (error < 0) {
		mnt = ERR_PTR(error);
		goto out;
	}

	for (loc = 0; loc < locations->nlocations; loc++) {
		const struct nfs4_fs_location *location = &locations->locations[loc];

		if (location == NULL || location->nservers <= 0 ||
		    location->rootpath.ncomponents == 0)
			continue;

		mnt = try_location(&mountdata, page, page2, location);
		if (!IS_ERR(mnt))
			break;
	}

out:
	free_page((unsigned long) page);
	free_page((unsigned long) page2);
	dprintk("%s: done\n", __func__);
	return mnt;
}

/*
 * nfs_do_refmount - handle crossing a referral on server
 * @dentry - dentry of referral
 *
 */
static struct vfsmount *nfs_do_refmount(struct rpc_clnt *client, struct dentry *dentry)
{
	struct vfsmount *mnt = ERR_PTR(-ENOMEM);
	struct dentry *parent;
	struct nfs4_fs_locations *fs_locations = NULL;
	struct page *page;
	int err;

	/* BUG_ON(IS_ROOT(dentry)); */
	dprintk("%s: enter\n", __func__);

	page = alloc_page(GFP_KERNEL);
	if (page == NULL)
		goto out;

	fs_locations = kmalloc(sizeof(struct nfs4_fs_locations), GFP_KERNEL);
	if (fs_locations == NULL)
		goto out_free;

	/* Get locations */
	mnt = ERR_PTR(-ENOENT);

	parent = dget_parent(dentry);
	dprintk("%s: getting locations for %pd2\n",
		__func__, dentry);

	err = nfs4_proc_fs_locations(client, parent->d_inode, &dentry->d_name, fs_locations, page);
	dput(parent);
	if (err != 0 ||
	    fs_locations->nlocations <= 0 ||
	    fs_locations->fs_path.ncomponents <= 0)
		goto out_free;

	mnt = nfs_follow_referral(dentry, fs_locations);
out_free:
	__free_page(page);
	kfree(fs_locations);
out:
	dprintk("%s: done\n", __func__);
	return mnt;
}

struct vfsmount *nfs4_submount(struct nfs_server *server, struct dentry *dentry,
			       struct nfs_fh *fh, struct nfs_fattr *fattr)
{
	rpc_authflavor_t flavor = server->client->cl_auth->au_flavor;
	struct dentry *parent = dget_parent(dentry);
	struct inode *dir = parent->d_inode;
	struct qstr *name = &dentry->d_name;
	struct rpc_clnt *client;
	struct vfsmount *mnt;

	/* Look it up again to get its attributes and sec flavor */
	client = nfs4_proc_lookup_mountpoint(dir, name, fh, fattr);
	dput(parent);
	if (IS_ERR(client))
		return ERR_CAST(client);

	if (fattr->valid & NFS_ATTR_FATTR_V4_REFERRAL) {
		mnt = nfs_do_refmount(client, dentry);
		goto out;
	}

	if (client->cl_auth->au_flavor != flavor)
		flavor = client->cl_auth->au_flavor;
	mnt = nfs_do_submount(dentry, fh, fattr, flavor);
out:
	rpc_shutdown_client(client);
	return mnt;
}

/*
 * Try one location from the fs_locations array.
 *
 * Returns zero on success, or a negative errno value.
 */
static int nfs4_try_replacing_one_location(struct nfs_server *server,
		char *page, char *page2,
		const struct nfs4_fs_location *location)
{
	const size_t addr_bufsize = sizeof(struct sockaddr_storage);
	struct net *net = rpc_net_ns(server->client);
	struct sockaddr *sap;
	unsigned int s;
	size_t salen;
	int error;

	sap = kmalloc(addr_bufsize, GFP_KERNEL);
	if (sap == NULL)
		return -ENOMEM;

	error = -ENOENT;
	for (s = 0; s < location->nservers; s++) {
		const struct nfs4_string *buf = &location->servers[s];
		char *hostname;

		if (buf->len <= 0 || buf->len > PAGE_SIZE)
			continue;

		if (memchr(buf->data, IPV6_SCOPE_DELIMITER, buf->len) != NULL)
			continue;

		salen = nfs_parse_server_name(buf->data, buf->len,
						sap, addr_bufsize, net);
		if (salen == 0)
			continue;
		rpc_set_port(sap, NFS_PORT);

		error = -ENOMEM;
		hostname = kstrndup(buf->data, buf->len, GFP_KERNEL);
		if (hostname == NULL)
			break;

		error = nfs4_update_server(server, hostname, sap, salen, net);
		kfree(hostname);
		if (error == 0)
			break;
	}

	kfree(sap);
	return error;
}

/**
 * nfs4_replace_transport - set up transport to destination server
 *
 * @server: export being migrated
 * @locations: fs_locations array
 *
 * Returns zero on success, or a negative errno value.
 *
 * The client tries all the entries in the "locations" array, in the
 * order returned by the server, until one works or the end of the
 * array is reached.
 */
int nfs4_replace_transport(struct nfs_server *server,
			   const struct nfs4_fs_locations *locations)
{
	char *page = NULL, *page2 = NULL;
	int loc, error;

	error = -ENOENT;
	if (locations == NULL || locations->nlocations <= 0)
		goto out;

	error = -ENOMEM;
	page = (char *) __get_free_page(GFP_USER);
	if (!page)
		goto out;
	page2 = (char *) __get_free_page(GFP_USER);
	if (!page2)
		goto out;

	for (loc = 0; loc < locations->nlocations; loc++) {
		const struct nfs4_fs_location *location =
						&locations->locations[loc];

		if (location == NULL || location->nservers <= 0 ||
		    location->rootpath.ncomponents == 0)
			continue;

		error = nfs4_try_replacing_one_location(server, page,
							page2, location);
		if (error == 0)
			break;
	}

out:
	free_page((unsigned long)page);
	free_page((unsigned long)page2);
	return error;
}
