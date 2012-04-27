/*
 *  Device operations for the pnfs nfs4 file layout driver.
 *
 *  Copyright (c) 2002
 *  The Regents of the University of Michigan
 *  All Rights Reserved
 *
 *  Dean Hildebrand <dhildebz@umich.edu>
 *  Garth Goodson   <Garth.Goodson@netapp.com>
 *
 *  Permission is granted to use, copy, create derivative works, and
 *  redistribute this software and such derivative works for any purpose,
 *  so long as the name of the University of Michigan is not used in
 *  any advertising or publicity pertaining to the use or distribution
 *  of this software without specific, written prior authorization. If
 *  the above copyright notice or any other identification of the
 *  University of Michigan is included in any copy of any portion of
 *  this software, then the disclaimer below must also be included.
 *
 *  This software is provided as is, without representation or warranty
 *  of any kind either express or implied, including without limitation
 *  the implied warranties of merchantability, fitness for a particular
 *  purpose, or noninfringement.  The Regents of the University of
 *  Michigan shall not be liable for any damages, including special,
 *  indirect, incidental, or consequential damages, with respect to any
 *  claim arising out of or in connection with the use of the software,
 *  even if it has been or is hereafter advised of the possibility of
 *  such damages.
 */

#include <linux/nfs_fs.h>
#include <linux/vmalloc.h>
#include <linux/module.h>

#include "internal.h"
#include "nfs4filelayout.h"

#define NFSDBG_FACILITY		NFSDBG_PNFS_LD

static unsigned int dataserver_timeo = NFS4_DEF_DS_TIMEO;
static unsigned int dataserver_retrans = NFS4_DEF_DS_RETRANS;

/*
 * Data server cache
 *
 * Data servers can be mapped to different device ids.
 * nfs4_pnfs_ds reference counting
 *   - set to 1 on allocation
 *   - incremented when a device id maps a data server already in the cache.
 *   - decremented when deviceid is removed from the cache.
 */
static DEFINE_SPINLOCK(nfs4_ds_cache_lock);
static LIST_HEAD(nfs4_data_server_cache);

/* Debug routines */
void
print_ds(struct nfs4_pnfs_ds *ds)
{
	if (ds == NULL) {
		printk("%s NULL device\n", __func__);
		return;
	}
	printk("        ds %s\n"
		"        ref count %d\n"
		"        client %p\n"
		"        cl_exchange_flags %x\n",
		ds->ds_remotestr,
		atomic_read(&ds->ds_count), ds->ds_clp,
		ds->ds_clp ? ds->ds_clp->cl_exchange_flags : 0);
}

static bool
same_sockaddr(struct sockaddr *addr1, struct sockaddr *addr2)
{
	struct sockaddr_in *a, *b;
	struct sockaddr_in6 *a6, *b6;

	if (addr1->sa_family != addr2->sa_family)
		return false;

	switch (addr1->sa_family) {
	case AF_INET:
		a = (struct sockaddr_in *)addr1;
		b = (struct sockaddr_in *)addr2;

		if (a->sin_addr.s_addr == b->sin_addr.s_addr &&
		    a->sin_port == b->sin_port)
			return true;
		break;

	case AF_INET6:
		a6 = (struct sockaddr_in6 *)addr1;
		b6 = (struct sockaddr_in6 *)addr2;

		/* LINKLOCAL addresses must have matching scope_id */
		if (ipv6_addr_scope(&a6->sin6_addr) ==
		    IPV6_ADDR_SCOPE_LINKLOCAL &&
		    a6->sin6_scope_id != b6->sin6_scope_id)
			return false;

		if (ipv6_addr_equal(&a6->sin6_addr, &b6->sin6_addr) &&
		    a6->sin6_port == b6->sin6_port)
			return true;
		break;

	default:
		dprintk("%s: unhandled address family: %u\n",
			__func__, addr1->sa_family);
		return false;
	}

	return false;
}

