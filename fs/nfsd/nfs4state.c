/*
*  linux/fs/nfsd/nfs4state.c
*
*  Copyright (c) 2001 The Regents of the University of Michigan.
*  All rights reserved.
*
*  Kendrick Smith <kmsmith@umich.edu>
*  Andy Adamson <kandros@umich.edu>
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*  1. Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*  2. Redistributions in binary form must reproduce the above copyright
*     notice, this list of conditions and the following disclaimer in the
*     documentation and/or other materials provided with the distribution.
*  3. Neither the name of the University nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
*  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
*  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
*  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
*  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
*  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
*  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
*  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
*  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
*  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
*  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
*/

#include <linux/param.h>
#include <linux/major.h>
#include <linux/slab.h>

#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/cache.h>
#include <linux/mount.h>
#include <linux/workqueue.h>
#include <linux/smp_lock.h>
#include <linux/kthread.h>
#include <linux/nfs4.h>
#include <linux/nfsd/state.h>
#include <linux/nfsd/xdr4.h>
#include <linux/namei.h>

#define NFSDDBG_FACILITY                NFSDDBG_PROC

/* Globals */
static time_t lease_time = 90;     /* default lease time */
static time_t user_lease_time = 90;
static time_t boot_time;
static int in_grace = 1;
static u32 current_clientid = 1;
static u32 current_ownerid = 1;
static u32 current_fileid = 1;
static u32 current_delegid = 1;
static u32 nfs4_init;
static stateid_t zerostateid;             /* bits all 0 */
static stateid_t onestateid;              /* bits all 1 */

#define ZERO_STATEID(stateid) (!memcmp((stateid), &zerostateid, sizeof(stateid_t)))
#define ONE_STATEID(stateid)  (!memcmp((stateid), &onestateid, sizeof(stateid_t)))

/* forward declarations */
static struct nfs4_stateid * find_stateid(stateid_t *stid, int flags);
static struct nfs4_delegation * find_delegation_stateid(struct inode *ino, stateid_t *stid);
static void release_stateid_lockowners(struct nfs4_stateid *open_stp);
static char user_recovery_dirname[PATH_MAX] = "/var/lib/nfs/v4recovery";
static void nfs4_set_recdir(char *recdir);

/* Locking:
 *
 * client_sema: 
 * 	protects clientid_hashtbl[], clientstr_hashtbl[],
 * 	unconfstr_hashtbl[], uncofid_hashtbl[].
 */
static DECLARE_MUTEX(client_sema);

static kmem_cache_t *stateowner_slab = NULL;
static kmem_cache_t *file_slab = NULL;
static kmem_cache_t *stateid_slab = NULL;
static kmem_cache_t *deleg_slab = NULL;

void
nfs4_lock_state(void)
{
	down(&client_sema);
}

void
nfs4_unlock_state(void)
{
	up(&client_sema);
}

static inline u32
opaque_hashval(const void *ptr, int nbytes)
{
	unsigned char *cptr = (unsigned char *) ptr;

	u32 x = 0;
	while (nbytes--) {
		x *= 37;
		x += *cptr++;
	}
	return x;
}

/* forward declarations */
static void release_stateowner(struct nfs4_stateowner *sop);
static void release_stateid(struct nfs4_stateid *stp, int flags);

/*
 * Delegation state
 */

/* recall_lock protects the del_recall_lru */
static spinlock_t recall_lock = SPIN_LOCK_UNLOCKED;
static struct list_head del_recall_lru;

static void
free_nfs4_file(struct kref *kref)
{
	struct nfs4_file *fp = container_of(kref, struct nfs4_file, fi_ref);
	list_del(&fp->fi_hash);
	iput(fp->fi_inode);
	kmem_cache_free(file_slab, fp);
}

static inline void
put_nfs4_file(struct nfs4_file *fi)
{
	kref_put(&fi->fi_ref, free_nfs4_file);
}

static inline void
get_nfs4_file(struct nfs4_file *fi)
{
	kref_get(&fi->fi_ref);
}

static struct nfs4_delegation *
alloc_init_deleg(struct nfs4_client *clp, struct nfs4_stateid *stp, struct svc_fh *current_fh, u32 type)
{
	struct nfs4_delegation *dp;
	struct nfs4_file *fp = stp->st_file;
	struct nfs4_callback *cb = &stp->st_stateowner->so_client->cl_callback;

	dprintk("NFSD alloc_init_deleg\n");
	dp = kmem_cache_alloc(deleg_slab, GFP_KERNEL);
	if (dp == NULL)
		return dp;
	INIT_LIST_HEAD(&dp->dl_perfile);
	INIT_LIST_HEAD(&dp->dl_perclnt);
	INIT_LIST_HEAD(&dp->dl_recall_lru);
	dp->dl_client = clp;
	get_nfs4_file(fp);
	dp->dl_file = fp;
	dp->dl_flock = NULL;
	get_file(stp->st_vfs_file);
	dp->dl_vfs_file = stp->st_vfs_file;
	dp->dl_type = type;
	dp->dl_recall.cbr_dp = NULL;
	dp->dl_recall.cbr_ident = cb->cb_ident;
	dp->dl_recall.cbr_trunc = 0;
	dp->dl_stateid.si_boot = boot_time;
	dp->dl_stateid.si_stateownerid = current_delegid++;
	dp->dl_stateid.si_fileid = 0;
	dp->dl_stateid.si_generation = 0;
	dp->dl_fhlen = current_fh->fh_handle.fh_size;
	memcpy(dp->dl_fhval, &current_fh->fh_handle.fh_base,
		        current_fh->fh_handle.fh_size);
	dp->dl_time = 0;
	atomic_set(&dp->dl_count, 1);
	list_add(&dp->dl_perfile, &fp->fi_delegations);
	list_add(&dp->dl_perclnt, &clp->cl_delegations);
	return dp;
}

void
nfs4_put_delegation(struct nfs4_delegation *dp)
{
	if (atomic_dec_and_test(&dp->dl_count)) {
		dprintk("NFSD: freeing dp %p\n",dp);
		put_nfs4_file(dp->dl_file);
		kmem_cache_free(deleg_slab, dp);
	}
}

/* Remove the associated file_lock first, then remove the delegation.
 * lease_modify() is called to remove the FS_LEASE file_lock from
 * the i_flock list, eventually calling nfsd's lock_manager
 * fl_release_callback.
 */
static void
nfs4_close_delegation(struct nfs4_delegation *dp)
{
	struct file *filp = dp->dl_vfs_file;

	dprintk("NFSD: close_delegation dp %p\n",dp);
	dp->dl_vfs_file = NULL;
	/* The following nfsd_close may not actually close the file,
	 * but we want to remove the lease in any case. */
	if (dp->dl_flock)
		setlease(filp, F_UNLCK, &dp->dl_flock);
	nfsd_close(filp);
}

/* Called under the state lock. */
static void
unhash_delegation(struct nfs4_delegation *dp)
{
	list_del_init(&dp->dl_perfile);
	list_del_init(&dp->dl_perclnt);
	spin_lock(&recall_lock);
	list_del_init(&dp->dl_recall_lru);
	spin_unlock(&recall_lock);
	nfs4_close_delegation(dp);
	nfs4_put_delegation(dp);
}

/* 
 * SETCLIENTID state 
 */

/* Hash tables for nfs4_clientid state */
#define CLIENT_HASH_BITS                 4
#define CLIENT_HASH_SIZE                (1 << CLIENT_HASH_BITS)
#define CLIENT_HASH_MASK                (CLIENT_HASH_SIZE - 1)

#define clientid_hashval(id) \
	((id) & CLIENT_HASH_MASK)
#define clientstr_hashval(name) \
	(opaque_hashval((name), 8) & CLIENT_HASH_MASK)
/*
 * reclaim_str_hashtbl[] holds known client info from previous reset/reboot
 * used in reboot/reset lease grace period processing
 *
 * conf_id_hashtbl[], and conf_str_hashtbl[] hold confirmed
 * setclientid_confirmed info. 
 *
 * unconf_str_hastbl[] and unconf_id_hashtbl[] hold unconfirmed 
 * setclientid info.
 *
 * client_lru holds client queue ordered by nfs4_client.cl_time
 * for lease renewal.
 *
 * close_lru holds (open) stateowner queue ordered by nfs4_stateowner.so_time
 * for last close replay.
 */
static struct list_head	reclaim_str_hashtbl[CLIENT_HASH_SIZE];
static int reclaim_str_hashtbl_size = 0;
static struct list_head	conf_id_hashtbl[CLIENT_HASH_SIZE];
static struct list_head	conf_str_hashtbl[CLIENT_HASH_SIZE];
static struct list_head	unconf_str_hashtbl[CLIENT_HASH_SIZE];
static struct list_head	unconf_id_hashtbl[CLIENT_HASH_SIZE];
static struct list_head client_lru;
static struct list_head close_lru;

static inline void
renew_client(struct nfs4_client *clp)
{
	/*
	* Move client to the end to the LRU list.
	*/
	dprintk("renewing client (clientid %08x/%08x)\n", 
			clp->cl_clientid.cl_boot, 
			clp->cl_clientid.cl_id);
	list_move_tail(&clp->cl_lru, &client_lru);
	clp->cl_time = get_seconds();
}

/* SETCLIENTID and SETCLIENTID_CONFIRM Helper functions */
static int
STALE_CLIENTID(clientid_t *clid)
{
	if (clid->cl_boot == boot_time)
		return 0;
	dprintk("NFSD stale clientid (%08x/%08x)\n", 
			clid->cl_boot, clid->cl_id);
	return 1;
}

/* 
 * XXX Should we use a slab cache ?
 * This type of memory management is somewhat inefficient, but we use it
 * anyway since SETCLIENTID is not a common operation.
 */
static inline struct nfs4_client *
alloc_client(struct xdr_netobj name)
{
	struct nfs4_client *clp;

	if ((clp = kmalloc(sizeof(struct nfs4_client), GFP_KERNEL))!= NULL) {
		memset(clp, 0, sizeof(*clp));
		if ((clp->cl_name.data = kmalloc(name.len, GFP_KERNEL)) != NULL) {
			memcpy(clp->cl_name.data, name.data, name.len);
			clp->cl_name.len = name.len;
		}
		else {
			kfree(clp);
			clp = NULL;
		}
	}
	return clp;
}

static inline void
free_client(struct nfs4_client *clp)
{
	if (clp->cl_cred.cr_group_info)
		put_group_info(clp->cl_cred.cr_group_info);
	kfree(clp->cl_name.data);
	kfree(clp);
}

void
put_nfs4_client(struct nfs4_client *clp)
{
	if (atomic_dec_and_test(&clp->cl_count))
		free_client(clp);
}

static void
expire_client(struct nfs4_client *clp)
{
	struct nfs4_stateowner *sop;
	struct nfs4_delegation *dp;
	struct nfs4_callback *cb = &clp->cl_callback;
	struct rpc_clnt *clnt = clp->cl_callback.cb_client;
	struct list_head reaplist;

	dprintk("NFSD: expire_client cl_count %d\n",
	                    atomic_read(&clp->cl_count));

	/* shutdown rpc client, ending any outstanding recall rpcs */
	if (atomic_read(&cb->cb_set) == 1 && clnt) {
		rpc_shutdown_client(clnt);
		clnt = clp->cl_callback.cb_client = NULL;
	}

	INIT_LIST_HEAD(&reaplist);
	spin_lock(&recall_lock);
	while (!list_empty(&clp->cl_delegations)) {
		dp = list_entry(clp->cl_delegations.next, struct nfs4_delegation, dl_perclnt);
		dprintk("NFSD: expire client. dp %p, fp %p\n", dp,
				dp->dl_flock);
		list_del_init(&dp->dl_perclnt);
		list_move(&dp->dl_recall_lru, &reaplist);
	}
	spin_unlock(&recall_lock);
	while (!list_empty(&reaplist)) {
		dp = list_entry(reaplist.next, struct nfs4_delegation, dl_recall_lru);
		list_del_init(&dp->dl_recall_lru);
		unhash_delegation(dp);
	}
	list_del(&clp->cl_idhash);
	list_del(&clp->cl_strhash);
	list_del(&clp->cl_lru);
	while (!list_empty(&clp->cl_openowners)) {
		sop = list_entry(clp->cl_openowners.next, struct nfs4_stateowner, so_perclient);
		release_stateowner(sop);
	}
	put_nfs4_client(clp);
}

static struct nfs4_client *
create_client(struct xdr_netobj name, char *recdir) {
	struct nfs4_client *clp;

	if (!(clp = alloc_client(name)))
		goto out;
	memcpy(clp->cl_recdir, recdir, HEXDIR_LEN);
	atomic_set(&clp->cl_count, 1);
	atomic_set(&clp->cl_callback.cb_set, 0);
	INIT_LIST_HEAD(&clp->cl_idhash);
	INIT_LIST_HEAD(&clp->cl_strhash);
	INIT_LIST_HEAD(&clp->cl_openowners);
	INIT_LIST_HEAD(&clp->cl_delegations);
	INIT_LIST_HEAD(&clp->cl_lru);
out:
	return clp;
}

static void
copy_verf(struct nfs4_client *target, nfs4_verifier *source) {
	memcpy(target->cl_verifier.data, source->data, sizeof(target->cl_verifier.data));
}

static void
copy_clid(struct nfs4_client *target, struct nfs4_client *source) {
	target->cl_clientid.cl_boot = source->cl_clientid.cl_boot; 
	target->cl_clientid.cl_id = source->cl_clientid.cl_id; 
}

static void
copy_cred(struct svc_cred *target, struct svc_cred *source) {

	target->cr_uid = source->cr_uid;
	target->cr_gid = source->cr_gid;
	target->cr_group_info = source->cr_group_info;
	get_group_info(target->cr_group_info);
}

static inline int
same_name(const char *n1, const char *n2) {
	return 0 == memcmp(n1, n2, HEXDIR_LEN);
}

static int
cmp_verf(nfs4_verifier *v1, nfs4_verifier *v2) {
	return(!memcmp(v1->data,v2->data,sizeof(v1->data)));
}