static bool
_same_data_server_addrs_locked(const struct list_head *dsaddrs1,
			       const struct list_head *dsaddrs2)
{
	struct nfs4_pnfs_ds_addr *da1, *da2;

	/* step through both lists, comparing as we go */
	for (da1 = list_first_entry(dsaddrs1, typeof(*da1), da_node),
	     da2 = list_first_entry(dsaddrs2, typeof(*da2), da_node);
	     da1 != NULL && da2 != NULL;
	     da1 = list_entry(da1->da_node.next, typeof(*da1), da_node),
	     da2 = list_entry(da2->da_node.next, typeof(*da2), da_node)) {
		if (!same_sockaddr((struct sockaddr *)&da1->da_addr,
				   (struct sockaddr *)&da2->da_addr))
			return false;
	}
	if (da1 == NULL && da2 == NULL)
		return true;

	return false;
}

/*
 * Lookup DS by addresses.  nfs4_ds_cache_lock is held
 */
static struct nfs4_pnfs_ds *
_data_server_lookup_locked(const struct list_head *dsaddrs)
{
	struct nfs4_pnfs_ds *ds;

	list_for_each_entry(ds, &nfs4_data_server_cache, ds_node)
		if (_same_data_server_addrs_locked(&ds->ds_addrs, dsaddrs))
			return ds;
	return NULL;
}

/*
 * Lookup DS by nfs_client pointer. Zero data server client pointer
 */
void nfs4_ds_disconnect(struct nfs_client *clp)
{
	struct nfs4_pnfs_ds *ds;
	struct nfs_client *found = NULL;

	dprintk("%s clp %p\n", __func__, clp);
	spin_lock(&nfs4_ds_cache_lock);
	list_for_each_entry(ds, &nfs4_data_server_cache, ds_node)
		if (ds->ds_clp && ds->ds_clp == clp) {
			found = ds->ds_clp;
			ds->ds_clp = NULL;
		}
	spin_unlock(&nfs4_ds_cache_lock);
	if (found) {
		set_bit(NFS_CS_STOP_RENEW, &clp->cl_res_state);
		nfs_put_client(clp);
	}
}

/*
 * Create an rpc connection to the nfs4_pnfs_ds data server
 * Currently only supports IPv4 and IPv6 addresses
 */
static int
nfs4_ds_connect(struct nfs_server *mds_srv, struct nfs4_pnfs_ds *ds)
{
	struct nfs_client *clp = ERR_PTR(-EIO);
	struct nfs4_pnfs_ds_addr *da;
	int status = 0;

	dprintk("--> %s DS %s au_flavor %d\n", __func__, ds->ds_remotestr,
		mds_srv->nfs_client->cl_rpcclient->cl_auth->au_flavor);

	BUG_ON(list_empty(&ds->ds_addrs));

	list_for_each_entry(da, &ds->ds_addrs, da_node) {
		dprintk("%s: DS %s: trying address %s\n",
			__func__, ds->ds_remotestr, da->da_remotestr);

		clp = nfs4_set_ds_client(mds_srv->nfs_client,
					(struct sockaddr *)&da->da_addr,
					da->da_addrlen, IPPROTO_TCP,
					dataserver_timeo, dataserver_retrans);
		if (!IS_ERR(clp))
			break;
	}

	if (IS_ERR(clp)) {
		status = PTR_ERR(clp);
		goto out;
	}

	if ((clp->cl_exchange_flags & EXCHGID4_FLAG_MASK_PNFS) != 0) {
		if (!is_ds_client(clp)) {
			status = -ENODEV;
			goto out_put;
		}
		ds->ds_clp = clp;
		dprintk("%s [existing] server=%s\n", __func__,
			ds->ds_remotestr);
		goto out;
	}

	/*
	 * Do not set NFS_CS_CHECK_LEASE_TIME instead set the DS lease to
	 * be equal to the MDS lease. Renewal is scheduled in create_session.
	 */
	spin_lock(&mds_srv->nfs_client->cl_lock);
	clp->cl_lease_time = mds_srv->nfs_client->cl_lease_time;
	spin_unlock(&mds_srv->nfs_client->cl_lock);
	clp->cl_last_renewal = jiffies;

	/* New nfs_client */
	status = nfs4_init_ds_session(clp);
	if (status)
		goto out_put;

	ds->ds_clp = clp;
	dprintk("%s [new] addr: %s\n", __func__, ds->ds_remotestr);
out:
	return status;
out_put:
	nfs_put_client(clp);
	goto out;
}