static int
cmp_clid(clientid_t * cl1, clientid_t * cl2) {
	return((cl1->cl_boot == cl2->cl_boot) &&
	   	(cl1->cl_id == cl2->cl_id));
}

/* XXX what about NGROUP */
static int
cmp_creds(struct svc_cred *cr1, struct svc_cred *cr2){
	return(cr1->cr_uid == cr2->cr_uid);

}

static void
gen_clid(struct nfs4_client *clp) {
	clp->cl_clientid.cl_boot = boot_time;
	clp->cl_clientid.cl_id = current_clientid++; 
}

static void
gen_confirm(struct nfs4_client *clp) {
	struct timespec 	tv;
	u32 *			p;

	tv = CURRENT_TIME;
	p = (u32 *)clp->cl_confirm.data;
	*p++ = tv.tv_sec;
	*p++ = tv.tv_nsec;
}

static int
check_name(struct xdr_netobj name) {

	if (name.len == 0) 
		return 0;
	if (name.len > NFS4_OPAQUE_LIMIT) {
		printk("NFSD: check_name: name too long(%d)!\n", name.len);
		return 0;
	}
	return 1;
}

static void
add_to_unconfirmed(struct nfs4_client *clp, unsigned int strhashval)
{
	unsigned int idhashval;

	list_add(&clp->cl_strhash, &unconf_str_hashtbl[strhashval]);
	idhashval = clientid_hashval(clp->cl_clientid.cl_id);
	list_add(&clp->cl_idhash, &unconf_id_hashtbl[idhashval]);
	list_add_tail(&clp->cl_lru, &client_lru);
	clp->cl_time = get_seconds();
}

static void
move_to_confirmed(struct nfs4_client *clp)
{
	unsigned int idhashval = clientid_hashval(clp->cl_clientid.cl_id);
	unsigned int strhashval;

	dprintk("NFSD: move_to_confirm nfs4_client %p\n", clp);
	list_del_init(&clp->cl_strhash);
	list_del_init(&clp->cl_idhash);
	list_add(&clp->cl_idhash, &conf_id_hashtbl[idhashval]);
	strhashval = clientstr_hashval(clp->cl_recdir);
	list_add(&clp->cl_strhash, &conf_str_hashtbl[strhashval]);
	renew_client(clp);
}

static struct nfs4_client *
find_confirmed_client(clientid_t *clid)
{
	struct nfs4_client *clp;
	unsigned int idhashval = clientid_hashval(clid->cl_id);

	list_for_each_entry(clp, &conf_id_hashtbl[idhashval], cl_idhash) {
		if (cmp_clid(&clp->cl_clientid, clid))
			return clp;
	}
	return NULL;
}

static struct nfs4_client *
find_unconfirmed_client(clientid_t *clid)
{
	struct nfs4_client *clp;
	unsigned int idhashval = clientid_hashval(clid->cl_id);

	list_for_each_entry(clp, &unconf_id_hashtbl[idhashval], cl_idhash) {
		if (cmp_clid(&clp->cl_clientid, clid))
			return clp;
	}
	return NULL;
}

static struct nfs4_client *
find_confirmed_client_by_str(const char *dname, unsigned int hashval)
{
	struct nfs4_client *clp;

	list_for_each_entry(clp, &conf_str_hashtbl[hashval], cl_strhash) {
		if (same_name(clp->cl_recdir, dname))
			return clp;
	}
	return NULL;
}

static struct nfs4_client *
find_unconfirmed_client_by_str(const char *dname, unsigned int hashval)
{
	struct nfs4_client *clp;

	list_for_each_entry(clp, &unconf_str_hashtbl[hashval], cl_strhash) {
		if (same_name(clp->cl_recdir, dname))
			return clp;
	}
	return NULL;
}

/* a helper function for parse_callback */
static int
parse_octet(unsigned int *lenp, char **addrp)
{
	unsigned int len = *lenp;
	char *p = *addrp;
	int n = -1;
	char c;

	for (;;) {
		if (!len)
			break;
		len--;
		c = *p++;
		if (c == '.')
			break;
		if ((c < '0') || (c > '9')) {
			n = -1;
			break;
		}
		if (n < 0)
			n = 0;
		n = (n * 10) + (c - '0');
		if (n > 255) {
			n = -1;
			break;
		}
	}
	*lenp = len;
	*addrp = p;
	return n;
}

/* parse and set the setclientid ipv4 callback address */
static int
parse_ipv4(unsigned int addr_len, char *addr_val, unsigned int *cbaddrp, unsigned short *cbportp)
{
	int temp = 0;
	u32 cbaddr = 0;
	u16 cbport = 0;
	u32 addrlen = addr_len;
	char *addr = addr_val;
	int i, shift;

	/* ipaddress */
	shift = 24;
	for(i = 4; i > 0  ; i--) {
		if ((temp = parse_octet(&addrlen, &addr)) < 0) {
			return 0;
		}
		cbaddr |= (temp << shift);
		if (shift > 0)
		shift -= 8;
	}
	*cbaddrp = cbaddr;

	/* port */
	shift = 8;
	for(i = 2; i > 0  ; i--) {
		if ((temp = parse_octet(&addrlen, &addr)) < 0) {
			return 0;
		}
		cbport |= (temp << shift);
		if (shift > 0)
			shift -= 8;
	}
	*cbportp = cbport;
	return 1;
}

static void
gen_callback(struct nfs4_client *clp, struct nfsd4_setclientid *se)
{
	struct nfs4_callback *cb = &clp->cl_callback;

	/* Currently, we only support tcp for the callback channel */
	if ((se->se_callback_netid_len != 3) || memcmp((char *)se->se_callback_netid_val, "tcp", 3))
		goto out_err;

	if ( !(parse_ipv4(se->se_callback_addr_len, se->se_callback_addr_val,
	                 &cb->cb_addr, &cb->cb_port)))
		goto out_err;
	cb->cb_prog = se->se_callback_prog;
	cb->cb_ident = se->se_callback_ident;
	return;
out_err:
	dprintk(KERN_INFO "NFSD: this client (clientid %08x/%08x) "
		"will not receive delegations\n",
		clp->cl_clientid.cl_boot, clp->cl_clientid.cl_id);

	return;
}

/*
 * RFC 3010 has a complex implmentation description of processing a 
 * SETCLIENTID request consisting of 5 bullets, labeled as 
 * CASE0 - CASE4 below.
 *
 * NOTES:
 * 	callback information will be processed in a future patch
 *
 *	an unconfirmed record is added when:
 *      NORMAL (part of CASE 4): there is no confirmed nor unconfirmed record.
 *	CASE 1: confirmed record found with matching name, principal,
 *		verifier, and clientid.
 *	CASE 2: confirmed record found with matching name, principal,
 *		and there is no unconfirmed record with matching
 *		name and principal
 *
 *      an unconfirmed record is replaced when:
 *	CASE 3: confirmed record found with matching name, principal,
 *		and an unconfirmed record is found with matching 
 *		name, principal, and with clientid and
 *		confirm that does not match the confirmed record.
 *	CASE 4: there is no confirmed record with matching name and 
 *		principal. there is an unconfirmed record with 
 *		matching name, principal.
 *
 *	an unconfirmed record is deleted when:
 *	CASE 1: an unconfirmed record that matches input name, verifier,
 *		and confirmed clientid.
 *	CASE 4: any unconfirmed records with matching name and principal
 *		that exist after an unconfirmed record has been replaced
 *		as described above.
 *
 */
int
nfsd4_setclientid(struct svc_rqst *rqstp, struct nfsd4_setclientid *setclid)
{
	u32 			ip_addr = rqstp->rq_addr.sin_addr.s_addr;
	struct xdr_netobj 	clname = { 
		.len = setclid->se_namelen,
		.data = setclid->se_name,
	};
	nfs4_verifier		clverifier = setclid->se_verf;
	unsigned int 		strhashval;
	struct nfs4_client	*conf, *unconf, *new;
	int 			status;
	char                    dname[HEXDIR_LEN];
	
	if (!check_name(clname))
		return nfserr_inval;

	status = nfs4_make_rec_clidname(dname, &clname);
	if (status)
		return status;

	/* 
	 * XXX The Duplicate Request Cache (DRC) has been checked (??)
	 * We get here on a DRC miss.
	 */

	strhashval = clientstr_hashval(dname);

	nfs4_lock_state();
	conf = find_confirmed_client_by_str(dname, strhashval);
	if (conf) {
		/* 
		 * CASE 0:
		 * clname match, confirmed, different principal
		 * or different ip_address
		 */
		status = nfserr_clid_inuse;
		if (!cmp_creds(&conf->cl_cred, &rqstp->rq_cred)
				|| conf->cl_addr != ip_addr) {
			printk("NFSD: setclientid: string in use by client"
			"(clientid %08x/%08x)\n",
			conf->cl_clientid.cl_boot, conf->cl_clientid.cl_id);
			goto out;
		}
	}
	unconf = find_unconfirmed_client_by_str(dname, strhashval);
	status = nfserr_resource;
	if (!conf) {
		/* 
		 * CASE 4:
		 * placed first, because it is the normal case.
		 */
		if (unconf)
			expire_client(unconf);
		new = create_client(clname, dname);
		if (new == NULL)
			goto out;
		copy_verf(new, &clverifier);
		new->cl_addr = ip_addr;
		copy_cred(&new->cl_cred,&rqstp->rq_cred);
		gen_clid(new);
		gen_confirm(new);
		gen_callback(new, setclid);
		add_to_unconfirmed(new, strhashval);
	} else if (cmp_verf(&conf->cl_verifier, &clverifier)) {
		/*
		 * CASE 1:
		 * cl_name match, confirmed, principal match
		 * verifier match: probable callback update
		 *
		 * remove any unconfirmed nfs4_client with 
		 * matching cl_name, cl_verifier, and cl_clientid
		 *
		 * create and insert an unconfirmed nfs4_client with same 
		 * cl_name, cl_verifier, and cl_clientid as existing 
		 * nfs4_client,  but with the new callback info and a 
		 * new cl_confirm
		 */
		if (unconf) {
			/* Note this is removing unconfirmed {*x***},
			 * which is stronger than RFC recommended {vxc**}.
			 * This has the advantage that there is at most
			 * one {*x***} in either list at any time.
			 */
			expire_client(unconf);
		}
		new = create_client(clname, dname);
		if (new == NULL)
			goto out;
		copy_verf(new,&conf->cl_verifier);
		new->cl_addr = ip_addr;
		copy_cred(&new->cl_cred,&rqstp->rq_cred);
		copy_clid(new, conf);
		gen_confirm(new);
		gen_callback(new, setclid);
		add_to_unconfirmed(new,strhashval);
	} else if (!unconf) {
		/*
		 * CASE 2:
		 * clname match, confirmed, principal match
		 * verfier does not match
		 * no unconfirmed. create a new unconfirmed nfs4_client
		 * using input clverifier, clname, and callback info
		 * and generate a new cl_clientid and cl_confirm.
		 */
		new = create_client(clname, dname);
		if (new == NULL)
			goto out;
		copy_verf(new,&clverifier);
		new->cl_addr = ip_addr;
		copy_cred(&new->cl_cred,&rqstp->rq_cred);
		gen_clid(new);
		gen_confirm(new);
		gen_callback(new, setclid);
		add_to_unconfirmed(new, strhashval);
	} else if (!cmp_verf(&conf->cl_confirm, &unconf->cl_confirm)) {
		/*	
		 * CASE3:
		 * confirmed found (name, principal match)
		 * confirmed verifier does not match input clverifier
		 *
		 * unconfirmed found (name match)
		 * confirmed->cl_confirm != unconfirmed->cl_confirm
		 *
		 * remove unconfirmed.
		 *
		 * create an unconfirmed nfs4_client 
		 * with same cl_name as existing confirmed nfs4_client, 
		 * but with new callback info, new cl_clientid,
		 * new cl_verifier and a new cl_confirm
		 */
		expire_client(unconf);
		new = create_client(clname, dname);
		if (new == NULL)
			goto out;
		copy_verf(new,&clverifier);
		new->cl_addr = ip_addr;
		copy_cred(&new->cl_cred,&rqstp->rq_cred);
		gen_clid(new);
		gen_confirm(new);
		gen_callback(new, setclid);
		add_to_unconfirmed(new, strhashval);
	} else {
		/* No cases hit !!! */
		status = nfserr_inval;
		goto out;

	}
	setclid->se_clientid.cl_boot = new->cl_clientid.cl_boot;
	setclid->se_clientid.cl_id = new->cl_clientid.cl_id;
	memcpy(setclid->se_confirm.data, new->cl_confirm.data, sizeof(setclid->se_confirm.data));
	status = nfs_ok;
out:
	nfs4_unlock_state();
	return status;
}


/*
 * RFC 3010 has a complex implmentation description of processing a 
 * SETCLIENTID_CONFIRM request consisting of 4 bullets describing
 * processing on a DRC miss, labeled as CASE1 - CASE4 below.
 *
 * NOTE: callback information will be processed here in a future patch
 */
int
nfsd4_setclientid_confirm(struct svc_rqst *rqstp, struct nfsd4_setclientid_confirm *setclientid_confirm)
{
	u32 ip_addr = rqstp->rq_addr.sin_addr.s_addr;
	struct nfs4_client *conf, *unconf;
	nfs4_verifier confirm = setclientid_confirm->sc_confirm; 
	clientid_t * clid = &setclientid_confirm->sc_clientid;
	int status;

	if (STALE_CLIENTID(clid))
		return nfserr_stale_clientid;
	/* 
	 * XXX The Duplicate Request Cache (DRC) has been checked (??)
	 * We get here on a DRC miss.
	 */

	nfs4_lock_state();

	conf = find_confirmed_client(clid);
	unconf = find_unconfirmed_client(clid);

	status = nfserr_clid_inuse;
	if (conf && conf->cl_addr != ip_addr)
		goto out;
	if (unconf && unconf->cl_addr != ip_addr)
		goto out;

	if ((conf && unconf) && 
	    (cmp_verf(&unconf->cl_confirm, &confirm)) &&
	    (cmp_verf(&conf->cl_verifier, &unconf->cl_verifier)) &&
	    (same_name(conf->cl_recdir,unconf->cl_recdir))  &&
	    (!cmp_verf(&conf->cl_confirm, &unconf->cl_confirm))) {
		/* CASE 1:
		* unconf record that matches input clientid and input confirm.
		* conf record that matches input clientid.
		* conf and unconf records match names, verifiers
		*/
		if (!cmp_creds(&conf->cl_cred, &unconf->cl_cred)) 
			status = nfserr_clid_inuse;
		else {
			/* XXX: We just turn off callbacks until we can handle
			  * change request correctly. */
			atomic_set(&conf->cl_callback.cb_set, 0);
			gen_confirm(conf);
			nfsd4_remove_clid_dir(unconf);
			expire_client(unconf);
			status = nfs_ok;

		}
	} else if ((conf && !unconf) ||
	    ((conf && unconf) && 
	     (!cmp_verf(&conf->cl_verifier, &unconf->cl_verifier) ||
	      !same_name(conf->cl_recdir, unconf->cl_recdir)))) {
		/* CASE 2:
		 * conf record that matches input clientid.
		 * if unconf record matches input clientid, then
		 * unconf->cl_name or unconf->cl_verifier don't match the
		 * conf record.
		 */
		if (!cmp_creds(&conf->cl_cred,&rqstp->rq_cred))
			status = nfserr_clid_inuse;
		else
			status = nfs_ok;
	} else if (!conf && unconf
			&& cmp_verf(&unconf->cl_confirm, &confirm)) {
		/* CASE 3:
		 * conf record not found.
		 * unconf record found.
		 * unconf->cl_confirm matches input confirm
		 */
		if (!cmp_creds(&unconf->cl_cred, &rqstp->rq_cred)) {
			status = nfserr_clid_inuse;
		} else {
			unsigned int hash =
				clientstr_hashval(unconf->cl_recdir);
			conf = find_confirmed_client_by_str(unconf->cl_recdir,
									hash);
			if (conf) {
				nfsd4_remove_clid_dir(conf);
				expire_client(conf);
			}
			move_to_confirmed(unconf);
			conf = unconf;
			status = nfs_ok;
		}
	} else if ((!conf || (conf && !cmp_verf(&conf->cl_confirm, &confirm)))
	    && (!unconf || (unconf && !cmp_verf(&unconf->cl_confirm,
				    				&confirm)))) {
		/* CASE 4:
		 * conf record not found, or if conf, conf->cl_confirm does not
		 * match input confirm.
		 * unconf record not found, or if unconf, unconf->cl_confirm
		 * does not match input confirm.
		 */
		status = nfserr_stale_clientid;
	} else {
		/* check that we have hit one of the cases...*/
		status = nfserr_clid_inuse;
	}
out:
	if (!status)
		nfsd4_probe_callback(conf);
	nfs4_unlock_state();
	return status;
}

/* 
 * Open owner state (share locks)
 */

/* hash tables for nfs4_stateowner */
#define OWNER_HASH_BITS              8
#define OWNER_HASH_SIZE             (1 << OWNER_HASH_BITS)
#define OWNER_HASH_MASK             (OWNER_HASH_SIZE - 1)

#define ownerid_hashval(id) \
        ((id) & OWNER_HASH_MASK)
#define ownerstr_hashval(clientid, ownername) \
        (((clientid) + opaque_hashval((ownername.data), (ownername.len))) & OWNER_HASH_MASK)

static struct list_head	ownerid_hashtbl[OWNER_HASH_SIZE];
static struct list_head	ownerstr_hashtbl[OWNER_HASH_SIZE];

/* hash table for nfs4_file */
#define FILE_HASH_BITS                   8
#define FILE_HASH_SIZE                  (1 << FILE_HASH_BITS)
#define FILE_HASH_MASK                  (FILE_HASH_SIZE - 1)
/* hash table for (open)nfs4_stateid */
#define STATEID_HASH_BITS              10
#define STATEID_HASH_SIZE              (1 << STATEID_HASH_BITS)
#define STATEID_HASH_MASK              (STATEID_HASH_SIZE - 1)

#define file_hashval(x) \
        hash_ptr(x, FILE_HASH_BITS)
#define stateid_hashval(owner_id, file_id)  \
        (((owner_id) + (file_id)) & STATEID_HASH_MASK)

static struct list_head file_hashtbl[FILE_HASH_SIZE];
static struct list_head stateid_hashtbl[STATEID_HASH_SIZE];

/* OPEN Share state helper functions */
static inline struct nfs4_file *
alloc_init_file(struct inode *ino)
{
	struct nfs4_file *fp;
	unsigned int hashval = file_hashval(ino);

	fp = kmem_cache_alloc(file_slab, GFP_KERNEL);
	if (fp) {
		kref_init(&fp->fi_ref);
		INIT_LIST_HEAD(&fp->fi_hash);
		INIT_LIST_HEAD(&fp->fi_stateids);
		INIT_LIST_HEAD(&fp->fi_delegations);
		list_add(&fp->fi_hash, &file_hashtbl[hashval]);
		fp->fi_inode = igrab(ino);
		fp->fi_id = current_fileid++;
		return fp;
	}
	return NULL;
}

static void
nfsd4_free_slab(kmem_cache_t **slab)
{
	int status;

	if (*slab == NULL)
		return;
	status = kmem_cache_destroy(*slab);
	*slab = NULL;
	WARN_ON(status);
}

static void
nfsd4_free_slabs(void)
{
	nfsd4_free_slab(&stateowner_slab);
	nfsd4_free_slab(&file_slab);
	nfsd4_free_slab(&stateid_slab);
	nfsd4_free_slab(&deleg_slab);
}

static int
nfsd4_init_slabs(void)
{
	stateowner_slab = kmem_cache_create("nfsd4_stateowners",
			sizeof(struct nfs4_stateowner), 0, 0, NULL, NULL);
	if (stateowner_slab == NULL)
		goto out_nomem;
	file_slab = kmem_cache_create("nfsd4_files",
			sizeof(struct nfs4_file), 0, 0, NULL, NULL);
	if (file_slab == NULL)
		goto out_nomem;
	stateid_slab = kmem_cache_create("nfsd4_stateids",
			sizeof(struct nfs4_stateid), 0, 0, NULL, NULL);
	if (stateid_slab == NULL)
		goto out_nomem;
	deleg_slab = kmem_cache_create("nfsd4_delegations",
			sizeof(struct nfs4_delegation), 0, 0, NULL, NULL);
	if (deleg_slab == NULL)
		goto out_nomem;
	return 0;
out_nomem:
	nfsd4_free_slabs();
	dprintk("nfsd4: out of memory while initializing nfsv4\n");
	return -ENOMEM;
}

void
nfs4_free_stateowner(struct kref *kref)
{
	struct nfs4_stateowner *sop =
		container_of(kref, struct nfs4_stateowner, so_ref);
	kfree(sop->so_owner.data);
	kmem_cache_free(stateowner_slab, sop);
}

static inline struct nfs4_stateowner *
alloc_stateowner(struct xdr_netobj *owner)
{
	struct nfs4_stateowner *sop;

	if ((sop = kmem_cache_alloc(stateowner_slab, GFP_KERNEL))) {
		if ((sop->so_owner.data = kmalloc(owner->len, GFP_KERNEL))) {
			memcpy(sop->so_owner.data, owner->data, owner->len);
			sop->so_owner.len = owner->len;
			kref_init(&sop->so_ref);
			return sop;
		} 
		kmem_cache_free(stateowner_slab, sop);
	}
	return NULL;
}

static struct nfs4_stateowner *
alloc_init_open_stateowner(unsigned int strhashval, struct nfs4_client *clp, struct nfsd4_open *open) {
	struct nfs4_stateowner *sop;
	struct nfs4_replay *rp;
	unsigned int idhashval;

	if (!(sop = alloc_stateowner(&open->op_owner)))
		return NULL;
	idhashval = ownerid_hashval(current_ownerid);
	INIT_LIST_HEAD(&sop->so_idhash);
	INIT_LIST_HEAD(&sop->so_strhash);
	INIT_LIST_HEAD(&sop->so_perclient);
	INIT_LIST_HEAD(&sop->so_stateids);
	INIT_LIST_HEAD(&sop->so_perstateid);  /* not used */
	INIT_LIST_HEAD(&sop->so_close_lru);
	sop->so_time = 0;
	list_add(&sop->so_idhash, &ownerid_hashtbl[idhashval]);
	list_add(&sop->so_strhash, &ownerstr_hashtbl[strhashval]);
	list_add(&sop->so_perclient, &clp->cl_openowners);
	sop->so_is_open_owner = 1;
	sop->so_id = current_ownerid++;
	sop->so_client = clp;
	sop->so_seqid = open->op_seqid;
	sop->so_confirmed = 0;
	rp = &sop->so_replay;
	rp->rp_status = nfserr_serverfault;
	rp->rp_buflen = 0;
	rp->rp_buf = rp->rp_ibuf;
	return sop;
}

static void
release_stateid_lockowners(struct nfs4_stateid *open_stp)
{
	struct nfs4_stateowner *lock_sop;

	while (!list_empty(&open_stp->st_lockowners)) {
		lock_sop = list_entry(open_stp->st_lockowners.next,
				struct nfs4_stateowner, so_perstateid);
		/* list_del(&open_stp->st_lockowners);  */
		BUG_ON(lock_sop->so_is_open_owner);
		release_stateowner(lock_sop);
	}
}

static void
unhash_stateowner(struct nfs4_stateowner *sop)
{
	struct nfs4_stateid *stp;

	list_del(&sop->so_idhash);
	list_del(&sop->so_strhash);
	if (sop->so_is_open_owner)
		list_del(&sop->so_perclient);
	list_del(&sop->so_perstateid);
	while (!list_empty(&sop->so_stateids)) {
		stp = list_entry(sop->so_stateids.next,
			struct nfs4_stateid, st_perstateowner);
		if (sop->so_is_open_owner)
			release_stateid(stp, OPEN_STATE);
		else
			release_stateid(stp, LOCK_STATE);
	}
}

static void
release_stateowner(struct nfs4_stateowner *sop)
{
	unhash_stateowner(sop);
	list_del(&sop->so_close_lru);
	nfs4_put_stateowner(sop);
}

static inline void
init_stateid(struct nfs4_stateid *stp, struct nfs4_file *fp, struct nfsd4_open *open) {
	struct nfs4_stateowner *sop = open->op_stateowner;
	unsigned int hashval = stateid_hashval(sop->so_id, fp->fi_id);

	INIT_LIST_HEAD(&stp->st_hash);
	INIT_LIST_HEAD(&stp->st_perstateowner);
	INIT_LIST_HEAD(&stp->st_lockowners);
	INIT_LIST_HEAD(&stp->st_perfile);
	list_add(&stp->st_hash, &stateid_hashtbl[hashval]);
	list_add(&stp->st_perstateowner, &sop->so_stateids);
	list_add(&stp->st_perfile, &fp->fi_stateids);
	stp->st_stateowner = sop;
	get_nfs4_file(fp);
	stp->st_file = fp;
	stp->st_stateid.si_boot = boot_time;
	stp->st_stateid.si_stateownerid = sop->so_id;
	stp->st_stateid.si_fileid = fp->fi_id;
	stp->st_stateid.si_generation = 0;
	stp->st_access_bmap = 0;
	stp->st_deny_bmap = 0;
	__set_bit(open->op_share_access, &stp->st_access_bmap);
	__set_bit(open->op_share_deny, &stp->st_deny_bmap);
	stp->st_openstp = NULL;
}

static void
release_stateid(struct nfs4_stateid *stp, int flags)
{
	struct file *filp = stp->st_vfs_file;

	list_del(&stp->st_hash);
	list_del(&stp->st_perfile);
	list_del(&stp->st_perstateowner);
	if (flags & OPEN_STATE) {
		release_stateid_lockowners(stp);
		stp->st_vfs_file = NULL;
		nfsd_close(filp);
	} else if (flags & LOCK_STATE)
		locks_remove_posix(filp, (fl_owner_t) stp->st_stateowner);
	put_nfs4_file(stp->st_file);
	kmem_cache_free(stateid_slab, stp);
}

static void
move_to_close_lru(struct nfs4_stateowner *sop)
{
	dprintk("NFSD: move_to_close_lru nfs4_stateowner %p\n", sop);

	unhash_stateowner(sop);
	list_add_tail(&sop->so_close_lru, &close_lru);
	sop->so_time = get_seconds();
}

static int
cmp_owner_str(struct nfs4_stateowner *sop, struct xdr_netobj *owner, clientid_t *clid) {
	return ((sop->so_owner.len == owner->len) && 
	 !memcmp(sop->so_owner.data, owner->data, owner->len) && 
	  (sop->so_client->cl_clientid.cl_id == clid->cl_id));
}

static struct nfs4_stateowner *
find_openstateowner_str(unsigned int hashval, struct nfsd4_open *open)
{
	struct nfs4_stateowner *so = NULL;

	list_for_each_entry(so, &ownerstr_hashtbl[hashval], so_strhash) {
		if (cmp_owner_str(so, &open->op_owner, &open->op_clientid))
			return so;
	}
	return NULL;
}

/* search file_hashtbl[] for file */
static struct nfs4_file *
find_file(struct inode *ino)
{
	unsigned int hashval = file_hashval(ino);
	struct nfs4_file *fp;

	list_for_each_entry(fp, &file_hashtbl[hashval], fi_hash) {
		if (fp->fi_inode == ino) {
			get_nfs4_file(fp);
			return fp;
		}
	}
	return NULL;
}

#define TEST_ACCESS(x) ((x > 0 || x < 4)?1:0)
#define TEST_DENY(x) ((x >= 0 || x < 5)?1:0)