static void
destroy_ds(struct nfs4_pnfs_ds *ds)
{
	struct nfs4_pnfs_ds_addr *da;

	dprintk("--> %s\n", __func__);
	ifdebug(FACILITY)
		print_ds(ds);

	if (ds->ds_clp)
		nfs_put_client(ds->ds_clp);

	while (!list_empty(&ds->ds_addrs)) {
		da = list_first_entry(&ds->ds_addrs,
				      struct nfs4_pnfs_ds_addr,
				      da_node);
		list_del_init(&da->da_node);
		kfree(da->da_remotestr);
		kfree(da);
	}

	kfree(ds->ds_remotestr);
	kfree(ds);
}

void
nfs4_fl_free_deviceid(struct nfs4_file_layout_dsaddr *dsaddr)
{
	struct nfs4_pnfs_ds *ds;
	int i;

	nfs4_print_deviceid(&dsaddr->id_node.deviceid);

	for (i = 0; i < dsaddr->ds_num; i++) {
		ds = dsaddr->ds_list[i];
		if (ds != NULL) {
			if (atomic_dec_and_lock(&ds->ds_count,
						&nfs4_ds_cache_lock)) {
				list_del_init(&ds->ds_node);
				spin_unlock(&nfs4_ds_cache_lock);
				destroy_ds(ds);
			}
		}
	}
	kfree(dsaddr->stripe_indices);
	kfree(dsaddr);
}

/*
 * Create a string with a human readable address and port to avoid
 * complicated setup around many dprinks.
 */
static char *
nfs4_pnfs_remotestr(struct list_head *dsaddrs, gfp_t gfp_flags)
{
	struct nfs4_pnfs_ds_addr *da;
	char *remotestr;
	size_t len;
	char *p;

	len = 3;        /* '{', '}' and eol */
	list_for_each_entry(da, dsaddrs, da_node) {
		len += strlen(da->da_remotestr) + 1;    /* string plus comma */
	}

	remotestr = kzalloc(len, gfp_flags);
	if (!remotestr)
		return NULL;

	p = remotestr;
	*(p++) = '{';
	len--;
	list_for_each_entry(da, dsaddrs, da_node) {
		size_t ll = strlen(da->da_remotestr);

		if (ll > len)
			goto out_err;

		memcpy(p, da->da_remotestr, ll);
		p += ll;
		len -= ll;

		if (len < 1)
			goto out_err;
		(*p++) = ',';
		len--;
	}
	if (len < 2)
		goto out_err;
	*(p++) = '}';
	*p = '\0';
	return remotestr;
out_err:
	kfree(remotestr);
	return NULL;
}

static struct nfs4_pnfs_ds *
nfs4_pnfs_ds_add(struct list_head *dsaddrs, gfp_t gfp_flags)
{
	struct nfs4_pnfs_ds *tmp_ds, *ds = NULL;
	char *remotestr;

	if (list_empty(dsaddrs)) {
		dprintk("%s: no addresses defined\n", __func__);
		goto out;
	}

	ds = kzalloc(sizeof(*ds), gfp_flags);
	if (!ds)
		goto out;

	/* this is only used for debugging, so it's ok if its NULL */
	remotestr = nfs4_pnfs_remotestr(dsaddrs, gfp_flags);

	spin_lock(&nfs4_ds_cache_lock);
	tmp_ds = _data_server_lookup_locked(dsaddrs);
	if (tmp_ds == NULL) {
		INIT_LIST_HEAD(&ds->ds_addrs);
		list_splice_init(dsaddrs, &ds->ds_addrs);
		ds->ds_remotestr = remotestr;
		atomic_set(&ds->ds_count, 1);
		INIT_LIST_HEAD(&ds->ds_node);
		ds->ds_clp = NULL;
		list_add(&ds->ds_node, &nfs4_data_server_cache);
		dprintk("%s add new data server %s\n", __func__,
			ds->ds_remotestr);
	} else {
		kfree(remotestr);
		kfree(ds);
		atomic_inc(&tmp_ds->ds_count);
		dprintk("%s data server %s found, inc'ed ds_count to %d\n",
			__func__, tmp_ds->ds_remotestr,
			atomic_read(&tmp_ds->ds_count));
		ds = tmp_ds;
	}
	spin_unlock(&nfs4_ds_cache_lock);
out:
	return ds;
}