static void
set_access(unsigned int *access, unsigned long bmap) {
	int i;

	*access = 0;
	for (i = 1; i < 4; i++) {
		if (test_bit(i, &bmap))
			*access |= i;
	}
}

static void
set_deny(unsigned int *deny, unsigned long bmap) {
	int i;

	*deny = 0;
	for (i = 0; i < 4; i++) {
		if (test_bit(i, &bmap))
			*deny |= i ;
	}
}

static int
test_share(struct nfs4_stateid *stp, struct nfsd4_open *open) {
	unsigned int access, deny;

	set_access(&access, stp->st_access_bmap);
	set_deny(&deny, stp->st_deny_bmap);
	if ((access & open->op_share_deny) || (deny & open->op_share_access))
		return 0;
	return 1;
}

/*
 * Called to check deny when READ with all zero stateid or
 * WRITE with all zero or all one stateid
 */
static int
nfs4_share_conflict(struct svc_fh *current_fh, unsigned int deny_type)
{
	struct inode *ino = current_fh->fh_dentry->d_inode;
	struct nfs4_file *fp;
	struct nfs4_stateid *stp;
	int ret;

	dprintk("NFSD: nfs4_share_conflict\n");

	fp = find_file(ino);
	if (!fp)
		return nfs_ok;
	ret = nfserr_locked;
	/* Search for conflicting share reservations */
	list_for_each_entry(stp, &fp->fi_stateids, st_perfile) {
		if (test_bit(deny_type, &stp->st_deny_bmap) ||
		    test_bit(NFS4_SHARE_DENY_BOTH, &stp->st_deny_bmap))
			goto out;
	}
	ret = nfs_ok;
out:
	put_nfs4_file(fp);
	return ret;
}

static inline void
nfs4_file_downgrade(struct file *filp, unsigned int share_access)
{
	if (share_access & NFS4_SHARE_ACCESS_WRITE) {
		put_write_access(filp->f_dentry->d_inode);
		filp->f_mode = (filp->f_mode | FMODE_READ) & ~FMODE_WRITE;
	}
}

/*
 * Recall a delegation
 */
static int
do_recall(void *__dp)
{
	struct nfs4_delegation *dp = __dp;

	daemonize("nfsv4-recall");

	nfsd4_cb_recall(dp);
	return 0;
}

/*
 * Spawn a thread to perform a recall on the delegation represented
 * by the lease (file_lock)
 *
 * Called from break_lease() with lock_kernel() held.
 * Note: we assume break_lease will only call this *once* for any given
 * lease.
 */
static
void nfsd_break_deleg_cb(struct file_lock *fl)
{
	struct nfs4_delegation *dp=  (struct nfs4_delegation *)fl->fl_owner;
	struct task_struct *t;

	dprintk("NFSD nfsd_break_deleg_cb: dp %p fl %p\n",dp,fl);
	if (!dp)
		return;

	/* We're assuming the state code never drops its reference
	 * without first removing the lease.  Since we're in this lease
	 * callback (and since the lease code is serialized by the kernel
	 * lock) we know the server hasn't removed the lease yet, we know
	 * it's safe to take a reference: */
	atomic_inc(&dp->dl_count);

	spin_lock(&recall_lock);
	list_add_tail(&dp->dl_recall_lru, &del_recall_lru);
	spin_unlock(&recall_lock);

	/* only place dl_time is set. protected by lock_kernel*/
	dp->dl_time = get_seconds();

	/* XXX need to merge NFSD_LEASE_TIME with fs/locks.c:lease_break_time */
	fl->fl_break_time = jiffies + NFSD_LEASE_TIME * HZ;

	t = kthread_run(do_recall, dp, "%s", "nfs4_cb_recall");
	if (IS_ERR(t)) {
		struct nfs4_client *clp = dp->dl_client;

		printk(KERN_INFO "NFSD: Callback thread failed for "
			"for client (clientid %08x/%08x)\n",
			clp->cl_clientid.cl_boot, clp->cl_clientid.cl_id);
		nfs4_put_delegation(dp);
	}
}

/*
 * The file_lock is being reapd.
 *
 * Called by locks_free_lock() with lock_kernel() held.
 */
static
void nfsd_release_deleg_cb(struct file_lock *fl)
{
	struct nfs4_delegation *dp = (struct nfs4_delegation *)fl->fl_owner;

	dprintk("NFSD nfsd_release_deleg_cb: fl %p dp %p dl_count %d\n", fl,dp, atomic_read(&dp->dl_count));

	if (!(fl->fl_flags & FL_LEASE) || !dp)
		return;
	dp->dl_flock = NULL;
}

/*
 * Set the delegation file_lock back pointer.
 *
 * Called from __setlease() with lock_kernel() held.
 */
static
void nfsd_copy_lock_deleg_cb(struct file_lock *new, struct file_lock *fl)
{
	struct nfs4_delegation *dp = (struct nfs4_delegation *)new->fl_owner;

	dprintk("NFSD: nfsd_copy_lock_deleg_cb: new fl %p dp %p\n", new, dp);
	if (!dp)
		return;
	dp->dl_flock = new;
}

/*
 * Called from __setlease() with lock_kernel() held
 */
static
int nfsd_same_client_deleg_cb(struct file_lock *onlist, struct file_lock *try)
{
	struct nfs4_delegation *onlistd =
		(struct nfs4_delegation *)onlist->fl_owner;
	struct nfs4_delegation *tryd =
		(struct nfs4_delegation *)try->fl_owner;

	if (onlist->fl_lmops != try->fl_lmops)
		return 0;

	return onlistd->dl_client == tryd->dl_client;
}


static
int nfsd_change_deleg_cb(struct file_lock **onlist, int arg)
{
	if (arg & F_UNLCK)
		return lease_modify(onlist, arg);
	else
		return -EAGAIN;
}

static struct lock_manager_operations nfsd_lease_mng_ops = {
	.fl_break = nfsd_break_deleg_cb,
	.fl_release_private = nfsd_release_deleg_cb,
	.fl_copy_lock = nfsd_copy_lock_deleg_cb,
	.fl_mylease = nfsd_same_client_deleg_cb,
	.fl_change = nfsd_change_deleg_cb,
};


int
nfsd4_process_open1(struct nfsd4_open *open)
{
	clientid_t *clientid = &open->op_clientid;
	struct nfs4_client *clp = NULL;
	unsigned int strhashval;
	struct nfs4_stateowner *sop = NULL;

	if (!check_name(open->op_owner))
		return nfserr_inval;

	if (STALE_CLIENTID(&open->op_clientid))
		return nfserr_stale_clientid;

	strhashval = ownerstr_hashval(clientid->cl_id, open->op_owner);
	sop = find_openstateowner_str(strhashval, open);
	open->op_stateowner = sop;
	if (!sop) {
		/* Make sure the client's lease hasn't expired. */
		clp = find_confirmed_client(clientid);
		if (clp == NULL)
			return nfserr_expired;
		goto renew;
	}
	if (!sop->so_confirmed) {
		/* Replace unconfirmed owners without checking for replay. */
		clp = sop->so_client;
		release_stateowner(sop);
		open->op_stateowner = NULL;
		goto renew;
	}
	if (open->op_seqid == sop->so_seqid - 1) {
		if (sop->so_replay.rp_buflen)
			return NFSERR_REPLAY_ME;
		/* The original OPEN failed so spectacularly
		 * that we don't even have replay data saved!
		 * Therefore, we have no choice but to continue
		 * processing this OPEN; presumably, we'll
		 * fail again for the same reason.
		 */
		dprintk("nfsd4_process_open1: replay with no replay cache\n");
		goto renew;
	}
	if (open->op_seqid != sop->so_seqid)
		return nfserr_bad_seqid;
renew:
	if (open->op_stateowner == NULL) {
		sop = alloc_init_open_stateowner(strhashval, clp, open);
		if (sop == NULL)
			return nfserr_resource;
		open->op_stateowner = sop;
	}
	list_del_init(&sop->so_close_lru);
	renew_client(sop->so_client);
	return nfs_ok;
}

static inline int
nfs4_check_delegmode(struct nfs4_delegation *dp, int flags)
{
	if ((flags & WR_STATE) && (dp->dl_type == NFS4_OPEN_DELEGATE_READ))
		return nfserr_openmode;
	else
		return nfs_ok;
}

static struct nfs4_delegation *
find_delegation_file(struct nfs4_file *fp, stateid_t *stid)
{
	struct nfs4_delegation *dp;

	list_for_each_entry(dp, &fp->fi_delegations, dl_perfile) {
		if (dp->dl_stateid.si_stateownerid == stid->si_stateownerid)
			return dp;
	}
	return NULL;
}

static int
nfs4_check_deleg(struct nfs4_file *fp, struct nfsd4_open *open,
		struct nfs4_delegation **dp)
{
	int flags;
	int status = nfserr_bad_stateid;

	*dp = find_delegation_file(fp, &open->op_delegate_stateid);
	if (*dp == NULL)
		goto out;
	flags = open->op_share_access == NFS4_SHARE_ACCESS_READ ?
						RD_STATE : WR_STATE;
	status = nfs4_check_delegmode(*dp, flags);
	if (status)
		*dp = NULL;
out:
	if (open->op_claim_type != NFS4_OPEN_CLAIM_DELEGATE_CUR)
		return nfs_ok;
	if (status)
		return status;
	open->op_stateowner->so_confirmed = 1;
	return nfs_ok;
}

static int
nfs4_check_open(struct nfs4_file *fp, struct nfsd4_open *open, struct nfs4_stateid **stpp)
{
	struct nfs4_stateid *local;
	int status = nfserr_share_denied;
	struct nfs4_stateowner *sop = open->op_stateowner;

	list_for_each_entry(local, &fp->fi_stateids, st_perfile) {
		/* ignore lock owners */
		if (local->st_stateowner->so_is_open_owner == 0)
			continue;
		/* remember if we have seen this open owner */
		if (local->st_stateowner == sop)
			*stpp = local;
		/* check for conflicting share reservations */
		if (!test_share(local, open))
			goto out;
	}
	status = 0;
out:
	return status;
}

static inline struct nfs4_stateid *
nfs4_alloc_stateid(void)
{
	return kmem_cache_alloc(stateid_slab, GFP_KERNEL);
}

static int
nfs4_new_open(struct svc_rqst *rqstp, struct nfs4_stateid **stpp,
		struct nfs4_delegation *dp,
		struct svc_fh *cur_fh, int flags)
{
	struct nfs4_stateid *stp;

	stp = nfs4_alloc_stateid();
	if (stp == NULL)
		return nfserr_resource;

	if (dp) {
		get_file(dp->dl_vfs_file);
		stp->st_vfs_file = dp->dl_vfs_file;
	} else {
		int status;
		status = nfsd_open(rqstp, cur_fh, S_IFREG, flags,
				&stp->st_vfs_file);
		if (status) {
			if (status == nfserr_dropit)
				status = nfserr_jukebox;
			kmem_cache_free(stateid_slab, stp);
			return status;
		}
	}
	*stpp = stp;
	return 0;
}

static inline int
nfsd4_truncate(struct svc_rqst *rqstp, struct svc_fh *fh,
		struct nfsd4_open *open)
{
	struct iattr iattr = {
		.ia_valid = ATTR_SIZE,
		.ia_size = 0,
	};
	if (!open->op_truncate)
		return 0;
	if (!(open->op_share_access & NFS4_SHARE_ACCESS_WRITE))
		return nfserr_inval;
	return nfsd_setattr(rqstp, fh, &iattr, 0, (time_t)0);
}

static int
nfs4_upgrade_open(struct svc_rqst *rqstp, struct svc_fh *cur_fh, struct nfs4_stateid *stp, struct nfsd4_open *open)
{
	struct file *filp = stp->st_vfs_file;
	struct inode *inode = filp->f_dentry->d_inode;
	unsigned int share_access, new_writer;
	int status;

	set_access(&share_access, stp->st_access_bmap);
	new_writer = (~share_access) & open->op_share_access
			& NFS4_SHARE_ACCESS_WRITE;

	if (new_writer) {
		status = get_write_access(inode);
		if (status)
			return nfserrno(status);
	}
	status = nfsd4_truncate(rqstp, cur_fh, open);
	if (status) {
		if (new_writer)
			put_write_access(inode);
		return status;
	}
	/* remember the open */
	filp->f_mode |= open->op_share_access;
	set_bit(open->op_share_access, &stp->st_access_bmap);
	set_bit(open->op_share_deny, &stp->st_deny_bmap);

	return nfs_ok;
}


static void
nfs4_set_claim_prev(struct nfsd4_open *open)
{
	open->op_stateowner->so_confirmed = 1;
	open->op_stateowner->so_client->cl_firststate = 1;
}

/*
 * Attempt to hand out a delegation.
 */
static void
nfs4_open_delegation(struct svc_fh *fh, struct nfsd4_open *open, struct nfs4_stateid *stp)
{
	struct nfs4_delegation *dp;
	struct nfs4_stateowner *sop = stp->st_stateowner;
	struct nfs4_callback *cb = &sop->so_client->cl_callback;
	struct file_lock fl, *flp = &fl;
	int status, flag = 0;

	flag = NFS4_OPEN_DELEGATE_NONE;
	open->op_recall = 0;
	switch (open->op_claim_type) {
		case NFS4_OPEN_CLAIM_PREVIOUS:
			if (!atomic_read(&cb->cb_set))
				open->op_recall = 1;
			flag = open->op_delegate_type;
			if (flag == NFS4_OPEN_DELEGATE_NONE)
				goto out;
			break;
		case NFS4_OPEN_CLAIM_NULL:
			/* Let's not give out any delegations till everyone's
			 * had the chance to reclaim theirs.... */
			if (nfs4_in_grace())
				goto out;
			if (!atomic_read(&cb->cb_set) || !sop->so_confirmed)
				goto out;
			if (open->op_share_access & NFS4_SHARE_ACCESS_WRITE)
				flag = NFS4_OPEN_DELEGATE_WRITE;
			else
				flag = NFS4_OPEN_DELEGATE_READ;
			break;
		default:
			goto out;
	}

	dp = alloc_init_deleg(sop->so_client, stp, fh, flag);
	if (dp == NULL) {
		flag = NFS4_OPEN_DELEGATE_NONE;
		goto out;
	}
	locks_init_lock(&fl);
	fl.fl_lmops = &nfsd_lease_mng_ops;
	fl.fl_flags = FL_LEASE;
	fl.fl_end = OFFSET_MAX;
	fl.fl_owner =  (fl_owner_t)dp;
	fl.fl_file = stp->st_vfs_file;
	fl.fl_pid = current->tgid;

	/* setlease checks to see if delegation should be handed out.
	 * the lock_manager callbacks fl_mylease and fl_change are used
	 */
	if ((status = setlease(stp->st_vfs_file,
		flag == NFS4_OPEN_DELEGATE_READ? F_RDLCK: F_WRLCK, &flp))) {
		dprintk("NFSD: setlease failed [%d], no delegation\n", status);
		unhash_delegation(dp);
		flag = NFS4_OPEN_DELEGATE_NONE;
		goto out;
	}

	memcpy(&open->op_delegate_stateid, &dp->dl_stateid, sizeof(dp->dl_stateid));

	dprintk("NFSD: delegation stateid=(%08x/%08x/%08x/%08x)\n\n",
	             dp->dl_stateid.si_boot,
	             dp->dl_stateid.si_stateownerid,
	             dp->dl_stateid.si_fileid,
	             dp->dl_stateid.si_generation);
out:
	if (open->op_claim_type == NFS4_OPEN_CLAIM_PREVIOUS
			&& flag == NFS4_OPEN_DELEGATE_NONE
			&& open->op_delegate_type != NFS4_OPEN_DELEGATE_NONE)
		printk("NFSD: WARNING: refusing delegation reclaim\n");
	open->op_delegate_type = flag;
}

/*
 * called with nfs4_lock_state() held.
 */
int
nfsd4_process_open2(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open *open)
{
	struct nfs4_file *fp = NULL;
	struct inode *ino = current_fh->fh_dentry->d_inode;
	struct nfs4_stateid *stp = NULL;
	struct nfs4_delegation *dp = NULL;
	int status;

	status = nfserr_inval;
	if (!TEST_ACCESS(open->op_share_access) || !TEST_DENY(open->op_share_deny))
		goto out;
	/*
	 * Lookup file; if found, lookup stateid and check open request,
	 * and check for delegations in the process of being recalled.
	 * If not found, create the nfs4_file struct
	 */
	fp = find_file(ino);
	if (fp) {
		if ((status = nfs4_check_open(fp, open, &stp)))
			goto out;
		status = nfs4_check_deleg(fp, open, &dp);
		if (status)
			goto out;
	} else {
		status = nfserr_bad_stateid;
		if (open->op_claim_type == NFS4_OPEN_CLAIM_DELEGATE_CUR)
			goto out;
		status = nfserr_resource;
		fp = alloc_init_file(ino);
		if (fp == NULL)
			goto out;
	}

	/*
	 * OPEN the file, or upgrade an existing OPEN.
	 * If truncate fails, the OPEN fails.
	 */
	if (stp) {
		/* Stateid was found, this is an OPEN upgrade */
		status = nfs4_upgrade_open(rqstp, current_fh, stp, open);
		if (status)
			goto out;
		update_stateid(&stp->st_stateid);
	} else {
		/* Stateid was not found, this is a new OPEN */
		int flags = 0;
		if (open->op_share_access & NFS4_SHARE_ACCESS_WRITE)
			flags = MAY_WRITE;
		else
			flags = MAY_READ;
		status = nfs4_new_open(rqstp, &stp, dp, current_fh, flags);
		if (status)
			goto out;
		init_stateid(stp, fp, open);
		status = nfsd4_truncate(rqstp, current_fh, open);
		if (status) {
			release_stateid(stp, OPEN_STATE);
			goto out;
		}
	}
	memcpy(&open->op_stateid, &stp->st_stateid, sizeof(stateid_t));

	/*
	* Attempt to hand out a delegation. No error return, because the
	* OPEN succeeds even if we fail.
	*/
	nfs4_open_delegation(current_fh, open, stp);

	status = nfs_ok;

	dprintk("nfs4_process_open2: stateid=(%08x/%08x/%08x/%08x)\n",
	            stp->st_stateid.si_boot, stp->st_stateid.si_stateownerid,
	            stp->st_stateid.si_fileid, stp->st_stateid.si_generation);
out:
	if (fp)
		put_nfs4_file(fp);
	if (status == 0 && open->op_claim_type == NFS4_OPEN_CLAIM_PREVIOUS)
		nfs4_set_claim_prev(open);
	/*
	* To finish the open response, we just need to set the rflags.
	*/
	open->op_rflags = NFS4_OPEN_RESULT_LOCKTYPE_POSIX;
	if (!open->op_stateowner->so_confirmed)
		open->op_rflags |= NFS4_OPEN_RESULT_CONFIRM;

	return status;
}

static struct workqueue_struct *laundry_wq;
static struct work_struct laundromat_work;
static void laundromat_main(void *);
static DECLARE_WORK(laundromat_work, laundromat_main, NULL);

int 
nfsd4_renew(clientid_t *clid)
{
	struct nfs4_client *clp;
	int status;

	nfs4_lock_state();
	dprintk("process_renew(%08x/%08x): starting\n", 
			clid->cl_boot, clid->cl_id);
	status = nfserr_stale_clientid;
	if (STALE_CLIENTID(clid))
		goto out;
	clp = find_confirmed_client(clid);
	status = nfserr_expired;
	if (clp == NULL) {
		/* We assume the client took too long to RENEW. */
		dprintk("nfsd4_renew: clientid not found!\n");
		goto out;
	}
	renew_client(clp);
	status = nfserr_cb_path_down;
	if (!list_empty(&clp->cl_delegations)
			&& !atomic_read(&clp->cl_callback.cb_set))
		goto out;
	status = nfs_ok;
out:
	nfs4_unlock_state();
	return status;
}

static void
end_grace(void)
{
	dprintk("NFSD: end of grace period\n");
	nfsd4_recdir_purge_old();
	in_grace = 0;
}

static time_t
nfs4_laundromat(void)
{
	struct nfs4_client *clp;
	struct nfs4_stateowner *sop;
	struct nfs4_delegation *dp;
	struct list_head *pos, *next, reaplist;
	time_t cutoff = get_seconds() - NFSD_LEASE_TIME;
	time_t t, clientid_val = NFSD_LEASE_TIME;
	time_t u, test_val = NFSD_LEASE_TIME;

	nfs4_lock_state();

	dprintk("NFSD: laundromat service - starting\n");
	if (in_grace)
		end_grace();
	list_for_each_safe(pos, next, &client_lru) {
		clp = list_entry(pos, struct nfs4_client, cl_lru);
		if (time_after((unsigned long)clp->cl_time, (unsigned long)cutoff)) {
			t = clp->cl_time - cutoff;
			if (clientid_val > t)
				clientid_val = t;
			break;
		}
		dprintk("NFSD: purging unused client (clientid %08x)\n",
			clp->cl_clientid.cl_id);
		nfsd4_remove_clid_dir(clp);
		expire_client(clp);
	}
	INIT_LIST_HEAD(&reaplist);
	spin_lock(&recall_lock);
	list_for_each_safe(pos, next, &del_recall_lru) {
		dp = list_entry (pos, struct nfs4_delegation, dl_recall_lru);
		if (time_after((unsigned long)dp->dl_time, (unsigned long)cutoff)) {
			u = dp->dl_time - cutoff;
			if (test_val > u)
				test_val = u;
			break;
		}
		dprintk("NFSD: purging unused delegation dp %p, fp %p\n",
			            dp, dp->dl_flock);
		list_move(&dp->dl_recall_lru, &reaplist);
	}
	spin_unlock(&recall_lock);
	list_for_each_safe(pos, next, &reaplist) {
		dp = list_entry (pos, struct nfs4_delegation, dl_recall_lru);
		list_del_init(&dp->dl_recall_lru);
		unhash_delegation(dp);
	}
	test_val = NFSD_LEASE_TIME;
	list_for_each_safe(pos, next, &close_lru) {
		sop = list_entry(pos, struct nfs4_stateowner, so_close_lru);
		if (time_after((unsigned long)sop->so_time, (unsigned long)cutoff)) {
			u = sop->so_time - cutoff;
			if (test_val > u)
				test_val = u;
			break;
		}
		dprintk("NFSD: purging unused open stateowner (so_id %d)\n",
			sop->so_id);
		list_del(&sop->so_close_lru);
		nfs4_put_stateowner(sop);
	}
	if (clientid_val < NFSD_LAUNDROMAT_MINTIMEOUT)
		clientid_val = NFSD_LAUNDROMAT_MINTIMEOUT;
	nfs4_unlock_state();
	return clientid_val;
}

void
laundromat_main(void *not_used)
{
	time_t t;

	t = nfs4_laundromat();
	dprintk("NFSD: laundromat_main - sleeping for %ld seconds\n", t);
	queue_delayed_work(laundry_wq, &laundromat_work, t*HZ);
}

static struct nfs4_stateowner *
search_close_lru(u32 st_id, int flags)
{
	struct nfs4_stateowner *local = NULL;

	if (flags & CLOSE_STATE) {
		list_for_each_entry(local, &close_lru, so_close_lru) {
			if (local->so_id == st_id)
				return local;
		}
	}
	return NULL;
}

static inline int
nfs4_check_fh(struct svc_fh *fhp, struct nfs4_stateid *stp)
{
	return fhp->fh_dentry->d_inode != stp->st_vfs_file->f_dentry->d_inode;
}

static int
STALE_STATEID(stateid_t *stateid)
{
	if (stateid->si_boot == boot_time)
		return 0;
	dprintk("NFSD: stale stateid (%08x/%08x/%08x/%08x)!\n",
		stateid->si_boot, stateid->si_stateownerid, stateid->si_fileid,
		stateid->si_generation);
	return 1;
}

static inline int
access_permit_read(unsigned long access_bmap)
{
	return test_bit(NFS4_SHARE_ACCESS_READ, &access_bmap) ||
		test_bit(NFS4_SHARE_ACCESS_BOTH, &access_bmap) ||
		test_bit(NFS4_SHARE_ACCESS_WRITE, &access_bmap);
}

static inline int
access_permit_write(unsigned long access_bmap)
{
	return test_bit(NFS4_SHARE_ACCESS_WRITE, &access_bmap) ||
		test_bit(NFS4_SHARE_ACCESS_BOTH, &access_bmap);
}

static
int nfs4_check_openmode(struct nfs4_stateid *stp, int flags)
{
        int status = nfserr_openmode;

	if ((flags & WR_STATE) && (!access_permit_write(stp->st_access_bmap)))
                goto out;
	if ((flags & RD_STATE) && (!access_permit_read(stp->st_access_bmap)))
                goto out;
	status = nfs_ok;
out:
	return status;
}

static inline int
check_special_stateids(svc_fh *current_fh, stateid_t *stateid, int flags)
{
	/* Trying to call delegreturn with a special stateid? Yuch: */
	if (!(flags & (RD_STATE | WR_STATE)))
		return nfserr_bad_stateid;
	else if (ONE_STATEID(stateid) && (flags & RD_STATE))
		return nfs_ok;
	else if (nfs4_in_grace()) {
		/* Answer in remaining cases depends on existance of
		 * conflicting state; so we must wait out the grace period. */
		return nfserr_grace;
	} else if (flags & WR_STATE)
		return nfs4_share_conflict(current_fh,
				NFS4_SHARE_DENY_WRITE);
	else /* (flags & RD_STATE) && ZERO_STATEID(stateid) */
		return nfs4_share_conflict(current_fh,
				NFS4_SHARE_DENY_READ);
}

/*
 * Allow READ/WRITE during grace period on recovered state only for files
 * that are not able to provide mandatory locking.
 */
static inline int
io_during_grace_disallowed(struct inode *inode, int flags)
{
	return nfs4_in_grace() && (flags & (RD_STATE | WR_STATE))
		&& MANDATORY_LOCK(inode);
}

/*
* Checks for stateid operations
*/
int
nfs4_preprocess_stateid_op(struct svc_fh *current_fh, stateid_t *stateid, int flags, struct file **filpp)
{
	struct nfs4_stateid *stp = NULL;
	struct nfs4_delegation *dp = NULL;
	stateid_t *stidp;
	struct inode *ino = current_fh->fh_dentry->d_inode;
	int status;

	dprintk("NFSD: preprocess_stateid_op: stateid = (%08x/%08x/%08x/%08x)\n",
		stateid->si_boot, stateid->si_stateownerid, 
		stateid->si_fileid, stateid->si_generation); 
	if (filpp)
		*filpp = NULL;

	if (io_during_grace_disallowed(ino, flags))
		return nfserr_grace;

	if (ZERO_STATEID(stateid) || ONE_STATEID(stateid))
		return check_special_stateids(current_fh, stateid, flags);

	/* STALE STATEID */
	status = nfserr_stale_stateid;
	if (STALE_STATEID(stateid)) 
		goto out;

	/* BAD STATEID */
	status = nfserr_bad_stateid;
	if (!stateid->si_fileid) { /* delegation stateid */
		if(!(dp = find_delegation_stateid(ino, stateid))) {
			dprintk("NFSD: delegation stateid not found\n");
			if (nfs4_in_grace())
				status = nfserr_grace;
			goto out;
		}
		stidp = &dp->dl_stateid;
	} else { /* open or lock stateid */
		if (!(stp = find_stateid(stateid, flags))) {
			dprintk("NFSD: open or lock stateid not found\n");
			if (nfs4_in_grace())
				status = nfserr_grace;
			goto out;
		}
		if ((flags & CHECK_FH) && nfs4_check_fh(current_fh, stp))
			goto out;
		if (!stp->st_stateowner->so_confirmed)
			goto out;
		stidp = &stp->st_stateid;
	}
	if (stateid->si_generation > stidp->si_generation)
		goto out;

	/* OLD STATEID */
	status = nfserr_old_stateid;
	if (stateid->si_generation < stidp->si_generation)
		goto out;
	if (stp) {
		if ((status = nfs4_check_openmode(stp,flags)))
			goto out;
		renew_client(stp->st_stateowner->so_client);
		if (filpp)
			*filpp = stp->st_vfs_file;
	} else if (dp) {
		if ((status = nfs4_check_delegmode(dp, flags)))
			goto out;
		renew_client(dp->dl_client);
		if (flags & DELEG_RET)
			unhash_delegation(dp);
		if (filpp)
			*filpp = dp->dl_vfs_file;
	}
	status = nfs_ok;
out:
	return status;
}