/*
 * Currently only supports ipv4, ipv6 and one multi-path address.
 */
static struct nfs4_pnfs_ds_addr *
decode_ds_addr(struct net *net, struct xdr_stream *streamp, gfp_t gfp_flags)
{
	struct nfs4_pnfs_ds_addr *da = NULL;
	char *buf, *portstr;
	__be16 port;
	int nlen, rlen;
	int tmp[2];
	__be32 *p;
	char *netid, *match_netid;
	size_t len, match_netid_len;
	char *startsep = "";
	char *endsep = "";


	/* r_netid */
	p = xdr_inline_decode(streamp, 4);
	if (unlikely(!p))
		goto out_err;
	nlen = be32_to_cpup(p++);

	p = xdr_inline_decode(streamp, nlen);
	if (unlikely(!p))
		goto out_err;

	netid = kmalloc(nlen+1, gfp_flags);
	if (unlikely(!netid))
		goto out_err;

	netid[nlen] = '\0';
	memcpy(netid, p, nlen);

	/* r_addr: ip/ip6addr with port in dec octets - see RFC 5665 */
	p = xdr_inline_decode(streamp, 4);
	if (unlikely(!p))
		goto out_free_netid;
	rlen = be32_to_cpup(p);

	p = xdr_inline_decode(streamp, rlen);
	if (unlikely(!p))
		goto out_free_netid;

	/* port is ".ABC.DEF", 8 chars max */
	if (rlen > INET6_ADDRSTRLEN + IPV6_SCOPE_ID_LEN + 8) {
		dprintk("%s: Invalid address, length %d\n", __func__,
			rlen);
		goto out_free_netid;
	}
	buf = kmalloc(rlen + 1, gfp_flags);
	if (!buf) {
		dprintk("%s: Not enough memory\n", __func__);
		goto out_free_netid;
	}
	buf[rlen] = '\0';
	memcpy(buf, p, rlen);

	/* replace port '.' with '-' */
	portstr = strrchr(buf, '.');
	if (!portstr) {
		dprintk("%s: Failed finding expected dot in port\n",
			__func__);
		goto out_free_buf;
	}
	*portstr = '-';

	/* find '.' between address and port */
	portstr = strrchr(buf, '.');
	if (!portstr) {
		dprintk("%s: Failed finding expected dot between address and "
			"port\n", __func__);
		goto out_free_buf;
	}
	*portstr = '\0';

	da = kzalloc(sizeof(*da), gfp_flags);
	if (unlikely(!da))
		goto out_free_buf;

	INIT_LIST_HEAD(&da->da_node);

	if (!rpc_pton(net, buf, portstr-buf, (struct sockaddr *)&da->da_addr,
		      sizeof(da->da_addr))) {
		dprintk("%s: error parsing address %s\n", __func__, buf);
		goto out_free_da;
	}

	portstr++;
	sscanf(portstr, "%d-%d", &tmp[0], &tmp[1]);
	port = htons((tmp[0] << 8) | (tmp[1]));

	switch (da->da_addr.ss_family) {
	case AF_INET:
		((struct sockaddr_in *)&da->da_addr)->sin_port = port;
		da->da_addrlen = sizeof(struct sockaddr_in);
		match_netid = "tcp";
		match_netid_len = 3;
		break;

	case AF_INET6:
		((struct sockaddr_in6 *)&da->da_addr)->sin6_port = port;
		da->da_addrlen = sizeof(struct sockaddr_in6);
		match_netid = "tcp6";
		match_netid_len = 4;
		startsep = "[";
		endsep = "]";
		break;

	default:
		dprintk("%s: unsupported address family: %u\n",
			__func__, da->da_addr.ss_family);
		goto out_free_da;
	}

	if (nlen != match_netid_len || strncmp(netid, match_netid, nlen)) {
		dprintk("%s: ERROR: r_netid \"%s\" != \"%s\"\n",
			__func__, netid, match_netid);
		goto out_free_da;
	}

	/* save human readable address */
	len = strlen(startsep) + strlen(buf) + strlen(endsep) + 7;
	da->da_remotestr = kzalloc(len, gfp_flags);

	/* NULL is ok, only used for dprintk */
	if (da->da_remotestr)
		snprintf(da->da_remotestr, len, "%s%s%s:%u", startsep,
			 buf, endsep, ntohs(port));

	dprintk("%s: Parsed DS addr %s\n", __func__, da->da_remotestr);
	kfree(buf);
	kfree(netid);
	return da;

out_free_da:
	kfree(da);
out_free_buf:
	dprintk("%s: Error parsing DS addr: %s\n", __func__, buf);
	kfree(buf);
out_free_netid:
	kfree(netid);
out_err:
	return NULL;
}