static inline int
setlkflg (int type)
{
	return (type == NFS4_READW_LT || type == NFS4_READ_LT) ?
		RD_STATE : WR_STATE;
}

/* 
 * Checks for sequence id mutating operations. 
 */
static int
nfs4_preprocess_seqid_op(struct svc_fh *current_fh, u32 seqid, stateid_t *stateid, int flags, struct nfs4_stateowner **sopp, struct nfs4_stateid **stpp, struct nfsd4_lock *lock)
{
	struct nfs4_stateid *stp;
	struct nfs4_stateowner *sop;

	dprintk("NFSD: preprocess_seqid_op: seqid=%d " 
			"stateid = (%08x/%08x/%08x/%08x)\n", seqid,
		stateid->si_boot, stateid->si_stateownerid, stateid->si_fileid,
		stateid->si_generation);

	*stpp = NULL;
	*sopp = NULL;

	if (ZERO_STATEID(stateid) || ONE_STATEID(stateid)) {
		printk("NFSD: preprocess_seqid_op: magic stateid!\n");
		return nfserr_bad_stateid;
	}

	if (STALE_STATEID(stateid))
		return nfserr_stale_stateid;
	/*
	* We return BAD_STATEID if filehandle doesn't match stateid, 
	* the confirmed flag is incorrecly set, or the generation 
	* number is incorrect.  
	*/
	stp = find_stateid(stateid, flags);
	if (stp == NULL) {
		/*
		 * Also, we should make sure this isn't just the result of
		 * a replayed close:
		 */
		sop = search_close_lru(stateid->si_stateownerid, flags);
		if (sop == NULL)
			return nfserr_bad_stateid;
		*sopp = sop;
		goto check_replay;
	}

	if (lock) {
		struct nfs4_stateowner *sop = stp->st_stateowner;
		clientid_t *lockclid = &lock->v.new.clientid;
		struct nfs4_client *clp = sop->so_client;
		int lkflg = 0;
		int status;

		lkflg = setlkflg(lock->lk_type);

		if (lock->lk_is_new) {
                       if (!sop->so_is_open_owner)
			       return nfserr_bad_stateid;
                       if (!cmp_clid(&clp->cl_clientid, lockclid))
			       return nfserr_bad_stateid;
                       /* stp is the open stateid */
                       status = nfs4_check_openmode(stp, lkflg);
                       if (status)
			       return status;
               } else {
                       /* stp is the lock stateid */
                       status = nfs4_check_openmode(stp->st_openstp, lkflg);
                       if (status)
			       return status;
               }

	}

	if ((flags & CHECK_FH) && nfs4_check_fh(current_fh, stp)) {
		printk("NFSD: preprocess_seqid_op: fh-stateid mismatch!\n");
		return nfserr_bad_stateid;
	}

	*stpp = stp;
	*sopp = sop = stp->st_stateowner;

	/*
	*  We now validate the seqid and stateid generation numbers.
	*  For the moment, we ignore the possibility of 
	*  generation number wraparound.
	*/
	if (seqid != sop->so_seqid)
		goto check_replay;

	if (sop->so_confirmed && flags & CONFIRM) {
		printk("NFSD: preprocess_seqid_op: expected"
				" unconfirmed stateowner!\n");
		return nfserr_bad_stateid;
	}
	if (!sop->so_confirmed && !(flags & CONFIRM)) {
		printk("NFSD: preprocess_seqid_op: stateowner not"
				" confirmed yet!\n");
		return nfserr_bad_stateid;
	}
	if (stateid->si_generation > stp->st_stateid.si_generation) {
		printk("NFSD: preprocess_seqid_op: future stateid?!\n");
		return nfserr_bad_stateid;
	}

	if (stateid->si_generation < stp->st_stateid.si_generation) {
		printk("NFSD: preprocess_seqid_op: old stateid!\n");
		return nfserr_old_stateid;
	}
	renew_client(sop->so_client);
	return nfs_ok;

check_replay:
	if (seqid == sop->so_seqid - 1) {
		dprintk("NFSD: preprocess_seqid_op: retransmission?\n");
		/* indicate replay to calling function */
		return NFSERR_REPLAY_ME;
	}
	printk("NFSD: preprocess_seqid_op: bad seqid (expected %d, got %d)\n",
			sop->so_seqid, seqid);
	*sopp = NULL;
	return nfserr_bad_seqid;
}

int
nfsd4_open_confirm(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open_confirm *oc, struct nfs4_stateowner **replay_owner)
{
	int status;
	struct nfs4_stateowner *sop;
	struct nfs4_stateid *stp;

	dprintk("NFSD: nfsd4_open_confirm on file %.*s\n",
			(int)current_fh->fh_dentry->d_name.len,
			current_fh->fh_dentry->d_name.name);

	if ((status = fh_verify(rqstp, current_fh, S_IFREG, 0)))
		goto out;

	nfs4_lock_state();

	if ((status = nfs4_preprocess_seqid_op(current_fh, oc->oc_seqid,
					&oc->oc_req_stateid,
					CHECK_FH | CONFIRM | OPEN_STATE,
					&oc->oc_stateowner, &stp, NULL)))
		goto out; 

	sop = oc->oc_stateowner;
	sop->so_confirmed = 1;
	update_stateid(&stp->st_stateid);
	memcpy(&oc->oc_resp_stateid, &stp->st_stateid, sizeof(stateid_t));
	dprintk("NFSD: nfsd4_open_confirm: success, seqid=%d " 
		"stateid=(%08x/%08x/%08x/%08x)\n", oc->oc_seqid,
		         stp->st_stateid.si_boot,
		         stp->st_stateid.si_stateownerid,
		         stp->st_stateid.si_fileid,
		         stp->st_stateid.si_generation);

	nfsd4_create_clid_dir(sop->so_client);
out:
	if (oc->oc_stateowner) {
		nfs4_get_stateowner(oc->oc_stateowner);
		*replay_owner = oc->oc_stateowner;
	}
	nfs4_unlock_state();
	return status;
}


/*
 * unset all bits in union bitmap (bmap) that
 * do not exist in share (from successful OPEN_DOWNGRADE)
 */
static void
reset_union_bmap_access(unsigned long access, unsigned long *bmap)
{
	int i;
	for (i = 1; i < 4; i++) {
		if ((i & access) != i)
			__clear_bit(i, bmap);
	}
}

static void
reset_union_bmap_deny(unsigned long deny, unsigned long *bmap)
{
	int i;
	for (i = 0; i < 4; i++) {
		if ((i & deny) != i)
			__clear_bit(i, bmap);
	}
}

int
nfsd4_open_downgrade(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open_downgrade *od, struct nfs4_stateowner **replay_owner)
{
	int status;
	struct nfs4_stateid *stp;
	unsigned int share_access;

	dprintk("NFSD: nfsd4_open_downgrade on file %.*s\n", 
			(int)current_fh->fh_dentry->d_name.len,
			current_fh->fh_dentry->d_name.name);

	if (!TEST_ACCESS(od->od_share_access) || !TEST_DENY(od->od_share_deny))
		return nfserr_inval;

	nfs4_lock_state();
	if ((status = nfs4_preprocess_seqid_op(current_fh, od->od_seqid, 
					&od->od_stateid, 
					CHECK_FH | OPEN_STATE, 
					&od->od_stateowner, &stp, NULL)))
		goto out; 

	status = nfserr_inval;
	if (!test_bit(od->od_share_access, &stp->st_access_bmap)) {
		dprintk("NFSD:access not a subset current bitmap: 0x%lx, input access=%08x\n",
			stp->st_access_bmap, od->od_share_access);
		goto out;
	}
	if (!test_bit(od->od_share_deny, &stp->st_deny_bmap)) {
		dprintk("NFSD:deny not a subset current bitmap: 0x%lx, input deny=%08x\n",
			stp->st_deny_bmap, od->od_share_deny);
		goto out;
	}
	set_access(&share_access, stp->st_access_bmap);
	nfs4_file_downgrade(stp->st_vfs_file,
	                    share_access & ~od->od_share_access);

	reset_union_bmap_access(od->od_share_access, &stp->st_access_bmap);
	reset_union_bmap_deny(od->od_share_deny, &stp->st_deny_bmap);

	update_stateid(&stp->st_stateid);
	memcpy(&od->od_stateid, &stp->st_stateid, sizeof(stateid_t));
	status = nfs_ok;
out:
	if (od->od_stateowner) {
		nfs4_get_stateowner(od->od_stateowner);
		*replay_owner = od->od_stateowner;
	}
	nfs4_unlock_state();
	return status;
}

/*
 * nfs4_unlock_state() called after encode
 */
int
nfsd4_close(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_close *close, struct nfs4_stateowner **replay_owner)
{
	int status;
	struct nfs4_stateid *stp;

	dprintk("NFSD: nfsd4_close on file %.*s\n", 
			(int)current_fh->fh_dentry->d_name.len,
			current_fh->fh_dentry->d_name.name);

	nfs4_lock_state();
	/* check close_lru for replay */
	if ((status = nfs4_preprocess_seqid_op(current_fh, close->cl_seqid, 
					&close->cl_stateid, 
					CHECK_FH | OPEN_STATE | CLOSE_STATE,
					&close->cl_stateowner, &stp, NULL)))
		goto out; 
	status = nfs_ok;
	update_stateid(&stp->st_stateid);
	memcpy(&close->cl_stateid, &stp->st_stateid, sizeof(stateid_t));

	/* release_stateid() calls nfsd_close() if needed */
	release_stateid(stp, OPEN_STATE);

	/* place unused nfs4_stateowners on so_close_lru list to be
	 * released by the laundromat service after the lease period
	 * to enable us to handle CLOSE replay
	 */
	if (list_empty(&close->cl_stateowner->so_stateids))
		move_to_close_lru(close->cl_stateowner);
out:
	if (close->cl_stateowner) {
		nfs4_get_stateowner(close->cl_stateowner);
		*replay_owner = close->cl_stateowner;
	}
	nfs4_unlock_state();
	return status;
}

int
nfsd4_delegreturn(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_delegreturn *dr)
{
	int status;

	if ((status = fh_verify(rqstp, current_fh, S_IFREG, 0)))
		goto out;

	nfs4_lock_state();
	status = nfs4_preprocess_stateid_op(current_fh, &dr->dr_stateid, DELEG_RET, NULL);
	nfs4_unlock_state();
out:
	return status;
}


/* 
 * Lock owner state (byte-range locks)
 */
#define LOFF_OVERFLOW(start, len)      ((u64)(len) > ~(u64)(start))
#define LOCK_HASH_BITS              8
#define LOCK_HASH_SIZE             (1 << LOCK_HASH_BITS)
#define LOCK_HASH_MASK             (LOCK_HASH_SIZE - 1)

#define lockownerid_hashval(id) \
        ((id) & LOCK_HASH_MASK)

static inline unsigned int
lock_ownerstr_hashval(struct inode *inode, u32 cl_id,
		struct xdr_netobj *ownername)
{
	return (file_hashval(inode) + cl_id
			+ opaque_hashval(ownername->data, ownername->len))
		& LOCK_HASH_MASK;
}

static struct list_head lock_ownerid_hashtbl[LOCK_HASH_SIZE];
static struct list_head	lock_ownerstr_hashtbl[LOCK_HASH_SIZE];
static struct list_head lockstateid_hashtbl[STATEID_HASH_SIZE];

static struct nfs4_stateid *
find_stateid(stateid_t *stid, int flags)
{
	struct nfs4_stateid *local = NULL;
	u32 st_id = stid->si_stateownerid;
	u32 f_id = stid->si_fileid;
	unsigned int hashval;

	dprintk("NFSD: find_stateid flags 0x%x\n",flags);
	if ((flags & LOCK_STATE) || (flags & RD_STATE) || (flags & WR_STATE)) {
		hashval = stateid_hashval(st_id, f_id);
		list_for_each_entry(local, &lockstateid_hashtbl[hashval], st_hash) {
			if ((local->st_stateid.si_stateownerid == st_id) &&
			    (local->st_stateid.si_fileid == f_id))
				return local;
		}
	} 
	if ((flags & OPEN_STATE) || (flags & RD_STATE) || (flags & WR_STATE)) {
		hashval = stateid_hashval(st_id, f_id);
		list_for_each_entry(local, &stateid_hashtbl[hashval], st_hash) {
			if ((local->st_stateid.si_stateownerid == st_id) &&
			    (local->st_stateid.si_fileid == f_id))
				return local;
		}
	}
	return NULL;
}

static struct nfs4_delegation *
find_delegation_stateid(struct inode *ino, stateid_t *stid)
{
	struct nfs4_file *fp;
	struct nfs4_delegation *dl;

	dprintk("NFSD:find_delegation_stateid stateid=(%08x/%08x/%08x/%08x)\n",
                    stid->si_boot, stid->si_stateownerid,
                    stid->si_fileid, stid->si_generation);

	fp = find_file(ino);
	if (!fp)
		return NULL;
	dl = find_delegation_file(fp, stid);
	put_nfs4_file(fp);
	return dl;
}

/*
 * TODO: Linux file offsets are _signed_ 64-bit quantities, which means that
 * we can't properly handle lock requests that go beyond the (2^63 - 1)-th
 * byte, because of sign extension problems.  Since NFSv4 calls for 64-bit
 * locking, this prevents us from being completely protocol-compliant.  The
 * real solution to this problem is to start using unsigned file offsets in
 * the VFS, but this is a very deep change!
 */
static inline void
nfs4_transform_lock_offset(struct file_lock *lock)
{
	if (lock->fl_start < 0)
		lock->fl_start = OFFSET_MAX;
	if (lock->fl_end < 0)
		lock->fl_end = OFFSET_MAX;
}

static int
nfs4_verify_lock_stateowner(struct nfs4_stateowner *sop, unsigned int hashval)
{
	struct nfs4_stateowner *local = NULL;
	int status = 0;
			        
	if (hashval >= LOCK_HASH_SIZE)
		goto out;
	list_for_each_entry(local, &lock_ownerid_hashtbl[hashval], so_idhash) {
		if (local == sop) {
			status = 1;
			goto out;
		}
	}
out:
	return status;
}


static inline void
nfs4_set_lock_denied(struct file_lock *fl, struct nfsd4_lock_denied *deny)
{
	struct nfs4_stateowner *sop = (struct nfs4_stateowner *) fl->fl_owner;
	unsigned int hval = lockownerid_hashval(sop->so_id);

	deny->ld_sop = NULL;
	if (nfs4_verify_lock_stateowner(sop, hval)) {
		kref_get(&sop->so_ref);
		deny->ld_sop = sop;
		deny->ld_clientid = sop->so_client->cl_clientid;
	}
	deny->ld_start = fl->fl_start;
	deny->ld_length = ~(u64)0;
	if (fl->fl_end != ~(u64)0)
		deny->ld_length = fl->fl_end - fl->fl_start + 1;        
	deny->ld_type = NFS4_READ_LT;
	if (fl->fl_type != F_RDLCK)
		deny->ld_type = NFS4_WRITE_LT;
}

static struct nfs4_stateowner *
find_lockstateowner_str(struct inode *inode, clientid_t *clid,
		struct xdr_netobj *owner)
{
	unsigned int hashval = lock_ownerstr_hashval(inode, clid->cl_id, owner);
	struct nfs4_stateowner *op;

	list_for_each_entry(op, &lock_ownerstr_hashtbl[hashval], so_strhash) {
		if (cmp_owner_str(op, owner, clid))
			return op;
	}
	return NULL;
}

/*
 * Alloc a lock owner structure.
 * Called in nfsd4_lock - therefore, OPEN and OPEN_CONFIRM (if needed) has 
 * occured. 
 *
 * strhashval = lock_ownerstr_hashval 
 */

static struct nfs4_stateowner *
alloc_init_lock_stateowner(unsigned int strhashval, struct nfs4_client *clp, struct nfs4_stateid *open_stp, struct nfsd4_lock *lock) {
	struct nfs4_stateowner *sop;
	struct nfs4_replay *rp;
	unsigned int idhashval;

	if (!(sop = alloc_stateowner(&lock->lk_new_owner)))
		return NULL;
	idhashval = lockownerid_hashval(current_ownerid);
	INIT_LIST_HEAD(&sop->so_idhash);
	INIT_LIST_HEAD(&sop->so_strhash);
	INIT_LIST_HEAD(&sop->so_perclient);
	INIT_LIST_HEAD(&sop->so_stateids);
	INIT_LIST_HEAD(&sop->so_perstateid);
	INIT_LIST_HEAD(&sop->so_close_lru); /* not used */
	sop->so_time = 0;
	list_add(&sop->so_idhash, &lock_ownerid_hashtbl[idhashval]);
	list_add(&sop->so_strhash, &lock_ownerstr_hashtbl[strhashval]);
	list_add(&sop->so_perstateid, &open_stp->st_lockowners);
	sop->so_is_open_owner = 0;
	sop->so_id = current_ownerid++;
	sop->so_client = clp;
	/* It is the openowner seqid that will be incremented in encode in the
	 * case of new lockowners; so increment the lock seqid manually: */
	sop->so_seqid = lock->lk_new_lock_seqid + 1;
	sop->so_confirmed = 1;
	rp = &sop->so_replay;
	rp->rp_status = nfserr_serverfault;
	rp->rp_buflen = 0;
	rp->rp_buf = rp->rp_ibuf;
	return sop;
}

static struct nfs4_stateid *
alloc_init_lock_stateid(struct nfs4_stateowner *sop, struct nfs4_file *fp, struct nfs4_stateid *open_stp)
{
	struct nfs4_stateid *stp;
	unsigned int hashval = stateid_hashval(sop->so_id, fp->fi_id);

	stp = nfs4_alloc_stateid();
	if (stp == NULL)
		goto out;
	INIT_LIST_HEAD(&stp->st_hash);
	INIT_LIST_HEAD(&stp->st_perfile);
	INIT_LIST_HEAD(&stp->st_perstateowner);
	INIT_LIST_HEAD(&stp->st_lockowners); /* not used */
	list_add(&stp->st_hash, &lockstateid_hashtbl[hashval]);
	list_add(&stp->st_perfile, &fp->fi_stateids);
	list_add(&stp->st_perstateowner, &sop->so_stateids);
	stp->st_stateowner = sop;
	get_nfs4_file(fp);
	stp->st_file = fp;
	stp->st_stateid.si_boot = boot_time;
	stp->st_stateid.si_stateownerid = sop->so_id;
	stp->st_stateid.si_fileid = fp->fi_id;
	stp->st_stateid.si_generation = 0;
	stp->st_vfs_file = open_stp->st_vfs_file; /* FIXME refcount?? */
	stp->st_access_bmap = open_stp->st_access_bmap;
	stp->st_deny_bmap = open_stp->st_deny_bmap;
	stp->st_openstp = open_stp;

out:
	return stp;
}

static int
check_lock_length(u64 offset, u64 length)
{
	return ((length == 0)  || ((length != ~(u64)0) &&
	     LOFF_OVERFLOW(offset, length)));
}

/*
 *  LOCK operation 
 */
int
nfsd4_lock(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_lock *lock, struct nfs4_stateowner **replay_owner)
{
	struct nfs4_stateowner *open_sop = NULL;
	struct nfs4_stateowner *lock_sop = NULL;
	struct nfs4_stateid *lock_stp;
	struct file *filp;
	struct file_lock file_lock;
	struct file_lock *conflock;
	int status = 0;
	unsigned int strhashval;

	dprintk("NFSD: nfsd4_lock: start=%Ld length=%Ld\n",
		(long long) lock->lk_offset,
		(long long) lock->lk_length);

	if (check_lock_length(lock->lk_offset, lock->lk_length))
		 return nfserr_inval;

	if ((status = fh_verify(rqstp, current_fh, S_IFREG, MAY_LOCK))) {
		dprintk("NFSD: nfsd4_lock: permission denied!\n");
		return status;
	}

	nfs4_lock_state();

	if (lock->lk_is_new) {
		/*
		 * Client indicates that this is a new lockowner.
		 * Use open owner and open stateid to create lock owner and
		 * lock stateid.
		 */
		struct nfs4_stateid *open_stp = NULL;
		struct nfs4_file *fp;
		
		status = nfserr_stale_clientid;
		if (STALE_CLIENTID(&lock->lk_new_clientid))
			goto out;

		/* validate and update open stateid and open seqid */
		status = nfs4_preprocess_seqid_op(current_fh, 
				        lock->lk_new_open_seqid,
		                        &lock->lk_new_open_stateid,
		                        CHECK_FH | OPEN_STATE,
		                        &lock->lk_replay_owner, &open_stp,
					lock);
		if (status)
			goto out;
		open_sop = lock->lk_replay_owner;
		/* create lockowner and lock stateid */
		fp = open_stp->st_file;
		strhashval = lock_ownerstr_hashval(fp->fi_inode, 
				open_sop->so_client->cl_clientid.cl_id, 
				&lock->v.new.owner);
		/* XXX: Do we need to check for duplicate stateowners on
		 * the same file, or should they just be allowed (and
		 * create new stateids)? */
		status = nfserr_resource;
		lock_sop = alloc_init_lock_stateowner(strhashval,
				open_sop->so_client, open_stp, lock);
		if (lock_sop == NULL)
			goto out;
		lock_stp = alloc_init_lock_stateid(lock_sop, fp, open_stp);
		if (lock_stp == NULL)
			goto out;
	} else {
		/* lock (lock owner + lock stateid) already exists */
		status = nfs4_preprocess_seqid_op(current_fh,
				       lock->lk_old_lock_seqid, 
				       &lock->lk_old_lock_stateid, 
				       CHECK_FH | LOCK_STATE, 
				       &lock->lk_replay_owner, &lock_stp, lock);
		if (status)
			goto out;
		lock_sop = lock->lk_replay_owner;
	}
	/* lock->lk_replay_owner and lock_stp have been created or found */
	filp = lock_stp->st_vfs_file;

	status = nfserr_grace;
	if (nfs4_in_grace() && !lock->lk_reclaim)
		goto out;
	status = nfserr_no_grace;
	if (!nfs4_in_grace() && lock->lk_reclaim)
		goto out;

	locks_init_lock(&file_lock);
	switch (lock->lk_type) {
		case NFS4_READ_LT:
		case NFS4_READW_LT:
			file_lock.fl_type = F_RDLCK;
		break;
		case NFS4_WRITE_LT:
		case NFS4_WRITEW_LT:
			file_lock.fl_type = F_WRLCK;
		break;
		default:
			status = nfserr_inval;
		goto out;
	}
	file_lock.fl_owner = (fl_owner_t)lock_sop;
	file_lock.fl_pid = current->tgid;
	file_lock.fl_file = filp;
	file_lock.fl_flags = FL_POSIX;

	file_lock.fl_start = lock->lk_offset;
	if ((lock->lk_length == ~(u64)0) || 
			LOFF_OVERFLOW(lock->lk_offset, lock->lk_length))
		file_lock.fl_end = ~(u64)0;
	else
		file_lock.fl_end = lock->lk_offset + lock->lk_length - 1;
	nfs4_transform_lock_offset(&file_lock);

	/*
	* Try to lock the file in the VFS.
	* Note: locks.c uses the BKL to protect the inode's lock list.
	*/

	status = posix_lock_file(filp, &file_lock);
	dprintk("NFSD: nfsd4_lock: posix_lock_file status %d\n",status);
	switch (-status) {
	case 0: /* success! */
		update_stateid(&lock_stp->st_stateid);
		memcpy(&lock->lk_resp_stateid, &lock_stp->st_stateid, 
				sizeof(stateid_t));
		goto out;
	case (EAGAIN):
		goto conflicting_lock;
	case (EDEADLK):
		status = nfserr_deadlock;
		dprintk("NFSD: nfsd4_lock: posix_lock_file() failed! status %d\n",status);
		goto out;
	default:        
		status = nfserrno(status);
		dprintk("NFSD: nfsd4_lock: posix_lock_file() failed! status %d\n",status);
		goto out;
	}

conflicting_lock:
	dprintk("NFSD: nfsd4_lock: conflicting lock found!\n");
	status = nfserr_denied;
	/* XXX There is a race here. Future patch needed to provide 
	 * an atomic posix_lock_and_test_file
	 */
	if (!(conflock = posix_test_lock(filp, &file_lock))) {
		status = nfserr_serverfault;
		goto out;
	}
	nfs4_set_lock_denied(conflock, &lock->lk_denied);
out:
	if (status && lock->lk_is_new && lock_sop)
		release_stateowner(lock_sop);
	if (lock->lk_replay_owner) {
		nfs4_get_stateowner(lock->lk_replay_owner);
		*replay_owner = lock->lk_replay_owner;
	}
	nfs4_unlock_state();
	return status;
}

/*
 * LOCKT operation
 */
int
nfsd4_lockt(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_lockt *lockt)
{
	struct inode *inode;
	struct file file;
	struct file_lock file_lock;
	struct file_lock *conflicting_lock;
	int status;

	if (nfs4_in_grace())
		return nfserr_grace;

	if (check_lock_length(lockt->lt_offset, lockt->lt_length))
		 return nfserr_inval;

	lockt->lt_stateowner = NULL;
	nfs4_lock_state();

	status = nfserr_stale_clientid;
	if (STALE_CLIENTID(&lockt->lt_clientid))
		goto out;

	if ((status = fh_verify(rqstp, current_fh, S_IFREG, 0))) {
		dprintk("NFSD: nfsd4_lockt: fh_verify() failed!\n");
		if (status == nfserr_symlink)
			status = nfserr_inval;
		goto out;
	}

	inode = current_fh->fh_dentry->d_inode;
	locks_init_lock(&file_lock);
	switch (lockt->lt_type) {
		case NFS4_READ_LT:
		case NFS4_READW_LT:
			file_lock.fl_type = F_RDLCK;
		break;
		case NFS4_WRITE_LT:
		case NFS4_WRITEW_LT:
			file_lock.fl_type = F_WRLCK;
		break;
		default:
			printk("NFSD: nfs4_lockt: bad lock type!\n");
			status = nfserr_inval;
		goto out;
	}

	lockt->lt_stateowner = find_lockstateowner_str(inode,
			&lockt->lt_clientid, &lockt->lt_owner);
	if (lockt->lt_stateowner)
		file_lock.fl_owner = (fl_owner_t)lockt->lt_stateowner;
	file_lock.fl_pid = current->tgid;
	file_lock.fl_flags = FL_POSIX;

	file_lock.fl_start = lockt->lt_offset;
	if ((lockt->lt_length == ~(u64)0) || LOFF_OVERFLOW(lockt->lt_offset, lockt->lt_length))
		file_lock.fl_end = ~(u64)0;
	else
		file_lock.fl_end = lockt->lt_offset + lockt->lt_length - 1;

	nfs4_transform_lock_offset(&file_lock);

	/* posix_test_lock uses the struct file _only_ to resolve the inode.
	 * since LOCKT doesn't require an OPEN, and therefore a struct
	 * file may not exist, pass posix_test_lock a struct file with
	 * only the dentry:inode set.
	 */
	memset(&file, 0, sizeof (struct file));
	file.f_dentry = current_fh->fh_dentry;

	status = nfs_ok;
	conflicting_lock = posix_test_lock(&file, &file_lock);
	if (conflicting_lock) {
		status = nfserr_denied;
		nfs4_set_lock_denied(conflicting_lock, &lockt->lt_denied);
	}
out:
	nfs4_unlock_state();
	return status;
}