/* Decode opaque device data and return the result */
static struct nfs4_file_layout_dsaddr*
decode_device(struct inode *ino, struct pnfs_device *pdev, gfp_t gfp_flags)
{
	int i;
	u32 cnt, num;
	u8 *indexp;
	__be32 *p;
	u8 *stripe_indices;
	u8 max_stripe_index;
	struct nfs4_file_layout_dsaddr *dsaddr = NULL;
	struct xdr_stream stream;
	struct xdr_buf buf;
	struct page *scratch;
	struct list_head dsaddrs;
	struct nfs4_pnfs_ds_addr *da;

	/* set up xdr stream */
	scratch = alloc_page(gfp_flags);
	if (!scratch)
		goto out_err;

	xdr_init_decode_pages(&stream, &buf, pdev->pages, pdev->pglen);
	xdr_set_scratch_buffer(&stream, page_address(scratch), PAGE_SIZE);

	/* Get the stripe count (number of stripe index) */
	p = xdr_inline_decode(&stream, 4);
	if (unlikely(!p))
		goto out_err_free_scratch;

	cnt = be32_to_cpup(p);
	dprintk("%s stripe count  %d\n", __func__, cnt);
	if (cnt > NFS4_PNFS_MAX_STRIPE_CNT) {
		printk(KERN_WARNING "NFS: %s: stripe count %d greater than "
		       "supported maximum %d\n", __func__,
			cnt, NFS4_PNFS_MAX_STRIPE_CNT);
		goto out_err_free_scratch;
	}

	/* read stripe indices */
	stripe_indices = kcalloc(cnt, sizeof(u8), gfp_flags);
	if (!stripe_indices)
		goto out_err_free_scratch;

	p = xdr_inline_decode(&stream, cnt << 2);
	if (unlikely(!p))
		goto out_err_free_stripe_indices;

	indexp = &stripe_indices[0];
	max_stripe_index = 0;
	for (i = 0; i < cnt; i++) {
		*indexp = be32_to_cpup(p++);
		max_stripe_index = max(max_stripe_index, *indexp);
		indexp++;
	}

	/* Check the multipath list count */
	p = xdr_inline_decode(&stream, 4);
	if (unlikely(!p))
		goto out_err_free_stripe_indices;

	num = be32_to_cpup(p);
	dprintk("%s ds_num %u\n", __func__, num);
	if (num > NFS4_PNFS_MAX_MULTI_CNT) {
		printk(KERN_WARNING "NFS: %s: multipath count %d greater than "
			"supported maximum %d\n", __func__,
			num, NFS4_PNFS_MAX_MULTI_CNT);
		goto out_err_free_stripe_indices;
	}

	/* validate stripe indices are all < num */
	if (max_stripe_index >= num) {
		printk(KERN_WARNING "NFS: %s: stripe index %u >= num ds %u\n",
			__func__, max_stripe_index, num);
		goto out_err_free_stripe_indices;
	}

	dsaddr = kzalloc(sizeof(*dsaddr) +
			(sizeof(struct nfs4_pnfs_ds *) * (num - 1)),
			gfp_flags);
	if (!dsaddr)
		goto out_err_free_stripe_indices;

	dsaddr->stripe_count = cnt;
	dsaddr->stripe_indices = stripe_indices;
	stripe_indices = NULL;
	dsaddr->ds_num = num;
	nfs4_init_deviceid_node(&dsaddr->id_node,
				NFS_SERVER(ino)->pnfs_curr_ld,
				NFS_SERVER(ino)->nfs_client,
				&pdev->dev_id);

	INIT_LIST_HEAD(&dsaddrs);

	for (i = 0; i < dsaddr->ds_num; i++) {
		int j;
		u32 mp_count;

		p = xdr_inline_decode(&stream, 4);
		if (unlikely(!p))
			goto out_err_free_deviceid;

		mp_count = be32_to_cpup(p); /* multipath count */
		for (j = 0; j < mp_count; j++) {
			da = decode_ds_addr(NFS_SERVER(ino)->nfs_client->net,
					    &stream, gfp_flags);
			if (da)
				list_add_tail(&da->da_node, &dsaddrs);
		}
		if (list_empty(&dsaddrs)) {
			dprintk("%s: no suitable DS addresses found\n",
				__func__);
			goto out_err_free_deviceid;
		}

		dsaddr->ds_list[i] = nfs4_pnfs_ds_add(&dsaddrs, gfp_flags);
		if (!dsaddr->ds_list[i])
			goto out_err_drain_dsaddrs;

		/* If DS was already in cache, free ds addrs */
		while (!list_empty(&dsaddrs)) {
			da = list_first_entry(&dsaddrs,
					      struct nfs4_pnfs_ds_addr,
					      da_node);
			list_del_init(&da->da_node);
			kfree(da->da_remotestr);
			kfree(da);
		}
	}

	__free_page(scratch);
	return dsaddr;

out_err_drain_dsaddrs:
	while (!list_empty(&dsaddrs)) {
		da = list_first_entry(&dsaddrs, struct nfs4_pnfs_ds_addr,
				      da_node);
		list_del_init(&da->da_node);
		kfree(da->da_remotestr);
		kfree(da);
	}
out_err_free_deviceid:
	nfs4_fl_free_deviceid(dsaddr);
	/* stripe_indicies was part of dsaddr */
	goto out_err_free_scratch;
out_err_free_stripe_indices:
	kfree(stripe_indices);
out_err_free_scratch:
	__free_page(scratch);
out_err:
	dprintk("%s ERROR: returning NULL\n", __func__);
	return NULL;
}