int
nfsd4_locku(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_locku *locku, struct nfs4_stateowner **replay_owner)
{
	struct nfs4_stateid *stp;
	struct file *filp = NULL;
	struct file_lock file_lock;
	int status;
						        
	dprintk("NFSD: nfsd4_locku: start=%Ld length=%Ld\n",
		(long long) locku->lu_offset,
		(long long) locku->lu_length);

	if (check_lock_length(locku->lu_offset, locku->lu_length))
		 return nfserr_inval;

	nfs4_lock_state();
									        
	if ((status = nfs4_preprocess_seqid_op(current_fh, 
					locku->lu_seqid, 
					&locku->lu_stateid, 
					CHECK_FH | LOCK_STATE, 
					&locku->lu_stateowner, &stp, NULL)))
		goto out;

	filp = stp->st_vfs_file;
	BUG_ON(!filp);
	locks_init_lock(&file_lock);
	file_lock.fl_type = F_UNLCK;
	file_lock.fl_owner = (fl_owner_t) locku->lu_stateowner;
	file_lock.fl_pid = current->tgid;
	file_lock.fl_file = filp;
	file_lock.fl_flags = FL_POSIX; 
	file_lock.fl_start = locku->lu_offset;

	if ((locku->lu_length == ~(u64)0) || LOFF_OVERFLOW(locku->lu_offset, locku->lu_length))
		file_lock.fl_end = ~(u64)0;
	else
		file_lock.fl_end = locku->lu_offset + locku->lu_length - 1;
	nfs4_transform_lock_offset(&file_lock);

	/*
	*  Try to unlock the file in the VFS.
	*/
	status = posix_lock_file(filp, &file_lock); 
	if (status) {
		dprintk("NFSD: nfs4_locku: posix_lock_file failed!\n");
		goto out_nfserr;
	}
	/*
	* OK, unlock succeeded; the only thing left to do is update the stateid.
	*/
	update_stateid(&stp->st_stateid);
	memcpy(&locku->lu_stateid, &stp->st_stateid, sizeof(stateid_t));

out:
	if (locku->lu_stateowner) {
		nfs4_get_stateowner(locku->lu_stateowner);
		*replay_owner = locku->lu_stateowner;
	}
	nfs4_unlock_state();
	return status;

out_nfserr:
	status = nfserrno(status);
	goto out;
}

/*
 * returns
 * 	1: locks held by lockowner
 * 	0: no locks held by lockowner
 */
static int
check_for_locks(struct file *filp, struct nfs4_stateowner *lowner)
{
	struct file_lock **flpp;
	struct inode *inode = filp->f_dentry->d_inode;
	int status = 0;

	lock_kernel();
	for (flpp = &inode->i_flock; *flpp != NULL; flpp = &(*flpp)->fl_next) {
		if ((*flpp)->fl_owner == (fl_owner_t)lowner) {
			status = 1;
			goto out;
		}
	}
out:
	unlock_kernel();
	return status;
}

int
nfsd4_release_lockowner(struct svc_rqst *rqstp, struct nfsd4_release_lockowner *rlockowner)
{
	clientid_t *clid = &rlockowner->rl_clientid;
	struct nfs4_stateowner *sop;
	struct nfs4_stateid *stp;
	struct xdr_netobj *owner = &rlockowner->rl_owner;
	struct list_head matches;
	int i;
	int status;

	dprintk("nfsd4_release_lockowner clientid: (%08x/%08x):\n",
		clid->cl_boot, clid->cl_id);

	/* XXX check for lease expiration */

	status = nfserr_stale_clientid;
	if (STALE_CLIENTID(clid))
		return status;

	nfs4_lock_state();

	status = nfserr_locks_held;
	/* XXX: we're doing a linear search through all the lockowners.
	 * Yipes!  For now we'll just hope clients aren't really using
	 * release_lockowner much, but eventually we have to fix these
	 * data structures. */
	INIT_LIST_HEAD(&matches);
	for (i = 0; i < LOCK_HASH_SIZE; i++) {
		list_for_each_entry(sop, &lock_ownerid_hashtbl[i], so_idhash) {
			if (!cmp_owner_str(sop, owner, clid))
				continue;
			list_for_each_entry(stp, &sop->so_stateids,
					st_perstateowner) {
				if (check_for_locks(stp->st_vfs_file, sop))
					goto out;
				/* Note: so_perclient unused for lockowners,
				 * so it's OK to fool with here. */
				list_add(&sop->so_perclient, &matches);
			}
		}
	}
	/* Clients probably won't expect us to return with some (but not all)
	 * of the lockowner state released; so don't release any until all
	 * have been checked. */
	status = nfs_ok;
	while (!list_empty(&matches)) {
		sop = list_entry(matches.next, struct nfs4_stateowner,
								so_perclient);
		/* unhash_stateowner deletes so_perclient only
		 * for openowners. */
		list_del(&sop->so_perclient);
		release_stateowner(sop);
	}
out:
	nfs4_unlock_state();
	return status;
}

static inline struct nfs4_client_reclaim *
alloc_reclaim(void)
{
	return kmalloc(sizeof(struct nfs4_client_reclaim), GFP_KERNEL);
}

int
nfs4_has_reclaimed_state(const char *name)
{
	unsigned int strhashval = clientstr_hashval(name);
	struct nfs4_client *clp;

	clp = find_confirmed_client_by_str(name, strhashval);
	return clp ? 1 : 0;
}

/*
 * failure => all reset bets are off, nfserr_no_grace...
 */
int
nfs4_client_to_reclaim(const char *name)
{
	unsigned int strhashval;
	struct nfs4_client_reclaim *crp = NULL;

	dprintk("NFSD nfs4_client_to_reclaim NAME: %.*s\n", HEXDIR_LEN, name);
	crp = alloc_reclaim();
	if (!crp)
		return 0;
	strhashval = clientstr_hashval(name);
	INIT_LIST_HEAD(&crp->cr_strhash);
	list_add(&crp->cr_strhash, &reclaim_str_hashtbl[strhashval]);
	memcpy(crp->cr_recdir, name, HEXDIR_LEN);
	reclaim_str_hashtbl_size++;
	return 1;
}

static void
nfs4_release_reclaim(void)
{
	struct nfs4_client_reclaim *crp = NULL;
	int i;

	for (i = 0; i < CLIENT_HASH_SIZE; i++) {
		while (!list_empty(&reclaim_str_hashtbl[i])) {
			crp = list_entry(reclaim_str_hashtbl[i].next,
			                struct nfs4_client_reclaim, cr_strhash);
			list_del(&crp->cr_strhash);
			kfree(crp);
			reclaim_str_hashtbl_size--;
		}
	}
	BUG_ON(reclaim_str_hashtbl_size);
}

/*
 * called from OPEN, CLAIM_PREVIOUS with a new clientid. */
static struct nfs4_client_reclaim *
nfs4_find_reclaim_client(clientid_t *clid)
{
	unsigned int strhashval;
	struct nfs4_client *clp;
	struct nfs4_client_reclaim *crp = NULL;


	/* find clientid in conf_id_hashtbl */
	clp = find_confirmed_client(clid);
	if (clp == NULL)
		return NULL;

	dprintk("NFSD: nfs4_find_reclaim_client for %.*s with recdir %s\n",
		            clp->cl_name.len, clp->cl_name.data,
			    clp->cl_recdir);

	/* find clp->cl_name in reclaim_str_hashtbl */
	strhashval = clientstr_hashval(clp->cl_recdir);
	list_for_each_entry(crp, &reclaim_str_hashtbl[strhashval], cr_strhash) {
		if (same_name(crp->cr_recdir, clp->cl_recdir)) {
			return crp;
		}
	}
	return NULL;
}

/*
* Called from OPEN. Look for clientid in reclaim list.
*/
int
nfs4_check_open_reclaim(clientid_t *clid)
{
	return nfs4_find_reclaim_client(clid) ? nfs_ok : nfserr_reclaim_bad;
}

/* initialization to perform at module load time: */

void
nfs4_state_init(void)
{
	int i;

	for (i = 0; i < CLIENT_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&conf_id_hashtbl[i]);
		INIT_LIST_HEAD(&conf_str_hashtbl[i]);
		INIT_LIST_HEAD(&unconf_str_hashtbl[i]);
		INIT_LIST_HEAD(&unconf_id_hashtbl[i]);
	}
	for (i = 0; i < FILE_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&file_hashtbl[i]);
	}
	for (i = 0; i < OWNER_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&ownerstr_hashtbl[i]);
		INIT_LIST_HEAD(&ownerid_hashtbl[i]);
	}
	for (i = 0; i < STATEID_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&stateid_hashtbl[i]);
		INIT_LIST_HEAD(&lockstateid_hashtbl[i]);
	}
	for (i = 0; i < LOCK_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&lock_ownerid_hashtbl[i]);
		INIT_LIST_HEAD(&lock_ownerstr_hashtbl[i]);
	}
	memset(&onestateid, ~0, sizeof(stateid_t));
	INIT_LIST_HEAD(&close_lru);
	INIT_LIST_HEAD(&client_lru);
	INIT_LIST_HEAD(&del_recall_lru);
	for (i = 0; i < CLIENT_HASH_SIZE; i++)
		INIT_LIST_HEAD(&reclaim_str_hashtbl[i]);
	reclaim_str_hashtbl_size = 0;
}

static void
nfsd4_load_reboot_recovery_data(void)
{
	int status;

	nfs4_lock_state();
	nfsd4_init_recdir(user_recovery_dirname);
	status = nfsd4_recdir_load();
	nfs4_unlock_state();
	if (status)
		printk("NFSD: Failure reading reboot recovery data\n");
}

/* initialization to perform when the nfsd service is started: */

static void
__nfs4_state_start(void)
{
	time_t grace_time;

	boot_time = get_seconds();
	grace_time = max(user_lease_time, lease_time);
	lease_time = user_lease_time;
	in_grace = 1;
	printk("NFSD: starting %ld-second grace period\n", grace_time);
	laundry_wq = create_singlethread_workqueue("nfsd4");
	queue_delayed_work(laundry_wq, &laundromat_work, grace_time*HZ);
}

int
nfs4_state_start(void)
{
	int status;

	if (nfs4_init)
		return 0;
	status = nfsd4_init_slabs();
	if (status)
		return status;
	nfsd4_load_reboot_recovery_data();
	__nfs4_state_start();
	nfs4_init = 1;
	return 0;
}

int
nfs4_in_grace(void)
{
	return in_grace;
}

time_t
nfs4_lease_time(void)
{
	return lease_time;
}

static void
__nfs4_state_shutdown(void)
{
	int i;
	struct nfs4_client *clp = NULL;
	struct nfs4_delegation *dp = NULL;
	struct nfs4_stateowner *sop = NULL;
	struct list_head *pos, *next, reaplist;

	list_for_each_safe(pos, next, &close_lru) {
		sop = list_entry(pos, struct nfs4_stateowner, so_close_lru);
		list_del(&sop->so_close_lru);
		nfs4_put_stateowner(sop);
	}

	for (i = 0; i < CLIENT_HASH_SIZE; i++) {
		while (!list_empty(&conf_id_hashtbl[i])) {
			clp = list_entry(conf_id_hashtbl[i].next, struct nfs4_client, cl_idhash);
			expire_client(clp);
		}
		while (!list_empty(&unconf_str_hashtbl[i])) {
			clp = list_entry(unconf_str_hashtbl[i].next, struct nfs4_client, cl_strhash);
			expire_client(clp);
		}
	}
	INIT_LIST_HEAD(&reaplist);
	spin_lock(&recall_lock);
	list_for_each_safe(pos, next, &del_recall_lru) {
		dp = list_entry (pos, struct nfs4_delegation, dl_recall_lru);
		list_move(&dp->dl_recall_lru, &reaplist);
	}
	spin_unlock(&recall_lock);
	list_for_each_safe(pos, next, &reaplist) {
		dp = list_entry (pos, struct nfs4_delegation, dl_recall_lru);
		list_del_init(&dp->dl_recall_lru);
		unhash_delegation(dp);
	}

	cancel_delayed_work(&laundromat_work);
	flush_workqueue(laundry_wq);
	destroy_workqueue(laundry_wq);
	nfsd4_shutdown_recdir();
	nfs4_init = 0;
}

void
nfs4_state_shutdown(void)
{
	nfs4_lock_state();
	nfs4_release_reclaim();
	__nfs4_state_shutdown();
	nfsd4_free_slabs();
	nfs4_unlock_state();
}

static void
nfs4_set_recdir(char *recdir)
{
	nfs4_lock_state();
	strcpy(user_recovery_dirname, recdir);
	nfs4_unlock_state();
}

/*
 * Change the NFSv4 recovery directory to recdir.
 */
int
nfs4_reset_recoverydir(char *recdir)
{
	int status;
	struct nameidata nd;

	status = path_lookup(recdir, LOOKUP_FOLLOW, &nd);
	if (status)
		return status;
	status = -ENOTDIR;
	if (S_ISDIR(nd.dentry->d_inode->i_mode)) {
		nfs4_set_recdir(recdir);
		status = 0;
	}
	path_release(&nd);
	return status;
}

/*
 * Called when leasetime is changed.
 *
 * The only way the protocol gives us to handle on-the-fly lease changes is to
 * simulate a reboot.  Instead of doing that, we just wait till the next time
 * we start to register any changes in lease time.  If the administrator
 * really wants to change the lease time *now*, they can go ahead and bring
 * nfsd down and then back up again after changing the lease time.
 */
void
nfs4_reset_lease(time_t leasetime)
{
	lock_kernel();
	user_lease_time = leasetime;
	unlock_kernel();
}