/*
 * Decode the opaque device specified in 'dev' and add it to the cache of
 * available devices.
 */
static struct nfs4_file_layout_dsaddr *
decode_and_add_device(struct inode *inode, struct pnfs_device *dev, gfp_t gfp_flags)
{
	struct nfs4_deviceid_node *d;
	struct nfs4_file_layout_dsaddr *n, *new;

	new = decode_device(inode, dev, gfp_flags);
	if (!new) {
		printk(KERN_WARNING "NFS: %s: Could not decode or add device\n",
			__func__);
		return NULL;
	}

	d = nfs4_insert_deviceid_node(&new->id_node);
	n = container_of(d, struct nfs4_file_layout_dsaddr, id_node);
	if (n != new) {
		nfs4_fl_free_deviceid(new);
		return n;
	}

	return new;
}

/*
 * Retrieve the information for dev_id, add it to the list
 * of available devices, and return it.
 */
struct nfs4_file_layout_dsaddr *
get_device_info(struct inode *inode, struct nfs4_deviceid *dev_id, gfp_t gfp_flags)
{
	struct pnfs_device *pdev = NULL;
	u32 max_resp_sz;
	int max_pages;
	struct page **pages = NULL;
	struct nfs4_file_layout_dsaddr *dsaddr = NULL;
	int rc, i;
	struct nfs_server *server = NFS_SERVER(inode);

	/*
	 * Use the session max response size as the basis for setting
	 * GETDEVICEINFO's maxcount
	 */
	max_resp_sz = server->nfs_client->cl_session->fc_attrs.max_resp_sz;
	max_pages = nfs_page_array_len(0, max_resp_sz);
	dprintk("%s inode %p max_resp_sz %u max_pages %d\n",
		__func__, inode, max_resp_sz, max_pages);

	pdev = kzalloc(sizeof(struct pnfs_device), gfp_flags);
	if (pdev == NULL)
		return NULL;

	pages = kzalloc(max_pages * sizeof(struct page *), gfp_flags);
	if (pages == NULL) {
		kfree(pdev);
		return NULL;
	}
	for (i = 0; i < max_pages; i++) {
		pages[i] = alloc_page(gfp_flags);
		if (!pages[i])
			goto out_free;
	}

	memcpy(&pdev->dev_id, dev_id, sizeof(*dev_id));
	pdev->layout_type = LAYOUT_NFSV4_1_FILES;
	pdev->pages = pages;
	pdev->pgbase = 0;
	pdev->pglen = PAGE_SIZE * max_pages;
	pdev->mincount = 0;

	rc = nfs4_proc_getdeviceinfo(server, pdev);
	dprintk("%s getdevice info returns %d\n", __func__, rc);
	if (rc)
		goto out_free;

	/*
	 * Found new device, need to decode it and then add it to the
	 * list of known devices for this mountpoint.
	 */
	dsaddr = decode_and_add_device(inode, pdev, gfp_flags);
out_free:
	for (i = 0; i < max_pages; i++)
		__free_page(pages[i]);
	kfree(pages);
	kfree(pdev);
	dprintk("<-- %s dsaddr %p\n", __func__, dsaddr);
	return dsaddr;
}

void
nfs4_fl_put_deviceid(struct nfs4_file_layout_dsaddr *dsaddr)
{
	nfs4_put_deviceid_node(&dsaddr->id_node);
}

/*
 * Want res = (offset - layout->pattern_offset)/ layout->stripe_unit
 * Then: ((res + fsi) % dsaddr->stripe_count)
 */
u32
nfs4_fl_calc_j_index(struct pnfs_layout_segment *lseg, loff_t offset)
{
	struct nfs4_filelayout_segment *flseg = FILELAYOUT_LSEG(lseg);
	u64 tmp;

	tmp = offset - flseg->pattern_offset;
	do_div(tmp, flseg->stripe_unit);
	tmp += flseg->first_stripe_index;
	return do_div(tmp, flseg->dsaddr->stripe_count);
}

u32
nfs4_fl_calc_ds_index(struct pnfs_layout_segment *lseg, u32 j)
{
	return FILELAYOUT_LSEG(lseg)->dsaddr->stripe_indices[j];
}

struct nfs_fh *
nfs4_fl_select_ds_fh(struct pnfs_layout_segment *lseg, u32 j)
{
	struct nfs4_filelayout_segment *flseg = FILELAYOUT_LSEG(lseg);
	u32 i;

	if (flseg->stripe_type == STRIPE_SPARSE) {
		if (flseg->num_fh == 1)
			i = 0;
		else if (flseg->num_fh == 0)
			/* Use the MDS OPEN fh set in nfs_read_rpcsetup */
			return NULL;
		else
			i = nfs4_fl_calc_ds_index(lseg, j);
	} else
		i = j;
	return flseg->fh_array[i];
}

struct nfs4_pnfs_ds *
nfs4_fl_prepare_ds(struct pnfs_layout_segment *lseg, u32 ds_idx)
{
	struct nfs4_file_layout_dsaddr *dsaddr = FILELAYOUT_LSEG(lseg)->dsaddr;
	struct nfs4_pnfs_ds *ds = dsaddr->ds_list[ds_idx];
	struct nfs4_deviceid_node *devid = FILELAYOUT_DEVID_NODE(lseg);

	if (filelayout_test_devid_invalid(devid))
		return NULL;

	if (ds == NULL) {
		printk(KERN_ERR "NFS: %s: No data server for offset index %d\n",
			__func__, ds_idx);
		goto mark_dev_invalid;
	}

	if (!ds->ds_clp) {
		struct nfs_server *s = NFS_SERVER(lseg->pls_layout->plh_inode);
		int err;

		err = nfs4_ds_connect(s, ds);
		if (err)
			goto mark_dev_invalid;
	}
	return ds;

mark_dev_invalid:
	filelayout_mark_devid_invalid(devid);
	return NULL;
}

module_param(dataserver_retrans, uint, 0644);
MODULE_PARM_DESC(dataserver_retrans, "The  number of times the NFSv4.1 client "
			"retries a request before it attempts further "
			" recovery  action.");
module_param(dataserver_timeo, uint, 0644);
MODULE_PARM_DESC(dataserver_timeo, "The time (in tenths of a second) the "
			"NFSv4.1  client  waits for a response from a "
			" data server before it retries an NFS request.");
