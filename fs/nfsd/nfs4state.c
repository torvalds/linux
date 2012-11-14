/*
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

#include <linux/file.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/namei.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/ratelimit.h>
#include <linux/sunrpc/svcauth_gss.h>
#include <linux/sunrpc/clnt.h>
#include "xdr4.h"
#include "vfs.h"
#include "current_stateid.h"
#include "fault_inject.h"

#include "netns.h"

#define NFSDDBG_FACILITY                NFSDDBG_PROC

/* Globals */
time_t nfsd4_lease = 90;     /* default lease time */
time_t nfsd4_grace = 90;

#define all_ones {{~0,~0},~0}
static const stateid_t one_stateid = {
	.si_generation = ~0,
	.si_opaque = all_ones,
};
static const stateid_t zero_stateid = {
	/* all fields zero */
};
static const stateid_t currentstateid = {
	.si_generation = 1,
};

static u64 current_sessionid = 1;

#define ZERO_STATEID(stateid) (!memcmp((stateid), &zero_stateid, sizeof(stateid_t)))
#define ONE_STATEID(stateid)  (!memcmp((stateid), &one_stateid, sizeof(stateid_t)))
#define CURRENT_STATEID(stateid) (!memcmp((stateid), &currentstateid, sizeof(stateid_t)))

/* forward declarations */
static int check_for_locks(struct nfs4_file *filp, struct nfs4_lockowner *lowner);

/* Locking: */

/* Currently used for almost all code touching nfsv4 state: */
static DEFINE_MUTEX(client_mutex);

/*
 * Currently used for the del_recall_lru and file hash table.  In an
 * effort to decrease the scope of the client_mutex, this spinlock may
 * eventually cover more:
 */
static DEFINE_SPINLOCK(recall_lock);

static struct kmem_cache *openowner_slab = NULL;
static struct kmem_cache *lockowner_slab = NULL;
static struct kmem_cache *file_slab = NULL;
static struct kmem_cache *stateid_slab = NULL;
static struct kmem_cache *deleg_slab = NULL;

void
nfs4_lock_state(void)
{
	mutex_lock(&client_mutex);
}

static void free_session(struct kref *);

/* Must be called under the client_lock */
static void nfsd4_put_session_locked(struct nfsd4_session *ses)
{
	kref_put(&ses->se_ref, free_session);
}

static void nfsd4_get_session(struct nfsd4_session *ses)
{
	kref_get(&ses->se_ref);
}

void
nfs4_unlock_state(void)
{
	mutex_unlock(&client_mutex);
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

static struct list_head del_recall_lru;

static void nfsd4_free_file(struct nfs4_file *f)
{
	kmem_cache_free(file_slab, f);
}

static inline void
put_nfs4_file(struct nfs4_file *fi)
{
	if (atomic_dec_and_lock(&fi->fi_ref, &recall_lock)) {
		list_del(&fi->fi_hash);
		spin_unlock(&recall_lock);
		iput(fi->fi_inode);
		nfsd4_free_file(fi);
	}
}

static inline void
get_nfs4_file(struct nfs4_file *fi)
{
	atomic_inc(&fi->fi_ref);
}

static int num_delegations;
unsigned int max_delegations;

/*
 * Open owner state (share locks)
 */

/* hash tables for lock and open owners */
#define OWNER_HASH_BITS              8
#define OWNER_HASH_SIZE             (1 << OWNER_HASH_BITS)
#define OWNER_HASH_MASK             (OWNER_HASH_SIZE - 1)

static unsigned int ownerstr_hashval(u32 clientid, struct xdr_netobj *ownername)
{
	unsigned int ret;

	ret = opaque_hashval(ownername->data, ownername->len);
	ret += clientid;
	return ret & OWNER_HASH_MASK;
}

/* hash table for nfs4_file */
#define FILE_HASH_BITS                   8
#define FILE_HASH_SIZE                  (1 << FILE_HASH_BITS)

static unsigned int file_hashval(struct inode *ino)
{
	/* XXX: why are we hashing on inode pointer, anyway? */
	return hash_ptr(ino, FILE_HASH_BITS);
}

static struct list_head file_hashtbl[FILE_HASH_SIZE];

static void __nfs4_file_get_access(struct nfs4_file *fp, int oflag)
{
	BUG_ON(!(fp->fi_fds[oflag] || fp->fi_fds[O_RDWR]));
	atomic_inc(&fp->fi_access[oflag]);
}

static void nfs4_file_get_access(struct nfs4_file *fp, int oflag)
{
	if (oflag == O_RDWR) {
		__nfs4_file_get_access(fp, O_RDONLY);
		__nfs4_file_get_access(fp, O_WRONLY);
	} else
		__nfs4_file_get_access(fp, oflag);
}

static void nfs4_file_put_fd(struct nfs4_file *fp, int oflag)
{
	if (fp->fi_fds[oflag]) {
		fput(fp->fi_fds[oflag]);
		fp->fi_fds[oflag] = NULL;
	}
}

static void __nfs4_file_put_access(struct nfs4_file *fp, int oflag)
{
	if (atomic_dec_and_test(&fp->fi_access[oflag])) {
		nfs4_file_put_fd(fp, oflag);
		/*
		 * It's also safe to get rid of the RDWR open *if*
		 * we no longer have need of the other kind of access
		 * or if we already have the other kind of open:
		 */
		if (fp->fi_fds[1-oflag]
			|| atomic_read(&fp->fi_access[1 - oflag]) == 0)
			nfs4_file_put_fd(fp, O_RDWR);
	}
}

static void nfs4_file_put_access(struct nfs4_file *fp, int oflag)
{
	if (oflag == O_RDWR) {
		__nfs4_file_put_access(fp, O_RDONLY);
		__nfs4_file_put_access(fp, O_WRONLY);
	} else
		__nfs4_file_put_access(fp, oflag);
}

static inline int get_new_stid(struct nfs4_stid *stid)
{
	static int min_stateid = 0;
	struct idr *stateids = &stid->sc_client->cl_stateids;
	int new_stid;
	int error;

	error = idr_get_new_above(stateids, stid, min_stateid, &new_stid);
	/*
	 * Note: the necessary preallocation was done in
	 * nfs4_alloc_stateid().  The idr code caps the number of
	 * preallocations that can exist at a time, but the state lock
	 * prevents anyone from using ours before we get here:
	 */
	BUG_ON(error);
	/*
	 * It shouldn't be a problem to reuse an opaque stateid value.
	 * I don't think it is for 4.1.  But with 4.0 I worry that, for
	 * example, a stray write retransmission could be accepted by
	 * the server when it should have been rejected.  Therefore,
	 * adopt a trick from the sctp code to attempt to maximize the
	 * amount of time until an id is reused, by ensuring they always
	 * "increase" (mod INT_MAX):
	 */

	min_stateid = new_stid+1;
	if (min_stateid == INT_MAX)
		min_stateid = 0;
	return new_stid;
}

static void init_stid(struct nfs4_stid *stid, struct nfs4_client *cl, unsigned char type)
{
	stateid_t *s = &stid->sc_stateid;
	int new_id;

	stid->sc_type = type;
	stid->sc_client = cl;
	s->si_opaque.so_clid = cl->cl_clientid;
	new_id = get_new_stid(stid);
	s->si_opaque.so_id = (u32)new_id;
	/* Will be incremented before return to client: */
	s->si_generation = 0;
}

static struct nfs4_stid *nfs4_alloc_stid(struct nfs4_client *cl, struct kmem_cache *slab)
{
	struct idr *stateids = &cl->cl_stateids;

	if (!idr_pre_get(stateids, GFP_KERNEL))
		return NULL;
	/*
	 * Note: if we fail here (or any time between now and the time
	 * we actually get the new idr), we won't need to undo the idr
	 * preallocation, since the idr code caps the number of
	 * preallocated entries.
	 */
	return kmem_cache_alloc(slab, GFP_KERNEL);
}

static struct nfs4_ol_stateid * nfs4_alloc_stateid(struct nfs4_client *clp)
{
	return openlockstateid(nfs4_alloc_stid(clp, stateid_slab));
}

static struct nfs4_delegation *
alloc_init_deleg(struct nfs4_client *clp, struct nfs4_ol_stateid *stp, struct svc_fh *current_fh, u32 type)
{
	struct nfs4_delegation *dp;
	struct nfs4_file *fp = stp->st_file;

	dprintk("NFSD alloc_init_deleg\n");
	/*
	 * Major work on the lease subsystem (for example, to support
	 * calbacks on stat) will be required before we can support
	 * write delegations properly.
	 */
	if (type != NFS4_OPEN_DELEGATE_READ)
		return NULL;
	if (fp->fi_had_conflict)
		return NULL;
	if (num_delegations > max_delegations)
		return NULL;
	dp = delegstateid(nfs4_alloc_stid(clp, deleg_slab));
	if (dp == NULL)
		return dp;
	init_stid(&dp->dl_stid, clp, NFS4_DELEG_STID);
	/*
	 * delegation seqid's are never incremented.  The 4.1 special
	 * meaning of seqid 0 isn't meaningful, really, but let's avoid
	 * 0 anyway just for consistency and use 1:
	 */
	dp->dl_stid.sc_stateid.si_generation = 1;
	num_delegations++;
	INIT_LIST_HEAD(&dp->dl_perfile);
	INIT_LIST_HEAD(&dp->dl_perclnt);
	INIT_LIST_HEAD(&dp->dl_recall_lru);
	get_nfs4_file(fp);
	dp->dl_file = fp;
	dp->dl_type = type;
	fh_copy_shallow(&dp->dl_fh, &current_fh->fh_handle);
	dp->dl_time = 0;
	atomic_set(&dp->dl_count, 1);
	nfsd4_init_callback(&dp->dl_recall);
	return dp;
}

void
nfs4_put_delegation(struct nfs4_delegation *dp)
{
	if (atomic_dec_and_test(&dp->dl_count)) {
		dprintk("NFSD: freeing dp %p\n",dp);
		put_nfs4_file(dp->dl_file);
		kmem_cache_free(deleg_slab, dp);
		num_delegations--;
	}
}

static void nfs4_put_deleg_lease(struct nfs4_file *fp)
{
	if (atomic_dec_and_test(&fp->fi_delegees)) {
		vfs_setlease(fp->fi_deleg_file, F_UNLCK, &fp->fi_lease);
		fp->fi_lease = NULL;
		fput(fp->fi_deleg_file);
		fp->fi_deleg_file = NULL;
	}
}

static void unhash_stid(struct nfs4_stid *s)
{
	struct idr *stateids = &s->sc_client->cl_stateids;

	idr_remove(stateids, s->sc_stateid.si_opaque.so_id);
}

/* Called under the state lock. */
static void
unhash_delegation(struct nfs4_delegation *dp)
{
	unhash_stid(&dp->dl_stid);
	list_del_init(&dp->dl_perclnt);
	spin_lock(&recall_lock);
	list_del_init(&dp->dl_perfile);
	list_del_init(&dp->dl_recall_lru);
	spin_unlock(&recall_lock);
	nfs4_put_deleg_lease(dp->dl_file);
	nfs4_put_delegation(dp);
}

/* 
 * SETCLIENTID state 
 */

/* client_lock protects the client lru list and session hash table */
static DEFINE_SPINLOCK(client_lock);

static unsigned int clientid_hashval(u32 id)
{
	return id & CLIENT_HASH_MASK;
}

static unsigned int clientstr_hashval(const char *name)
{
	return opaque_hashval(name, 8) & CLIENT_HASH_MASK;
}

/*
 * We store the NONE, READ, WRITE, and BOTH bits separately in the
 * st_{access,deny}_bmap field of the stateid, in order to track not
 * only what share bits are currently in force, but also what
 * combinations of share bits previous opens have used.  This allows us
 * to enforce the recommendation of rfc 3530 14.2.19 that the server
 * return an error if the client attempt to downgrade to a combination
 * of share bits not explicable by closing some of its previous opens.
 *
 * XXX: This enforcement is actually incomplete, since we don't keep
 * track of access/deny bit combinations; so, e.g., we allow:
 *
 *	OPEN allow read, deny write
 *	OPEN allow both, deny none
 *	DOWNGRADE allow read, deny none
 *
 * which we should reject.
 */
static unsigned int
bmap_to_share_mode(unsigned long bmap) {
	int i;
	unsigned int access = 0;

	for (i = 1; i < 4; i++) {
		if (test_bit(i, &bmap))
			access |= i;
	}
	return access;
}

static bool
test_share(struct nfs4_ol_stateid *stp, struct nfsd4_open *open) {
	unsigned int access, deny;

	access = bmap_to_share_mode(stp->st_access_bmap);
	deny = bmap_to_share_mode(stp->st_deny_bmap);
	if ((access & open->op_share_deny) || (deny & open->op_share_access))
		return false;
	return true;
}

/* set share access for a given stateid */
static inline void
set_access(u32 access, struct nfs4_ol_stateid *stp)
{
	__set_bit(access, &stp->st_access_bmap);
}

/* clear share access for a given stateid */
static inline void
clear_access(u32 access, struct nfs4_ol_stateid *stp)
{
	__clear_bit(access, &stp->st_access_bmap);
}

/* test whether a given stateid has access */
static inline bool
test_access(u32 access, struct nfs4_ol_stateid *stp)
{
	return test_bit(access, &stp->st_access_bmap);
}

/* set share deny for a given stateid */
static inline void
set_deny(u32 access, struct nfs4_ol_stateid *stp)
{
	__set_bit(access, &stp->st_deny_bmap);
}

/* clear share deny for a given stateid */
static inline void
clear_deny(u32 access, struct nfs4_ol_stateid *stp)
{
	__clear_bit(access, &stp->st_deny_bmap);
}

/* test whether a given stateid is denying specific access */
static inline bool
test_deny(u32 access, struct nfs4_ol_stateid *stp)
{
	return test_bit(access, &stp->st_deny_bmap);
}

static int nfs4_access_to_omode(u32 access)
{
	switch (access & NFS4_SHARE_ACCESS_BOTH) {
	case NFS4_SHARE_ACCESS_READ:
		return O_RDONLY;
	case NFS4_SHARE_ACCESS_WRITE:
		return O_WRONLY;
	case NFS4_SHARE_ACCESS_BOTH:
		return O_RDWR;
	}
	BUG();
}

/* release all access and file references for a given stateid */
static void
release_all_access(struct nfs4_ol_stateid *stp)
{
	int i;

	for (i = 1; i < 4; i++) {
		if (test_access(i, stp))
			nfs4_file_put_access(stp->st_file,
					     nfs4_access_to_omode(i));
		clear_access(i, stp);
	}
}

static void unhash_generic_stateid(struct nfs4_ol_stateid *stp)
{
	list_del(&stp->st_perfile);
	list_del(&stp->st_perstateowner);
}

static void close_generic_stateid(struct nfs4_ol_stateid *stp)
{
	release_all_access(stp);
	put_nfs4_file(stp->st_file);
	stp->st_file = NULL;
}

static void free_generic_stateid(struct nfs4_ol_stateid *stp)
{
	kmem_cache_free(stateid_slab, stp);
}

static void release_lock_stateid(struct nfs4_ol_stateid *stp)
{
	struct file *file;

	unhash_generic_stateid(stp);
	unhash_stid(&stp->st_stid);
	file = find_any_file(stp->st_file);
	if (file)
		locks_remove_posix(file, (fl_owner_t)lockowner(stp->st_stateowner));
	close_generic_stateid(stp);
	free_generic_stateid(stp);
}

static void unhash_lockowner(struct nfs4_lockowner *lo)
{
	struct nfs4_ol_stateid *stp;

	list_del(&lo->lo_owner.so_strhash);
	list_del(&lo->lo_perstateid);
	list_del(&lo->lo_owner_ino_hash);
	while (!list_empty(&lo->lo_owner.so_stateids)) {
		stp = list_first_entry(&lo->lo_owner.so_stateids,
				struct nfs4_ol_stateid, st_perstateowner);
		release_lock_stateid(stp);
	}
}

static void release_lockowner(struct nfs4_lockowner *lo)
{
	unhash_lockowner(lo);
	nfs4_free_lockowner(lo);
}

static void
release_stateid_lockowners(struct nfs4_ol_stateid *open_stp)
{
	struct nfs4_lockowner *lo;

	while (!list_empty(&open_stp->st_lockowners)) {
		lo = list_entry(open_stp->st_lockowners.next,
				struct nfs4_lockowner, lo_perstateid);
		release_lockowner(lo);
	}
}

static void unhash_open_stateid(struct nfs4_ol_stateid *stp)
{
	unhash_generic_stateid(stp);
	release_stateid_lockowners(stp);
	close_generic_stateid(stp);
}

static void release_open_stateid(struct nfs4_ol_stateid *stp)
{
	unhash_open_stateid(stp);
	unhash_stid(&stp->st_stid);
	free_generic_stateid(stp);
}

static void unhash_openowner(struct nfs4_openowner *oo)
{
	struct nfs4_ol_stateid *stp;

	list_del(&oo->oo_owner.so_strhash);
	list_del(&oo->oo_perclient);
	while (!list_empty(&oo->oo_owner.so_stateids)) {
		stp = list_first_entry(&oo->oo_owner.so_stateids,
				struct nfs4_ol_stateid, st_perstateowner);
		release_open_stateid(stp);
	}
}

static void release_last_closed_stateid(struct nfs4_openowner *oo)
{
	struct nfs4_ol_stateid *s = oo->oo_last_closed_stid;

	if (s) {
		unhash_stid(&s->st_stid);
		free_generic_stateid(s);
		oo->oo_last_closed_stid = NULL;
	}
}

static void release_openowner(struct nfs4_openowner *oo)
{
	unhash_openowner(oo);
	list_del(&oo->oo_close_lru);
	release_last_closed_stateid(oo);
	nfs4_free_openowner(oo);
}

static inline int
hash_sessionid(struct nfs4_sessionid *sessionid)
{
	struct nfsd4_sessionid *sid = (struct nfsd4_sessionid *)sessionid;

	return sid->sequence % SESSION_HASH_SIZE;
}

#ifdef NFSD_DEBUG
static inline void
dump_sessionid(const char *fn, struct nfs4_sessionid *sessionid)
{
	u32 *ptr = (u32 *)(&sessionid->data[0]);
	dprintk("%s: %u:%u:%u:%u\n", fn, ptr[0], ptr[1], ptr[2], ptr[3]);
}
#else
static inline void
dump_sessionid(const char *fn, struct nfs4_sessionid *sessionid)
{
}
#endif


static void
gen_sessionid(struct nfsd4_session *ses)
{
	struct nfs4_client *clp = ses->se_client;
	struct nfsd4_sessionid *sid;

	sid = (struct nfsd4_sessionid *)ses->se_sessionid.data;
	sid->clientid = clp->cl_clientid;
	sid->sequence = current_sessionid++;
	sid->reserved = 0;
}

/*
 * The protocol defines ca_maxresponssize_cached to include the size of
 * the rpc header, but all we need to cache is the data starting after
 * the end of the initial SEQUENCE operation--the rest we regenerate
 * each time.  Therefore we can advertise a ca_maxresponssize_cached
 * value that is the number of bytes in our cache plus a few additional
 * bytes.  In order to stay on the safe side, and not promise more than
 * we can cache, those additional bytes must be the minimum possible: 24
 * bytes of rpc header (xid through accept state, with AUTH_NULL
 * verifier), 12 for the compound header (with zero-length tag), and 44
 * for the SEQUENCE op response:
 */
#define NFSD_MIN_HDR_SEQ_SZ  (24 + 12 + 44)

static void
free_session_slots(struct nfsd4_session *ses)
{
	int i;

	for (i = 0; i < ses->se_fchannel.maxreqs; i++)
		kfree(ses->se_slots[i]);
}

/*
 * We don't actually need to cache the rpc and session headers, so we
 * can allocate a little less for each slot:
 */
static inline int slot_bytes(struct nfsd4_channel_attrs *ca)
{
	return ca->maxresp_cached - NFSD_MIN_HDR_SEQ_SZ;
}

static int nfsd4_sanitize_slot_size(u32 size)
{
	size -= NFSD_MIN_HDR_SEQ_SZ; /* We don't cache the rpc header */
	size = min_t(u32, size, NFSD_SLOT_CACHE_SIZE);

	return size;
}

/*
 * XXX: If we run out of reserved DRC memory we could (up to a point)
 * re-negotiate active sessions and reduce their slot usage to make
 * room for new connections. For now we just fail the create session.
 */
static int nfsd4_get_drc_mem(int slotsize, u32 num)
{
	int avail;

	num = min_t(u32, num, NFSD_MAX_SLOTS_PER_SESSION);

	spin_lock(&nfsd_drc_lock);
	avail = min_t(int, NFSD_MAX_MEM_PER_SESSION,
			nfsd_drc_max_mem - nfsd_drc_mem_used);
	num = min_t(int, num, avail / slotsize);
	nfsd_drc_mem_used += num * slotsize;
	spin_unlock(&nfsd_drc_lock);

	return num;
}

static void nfsd4_put_drc_mem(int slotsize, int num)
{
	spin_lock(&nfsd_drc_lock);
	nfsd_drc_mem_used -= slotsize * num;
	spin_unlock(&nfsd_drc_lock);
}

static struct nfsd4_session *__alloc_session(int slotsize, int numslots)
{
	struct nfsd4_session *new;
	int mem, i;

	BUILD_BUG_ON(NFSD_MAX_SLOTS_PER_SESSION * sizeof(struct nfsd4_slot *)
			+ sizeof(struct nfsd4_session) > PAGE_SIZE);
	mem = numslots * sizeof(struct nfsd4_slot *);

	new = kzalloc(sizeof(*new) + mem, GFP_KERNEL);
	if (!new)
		return NULL;
	/* allocate each struct nfsd4_slot and data cache in one piece */
	for (i = 0; i < numslots; i++) {
		mem = sizeof(struct nfsd4_slot) + slotsize;
		new->se_slots[i] = kzalloc(mem, GFP_KERNEL);
		if (!new->se_slots[i])
			goto out_free;
	}
	return new;
out_free:
	while (i--)
		kfree(new->se_slots[i]);
	kfree(new);
	return NULL;
}

static void init_forechannel_attrs(struct nfsd4_channel_attrs *new, struct nfsd4_channel_attrs *req, int numslots, int slotsize)
{
	u32 maxrpc = nfsd_serv->sv_max_mesg;

	new->maxreqs = numslots;
	new->maxresp_cached = min_t(u32, req->maxresp_cached,
					slotsize + NFSD_MIN_HDR_SEQ_SZ);
	new->maxreq_sz = min_t(u32, req->maxreq_sz, maxrpc);
	new->maxresp_sz = min_t(u32, req->maxresp_sz, maxrpc);
	new->maxops = min_t(u32, req->maxops, NFSD_MAX_OPS_PER_COMPOUND);
}

static void free_conn(struct nfsd4_conn *c)
{
	svc_xprt_put(c->cn_xprt);
	kfree(c);
}

static void nfsd4_conn_lost(struct svc_xpt_user *u)
{
	struct nfsd4_conn *c = container_of(u, struct nfsd4_conn, cn_xpt_user);
	struct nfs4_client *clp = c->cn_session->se_client;

	spin_lock(&clp->cl_lock);
	if (!list_empty(&c->cn_persession)) {
		list_del(&c->cn_persession);
		free_conn(c);
	}
	spin_unlock(&clp->cl_lock);
	nfsd4_probe_callback(clp);
}

static struct nfsd4_conn *alloc_conn(struct svc_rqst *rqstp, u32 flags)
{
	struct nfsd4_conn *conn;

	conn = kmalloc(sizeof(struct nfsd4_conn), GFP_KERNEL);
	if (!conn)
		return NULL;
	svc_xprt_get(rqstp->rq_xprt);
	conn->cn_xprt = rqstp->rq_xprt;
	conn->cn_flags = flags;
	INIT_LIST_HEAD(&conn->cn_xpt_user.list);
	return conn;
}

static void __nfsd4_hash_conn(struct nfsd4_conn *conn, struct nfsd4_session *ses)
{
	conn->cn_session = ses;
	list_add(&conn->cn_persession, &ses->se_conns);
}

static void nfsd4_hash_conn(struct nfsd4_conn *conn, struct nfsd4_session *ses)
{
	struct nfs4_client *clp = ses->se_client;

	spin_lock(&clp->cl_lock);
	__nfsd4_hash_conn(conn, ses);
	spin_unlock(&clp->cl_lock);
}

static int nfsd4_register_conn(struct nfsd4_conn *conn)
{
	conn->cn_xpt_user.callback = nfsd4_conn_lost;
	return register_xpt_user(conn->cn_xprt, &conn->cn_xpt_user);
}

static void nfsd4_init_conn(struct svc_rqst *rqstp, struct nfsd4_conn *conn, struct nfsd4_session *ses)
{
	int ret;

	nfsd4_hash_conn(conn, ses);
	ret = nfsd4_register_conn(conn);
	if (ret)
		/* oops; xprt is already down: */
		nfsd4_conn_lost(&conn->cn_xpt_user);
	if (conn->cn_flags & NFS4_CDFC4_BACK) {
		/* callback channel may be back up */
		nfsd4_probe_callback(ses->se_client);
	}
}

static struct nfsd4_conn *alloc_conn_from_crses(struct svc_rqst *rqstp, struct nfsd4_create_session *cses)
{
	u32 dir = NFS4_CDFC4_FORE;

	if (cses->flags & SESSION4_BACK_CHAN)
		dir |= NFS4_CDFC4_BACK;
	return alloc_conn(rqstp, dir);
}

/* must be called under client_lock */
static void nfsd4_del_conns(struct nfsd4_session *s)
{
	struct nfs4_client *clp = s->se_client;
	struct nfsd4_conn *c;

	spin_lock(&clp->cl_lock);
	while (!list_empty(&s->se_conns)) {
		c = list_first_entry(&s->se_conns, struct nfsd4_conn, cn_persession);
		list_del_init(&c->cn_persession);
		spin_unlock(&clp->cl_lock);

		unregister_xpt_user(c->cn_xprt, &c->cn_xpt_user);
		free_conn(c);

		spin_lock(&clp->cl_lock);
	}
	spin_unlock(&clp->cl_lock);
}

static void __free_session(struct nfsd4_session *ses)
{
	nfsd4_put_drc_mem(slot_bytes(&ses->se_fchannel), ses->se_fchannel.maxreqs);
	free_session_slots(ses);
	kfree(ses);
}

static void free_session(struct kref *kref)
{
	struct nfsd4_session *ses;

	lockdep_assert_held(&client_lock);
	ses = container_of(kref, struct nfsd4_session, se_ref);
	nfsd4_del_conns(ses);
	__free_session(ses);
}

void nfsd4_put_session(struct nfsd4_session *ses)
{
	spin_lock(&client_lock);
	nfsd4_put_session_locked(ses);
	spin_unlock(&client_lock);
}

static struct nfsd4_session *alloc_session(struct nfsd4_channel_attrs *fchan)
{
	struct nfsd4_session *new;
	int numslots, slotsize;
	/*
	 * Note decreasing slot size below client's request may
	 * make it difficult for client to function correctly, whereas
	 * decreasing the number of slots will (just?) affect
	 * performance.  When short on memory we therefore prefer to
	 * decrease number of slots instead of their size.
	 */
	slotsize = nfsd4_sanitize_slot_size(fchan->maxresp_cached);
	numslots = nfsd4_get_drc_mem(slotsize, fchan->maxreqs);
	if (numslots < 1)
		return NULL;

	new = __alloc_session(slotsize, numslots);
	if (!new) {
		nfsd4_put_drc_mem(slotsize, fchan->maxreqs);
		return NULL;
	}
	init_forechannel_attrs(&new->se_fchannel, fchan, numslots, slotsize);
	return new;
}

static void init_session(struct svc_rqst *rqstp, struct nfsd4_session *new, struct nfs4_client *clp, struct nfsd4_create_session *cses)
{
	int idx;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	new->se_client = clp;
	gen_sessionid(new);

	INIT_LIST_HEAD(&new->se_conns);

	new->se_cb_seq_nr = 1;
	new->se_flags = cses->flags;
	new->se_cb_prog = cses->callback_prog;
	new->se_cb_sec = cses->cb_sec;
	kref_init(&new->se_ref);
	idx = hash_sessionid(&new->se_sessionid);
	spin_lock(&client_lock);
	list_add(&new->se_hash, &nn->sessionid_hashtbl[idx]);
	spin_lock(&clp->cl_lock);
	list_add(&new->se_perclnt, &clp->cl_sessions);
	spin_unlock(&clp->cl_lock);
	spin_unlock(&client_lock);

	if (cses->flags & SESSION4_BACK_CHAN) {
		struct sockaddr *sa = svc_addr(rqstp);
		/*
		 * This is a little silly; with sessions there's no real
		 * use for the callback address.  Use the peer address
		 * as a reasonable default for now, but consider fixing
		 * the rpc client not to require an address in the
		 * future:
		 */
		rpc_copy_addr((struct sockaddr *)&clp->cl_cb_conn.cb_addr, sa);
		clp->cl_cb_conn.cb_addrlen = svc_addr_len(sa);
	}
}

/* caller must hold client_lock */
static struct nfsd4_session *
find_in_sessionid_hashtbl(struct nfs4_sessionid *sessionid, struct net *net)
{
	struct nfsd4_session *elem;
	int idx;
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	dump_sessionid(__func__, sessionid);
	idx = hash_sessionid(sessionid);
	/* Search in the appropriate list */
	list_for_each_entry(elem, &nn->sessionid_hashtbl[idx], se_hash) {
		if (!memcmp(elem->se_sessionid.data, sessionid->data,
			    NFS4_MAX_SESSIONID_LEN)) {
			return elem;
		}
	}

	dprintk("%s: session not found\n", __func__);
	return NULL;
}

/* caller must hold client_lock */
static void
unhash_session(struct nfsd4_session *ses)
{
	list_del(&ses->se_hash);
	spin_lock(&ses->se_client->cl_lock);
	list_del(&ses->se_perclnt);
	spin_unlock(&ses->se_client->cl_lock);
}

/* must be called under the client_lock */
static inline void
renew_client_locked(struct nfs4_client *clp)
{
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	if (is_client_expired(clp)) {
		WARN_ON(1);
		printk("%s: client (clientid %08x/%08x) already expired\n",
			__func__,
			clp->cl_clientid.cl_boot,
			clp->cl_clientid.cl_id);
		return;
	}

	dprintk("renewing client (clientid %08x/%08x)\n", 
			clp->cl_clientid.cl_boot, 
			clp->cl_clientid.cl_id);
	list_move_tail(&clp->cl_lru, &nn->client_lru);
	clp->cl_time = get_seconds();
}

static inline void
renew_client(struct nfs4_client *clp)
{
	spin_lock(&client_lock);
	renew_client_locked(clp);
	spin_unlock(&client_lock);
}

/* SETCLIENTID and SETCLIENTID_CONFIRM Helper functions */
static int
STALE_CLIENTID(clientid_t *clid, struct nfsd_net *nn)
{
	if (clid->cl_boot == nn->boot_time)
		return 0;
	dprintk("NFSD stale clientid (%08x/%08x) boot_time %08lx\n",
		clid->cl_boot, clid->cl_id, nn->boot_time);
	return 1;
}

/* 
 * XXX Should we use a slab cache ?
 * This type of memory management is somewhat inefficient, but we use it
 * anyway since SETCLIENTID is not a common operation.
 */
static struct nfs4_client *alloc_client(struct xdr_netobj name)
{
	struct nfs4_client *clp;

	clp = kzalloc(sizeof(struct nfs4_client), GFP_KERNEL);
	if (clp == NULL)
		return NULL;
	clp->cl_name.data = kmemdup(name.data, name.len, GFP_KERNEL);
	if (clp->cl_name.data == NULL) {
		kfree(clp);
		return NULL;
	}
	clp->cl_name.len = name.len;
	return clp;
}

static inline void
free_client(struct nfs4_client *clp)
{
	lockdep_assert_held(&client_lock);
	while (!list_empty(&clp->cl_sessions)) {
		struct nfsd4_session *ses;
		ses = list_entry(clp->cl_sessions.next, struct nfsd4_session,
				se_perclnt);
		list_del(&ses->se_perclnt);
		nfsd4_put_session_locked(ses);
	}
	free_svc_cred(&clp->cl_cred);
	kfree(clp->cl_name.data);
	kfree(clp);
}

void
release_session_client(struct nfsd4_session *session)
{
	struct nfs4_client *clp = session->se_client;

	if (!atomic_dec_and_lock(&clp->cl_refcount, &client_lock))
		return;
	if (is_client_expired(clp)) {
		free_client(clp);
		session->se_client = NULL;
	} else
		renew_client_locked(clp);
	spin_unlock(&client_lock);
}

/* must be called under the client_lock */
static inline void
unhash_client_locked(struct nfs4_client *clp)
{
	struct nfsd4_session *ses;

	mark_client_expired(clp);
	list_del(&clp->cl_lru);
	spin_lock(&clp->cl_lock);
	list_for_each_entry(ses, &clp->cl_sessions, se_perclnt)
		list_del_init(&ses->se_hash);
	spin_unlock(&clp->cl_lock);
}

static void
destroy_client(struct nfs4_client *clp)
{
	struct nfs4_openowner *oo;
	struct nfs4_delegation *dp;
	struct list_head reaplist;
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	INIT_LIST_HEAD(&reaplist);
	spin_lock(&recall_lock);
	while (!list_empty(&clp->cl_delegations)) {
		dp = list_entry(clp->cl_delegations.next, struct nfs4_delegation, dl_perclnt);
		list_del_init(&dp->dl_perclnt);
		list_move(&dp->dl_recall_lru, &reaplist);
	}
	spin_unlock(&recall_lock);
	while (!list_empty(&reaplist)) {
		dp = list_entry(reaplist.next, struct nfs4_delegation, dl_recall_lru);
		unhash_delegation(dp);
	}
	while (!list_empty(&clp->cl_openowners)) {
		oo = list_entry(clp->cl_openowners.next, struct nfs4_openowner, oo_perclient);
		release_openowner(oo);
	}
	nfsd4_shutdown_callback(clp);
	if (clp->cl_cb_conn.cb_xprt)
		svc_xprt_put(clp->cl_cb_conn.cb_xprt);
	list_del(&clp->cl_idhash);
	if (test_bit(NFSD4_CLIENT_CONFIRMED, &clp->cl_flags))
		rb_erase(&clp->cl_namenode, &nn->conf_name_tree);
	else
		rb_erase(&clp->cl_namenode, &nn->unconf_name_tree);
	spin_lock(&client_lock);
	unhash_client_locked(clp);
	if (atomic_read(&clp->cl_refcount) == 0)
		free_client(clp);
	spin_unlock(&client_lock);
}

static void expire_client(struct nfs4_client *clp)
{
	nfsd4_client_record_remove(clp);
	destroy_client(clp);
}

static void copy_verf(struct nfs4_client *target, nfs4_verifier *source)
{
	memcpy(target->cl_verifier.data, source->data,
			sizeof(target->cl_verifier.data));
}

static void copy_clid(struct nfs4_client *target, struct nfs4_client *source)
{
	target->cl_clientid.cl_boot = source->cl_clientid.cl_boot; 
	target->cl_clientid.cl_id = source->cl_clientid.cl_id; 
}

static int copy_cred(struct svc_cred *target, struct svc_cred *source)
{
	if (source->cr_principal) {
		target->cr_principal =
				kstrdup(source->cr_principal, GFP_KERNEL);
		if (target->cr_principal == NULL)
			return -ENOMEM;
	} else
		target->cr_principal = NULL;
	target->cr_flavor = source->cr_flavor;
	target->cr_uid = source->cr_uid;
	target->cr_gid = source->cr_gid;
	target->cr_group_info = source->cr_group_info;
	get_group_info(target->cr_group_info);
	return 0;
}

static long long
compare_blob(const struct xdr_netobj *o1, const struct xdr_netobj *o2)
{
	long long res;

	res = o1->len - o2->len;
	if (res)
		return res;
	return (long long)memcmp(o1->data, o2->data, o1->len);
}

static int same_name(const char *n1, const char *n2)
{
	return 0 == memcmp(n1, n2, HEXDIR_LEN);
}

static int
same_verf(nfs4_verifier *v1, nfs4_verifier *v2)
{
	return 0 == memcmp(v1->data, v2->data, sizeof(v1->data));
}

static int
same_clid(clientid_t *cl1, clientid_t *cl2)
{
	return (cl1->cl_boot == cl2->cl_boot) && (cl1->cl_id == cl2->cl_id);
}

static bool groups_equal(struct group_info *g1, struct group_info *g2)
{
	int i;

	if (g1->ngroups != g2->ngroups)
		return false;
	for (i=0; i<g1->ngroups; i++)
		if (GROUP_AT(g1, i) != GROUP_AT(g2, i))
			return false;
	return true;
}

/*
 * RFC 3530 language requires clid_inuse be returned when the
 * "principal" associated with a requests differs from that previously
 * used.  We use uid, gid's, and gss principal string as our best
 * approximation.  We also don't want to allow non-gss use of a client
 * established using gss: in theory cr_principal should catch that
 * change, but in practice cr_principal can be null even in the gss case
 * since gssd doesn't always pass down a principal string.
 */
static bool is_gss_cred(struct svc_cred *cr)
{
	/* Is cr_flavor one of the gss "pseudoflavors"?: */
	return (cr->cr_flavor > RPC_AUTH_MAXFLAVOR);
}


static bool
same_creds(struct svc_cred *cr1, struct svc_cred *cr2)
{
	if ((is_gss_cred(cr1) != is_gss_cred(cr2))
		|| (cr1->cr_uid != cr2->cr_uid)
		|| (cr1->cr_gid != cr2->cr_gid)
		|| !groups_equal(cr1->cr_group_info, cr2->cr_group_info))
		return false;
	if (cr1->cr_principal == cr2->cr_principal)
		return true;
	if (!cr1->cr_principal || !cr2->cr_principal)
		return false;
	return 0 == strcmp(cr1->cr_principal, cr2->cr_principal);
}

static void gen_clid(struct nfs4_client *clp, struct nfsd_net *nn)
{
	static u32 current_clientid = 1;

	clp->cl_clientid.cl_boot = nn->boot_time;
	clp->cl_clientid.cl_id = current_clientid++; 
}

static void gen_confirm(struct nfs4_client *clp)
{
	__be32 verf[2];
	static u32 i;

	verf[0] = (__be32)get_seconds();
	verf[1] = (__be32)i++;
	memcpy(clp->cl_confirm.data, verf, sizeof(clp->cl_confirm.data));
}

static struct nfs4_stid *find_stateid(struct nfs4_client *cl, stateid_t *t)
{
	return idr_find(&cl->cl_stateids, t->si_opaque.so_id);
}

static struct nfs4_stid *find_stateid_by_type(struct nfs4_client *cl, stateid_t *t, char typemask)
{
	struct nfs4_stid *s;

	s = find_stateid(cl, t);
	if (!s)
		return NULL;
	if (typemask & s->sc_type)
		return s;
	return NULL;
}

static struct nfs4_client *create_client(struct xdr_netobj name,
		struct svc_rqst *rqstp, nfs4_verifier *verf)
{
	struct nfs4_client *clp;
	struct sockaddr *sa = svc_addr(rqstp);
	int ret;
	struct net *net = SVC_NET(rqstp);

	clp = alloc_client(name);
	if (clp == NULL)
		return NULL;

	INIT_LIST_HEAD(&clp->cl_sessions);
	ret = copy_cred(&clp->cl_cred, &rqstp->rq_cred);
	if (ret) {
		spin_lock(&client_lock);
		free_client(clp);
		spin_unlock(&client_lock);
		return NULL;
	}
	idr_init(&clp->cl_stateids);
	atomic_set(&clp->cl_refcount, 0);
	clp->cl_cb_state = NFSD4_CB_UNKNOWN;
	INIT_LIST_HEAD(&clp->cl_idhash);
	INIT_LIST_HEAD(&clp->cl_openowners);
	INIT_LIST_HEAD(&clp->cl_delegations);
	INIT_LIST_HEAD(&clp->cl_lru);
	INIT_LIST_HEAD(&clp->cl_callbacks);
	spin_lock_init(&clp->cl_lock);
	nfsd4_init_callback(&clp->cl_cb_null);
	clp->cl_time = get_seconds();
	clear_bit(0, &clp->cl_cb_slot_busy);
	rpc_init_wait_queue(&clp->cl_cb_waitq, "Backchannel slot table");
	copy_verf(clp, verf);
	rpc_copy_addr((struct sockaddr *) &clp->cl_addr, sa);
	gen_confirm(clp);
	clp->cl_cb_session = NULL;
	clp->net = net;
	return clp;
}

static void
add_clp_to_name_tree(struct nfs4_client *new_clp, struct rb_root *root)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;
	struct nfs4_client *clp;

	while (*new) {
		clp = rb_entry(*new, struct nfs4_client, cl_namenode);
		parent = *new;

		if (compare_blob(&clp->cl_name, &new_clp->cl_name) > 0)
			new = &((*new)->rb_left);
		else
			new = &((*new)->rb_right);
	}

	rb_link_node(&new_clp->cl_namenode, parent, new);
	rb_insert_color(&new_clp->cl_namenode, root);
}

static struct nfs4_client *
find_clp_in_name_tree(struct xdr_netobj *name, struct rb_root *root)
{
	long long cmp;
	struct rb_node *node = root->rb_node;
	struct nfs4_client *clp;

	while (node) {
		clp = rb_entry(node, struct nfs4_client, cl_namenode);
		cmp = compare_blob(&clp->cl_name, name);
		if (cmp > 0)
			node = node->rb_left;
		else if (cmp < 0)
			node = node->rb_right;
		else
			return clp;
	}
	return NULL;
}

static void
add_to_unconfirmed(struct nfs4_client *clp)
{
	unsigned int idhashval;
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	clear_bit(NFSD4_CLIENT_CONFIRMED, &clp->cl_flags);
	add_clp_to_name_tree(clp, &nn->unconf_name_tree);
	idhashval = clientid_hashval(clp->cl_clientid.cl_id);
	list_add(&clp->cl_idhash, &nn->unconf_id_hashtbl[idhashval]);
	renew_client(clp);
}

static void
move_to_confirmed(struct nfs4_client *clp)
{
	unsigned int idhashval = clientid_hashval(clp->cl_clientid.cl_id);
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	dprintk("NFSD: move_to_confirm nfs4_client %p\n", clp);
	list_move(&clp->cl_idhash, &nn->conf_id_hashtbl[idhashval]);
	rb_erase(&clp->cl_namenode, &nn->unconf_name_tree);
	add_clp_to_name_tree(clp, &nn->conf_name_tree);
	set_bit(NFSD4_CLIENT_CONFIRMED, &clp->cl_flags);
	renew_client(clp);
}

static struct nfs4_client *
find_confirmed_client(clientid_t *clid, bool sessions, struct nfsd_net *nn)
{
	struct nfs4_client *clp;
	unsigned int idhashval = clientid_hashval(clid->cl_id);

	list_for_each_entry(clp, &nn->conf_id_hashtbl[idhashval], cl_idhash) {
		if (same_clid(&clp->cl_clientid, clid)) {
			if ((bool)clp->cl_minorversion != sessions)
				return NULL;
			renew_client(clp);
			return clp;
		}
	}
	return NULL;
}

static struct nfs4_client *
find_unconfirmed_client(clientid_t *clid, bool sessions, struct nfsd_net *nn)
{
	struct nfs4_client *clp;
	unsigned int idhashval = clientid_hashval(clid->cl_id);

	list_for_each_entry(clp, &nn->unconf_id_hashtbl[idhashval], cl_idhash) {
		if (same_clid(&clp->cl_clientid, clid)) {
			if ((bool)clp->cl_minorversion != sessions)
				return NULL;
			return clp;
		}
	}
	return NULL;
}

static bool clp_used_exchangeid(struct nfs4_client *clp)
{
	return clp->cl_exchange_flags != 0;
} 

static struct nfs4_client *
find_confirmed_client_by_name(struct xdr_netobj *name, struct nfsd_net *nn)
{
	return find_clp_in_name_tree(name, &nn->conf_name_tree);
}

static struct nfs4_client *
find_unconfirmed_client_by_name(struct xdr_netobj *name, struct nfsd_net *nn)
{
	return find_clp_in_name_tree(name, &nn->unconf_name_tree);
}

static void
gen_callback(struct nfs4_client *clp, struct nfsd4_setclientid *se, struct svc_rqst *rqstp)
{
	struct nfs4_cb_conn *conn = &clp->cl_cb_conn;
	struct sockaddr	*sa = svc_addr(rqstp);
	u32 scopeid = rpc_get_scope_id(sa);
	unsigned short expected_family;

	/* Currently, we only support tcp and tcp6 for the callback channel */
	if (se->se_callback_netid_len == 3 &&
	    !memcmp(se->se_callback_netid_val, "tcp", 3))
		expected_family = AF_INET;
	else if (se->se_callback_netid_len == 4 &&
		 !memcmp(se->se_callback_netid_val, "tcp6", 4))
		expected_family = AF_INET6;
	else
		goto out_err;

	conn->cb_addrlen = rpc_uaddr2sockaddr(clp->net, se->se_callback_addr_val,
					    se->se_callback_addr_len,
					    (struct sockaddr *)&conn->cb_addr,
					    sizeof(conn->cb_addr));

	if (!conn->cb_addrlen || conn->cb_addr.ss_family != expected_family)
		goto out_err;

	if (conn->cb_addr.ss_family == AF_INET6)
		((struct sockaddr_in6 *)&conn->cb_addr)->sin6_scope_id = scopeid;

	conn->cb_prog = se->se_callback_prog;
	conn->cb_ident = se->se_callback_ident;
	memcpy(&conn->cb_saddr, &rqstp->rq_daddr, rqstp->rq_daddrlen);
	return;
out_err:
	conn->cb_addr.ss_family = AF_UNSPEC;
	conn->cb_addrlen = 0;
	dprintk(KERN_INFO "NFSD: this client (clientid %08x/%08x) "
		"will not receive delegations\n",
		clp->cl_clientid.cl_boot, clp->cl_clientid.cl_id);

	return;
}

/*
 * Cache a reply. nfsd4_check_drc_limit() has bounded the cache size.
 */
void
nfsd4_store_cache_entry(struct nfsd4_compoundres *resp)
{
	struct nfsd4_slot *slot = resp->cstate.slot;
	unsigned int base;

	dprintk("--> %s slot %p\n", __func__, slot);

	slot->sl_opcnt = resp->opcnt;
	slot->sl_status = resp->cstate.status;

	slot->sl_flags |= NFSD4_SLOT_INITIALIZED;
	if (nfsd4_not_cached(resp)) {
		slot->sl_datalen = 0;
		return;
	}
	slot->sl_datalen = (char *)resp->p - (char *)resp->cstate.datap;
	base = (char *)resp->cstate.datap -
					(char *)resp->xbuf->head[0].iov_base;
	if (read_bytes_from_xdr_buf(resp->xbuf, base, slot->sl_data,
				    slot->sl_datalen))
		WARN("%s: sessions DRC could not cache compound\n", __func__);
	return;
}

/*
 * Encode the replay sequence operation from the slot values.
 * If cachethis is FALSE encode the uncached rep error on the next
 * operation which sets resp->p and increments resp->opcnt for
 * nfs4svc_encode_compoundres.
 *
 */
static __be32
nfsd4_enc_sequence_replay(struct nfsd4_compoundargs *args,
			  struct nfsd4_compoundres *resp)
{
	struct nfsd4_op *op;
	struct nfsd4_slot *slot = resp->cstate.slot;

	/* Encode the replayed sequence operation */
	op = &args->ops[resp->opcnt - 1];
	nfsd4_encode_operation(resp, op);

	/* Return nfserr_retry_uncached_rep in next operation. */
	if (args->opcnt > 1 && !(slot->sl_flags & NFSD4_SLOT_CACHETHIS)) {
		op = &args->ops[resp->opcnt++];
		op->status = nfserr_retry_uncached_rep;
		nfsd4_encode_operation(resp, op);
	}
	return op->status;
}

/*
 * The sequence operation is not cached because we can use the slot and
 * session values.
 */
__be32
nfsd4_replay_cache_entry(struct nfsd4_compoundres *resp,
			 struct nfsd4_sequence *seq)
{
	struct nfsd4_slot *slot = resp->cstate.slot;
	__be32 status;

	dprintk("--> %s slot %p\n", __func__, slot);

	/* Either returns 0 or nfserr_retry_uncached */
	status = nfsd4_enc_sequence_replay(resp->rqstp->rq_argp, resp);
	if (status == nfserr_retry_uncached_rep)
		return status;

	/* The sequence operation has been encoded, cstate->datap set. */
	memcpy(resp->cstate.datap, slot->sl_data, slot->sl_datalen);

	resp->opcnt = slot->sl_opcnt;
	resp->p = resp->cstate.datap + XDR_QUADLEN(slot->sl_datalen);
	status = slot->sl_status;

	return status;
}

/*
 * Set the exchange_id flags returned by the server.
 */
static void
nfsd4_set_ex_flags(struct nfs4_client *new, struct nfsd4_exchange_id *clid)
{
	/* pNFS is not supported */
	new->cl_exchange_flags |= EXCHGID4_FLAG_USE_NON_PNFS;

	/* Referrals are supported, Migration is not. */
	new->cl_exchange_flags |= EXCHGID4_FLAG_SUPP_MOVED_REFER;

	/* set the wire flags to return to client. */
	clid->flags = new->cl_exchange_flags;
}

static bool client_has_state(struct nfs4_client *clp)
{
	/*
	 * Note clp->cl_openowners check isn't quite right: there's no
	 * need to count owners without stateid's.
	 *
	 * Also note we should probably be using this in 4.0 case too.
	 */
	return !list_empty(&clp->cl_openowners)
		|| !list_empty(&clp->cl_delegations)
		|| !list_empty(&clp->cl_sessions);
}

__be32
nfsd4_exchange_id(struct svc_rqst *rqstp,
		  struct nfsd4_compound_state *cstate,
		  struct nfsd4_exchange_id *exid)
{
	struct nfs4_client *unconf, *conf, *new;
	__be32 status;
	char			addr_str[INET6_ADDRSTRLEN];
	nfs4_verifier		verf = exid->verifier;
	struct sockaddr		*sa = svc_addr(rqstp);
	bool	update = exid->flags & EXCHGID4_FLAG_UPD_CONFIRMED_REC_A;
	struct nfsd_net		*nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	rpc_ntop(sa, addr_str, sizeof(addr_str));
	dprintk("%s rqstp=%p exid=%p clname.len=%u clname.data=%p "
		"ip_addr=%s flags %x, spa_how %d\n",
		__func__, rqstp, exid, exid->clname.len, exid->clname.data,
		addr_str, exid->flags, exid->spa_how);

	if (exid->flags & ~EXCHGID4_FLAG_MASK_A)
		return nfserr_inval;

	/* Currently only support SP4_NONE */
	switch (exid->spa_how) {
	case SP4_NONE:
		break;
	case SP4_SSV:
		return nfserr_serverfault;
	default:
		BUG();				/* checked by xdr code */
	case SP4_MACH_CRED:
		return nfserr_serverfault;	/* no excuse :-/ */
	}

	/* Cases below refer to rfc 5661 section 18.35.4: */
	nfs4_lock_state();
	conf = find_confirmed_client_by_name(&exid->clname, nn);
	if (conf) {
		bool creds_match = same_creds(&conf->cl_cred, &rqstp->rq_cred);
		bool verfs_match = same_verf(&verf, &conf->cl_verifier);

		if (update) {
			if (!clp_used_exchangeid(conf)) { /* buggy client */
				status = nfserr_inval;
				goto out;
			}
			if (!creds_match) { /* case 9 */
				status = nfserr_perm;
				goto out;
			}
			if (!verfs_match) { /* case 8 */
				status = nfserr_not_same;
				goto out;
			}
			/* case 6 */
			exid->flags |= EXCHGID4_FLAG_CONFIRMED_R;
			new = conf;
			goto out_copy;
		}
		if (!creds_match) { /* case 3 */
			if (client_has_state(conf)) {
				status = nfserr_clid_inuse;
				goto out;
			}
			expire_client(conf);
			goto out_new;
		}
		if (verfs_match) { /* case 2 */
			conf->cl_exchange_flags |= EXCHGID4_FLAG_CONFIRMED_R;
			new = conf;
			goto out_copy;
		}
		/* case 5, client reboot */
		goto out_new;
	}

	if (update) { /* case 7 */
		status = nfserr_noent;
		goto out;
	}

	unconf  = find_unconfirmed_client_by_name(&exid->clname, nn);
	if (unconf) /* case 4, possible retry or client restart */
		expire_client(unconf);

	/* case 1 (normal case) */
out_new:
	new = create_client(exid->clname, rqstp, &verf);
	if (new == NULL) {
		status = nfserr_jukebox;
		goto out;
	}
	new->cl_minorversion = 1;

	gen_clid(new, nn);
	add_to_unconfirmed(new);
out_copy:
	exid->clientid.cl_boot = new->cl_clientid.cl_boot;
	exid->clientid.cl_id = new->cl_clientid.cl_id;

	exid->seqid = new->cl_cs_slot.sl_seqid + 1;
	nfsd4_set_ex_flags(new, exid);

	dprintk("nfsd4_exchange_id seqid %d flags %x\n",
		new->cl_cs_slot.sl_seqid, new->cl_exchange_flags);
	status = nfs_ok;

out:
	nfs4_unlock_state();
	return status;
}

static __be32
check_slot_seqid(u32 seqid, u32 slot_seqid, int slot_inuse)
{
	dprintk("%s enter. seqid %d slot_seqid %d\n", __func__, seqid,
		slot_seqid);

	/* The slot is in use, and no response has been sent. */
	if (slot_inuse) {
		if (seqid == slot_seqid)
			return nfserr_jukebox;
		else
			return nfserr_seq_misordered;
	}
	/* Note unsigned 32-bit arithmetic handles wraparound: */
	if (likely(seqid == slot_seqid + 1))
		return nfs_ok;
	if (seqid == slot_seqid)
		return nfserr_replay_cache;
	return nfserr_seq_misordered;
}

/*
 * Cache the create session result into the create session single DRC
 * slot cache by saving the xdr structure. sl_seqid has been set.
 * Do this for solo or embedded create session operations.
 */
static void
nfsd4_cache_create_session(struct nfsd4_create_session *cr_ses,
			   struct nfsd4_clid_slot *slot, __be32 nfserr)
{
	slot->sl_status = nfserr;
	memcpy(&slot->sl_cr_ses, cr_ses, sizeof(*cr_ses));
}

static __be32
nfsd4_replay_create_session(struct nfsd4_create_session *cr_ses,
			    struct nfsd4_clid_slot *slot)
{
	memcpy(cr_ses, &slot->sl_cr_ses, sizeof(*cr_ses));
	return slot->sl_status;
}

#define NFSD_MIN_REQ_HDR_SEQ_SZ	((\
			2 * 2 + /* credential,verifier: AUTH_NULL, length 0 */ \
			1 +	/* MIN tag is length with zero, only length */ \
			3 +	/* version, opcount, opcode */ \
			XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN) + \
				/* seqid, slotID, slotID, cache */ \
			4 ) * sizeof(__be32))

#define NFSD_MIN_RESP_HDR_SEQ_SZ ((\
			2 +	/* verifier: AUTH_NULL, length 0 */\
			1 +	/* status */ \
			1 +	/* MIN tag is length with zero, only length */ \
			3 +	/* opcount, opcode, opstatus*/ \
			XDR_QUADLEN(NFS4_MAX_SESSIONID_LEN) + \
				/* seqid, slotID, slotID, slotID, status */ \
			5 ) * sizeof(__be32))

static bool check_forechannel_attrs(struct nfsd4_channel_attrs fchannel)
{
	return fchannel.maxreq_sz < NFSD_MIN_REQ_HDR_SEQ_SZ
		|| fchannel.maxresp_sz < NFSD_MIN_RESP_HDR_SEQ_SZ;
}

__be32
nfsd4_create_session(struct svc_rqst *rqstp,
		     struct nfsd4_compound_state *cstate,
		     struct nfsd4_create_session *cr_ses)
{
	struct sockaddr *sa = svc_addr(rqstp);
	struct nfs4_client *conf, *unconf;
	struct nfsd4_session *new;
	struct nfsd4_conn *conn;
	struct nfsd4_clid_slot *cs_slot = NULL;
	__be32 status = 0;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	if (cr_ses->flags & ~SESSION4_FLAG_MASK_A)
		return nfserr_inval;
	if (check_forechannel_attrs(cr_ses->fore_channel))
		return nfserr_toosmall;
	new = alloc_session(&cr_ses->fore_channel);
	if (!new)
		return nfserr_jukebox;
	status = nfserr_jukebox;
	conn = alloc_conn_from_crses(rqstp, cr_ses);
	if (!conn)
		goto out_free_session;

	nfs4_lock_state();
	unconf = find_unconfirmed_client(&cr_ses->clientid, true, nn);
	conf = find_confirmed_client(&cr_ses->clientid, true, nn);

	if (conf) {
		cs_slot = &conf->cl_cs_slot;
		status = check_slot_seqid(cr_ses->seqid, cs_slot->sl_seqid, 0);
		if (status == nfserr_replay_cache) {
			status = nfsd4_replay_create_session(cr_ses, cs_slot);
			goto out_free_conn;
		} else if (cr_ses->seqid != cs_slot->sl_seqid + 1) {
			status = nfserr_seq_misordered;
			goto out_free_conn;
		}
	} else if (unconf) {
		struct nfs4_client *old;
		if (!same_creds(&unconf->cl_cred, &rqstp->rq_cred) ||
		    !rpc_cmp_addr(sa, (struct sockaddr *) &unconf->cl_addr)) {
			status = nfserr_clid_inuse;
			goto out_free_conn;
		}
		cs_slot = &unconf->cl_cs_slot;
		status = check_slot_seqid(cr_ses->seqid, cs_slot->sl_seqid, 0);
		if (status) {
			/* an unconfirmed replay returns misordered */
			status = nfserr_seq_misordered;
			goto out_free_conn;
		}
		old = find_confirmed_client_by_name(&unconf->cl_name, nn);
		if (old)
			expire_client(old);
		move_to_confirmed(unconf);
		conf = unconf;
	} else {
		status = nfserr_stale_clientid;
		goto out_free_conn;
	}
	status = nfs_ok;
	/*
	 * We do not support RDMA or persistent sessions
	 */
	cr_ses->flags &= ~SESSION4_PERSIST;
	cr_ses->flags &= ~SESSION4_RDMA;

	init_session(rqstp, new, conf, cr_ses);
	nfsd4_init_conn(rqstp, conn, new);

	memcpy(cr_ses->sessionid.data, new->se_sessionid.data,
	       NFS4_MAX_SESSIONID_LEN);
	memcpy(&cr_ses->fore_channel, &new->se_fchannel,
		sizeof(struct nfsd4_channel_attrs));
	cs_slot->sl_seqid++;
	cr_ses->seqid = cs_slot->sl_seqid;

	/* cache solo and embedded create sessions under the state lock */
	nfsd4_cache_create_session(cr_ses, cs_slot, status);
out:
	nfs4_unlock_state();
	dprintk("%s returns %d\n", __func__, ntohl(status));
	return status;
out_free_conn:
	free_conn(conn);
out_free_session:
	__free_session(new);
	goto out;
}

static bool nfsd4_last_compound_op(struct svc_rqst *rqstp)
{
	struct nfsd4_compoundres *resp = rqstp->rq_resp;
	struct nfsd4_compoundargs *argp = rqstp->rq_argp;

	return argp->opcnt == resp->opcnt;
}

static __be32 nfsd4_map_bcts_dir(u32 *dir)
{
	switch (*dir) {
	case NFS4_CDFC4_FORE:
	case NFS4_CDFC4_BACK:
		return nfs_ok;
	case NFS4_CDFC4_FORE_OR_BOTH:
	case NFS4_CDFC4_BACK_OR_BOTH:
		*dir = NFS4_CDFC4_BOTH;
		return nfs_ok;
	};
	return nfserr_inval;
}

__be32 nfsd4_backchannel_ctl(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate, struct nfsd4_backchannel_ctl *bc)
{
	struct nfsd4_session *session = cstate->session;

	spin_lock(&client_lock);
	session->se_cb_prog = bc->bc_cb_program;
	session->se_cb_sec = bc->bc_cb_sec;
	spin_unlock(&client_lock);

	nfsd4_probe_callback(session->se_client);

	return nfs_ok;
}

__be32 nfsd4_bind_conn_to_session(struct svc_rqst *rqstp,
		     struct nfsd4_compound_state *cstate,
		     struct nfsd4_bind_conn_to_session *bcts)
{
	__be32 status;
	struct nfsd4_conn *conn;

	if (!nfsd4_last_compound_op(rqstp))
		return nfserr_not_only_op;
	spin_lock(&client_lock);
	cstate->session = find_in_sessionid_hashtbl(&bcts->sessionid, SVC_NET(rqstp));
	/* Sorta weird: we only need the refcnt'ing because new_conn acquires
	 * client_lock iself: */
	if (cstate->session) {
		nfsd4_get_session(cstate->session);
		atomic_inc(&cstate->session->se_client->cl_refcount);
	}
	spin_unlock(&client_lock);
	if (!cstate->session)
		return nfserr_badsession;

	status = nfsd4_map_bcts_dir(&bcts->dir);
	if (status)
		return status;
	conn = alloc_conn(rqstp, bcts->dir);
	if (!conn)
		return nfserr_jukebox;
	nfsd4_init_conn(rqstp, conn, cstate->session);
	return nfs_ok;
}

static bool nfsd4_compound_in_session(struct nfsd4_session *session, struct nfs4_sessionid *sid)
{
	if (!session)
		return 0;
	return !memcmp(sid, &session->se_sessionid, sizeof(*sid));
}

__be32
nfsd4_destroy_session(struct svc_rqst *r,
		      struct nfsd4_compound_state *cstate,
		      struct nfsd4_destroy_session *sessionid)
{
	struct nfsd4_session *ses;
	__be32 status = nfserr_badsession;

	/* Notes:
	 * - The confirmed nfs4_client->cl_sessionid holds destroyed sessinid
	 * - Should we return nfserr_back_chan_busy if waiting for
	 *   callbacks on to-be-destroyed session?
	 * - Do we need to clear any callback info from previous session?
	 */

	if (nfsd4_compound_in_session(cstate->session, &sessionid->sessionid)) {
		if (!nfsd4_last_compound_op(r))
			return nfserr_not_only_op;
	}
	dump_sessionid(__func__, &sessionid->sessionid);
	spin_lock(&client_lock);
	ses = find_in_sessionid_hashtbl(&sessionid->sessionid, SVC_NET(r));
	if (!ses) {
		spin_unlock(&client_lock);
		goto out;
	}

	unhash_session(ses);
	spin_unlock(&client_lock);

	nfs4_lock_state();
	nfsd4_probe_callback_sync(ses->se_client);
	nfs4_unlock_state();

	spin_lock(&client_lock);
	nfsd4_del_conns(ses);
	nfsd4_put_session_locked(ses);
	spin_unlock(&client_lock);
	status = nfs_ok;
out:
	dprintk("%s returns %d\n", __func__, ntohl(status));
	return status;
}

static struct nfsd4_conn *__nfsd4_find_conn(struct svc_xprt *xpt, struct nfsd4_session *s)
{
	struct nfsd4_conn *c;

	list_for_each_entry(c, &s->se_conns, cn_persession) {
		if (c->cn_xprt == xpt) {
			return c;
		}
	}
	return NULL;
}

static void nfsd4_sequence_check_conn(struct nfsd4_conn *new, struct nfsd4_session *ses)
{
	struct nfs4_client *clp = ses->se_client;
	struct nfsd4_conn *c;
	int ret;

	spin_lock(&clp->cl_lock);
	c = __nfsd4_find_conn(new->cn_xprt, ses);
	if (c) {
		spin_unlock(&clp->cl_lock);
		free_conn(new);
		return;
	}
	__nfsd4_hash_conn(new, ses);
	spin_unlock(&clp->cl_lock);
	ret = nfsd4_register_conn(new);
	if (ret)
		/* oops; xprt is already down: */
		nfsd4_conn_lost(&new->cn_xpt_user);
	return;
}

static bool nfsd4_session_too_many_ops(struct svc_rqst *rqstp, struct nfsd4_session *session)
{
	struct nfsd4_compoundargs *args = rqstp->rq_argp;

	return args->opcnt > session->se_fchannel.maxops;
}

static bool nfsd4_request_too_big(struct svc_rqst *rqstp,
				  struct nfsd4_session *session)
{
	struct xdr_buf *xb = &rqstp->rq_arg;

	return xb->len > session->se_fchannel.maxreq_sz;
}

__be32
nfsd4_sequence(struct svc_rqst *rqstp,
	       struct nfsd4_compound_state *cstate,
	       struct nfsd4_sequence *seq)
{
	struct nfsd4_compoundres *resp = rqstp->rq_resp;
	struct nfsd4_session *session;
	struct nfsd4_slot *slot;
	struct nfsd4_conn *conn;
	__be32 status;

	if (resp->opcnt != 1)
		return nfserr_sequence_pos;

	/*
	 * Will be either used or freed by nfsd4_sequence_check_conn
	 * below.
	 */
	conn = alloc_conn(rqstp, NFS4_CDFC4_FORE);
	if (!conn)
		return nfserr_jukebox;

	spin_lock(&client_lock);
	status = nfserr_badsession;
	session = find_in_sessionid_hashtbl(&seq->sessionid, SVC_NET(rqstp));
	if (!session)
		goto out;

	status = nfserr_too_many_ops;
	if (nfsd4_session_too_many_ops(rqstp, session))
		goto out;

	status = nfserr_req_too_big;
	if (nfsd4_request_too_big(rqstp, session))
		goto out;

	status = nfserr_badslot;
	if (seq->slotid >= session->se_fchannel.maxreqs)
		goto out;

	slot = session->se_slots[seq->slotid];
	dprintk("%s: slotid %d\n", __func__, seq->slotid);

	/* We do not negotiate the number of slots yet, so set the
	 * maxslots to the session maxreqs which is used to encode
	 * sr_highest_slotid and the sr_target_slot id to maxslots */
	seq->maxslots = session->se_fchannel.maxreqs;

	status = check_slot_seqid(seq->seqid, slot->sl_seqid,
					slot->sl_flags & NFSD4_SLOT_INUSE);
	if (status == nfserr_replay_cache) {
		status = nfserr_seq_misordered;
		if (!(slot->sl_flags & NFSD4_SLOT_INITIALIZED))
			goto out;
		cstate->slot = slot;
		cstate->session = session;
		/* Return the cached reply status and set cstate->status
		 * for nfsd4_proc_compound processing */
		status = nfsd4_replay_cache_entry(resp, seq);
		cstate->status = nfserr_replay_cache;
		goto out;
	}
	if (status)
		goto out;

	nfsd4_sequence_check_conn(conn, session);
	conn = NULL;

	/* Success! bump slot seqid */
	slot->sl_seqid = seq->seqid;
	slot->sl_flags |= NFSD4_SLOT_INUSE;
	if (seq->cachethis)
		slot->sl_flags |= NFSD4_SLOT_CACHETHIS;
	else
		slot->sl_flags &= ~NFSD4_SLOT_CACHETHIS;

	cstate->slot = slot;
	cstate->session = session;

out:
	/* Hold a session reference until done processing the compound. */
	if (cstate->session) {
		struct nfs4_client *clp = session->se_client;

		nfsd4_get_session(cstate->session);
		atomic_inc(&clp->cl_refcount);
		switch (clp->cl_cb_state) {
		case NFSD4_CB_DOWN:
			seq->status_flags = SEQ4_STATUS_CB_PATH_DOWN;
			break;
		case NFSD4_CB_FAULT:
			seq->status_flags = SEQ4_STATUS_BACKCHANNEL_FAULT;
			break;
		default:
			seq->status_flags = 0;
		}
	}
	kfree(conn);
	spin_unlock(&client_lock);
	dprintk("%s: return %d\n", __func__, ntohl(status));
	return status;
}

__be32
nfsd4_destroy_clientid(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate, struct nfsd4_destroy_clientid *dc)
{
	struct nfs4_client *conf, *unconf, *clp;
	__be32 status = 0;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	nfs4_lock_state();
	unconf = find_unconfirmed_client(&dc->clientid, true, nn);
	conf = find_confirmed_client(&dc->clientid, true, nn);

	if (conf) {
		clp = conf;

		if (!is_client_expired(conf) && client_has_state(conf)) {
			status = nfserr_clientid_busy;
			goto out;
		}

		/* rfc5661 18.50.3 */
		if (cstate->session && conf == cstate->session->se_client) {
			status = nfserr_clientid_busy;
			goto out;
		}
	} else if (unconf)
		clp = unconf;
	else {
		status = nfserr_stale_clientid;
		goto out;
	}

	expire_client(clp);
out:
	nfs4_unlock_state();
	dprintk("%s return %d\n", __func__, ntohl(status));
	return status;
}

__be32
nfsd4_reclaim_complete(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate, struct nfsd4_reclaim_complete *rc)
{
	__be32 status = 0;

	if (rc->rca_one_fs) {
		if (!cstate->current_fh.fh_dentry)
			return nfserr_nofilehandle;
		/*
		 * We don't take advantage of the rca_one_fs case.
		 * That's OK, it's optional, we can safely ignore it.
		 */
		 return nfs_ok;
	}

	nfs4_lock_state();
	status = nfserr_complete_already;
	if (test_and_set_bit(NFSD4_CLIENT_RECLAIM_COMPLETE,
			     &cstate->session->se_client->cl_flags))
		goto out;

	status = nfserr_stale_clientid;
	if (is_client_expired(cstate->session->se_client))
		/*
		 * The following error isn't really legal.
		 * But we only get here if the client just explicitly
		 * destroyed the client.  Surely it no longer cares what
		 * error it gets back on an operation for the dead
		 * client.
		 */
		goto out;

	status = nfs_ok;
	nfsd4_client_record_create(cstate->session->se_client);
out:
	nfs4_unlock_state();
	return status;
}

__be32
nfsd4_setclientid(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		  struct nfsd4_setclientid *setclid)
{
	struct xdr_netobj 	clname = setclid->se_name;
	nfs4_verifier		clverifier = setclid->se_verf;
	struct nfs4_client	*conf, *unconf, *new;
	__be32 			status;
	struct nfsd_net		*nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	/* Cases below refer to rfc 3530 section 14.2.33: */
	nfs4_lock_state();
	conf = find_confirmed_client_by_name(&clname, nn);
	if (conf) {
		/* case 0: */
		status = nfserr_clid_inuse;
		if (clp_used_exchangeid(conf))
			goto out;
		if (!same_creds(&conf->cl_cred, &rqstp->rq_cred)) {
			char addr_str[INET6_ADDRSTRLEN];
			rpc_ntop((struct sockaddr *) &conf->cl_addr, addr_str,
				 sizeof(addr_str));
			dprintk("NFSD: setclientid: string in use by client "
				"at %s\n", addr_str);
			goto out;
		}
	}
	unconf = find_unconfirmed_client_by_name(&clname, nn);
	if (unconf)
		expire_client(unconf);
	status = nfserr_jukebox;
	new = create_client(clname, rqstp, &clverifier);
	if (new == NULL)
		goto out;
	if (conf && same_verf(&conf->cl_verifier, &clverifier))
		/* case 1: probable callback update */
		copy_clid(new, conf);
	else /* case 4 (new client) or cases 2, 3 (client reboot): */
		gen_clid(new, nn);
	new->cl_minorversion = 0;
	gen_callback(new, setclid, rqstp);
	add_to_unconfirmed(new);
	setclid->se_clientid.cl_boot = new->cl_clientid.cl_boot;
	setclid->se_clientid.cl_id = new->cl_clientid.cl_id;
	memcpy(setclid->se_confirm.data, new->cl_confirm.data, sizeof(setclid->se_confirm.data));
	status = nfs_ok;
out:
	nfs4_unlock_state();
	return status;
}


__be32
nfsd4_setclientid_confirm(struct svc_rqst *rqstp,
			 struct nfsd4_compound_state *cstate,
			 struct nfsd4_setclientid_confirm *setclientid_confirm)
{
	struct nfs4_client *conf, *unconf;
	nfs4_verifier confirm = setclientid_confirm->sc_confirm; 
	clientid_t * clid = &setclientid_confirm->sc_clientid;
	__be32 status;
	struct nfsd_net	*nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	if (STALE_CLIENTID(clid, nn))
		return nfserr_stale_clientid;
	nfs4_lock_state();

	conf = find_confirmed_client(clid, false, nn);
	unconf = find_unconfirmed_client(clid, false, nn);
	/*
	 * We try hard to give out unique clientid's, so if we get an
	 * attempt to confirm the same clientid with a different cred,
	 * there's a bug somewhere.  Let's charitably assume it's our
	 * bug.
	 */
	status = nfserr_serverfault;
	if (unconf && !same_creds(&unconf->cl_cred, &rqstp->rq_cred))
		goto out;
	if (conf && !same_creds(&conf->cl_cred, &rqstp->rq_cred))
		goto out;
	/* cases below refer to rfc 3530 section 14.2.34: */
	if (!unconf || !same_verf(&confirm, &unconf->cl_confirm)) {
		if (conf && !unconf) /* case 2: probable retransmit */
			status = nfs_ok;
		else /* case 4: client hasn't noticed we rebooted yet? */
			status = nfserr_stale_clientid;
		goto out;
	}
	status = nfs_ok;
	if (conf) { /* case 1: callback update */
		nfsd4_change_callback(conf, &unconf->cl_cb_conn);
		nfsd4_probe_callback(conf);
		expire_client(unconf);
	} else { /* case 3: normal case; new or rebooted client */
		conf = find_confirmed_client_by_name(&unconf->cl_name, nn);
		if (conf)
			expire_client(conf);
		move_to_confirmed(unconf);
		nfsd4_probe_callback(unconf);
	}
out:
	nfs4_unlock_state();
	return status;
}

static struct nfs4_file *nfsd4_alloc_file(void)
{
	return kmem_cache_alloc(file_slab, GFP_KERNEL);
}

/* OPEN Share state helper functions */
static void nfsd4_init_file(struct nfs4_file *fp, struct inode *ino)
{
	unsigned int hashval = file_hashval(ino);

	atomic_set(&fp->fi_ref, 1);
	INIT_LIST_HEAD(&fp->fi_hash);
	INIT_LIST_HEAD(&fp->fi_stateids);
	INIT_LIST_HEAD(&fp->fi_delegations);
	fp->fi_inode = igrab(ino);
	fp->fi_had_conflict = false;
	fp->fi_lease = NULL;
	memset(fp->fi_fds, 0, sizeof(fp->fi_fds));
	memset(fp->fi_access, 0, sizeof(fp->fi_access));
	spin_lock(&recall_lock);
	list_add(&fp->fi_hash, &file_hashtbl[hashval]);
	spin_unlock(&recall_lock);
}

static void
nfsd4_free_slab(struct kmem_cache **slab)
{
	if (*slab == NULL)
		return;
	kmem_cache_destroy(*slab);
	*slab = NULL;
}

void
nfsd4_free_slabs(void)
{
	nfsd4_free_slab(&openowner_slab);
	nfsd4_free_slab(&lockowner_slab);
	nfsd4_free_slab(&file_slab);
	nfsd4_free_slab(&stateid_slab);
	nfsd4_free_slab(&deleg_slab);
}

int
nfsd4_init_slabs(void)
{
	openowner_slab = kmem_cache_create("nfsd4_openowners",
			sizeof(struct nfs4_openowner), 0, 0, NULL);
	if (openowner_slab == NULL)
		goto out_nomem;
	lockowner_slab = kmem_cache_create("nfsd4_lockowners",
			sizeof(struct nfs4_lockowner), 0, 0, NULL);
	if (lockowner_slab == NULL)
		goto out_nomem;
	file_slab = kmem_cache_create("nfsd4_files",
			sizeof(struct nfs4_file), 0, 0, NULL);
	if (file_slab == NULL)
		goto out_nomem;
	stateid_slab = kmem_cache_create("nfsd4_stateids",
			sizeof(struct nfs4_ol_stateid), 0, 0, NULL);
	if (stateid_slab == NULL)
		goto out_nomem;
	deleg_slab = kmem_cache_create("nfsd4_delegations",
			sizeof(struct nfs4_delegation), 0, 0, NULL);
	if (deleg_slab == NULL)
		goto out_nomem;
	return 0;
out_nomem:
	nfsd4_free_slabs();
	dprintk("nfsd4: out of memory while initializing nfsv4\n");
	return -ENOMEM;
}

void nfs4_free_openowner(struct nfs4_openowner *oo)
{
	kfree(oo->oo_owner.so_owner.data);
	kmem_cache_free(openowner_slab, oo);
}

void nfs4_free_lockowner(struct nfs4_lockowner *lo)
{
	kfree(lo->lo_owner.so_owner.data);
	kmem_cache_free(lockowner_slab, lo);
}

static void init_nfs4_replay(struct nfs4_replay *rp)
{
	rp->rp_status = nfserr_serverfault;
	rp->rp_buflen = 0;
	rp->rp_buf = rp->rp_ibuf;
}

static inline void *alloc_stateowner(struct kmem_cache *slab, struct xdr_netobj *owner, struct nfs4_client *clp)
{
	struct nfs4_stateowner *sop;

	sop = kmem_cache_alloc(slab, GFP_KERNEL);
	if (!sop)
		return NULL;

	sop->so_owner.data = kmemdup(owner->data, owner->len, GFP_KERNEL);
	if (!sop->so_owner.data) {
		kmem_cache_free(slab, sop);
		return NULL;
	}
	sop->so_owner.len = owner->len;

	INIT_LIST_HEAD(&sop->so_stateids);
	sop->so_client = clp;
	init_nfs4_replay(&sop->so_replay);
	return sop;
}

static void hash_openowner(struct nfs4_openowner *oo, struct nfs4_client *clp, unsigned int strhashval)
{
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	list_add(&oo->oo_owner.so_strhash, &nn->ownerstr_hashtbl[strhashval]);
	list_add(&oo->oo_perclient, &clp->cl_openowners);
}

static struct nfs4_openowner *
alloc_init_open_stateowner(unsigned int strhashval, struct nfs4_client *clp, struct nfsd4_open *open) {
	struct nfs4_openowner *oo;

	oo = alloc_stateowner(openowner_slab, &open->op_owner, clp);
	if (!oo)
		return NULL;
	oo->oo_owner.so_is_open_owner = 1;
	oo->oo_owner.so_seqid = open->op_seqid;
	oo->oo_flags = NFS4_OO_NEW;
	oo->oo_time = 0;
	oo->oo_last_closed_stid = NULL;
	INIT_LIST_HEAD(&oo->oo_close_lru);
	hash_openowner(oo, clp, strhashval);
	return oo;
}

static void init_open_stateid(struct nfs4_ol_stateid *stp, struct nfs4_file *fp, struct nfsd4_open *open) {
	struct nfs4_openowner *oo = open->op_openowner;
	struct nfs4_client *clp = oo->oo_owner.so_client;

	init_stid(&stp->st_stid, clp, NFS4_OPEN_STID);
	INIT_LIST_HEAD(&stp->st_lockowners);
	list_add(&stp->st_perstateowner, &oo->oo_owner.so_stateids);
	list_add(&stp->st_perfile, &fp->fi_stateids);
	stp->st_stateowner = &oo->oo_owner;
	get_nfs4_file(fp);
	stp->st_file = fp;
	stp->st_access_bmap = 0;
	stp->st_deny_bmap = 0;
	set_access(open->op_share_access, stp);
	set_deny(open->op_share_deny, stp);
	stp->st_openstp = NULL;
}

static void
move_to_close_lru(struct nfs4_openowner *oo, struct net *net)
{
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	dprintk("NFSD: move_to_close_lru nfs4_openowner %p\n", oo);

	list_move_tail(&oo->oo_close_lru, &nn->close_lru);
	oo->oo_time = get_seconds();
}

static int
same_owner_str(struct nfs4_stateowner *sop, struct xdr_netobj *owner,
							clientid_t *clid)
{
	return (sop->so_owner.len == owner->len) &&
		0 == memcmp(sop->so_owner.data, owner->data, owner->len) &&
		(sop->so_client->cl_clientid.cl_id == clid->cl_id);
}

static struct nfs4_openowner *
find_openstateowner_str(unsigned int hashval, struct nfsd4_open *open,
			bool sessions, struct nfsd_net *nn)
{
	struct nfs4_stateowner *so;
	struct nfs4_openowner *oo;
	struct nfs4_client *clp;

	list_for_each_entry(so, &nn->ownerstr_hashtbl[hashval], so_strhash) {
		if (!so->so_is_open_owner)
			continue;
		if (same_owner_str(so, &open->op_owner, &open->op_clientid)) {
			oo = openowner(so);
			clp = oo->oo_owner.so_client;
			if ((bool)clp->cl_minorversion != sessions)
				return NULL;
			renew_client(oo->oo_owner.so_client);
			return oo;
		}
	}
	return NULL;
}

/* search file_hashtbl[] for file */
static struct nfs4_file *
find_file(struct inode *ino)
{
	unsigned int hashval = file_hashval(ino);
	struct nfs4_file *fp;

	spin_lock(&recall_lock);
	list_for_each_entry(fp, &file_hashtbl[hashval], fi_hash) {
		if (fp->fi_inode == ino) {
			get_nfs4_file(fp);
			spin_unlock(&recall_lock);
			return fp;
		}
	}
	spin_unlock(&recall_lock);
	return NULL;
}

/*
 * Called to check deny when READ with all zero stateid or
 * WRITE with all zero or all one stateid
 */
static __be32
nfs4_share_conflict(struct svc_fh *current_fh, unsigned int deny_type)
{
	struct inode *ino = current_fh->fh_dentry->d_inode;
	struct nfs4_file *fp;
	struct nfs4_ol_stateid *stp;
	__be32 ret;

	dprintk("NFSD: nfs4_share_conflict\n");

	fp = find_file(ino);
	if (!fp)
		return nfs_ok;
	ret = nfserr_locked;
	/* Search for conflicting share reservations */
	list_for_each_entry(stp, &fp->fi_stateids, st_perfile) {
		if (test_deny(deny_type, stp) ||
		    test_deny(NFS4_SHARE_DENY_BOTH, stp))
			goto out;
	}
	ret = nfs_ok;
out:
	put_nfs4_file(fp);
	return ret;
}

static void nfsd_break_one_deleg(struct nfs4_delegation *dp)
{
	/* We're assuming the state code never drops its reference
	 * without first removing the lease.  Since we're in this lease
	 * callback (and since the lease code is serialized by the kernel
	 * lock) we know the server hasn't removed the lease yet, we know
	 * it's safe to take a reference: */
	atomic_inc(&dp->dl_count);

	list_add_tail(&dp->dl_recall_lru, &del_recall_lru);

	/* only place dl_time is set. protected by lock_flocks*/
	dp->dl_time = get_seconds();

	nfsd4_cb_recall(dp);
}

/* Called from break_lease() with lock_flocks() held. */
static void nfsd_break_deleg_cb(struct file_lock *fl)
{
	struct nfs4_file *fp = (struct nfs4_file *)fl->fl_owner;
	struct nfs4_delegation *dp;

	if (!fp) {
		WARN(1, "(%p)->fl_owner NULL\n", fl);
		return;
	}
	if (fp->fi_had_conflict) {
		WARN(1, "duplicate break on %p\n", fp);
		return;
	}
	/*
	 * We don't want the locks code to timeout the lease for us;
	 * we'll remove it ourself if a delegation isn't returned
	 * in time:
	 */
	fl->fl_break_time = 0;

	spin_lock(&recall_lock);
	fp->fi_had_conflict = true;
	list_for_each_entry(dp, &fp->fi_delegations, dl_perfile)
		nfsd_break_one_deleg(dp);
	spin_unlock(&recall_lock);
}

static
int nfsd_change_deleg_cb(struct file_lock **onlist, int arg)
{
	if (arg & F_UNLCK)
		return lease_modify(onlist, arg);
	else
		return -EAGAIN;
}

static const struct lock_manager_operations nfsd_lease_mng_ops = {
	.lm_break = nfsd_break_deleg_cb,
	.lm_change = nfsd_change_deleg_cb,
};

static __be32 nfsd4_check_seqid(struct nfsd4_compound_state *cstate, struct nfs4_stateowner *so, u32 seqid)
{
	if (nfsd4_has_session(cstate))
		return nfs_ok;
	if (seqid == so->so_seqid - 1)
		return nfserr_replay_me;
	if (seqid == so->so_seqid)
		return nfs_ok;
	return nfserr_bad_seqid;
}

__be32
nfsd4_process_open1(struct nfsd4_compound_state *cstate,
		    struct nfsd4_open *open, struct nfsd_net *nn)
{
	clientid_t *clientid = &open->op_clientid;
	struct nfs4_client *clp = NULL;
	unsigned int strhashval;
	struct nfs4_openowner *oo = NULL;
	__be32 status;

	if (STALE_CLIENTID(&open->op_clientid, nn))
		return nfserr_stale_clientid;
	/*
	 * In case we need it later, after we've already created the
	 * file and don't want to risk a further failure:
	 */
	open->op_file = nfsd4_alloc_file();
	if (open->op_file == NULL)
		return nfserr_jukebox;

	strhashval = ownerstr_hashval(clientid->cl_id, &open->op_owner);
	oo = find_openstateowner_str(strhashval, open, cstate->minorversion, nn);
	open->op_openowner = oo;
	if (!oo) {
		clp = find_confirmed_client(clientid, cstate->minorversion,
					    nn);
		if (clp == NULL)
			return nfserr_expired;
		goto new_owner;
	}
	if (!(oo->oo_flags & NFS4_OO_CONFIRMED)) {
		/* Replace unconfirmed owners without checking for replay. */
		clp = oo->oo_owner.so_client;
		release_openowner(oo);
		open->op_openowner = NULL;
		goto new_owner;
	}
	status = nfsd4_check_seqid(cstate, &oo->oo_owner, open->op_seqid);
	if (status)
		return status;
	clp = oo->oo_owner.so_client;
	goto alloc_stateid;
new_owner:
	oo = alloc_init_open_stateowner(strhashval, clp, open);
	if (oo == NULL)
		return nfserr_jukebox;
	open->op_openowner = oo;
alloc_stateid:
	open->op_stp = nfs4_alloc_stateid(clp);
	if (!open->op_stp)
		return nfserr_jukebox;
	return nfs_ok;
}

static inline __be32
nfs4_check_delegmode(struct nfs4_delegation *dp, int flags)
{
	if ((flags & WR_STATE) && (dp->dl_type == NFS4_OPEN_DELEGATE_READ))
		return nfserr_openmode;
	else
		return nfs_ok;
}

static int share_access_to_flags(u32 share_access)
{
	return share_access == NFS4_SHARE_ACCESS_READ ? RD_STATE : WR_STATE;
}

static struct nfs4_delegation *find_deleg_stateid(struct nfs4_client *cl, stateid_t *s)
{
	struct nfs4_stid *ret;

	ret = find_stateid_by_type(cl, s, NFS4_DELEG_STID);
	if (!ret)
		return NULL;
	return delegstateid(ret);
}

static bool nfsd4_is_deleg_cur(struct nfsd4_open *open)
{
	return open->op_claim_type == NFS4_OPEN_CLAIM_DELEGATE_CUR ||
	       open->op_claim_type == NFS4_OPEN_CLAIM_DELEG_CUR_FH;
}

static __be32
nfs4_check_deleg(struct nfs4_client *cl, struct nfs4_file *fp, struct nfsd4_open *open,
		struct nfs4_delegation **dp)
{
	int flags;
	__be32 status = nfserr_bad_stateid;

	*dp = find_deleg_stateid(cl, &open->op_delegate_stateid);
	if (*dp == NULL)
		goto out;
	flags = share_access_to_flags(open->op_share_access);
	status = nfs4_check_delegmode(*dp, flags);
	if (status)
		*dp = NULL;
out:
	if (!nfsd4_is_deleg_cur(open))
		return nfs_ok;
	if (status)
		return status;
	open->op_openowner->oo_flags |= NFS4_OO_CONFIRMED;
	return nfs_ok;
}

static __be32
nfs4_check_open(struct nfs4_file *fp, struct nfsd4_open *open, struct nfs4_ol_stateid **stpp)
{
	struct nfs4_ol_stateid *local;
	struct nfs4_openowner *oo = open->op_openowner;

	list_for_each_entry(local, &fp->fi_stateids, st_perfile) {
		/* ignore lock owners */
		if (local->st_stateowner->so_is_open_owner == 0)
			continue;
		/* remember if we have seen this open owner */
		if (local->st_stateowner == &oo->oo_owner)
			*stpp = local;
		/* check for conflicting share reservations */
		if (!test_share(local, open))
			return nfserr_share_denied;
	}
	return nfs_ok;
}

static inline int nfs4_access_to_access(u32 nfs4_access)
{
	int flags = 0;

	if (nfs4_access & NFS4_SHARE_ACCESS_READ)
		flags |= NFSD_MAY_READ;
	if (nfs4_access & NFS4_SHARE_ACCESS_WRITE)
		flags |= NFSD_MAY_WRITE;
	return flags;
}

static __be32 nfs4_get_vfs_file(struct svc_rqst *rqstp, struct nfs4_file *fp,
		struct svc_fh *cur_fh, struct nfsd4_open *open)
{
	__be32 status;
	int oflag = nfs4_access_to_omode(open->op_share_access);
	int access = nfs4_access_to_access(open->op_share_access);

	if (!fp->fi_fds[oflag]) {
		status = nfsd_open(rqstp, cur_fh, S_IFREG, access,
			&fp->fi_fds[oflag]);
		if (status)
			return status;
	}
	nfs4_file_get_access(fp, oflag);

	return nfs_ok;
}

static inline __be32
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

static __be32
nfs4_upgrade_open(struct svc_rqst *rqstp, struct nfs4_file *fp, struct svc_fh *cur_fh, struct nfs4_ol_stateid *stp, struct nfsd4_open *open)
{
	u32 op_share_access = open->op_share_access;
	bool new_access;
	__be32 status;

	new_access = !test_access(op_share_access, stp);
	if (new_access) {
		status = nfs4_get_vfs_file(rqstp, fp, cur_fh, open);
		if (status)
			return status;
	}
	status = nfsd4_truncate(rqstp, cur_fh, open);
	if (status) {
		if (new_access) {
			int oflag = nfs4_access_to_omode(op_share_access);
			nfs4_file_put_access(fp, oflag);
		}
		return status;
	}
	/* remember the open */
	set_access(op_share_access, stp);
	set_deny(open->op_share_deny, stp);

	return nfs_ok;
}


static void
nfs4_set_claim_prev(struct nfsd4_open *open, bool has_session)
{
	open->op_openowner->oo_flags |= NFS4_OO_CONFIRMED;
}

/* Should we give out recallable state?: */
static bool nfsd4_cb_channel_good(struct nfs4_client *clp)
{
	if (clp->cl_cb_state == NFSD4_CB_UP)
		return true;
	/*
	 * In the sessions case, since we don't have to establish a
	 * separate connection for callbacks, we assume it's OK
	 * until we hear otherwise:
	 */
	return clp->cl_minorversion && clp->cl_cb_state == NFSD4_CB_UNKNOWN;
}

static struct file_lock *nfs4_alloc_init_lease(struct nfs4_delegation *dp, int flag)
{
	struct file_lock *fl;

	fl = locks_alloc_lock();
	if (!fl)
		return NULL;
	locks_init_lock(fl);
	fl->fl_lmops = &nfsd_lease_mng_ops;
	fl->fl_flags = FL_LEASE;
	fl->fl_type = flag == NFS4_OPEN_DELEGATE_READ? F_RDLCK: F_WRLCK;
	fl->fl_end = OFFSET_MAX;
	fl->fl_owner = (fl_owner_t)(dp->dl_file);
	fl->fl_pid = current->tgid;
	return fl;
}

static int nfs4_setlease(struct nfs4_delegation *dp, int flag)
{
	struct nfs4_file *fp = dp->dl_file;
	struct file_lock *fl;
	int status;

	fl = nfs4_alloc_init_lease(dp, flag);
	if (!fl)
		return -ENOMEM;
	fl->fl_file = find_readable_file(fp);
	list_add(&dp->dl_perclnt, &dp->dl_stid.sc_client->cl_delegations);
	status = vfs_setlease(fl->fl_file, fl->fl_type, &fl);
	if (status) {
		list_del_init(&dp->dl_perclnt);
		locks_free_lock(fl);
		return -ENOMEM;
	}
	fp->fi_lease = fl;
	fp->fi_deleg_file = get_file(fl->fl_file);
	atomic_set(&fp->fi_delegees, 1);
	list_add(&dp->dl_perfile, &fp->fi_delegations);
	return 0;
}

static int nfs4_set_delegation(struct nfs4_delegation *dp, int flag)
{
	struct nfs4_file *fp = dp->dl_file;

	if (!fp->fi_lease)
		return nfs4_setlease(dp, flag);
	spin_lock(&recall_lock);
	if (fp->fi_had_conflict) {
		spin_unlock(&recall_lock);
		return -EAGAIN;
	}
	atomic_inc(&fp->fi_delegees);
	list_add(&dp->dl_perfile, &fp->fi_delegations);
	spin_unlock(&recall_lock);
	list_add(&dp->dl_perclnt, &dp->dl_stid.sc_client->cl_delegations);
	return 0;
}

static void nfsd4_open_deleg_none_ext(struct nfsd4_open *open, int status)
{
	open->op_delegate_type = NFS4_OPEN_DELEGATE_NONE_EXT;
	if (status == -EAGAIN)
		open->op_why_no_deleg = WND4_CONTENTION;
	else {
		open->op_why_no_deleg = WND4_RESOURCE;
		switch (open->op_deleg_want) {
		case NFS4_SHARE_WANT_READ_DELEG:
		case NFS4_SHARE_WANT_WRITE_DELEG:
		case NFS4_SHARE_WANT_ANY_DELEG:
			break;
		case NFS4_SHARE_WANT_CANCEL:
			open->op_why_no_deleg = WND4_CANCELLED;
			break;
		case NFS4_SHARE_WANT_NO_DELEG:
			BUG();	/* not supposed to get here */
		}
	}
}

/*
 * Attempt to hand out a delegation.
 */
static void
nfs4_open_delegation(struct net *net, struct svc_fh *fh,
		     struct nfsd4_open *open, struct nfs4_ol_stateid *stp)
{
	struct nfs4_delegation *dp;
	struct nfs4_openowner *oo = container_of(stp->st_stateowner, struct nfs4_openowner, oo_owner);
	int cb_up;
	int status = 0, flag = 0;

	cb_up = nfsd4_cb_channel_good(oo->oo_owner.so_client);
	flag = NFS4_OPEN_DELEGATE_NONE;
	open->op_recall = 0;
	switch (open->op_claim_type) {
		case NFS4_OPEN_CLAIM_PREVIOUS:
			if (!cb_up)
				open->op_recall = 1;
			flag = open->op_delegate_type;
			if (flag == NFS4_OPEN_DELEGATE_NONE)
				goto out;
			break;
		case NFS4_OPEN_CLAIM_NULL:
			/* Let's not give out any delegations till everyone's
			 * had the chance to reclaim theirs.... */
			if (locks_in_grace(net))
				goto out;
			if (!cb_up || !(oo->oo_flags & NFS4_OO_CONFIRMED))
				goto out;
			if (open->op_share_access & NFS4_SHARE_ACCESS_WRITE)
				flag = NFS4_OPEN_DELEGATE_WRITE;
			else
				flag = NFS4_OPEN_DELEGATE_READ;
			break;
		default:
			goto out;
	}

	dp = alloc_init_deleg(oo->oo_owner.so_client, stp, fh, flag);
	if (dp == NULL)
		goto out_no_deleg;
	status = nfs4_set_delegation(dp, flag);
	if (status)
		goto out_free;

	memcpy(&open->op_delegate_stateid, &dp->dl_stid.sc_stateid, sizeof(dp->dl_stid.sc_stateid));

	dprintk("NFSD: delegation stateid=" STATEID_FMT "\n",
		STATEID_VAL(&dp->dl_stid.sc_stateid));
out:
	open->op_delegate_type = flag;
	if (flag == NFS4_OPEN_DELEGATE_NONE) {
		if (open->op_claim_type == NFS4_OPEN_CLAIM_PREVIOUS &&
		    open->op_delegate_type != NFS4_OPEN_DELEGATE_NONE)
			dprintk("NFSD: WARNING: refusing delegation reclaim\n");

		/* 4.1 client asking for a delegation? */
		if (open->op_deleg_want)
			nfsd4_open_deleg_none_ext(open, status);
	}
	return;
out_free:
	nfs4_put_delegation(dp);
out_no_deleg:
	flag = NFS4_OPEN_DELEGATE_NONE;
	goto out;
}

static void nfsd4_deleg_xgrade_none_ext(struct nfsd4_open *open,
					struct nfs4_delegation *dp)
{
	if (open->op_deleg_want == NFS4_SHARE_WANT_READ_DELEG &&
	    dp->dl_type == NFS4_OPEN_DELEGATE_WRITE) {
		open->op_delegate_type = NFS4_OPEN_DELEGATE_NONE_EXT;
		open->op_why_no_deleg = WND4_NOT_SUPP_DOWNGRADE;
	} else if (open->op_deleg_want == NFS4_SHARE_WANT_WRITE_DELEG &&
		   dp->dl_type == NFS4_OPEN_DELEGATE_WRITE) {
		open->op_delegate_type = NFS4_OPEN_DELEGATE_NONE_EXT;
		open->op_why_no_deleg = WND4_NOT_SUPP_UPGRADE;
	}
	/* Otherwise the client must be confused wanting a delegation
	 * it already has, therefore we don't return
	 * NFS4_OPEN_DELEGATE_NONE_EXT and reason.
	 */
}

/*
 * called with nfs4_lock_state() held.
 */
__be32
nfsd4_process_open2(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open *open)
{
	struct nfsd4_compoundres *resp = rqstp->rq_resp;
	struct nfs4_client *cl = open->op_openowner->oo_owner.so_client;
	struct nfs4_file *fp = NULL;
	struct inode *ino = current_fh->fh_dentry->d_inode;
	struct nfs4_ol_stateid *stp = NULL;
	struct nfs4_delegation *dp = NULL;
	__be32 status;

	/*
	 * Lookup file; if found, lookup stateid and check open request,
	 * and check for delegations in the process of being recalled.
	 * If not found, create the nfs4_file struct
	 */
	fp = find_file(ino);
	if (fp) {
		if ((status = nfs4_check_open(fp, open, &stp)))
			goto out;
		status = nfs4_check_deleg(cl, fp, open, &dp);
		if (status)
			goto out;
	} else {
		status = nfserr_bad_stateid;
		if (nfsd4_is_deleg_cur(open))
			goto out;
		status = nfserr_jukebox;
		fp = open->op_file;
		open->op_file = NULL;
		nfsd4_init_file(fp, ino);
	}

	/*
	 * OPEN the file, or upgrade an existing OPEN.
	 * If truncate fails, the OPEN fails.
	 */
	if (stp) {
		/* Stateid was found, this is an OPEN upgrade */
		status = nfs4_upgrade_open(rqstp, fp, current_fh, stp, open);
		if (status)
			goto out;
	} else {
		status = nfs4_get_vfs_file(rqstp, fp, current_fh, open);
		if (status)
			goto out;
		status = nfsd4_truncate(rqstp, current_fh, open);
		if (status)
			goto out;
		stp = open->op_stp;
		open->op_stp = NULL;
		init_open_stateid(stp, fp, open);
	}
	update_stateid(&stp->st_stid.sc_stateid);
	memcpy(&open->op_stateid, &stp->st_stid.sc_stateid, sizeof(stateid_t));

	if (nfsd4_has_session(&resp->cstate)) {
		open->op_openowner->oo_flags |= NFS4_OO_CONFIRMED;

		if (open->op_deleg_want & NFS4_SHARE_WANT_NO_DELEG) {
			open->op_delegate_type = NFS4_OPEN_DELEGATE_NONE_EXT;
			open->op_why_no_deleg = WND4_NOT_WANTED;
			goto nodeleg;
		}
	}

	/*
	* Attempt to hand out a delegation. No error return, because the
	* OPEN succeeds even if we fail.
	*/
	nfs4_open_delegation(SVC_NET(rqstp), current_fh, open, stp);
nodeleg:
	status = nfs_ok;

	dprintk("%s: stateid=" STATEID_FMT "\n", __func__,
		STATEID_VAL(&stp->st_stid.sc_stateid));
out:
	/* 4.1 client trying to upgrade/downgrade delegation? */
	if (open->op_delegate_type == NFS4_OPEN_DELEGATE_NONE && dp &&
	    open->op_deleg_want)
		nfsd4_deleg_xgrade_none_ext(open, dp);

	if (fp)
		put_nfs4_file(fp);
	if (status == 0 && open->op_claim_type == NFS4_OPEN_CLAIM_PREVIOUS)
		nfs4_set_claim_prev(open, nfsd4_has_session(&resp->cstate));
	/*
	* To finish the open response, we just need to set the rflags.
	*/
	open->op_rflags = NFS4_OPEN_RESULT_LOCKTYPE_POSIX;
	if (!(open->op_openowner->oo_flags & NFS4_OO_CONFIRMED) &&
	    !nfsd4_has_session(&resp->cstate))
		open->op_rflags |= NFS4_OPEN_RESULT_CONFIRM;

	return status;
}

void nfsd4_cleanup_open_state(struct nfsd4_open *open, __be32 status)
{
	if (open->op_openowner) {
		struct nfs4_openowner *oo = open->op_openowner;

		if (!list_empty(&oo->oo_owner.so_stateids))
			list_del_init(&oo->oo_close_lru);
		if (oo->oo_flags & NFS4_OO_NEW) {
			if (status) {
				release_openowner(oo);
				open->op_openowner = NULL;
			} else
				oo->oo_flags &= ~NFS4_OO_NEW;
		}
	}
	if (open->op_file)
		nfsd4_free_file(open->op_file);
	if (open->op_stp)
		free_generic_stateid(open->op_stp);
}

__be32
nfsd4_renew(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	    clientid_t *clid)
{
	struct nfs4_client *clp;
	__be32 status;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	nfs4_lock_state();
	dprintk("process_renew(%08x/%08x): starting\n", 
			clid->cl_boot, clid->cl_id);
	status = nfserr_stale_clientid;
	if (STALE_CLIENTID(clid, nn))
		goto out;
	clp = find_confirmed_client(clid, cstate->minorversion, nn);
	status = nfserr_expired;
	if (clp == NULL) {
		/* We assume the client took too long to RENEW. */
		dprintk("nfsd4_renew: clientid not found!\n");
		goto out;
	}
	status = nfserr_cb_path_down;
	if (!list_empty(&clp->cl_delegations)
			&& clp->cl_cb_state != NFSD4_CB_UP)
		goto out;
	status = nfs_ok;
out:
	nfs4_unlock_state();
	return status;
}

static void
nfsd4_end_grace(struct net *net)
{
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	/* do nothing if grace period already ended */
	if (nn->grace_ended)
		return;

	dprintk("NFSD: end of grace period\n");
	nn->grace_ended = true;
	nfsd4_record_grace_done(net, nn->boot_time);
	locks_end_grace(&nn->nfsd4_manager);
	/*
	 * Now that every NFSv4 client has had the chance to recover and
	 * to see the (possibly new, possibly shorter) lease time, we
	 * can safely set the next grace time to the current lease time:
	 */
	nfsd4_grace = nfsd4_lease;
}

static time_t
nfs4_laundromat(void)
{
	struct nfs4_client *clp;
	struct nfs4_openowner *oo;
	struct nfs4_delegation *dp;
	struct list_head *pos, *next, reaplist;
	time_t cutoff = get_seconds() - nfsd4_lease;
	time_t t, clientid_val = nfsd4_lease;
	time_t u, test_val = nfsd4_lease;
	struct nfsd_net *nn = net_generic(&init_net, nfsd_net_id);

	nfs4_lock_state();

	dprintk("NFSD: laundromat service - starting\n");
	nfsd4_end_grace(&init_net);
	INIT_LIST_HEAD(&reaplist);
	spin_lock(&client_lock);
	list_for_each_safe(pos, next, &nn->client_lru) {
		clp = list_entry(pos, struct nfs4_client, cl_lru);
		if (time_after((unsigned long)clp->cl_time, (unsigned long)cutoff)) {
			t = clp->cl_time - cutoff;
			if (clientid_val > t)
				clientid_val = t;
			break;
		}
		if (atomic_read(&clp->cl_refcount)) {
			dprintk("NFSD: client in use (clientid %08x)\n",
				clp->cl_clientid.cl_id);
			continue;
		}
		unhash_client_locked(clp);
		list_add(&clp->cl_lru, &reaplist);
	}
	spin_unlock(&client_lock);
	list_for_each_safe(pos, next, &reaplist) {
		clp = list_entry(pos, struct nfs4_client, cl_lru);
		dprintk("NFSD: purging unused client (clientid %08x)\n",
			clp->cl_clientid.cl_id);
		expire_client(clp);
	}
	spin_lock(&recall_lock);
	list_for_each_safe(pos, next, &del_recall_lru) {
		dp = list_entry (pos, struct nfs4_delegation, dl_recall_lru);
		if (time_after((unsigned long)dp->dl_time, (unsigned long)cutoff)) {
			u = dp->dl_time - cutoff;
			if (test_val > u)
				test_val = u;
			break;
		}
		list_move(&dp->dl_recall_lru, &reaplist);
	}
	spin_unlock(&recall_lock);
	list_for_each_safe(pos, next, &reaplist) {
		dp = list_entry (pos, struct nfs4_delegation, dl_recall_lru);
		unhash_delegation(dp);
	}
	test_val = nfsd4_lease;
	list_for_each_safe(pos, next, &nn->close_lru) {
		oo = container_of(pos, struct nfs4_openowner, oo_close_lru);
		if (time_after((unsigned long)oo->oo_time, (unsigned long)cutoff)) {
			u = oo->oo_time - cutoff;
			if (test_val > u)
				test_val = u;
			break;
		}
		release_openowner(oo);
	}
	if (clientid_val < NFSD_LAUNDROMAT_MINTIMEOUT)
		clientid_val = NFSD_LAUNDROMAT_MINTIMEOUT;
	nfs4_unlock_state();
	return clientid_val;
}

static struct workqueue_struct *laundry_wq;
static void laundromat_main(struct work_struct *);
static DECLARE_DELAYED_WORK(laundromat_work, laundromat_main);

static void
laundromat_main(struct work_struct *not_used)
{
	time_t t;

	t = nfs4_laundromat();
	dprintk("NFSD: laundromat_main - sleeping for %ld seconds\n", t);
	queue_delayed_work(laundry_wq, &laundromat_work, t*HZ);
}

static inline __be32 nfs4_check_fh(struct svc_fh *fhp, struct nfs4_ol_stateid *stp)
{
	if (fhp->fh_dentry->d_inode != stp->st_file->fi_inode)
		return nfserr_bad_stateid;
	return nfs_ok;
}

static int
STALE_STATEID(stateid_t *stateid, struct nfsd_net *nn)
{
	if (stateid->si_opaque.so_clid.cl_boot == nn->boot_time)
		return 0;
	dprintk("NFSD: stale stateid " STATEID_FMT "!\n",
		STATEID_VAL(stateid));
	return 1;
}

static inline int
access_permit_read(struct nfs4_ol_stateid *stp)
{
	return test_access(NFS4_SHARE_ACCESS_READ, stp) ||
		test_access(NFS4_SHARE_ACCESS_BOTH, stp) ||
		test_access(NFS4_SHARE_ACCESS_WRITE, stp);
}

static inline int
access_permit_write(struct nfs4_ol_stateid *stp)
{
	return test_access(NFS4_SHARE_ACCESS_WRITE, stp) ||
		test_access(NFS4_SHARE_ACCESS_BOTH, stp);
}

static
__be32 nfs4_check_openmode(struct nfs4_ol_stateid *stp, int flags)
{
        __be32 status = nfserr_openmode;

	/* For lock stateid's, we test the parent open, not the lock: */
	if (stp->st_openstp)
		stp = stp->st_openstp;
	if ((flags & WR_STATE) && !access_permit_write(stp))
                goto out;
	if ((flags & RD_STATE) && !access_permit_read(stp))
                goto out;
	status = nfs_ok;
out:
	return status;
}

static inline __be32
check_special_stateids(struct net *net, svc_fh *current_fh, stateid_t *stateid, int flags)
{
	if (ONE_STATEID(stateid) && (flags & RD_STATE))
		return nfs_ok;
	else if (locks_in_grace(net)) {
		/* Answer in remaining cases depends on existence of
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
grace_disallows_io(struct net *net, struct inode *inode)
{
	return locks_in_grace(net) && mandatory_lock(inode);
}

/* Returns true iff a is later than b: */
static bool stateid_generation_after(stateid_t *a, stateid_t *b)
{
	return (s32)a->si_generation - (s32)b->si_generation > 0;
}

static __be32 check_stateid_generation(stateid_t *in, stateid_t *ref, bool has_session)
{
	/*
	 * When sessions are used the stateid generation number is ignored
	 * when it is zero.
	 */
	if (has_session && in->si_generation == 0)
		return nfs_ok;

	if (in->si_generation == ref->si_generation)
		return nfs_ok;

	/* If the client sends us a stateid from the future, it's buggy: */
	if (stateid_generation_after(in, ref))
		return nfserr_bad_stateid;
	/*
	 * However, we could see a stateid from the past, even from a
	 * non-buggy client.  For example, if the client sends a lock
	 * while some IO is outstanding, the lock may bump si_generation
	 * while the IO is still in flight.  The client could avoid that
	 * situation by waiting for responses on all the IO requests,
	 * but better performance may result in retrying IO that
	 * receives an old_stateid error if requests are rarely
	 * reordered in flight:
	 */
	return nfserr_old_stateid;
}

static __be32 nfsd4_validate_stateid(struct nfs4_client *cl, stateid_t *stateid)
{
	struct nfs4_stid *s;
	struct nfs4_ol_stateid *ols;
	__be32 status;

	if (ZERO_STATEID(stateid) || ONE_STATEID(stateid))
		return nfserr_bad_stateid;
	/* Client debugging aid. */
	if (!same_clid(&stateid->si_opaque.so_clid, &cl->cl_clientid)) {
		char addr_str[INET6_ADDRSTRLEN];
		rpc_ntop((struct sockaddr *)&cl->cl_addr, addr_str,
				 sizeof(addr_str));
		pr_warn_ratelimited("NFSD: client %s testing state ID "
					"with incorrect client ID\n", addr_str);
		return nfserr_bad_stateid;
	}
	s = find_stateid(cl, stateid);
	if (!s)
		return nfserr_bad_stateid;
	status = check_stateid_generation(stateid, &s->sc_stateid, 1);
	if (status)
		return status;
	if (!(s->sc_type & (NFS4_OPEN_STID | NFS4_LOCK_STID)))
		return nfs_ok;
	ols = openlockstateid(s);
	if (ols->st_stateowner->so_is_open_owner
	    && !(openowner(ols->st_stateowner)->oo_flags & NFS4_OO_CONFIRMED))
		return nfserr_bad_stateid;
	return nfs_ok;
}

static __be32 nfsd4_lookup_stateid(stateid_t *stateid, unsigned char typemask,
				   struct nfs4_stid **s, bool sessions,
				   struct nfsd_net *nn)
{
	struct nfs4_client *cl;

	if (ZERO_STATEID(stateid) || ONE_STATEID(stateid))
		return nfserr_bad_stateid;
	if (STALE_STATEID(stateid, nn))
		return nfserr_stale_stateid;
	cl = find_confirmed_client(&stateid->si_opaque.so_clid, sessions, nn);
	if (!cl)
		return nfserr_expired;
	*s = find_stateid_by_type(cl, stateid, typemask);
	if (!*s)
		return nfserr_bad_stateid;
	return nfs_ok;

}

/*
* Checks for stateid operations
*/
__be32
nfs4_preprocess_stateid_op(struct net *net, struct nfsd4_compound_state *cstate,
			   stateid_t *stateid, int flags, struct file **filpp)
{
	struct nfs4_stid *s;
	struct nfs4_ol_stateid *stp = NULL;
	struct nfs4_delegation *dp = NULL;
	struct svc_fh *current_fh = &cstate->current_fh;
	struct inode *ino = current_fh->fh_dentry->d_inode;
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	__be32 status;

	if (filpp)
		*filpp = NULL;

	if (grace_disallows_io(net, ino))
		return nfserr_grace;

	if (ZERO_STATEID(stateid) || ONE_STATEID(stateid))
		return check_special_stateids(net, current_fh, stateid, flags);

	status = nfsd4_lookup_stateid(stateid, NFS4_DELEG_STID|NFS4_OPEN_STID|NFS4_LOCK_STID,
				      &s, cstate->minorversion, nn);
	if (status)
		return status;
	status = check_stateid_generation(stateid, &s->sc_stateid, nfsd4_has_session(cstate));
	if (status)
		goto out;
	switch (s->sc_type) {
	case NFS4_DELEG_STID:
		dp = delegstateid(s);
		status = nfs4_check_delegmode(dp, flags);
		if (status)
			goto out;
		if (filpp) {
			*filpp = dp->dl_file->fi_deleg_file;
			BUG_ON(!*filpp);
		}
		break;
	case NFS4_OPEN_STID:
	case NFS4_LOCK_STID:
		stp = openlockstateid(s);
		status = nfs4_check_fh(current_fh, stp);
		if (status)
			goto out;
		if (stp->st_stateowner->so_is_open_owner
		    && !(openowner(stp->st_stateowner)->oo_flags & NFS4_OO_CONFIRMED))
			goto out;
		status = nfs4_check_openmode(stp, flags);
		if (status)
			goto out;
		if (filpp) {
			if (flags & RD_STATE)
				*filpp = find_readable_file(stp->st_file);
			else
				*filpp = find_writeable_file(stp->st_file);
		}
		break;
	default:
		return nfserr_bad_stateid;
	}
	status = nfs_ok;
out:
	return status;
}

static __be32
nfsd4_free_lock_stateid(struct nfs4_ol_stateid *stp)
{
	if (check_for_locks(stp->st_file, lockowner(stp->st_stateowner)))
		return nfserr_locks_held;
	release_lock_stateid(stp);
	return nfs_ok;
}

/*
 * Test if the stateid is valid
 */
__be32
nfsd4_test_stateid(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		   struct nfsd4_test_stateid *test_stateid)
{
	struct nfsd4_test_stateid_id *stateid;
	struct nfs4_client *cl = cstate->session->se_client;

	nfs4_lock_state();
	list_for_each_entry(stateid, &test_stateid->ts_stateid_list, ts_id_list)
		stateid->ts_id_status =
			nfsd4_validate_stateid(cl, &stateid->ts_id_stateid);
	nfs4_unlock_state();

	return nfs_ok;
}

__be32
nfsd4_free_stateid(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		   struct nfsd4_free_stateid *free_stateid)
{
	stateid_t *stateid = &free_stateid->fr_stateid;
	struct nfs4_stid *s;
	struct nfs4_client *cl = cstate->session->se_client;
	__be32 ret = nfserr_bad_stateid;

	nfs4_lock_state();
	s = find_stateid(cl, stateid);
	if (!s)
		goto out;
	switch (s->sc_type) {
	case NFS4_DELEG_STID:
		ret = nfserr_locks_held;
		goto out;
	case NFS4_OPEN_STID:
	case NFS4_LOCK_STID:
		ret = check_stateid_generation(stateid, &s->sc_stateid, 1);
		if (ret)
			goto out;
		if (s->sc_type == NFS4_LOCK_STID)
			ret = nfsd4_free_lock_stateid(openlockstateid(s));
		else
			ret = nfserr_locks_held;
		break;
	default:
		ret = nfserr_bad_stateid;
	}
out:
	nfs4_unlock_state();
	return ret;
}

static inline int
setlkflg (int type)
{
	return (type == NFS4_READW_LT || type == NFS4_READ_LT) ?
		RD_STATE : WR_STATE;
}

static __be32 nfs4_seqid_op_checks(struct nfsd4_compound_state *cstate, stateid_t *stateid, u32 seqid, struct nfs4_ol_stateid *stp)
{
	struct svc_fh *current_fh = &cstate->current_fh;
	struct nfs4_stateowner *sop = stp->st_stateowner;
	__be32 status;

	status = nfsd4_check_seqid(cstate, sop, seqid);
	if (status)
		return status;
	if (stp->st_stid.sc_type == NFS4_CLOSED_STID)
		/*
		 * "Closed" stateid's exist *only* to return
		 * nfserr_replay_me from the previous step.
		 */
		return nfserr_bad_stateid;
	status = check_stateid_generation(stateid, &stp->st_stid.sc_stateid, nfsd4_has_session(cstate));
	if (status)
		return status;
	return nfs4_check_fh(current_fh, stp);
}

/* 
 * Checks for sequence id mutating operations. 
 */
static __be32
nfs4_preprocess_seqid_op(struct nfsd4_compound_state *cstate, u32 seqid,
			 stateid_t *stateid, char typemask,
			 struct nfs4_ol_stateid **stpp,
			 struct nfsd_net *nn)
{
	__be32 status;
	struct nfs4_stid *s;

	dprintk("NFSD: %s: seqid=%d stateid = " STATEID_FMT "\n", __func__,
		seqid, STATEID_VAL(stateid));

	*stpp = NULL;
	status = nfsd4_lookup_stateid(stateid, typemask, &s,
				      cstate->minorversion, nn);
	if (status)
		return status;
	*stpp = openlockstateid(s);
	cstate->replay_owner = (*stpp)->st_stateowner;

	return nfs4_seqid_op_checks(cstate, stateid, seqid, *stpp);
}

static __be32 nfs4_preprocess_confirmed_seqid_op(struct nfsd4_compound_state *cstate, u32 seqid,
						 stateid_t *stateid, struct nfs4_ol_stateid **stpp, struct nfsd_net *nn)
{
	__be32 status;
	struct nfs4_openowner *oo;

	status = nfs4_preprocess_seqid_op(cstate, seqid, stateid,
						NFS4_OPEN_STID, stpp, nn);
	if (status)
		return status;
	oo = openowner((*stpp)->st_stateowner);
	if (!(oo->oo_flags & NFS4_OO_CONFIRMED))
		return nfserr_bad_stateid;
	return nfs_ok;
}

__be32
nfsd4_open_confirm(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		   struct nfsd4_open_confirm *oc)
{
	__be32 status;
	struct nfs4_openowner *oo;
	struct nfs4_ol_stateid *stp;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	dprintk("NFSD: nfsd4_open_confirm on file %.*s\n",
			(int)cstate->current_fh.fh_dentry->d_name.len,
			cstate->current_fh.fh_dentry->d_name.name);

	status = fh_verify(rqstp, &cstate->current_fh, S_IFREG, 0);
	if (status)
		return status;

	nfs4_lock_state();

	status = nfs4_preprocess_seqid_op(cstate,
					oc->oc_seqid, &oc->oc_req_stateid,
					NFS4_OPEN_STID, &stp, nn);
	if (status)
		goto out;
	oo = openowner(stp->st_stateowner);
	status = nfserr_bad_stateid;
	if (oo->oo_flags & NFS4_OO_CONFIRMED)
		goto out;
	oo->oo_flags |= NFS4_OO_CONFIRMED;
	update_stateid(&stp->st_stid.sc_stateid);
	memcpy(&oc->oc_resp_stateid, &stp->st_stid.sc_stateid, sizeof(stateid_t));
	dprintk("NFSD: %s: success, seqid=%d stateid=" STATEID_FMT "\n",
		__func__, oc->oc_seqid, STATEID_VAL(&stp->st_stid.sc_stateid));

	nfsd4_client_record_create(oo->oo_owner.so_client);
	status = nfs_ok;
out:
	if (!cstate->replay_owner)
		nfs4_unlock_state();
	return status;
}

static inline void nfs4_stateid_downgrade_bit(struct nfs4_ol_stateid *stp, u32 access)
{
	if (!test_access(access, stp))
		return;
	nfs4_file_put_access(stp->st_file, nfs4_access_to_omode(access));
	clear_access(access, stp);
}

static inline void nfs4_stateid_downgrade(struct nfs4_ol_stateid *stp, u32 to_access)
{
	switch (to_access) {
	case NFS4_SHARE_ACCESS_READ:
		nfs4_stateid_downgrade_bit(stp, NFS4_SHARE_ACCESS_WRITE);
		nfs4_stateid_downgrade_bit(stp, NFS4_SHARE_ACCESS_BOTH);
		break;
	case NFS4_SHARE_ACCESS_WRITE:
		nfs4_stateid_downgrade_bit(stp, NFS4_SHARE_ACCESS_READ);
		nfs4_stateid_downgrade_bit(stp, NFS4_SHARE_ACCESS_BOTH);
		break;
	case NFS4_SHARE_ACCESS_BOTH:
		break;
	default:
		BUG();
	}
}

static void
reset_union_bmap_deny(unsigned long deny, struct nfs4_ol_stateid *stp)
{
	int i;
	for (i = 0; i < 4; i++) {
		if ((i & deny) != i)
			clear_deny(i, stp);
	}
}

__be32
nfsd4_open_downgrade(struct svc_rqst *rqstp,
		     struct nfsd4_compound_state *cstate,
		     struct nfsd4_open_downgrade *od)
{
	__be32 status;
	struct nfs4_ol_stateid *stp;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	dprintk("NFSD: nfsd4_open_downgrade on file %.*s\n", 
			(int)cstate->current_fh.fh_dentry->d_name.len,
			cstate->current_fh.fh_dentry->d_name.name);

	/* We don't yet support WANT bits: */
	if (od->od_deleg_want)
		dprintk("NFSD: %s: od_deleg_want=0x%x ignored\n", __func__,
			od->od_deleg_want);

	nfs4_lock_state();
	status = nfs4_preprocess_confirmed_seqid_op(cstate, od->od_seqid,
					&od->od_stateid, &stp, nn);
	if (status)
		goto out; 
	status = nfserr_inval;
	if (!test_access(od->od_share_access, stp)) {
		dprintk("NFSD: access not a subset current bitmap: 0x%lx, input access=%08x\n",
			stp->st_access_bmap, od->od_share_access);
		goto out;
	}
	if (!test_deny(od->od_share_deny, stp)) {
		dprintk("NFSD:deny not a subset current bitmap: 0x%lx, input deny=%08x\n",
			stp->st_deny_bmap, od->od_share_deny);
		goto out;
	}
	nfs4_stateid_downgrade(stp, od->od_share_access);

	reset_union_bmap_deny(od->od_share_deny, stp);

	update_stateid(&stp->st_stid.sc_stateid);
	memcpy(&od->od_stateid, &stp->st_stid.sc_stateid, sizeof(stateid_t));
	status = nfs_ok;
out:
	if (!cstate->replay_owner)
		nfs4_unlock_state();
	return status;
}

void nfsd4_purge_closed_stateid(struct nfs4_stateowner *so)
{
	struct nfs4_openowner *oo;
	struct nfs4_ol_stateid *s;

	if (!so->so_is_open_owner)
		return;
	oo = openowner(so);
	s = oo->oo_last_closed_stid;
	if (!s)
		return;
	if (!(oo->oo_flags & NFS4_OO_PURGE_CLOSE)) {
		/* Release the last_closed_stid on the next seqid bump: */
		oo->oo_flags |= NFS4_OO_PURGE_CLOSE;
		return;
	}
	oo->oo_flags &= ~NFS4_OO_PURGE_CLOSE;
	release_last_closed_stateid(oo);
}

static void nfsd4_close_open_stateid(struct nfs4_ol_stateid *s)
{
	unhash_open_stateid(s);
	s->st_stid.sc_type = NFS4_CLOSED_STID;
}

/*
 * nfs4_unlock_state() called after encode
 */
__be32
nfsd4_close(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	    struct nfsd4_close *close)
{
	__be32 status;
	struct nfs4_openowner *oo;
	struct nfs4_ol_stateid *stp;
	struct net *net = SVC_NET(rqstp);
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	dprintk("NFSD: nfsd4_close on file %.*s\n", 
			(int)cstate->current_fh.fh_dentry->d_name.len,
			cstate->current_fh.fh_dentry->d_name.name);

	nfs4_lock_state();
	status = nfs4_preprocess_seqid_op(cstate, close->cl_seqid,
					&close->cl_stateid,
					NFS4_OPEN_STID|NFS4_CLOSED_STID,
					&stp, nn);
	if (status)
		goto out; 
	oo = openowner(stp->st_stateowner);
	status = nfs_ok;
	update_stateid(&stp->st_stid.sc_stateid);
	memcpy(&close->cl_stateid, &stp->st_stid.sc_stateid, sizeof(stateid_t));

	nfsd4_close_open_stateid(stp);
	release_last_closed_stateid(oo);
	oo->oo_last_closed_stid = stp;

	if (list_empty(&oo->oo_owner.so_stateids)) {
		if (cstate->minorversion) {
			release_openowner(oo);
			cstate->replay_owner = NULL;
		} else {
			/*
			 * In the 4.0 case we need to keep the owners around a
			 * little while to handle CLOSE replay.
			 */
			if (list_empty(&oo->oo_owner.so_stateids))
				move_to_close_lru(oo, SVC_NET(rqstp));
		}
	}
out:
	if (!cstate->replay_owner)
		nfs4_unlock_state();
	return status;
}

__be32
nfsd4_delegreturn(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		  struct nfsd4_delegreturn *dr)
{
	struct nfs4_delegation *dp;
	stateid_t *stateid = &dr->dr_stateid;
	struct nfs4_stid *s;
	__be32 status;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	if ((status = fh_verify(rqstp, &cstate->current_fh, S_IFREG, 0)))
		return status;

	nfs4_lock_state();
	status = nfsd4_lookup_stateid(stateid, NFS4_DELEG_STID, &s,
				      cstate->minorversion, nn);
	if (status)
		goto out;
	dp = delegstateid(s);
	status = check_stateid_generation(stateid, &dp->dl_stid.sc_stateid, nfsd4_has_session(cstate));
	if (status)
		goto out;

	unhash_delegation(dp);
out:
	nfs4_unlock_state();

	return status;
}


#define LOFF_OVERFLOW(start, len)      ((u64)(len) > ~(u64)(start))

#define LOCKOWNER_INO_HASH_MASK (LOCKOWNER_INO_HASH_SIZE - 1)

static inline u64
end_offset(u64 start, u64 len)
{
	u64 end;

	end = start + len;
	return end >= start ? end: NFS4_MAX_UINT64;
}

/* last octet in a range */
static inline u64
last_byte_offset(u64 start, u64 len)
{
	u64 end;

	BUG_ON(!len);
	end = start + len;
	return end > start ? end - 1: NFS4_MAX_UINT64;
}

static unsigned int lockowner_ino_hashval(struct inode *inode, u32 cl_id, struct xdr_netobj *ownername)
{
	return (file_hashval(inode) + cl_id
			+ opaque_hashval(ownername->data, ownername->len))
		& LOCKOWNER_INO_HASH_MASK;
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

/* Hack!: For now, we're defining this just so we can use a pointer to it
 * as a unique cookie to identify our (NFSv4's) posix locks. */
static const struct lock_manager_operations nfsd_posix_mng_ops  = {
};

static inline void
nfs4_set_lock_denied(struct file_lock *fl, struct nfsd4_lock_denied *deny)
{
	struct nfs4_lockowner *lo;

	if (fl->fl_lmops == &nfsd_posix_mng_ops) {
		lo = (struct nfs4_lockowner *) fl->fl_owner;
		deny->ld_owner.data = kmemdup(lo->lo_owner.so_owner.data,
					lo->lo_owner.so_owner.len, GFP_KERNEL);
		if (!deny->ld_owner.data)
			/* We just don't care that much */
			goto nevermind;
		deny->ld_owner.len = lo->lo_owner.so_owner.len;
		deny->ld_clientid = lo->lo_owner.so_client->cl_clientid;
	} else {
nevermind:
		deny->ld_owner.len = 0;
		deny->ld_owner.data = NULL;
		deny->ld_clientid.cl_boot = 0;
		deny->ld_clientid.cl_id = 0;
	}
	deny->ld_start = fl->fl_start;
	deny->ld_length = NFS4_MAX_UINT64;
	if (fl->fl_end != NFS4_MAX_UINT64)
		deny->ld_length = fl->fl_end - fl->fl_start + 1;        
	deny->ld_type = NFS4_READ_LT;
	if (fl->fl_type != F_RDLCK)
		deny->ld_type = NFS4_WRITE_LT;
}

static bool same_lockowner_ino(struct nfs4_lockowner *lo, struct inode *inode, clientid_t *clid, struct xdr_netobj *owner)
{
	struct nfs4_ol_stateid *lst;

	if (!same_owner_str(&lo->lo_owner, owner, clid))
		return false;
	lst = list_first_entry(&lo->lo_owner.so_stateids,
			       struct nfs4_ol_stateid, st_perstateowner);
	return lst->st_file->fi_inode == inode;
}

static struct nfs4_lockowner *
find_lockowner_str(struct inode *inode, clientid_t *clid,
		   struct xdr_netobj *owner, struct nfsd_net *nn)
{
	unsigned int hashval = lockowner_ino_hashval(inode, clid->cl_id, owner);
	struct nfs4_lockowner *lo;

	list_for_each_entry(lo, &nn->lockowner_ino_hashtbl[hashval], lo_owner_ino_hash) {
		if (same_lockowner_ino(lo, inode, clid, owner))
			return lo;
	}
	return NULL;
}

static void hash_lockowner(struct nfs4_lockowner *lo, unsigned int strhashval, struct nfs4_client *clp, struct nfs4_ol_stateid *open_stp)
{
	struct inode *inode = open_stp->st_file->fi_inode;
	unsigned int inohash = lockowner_ino_hashval(inode,
			clp->cl_clientid.cl_id, &lo->lo_owner.so_owner);
	struct nfsd_net *nn = net_generic(clp->net, nfsd_net_id);

	list_add(&lo->lo_owner.so_strhash, &nn->ownerstr_hashtbl[strhashval]);
	list_add(&lo->lo_owner_ino_hash, &nn->lockowner_ino_hashtbl[inohash]);
	list_add(&lo->lo_perstateid, &open_stp->st_lockowners);
}

/*
 * Alloc a lock owner structure.
 * Called in nfsd4_lock - therefore, OPEN and OPEN_CONFIRM (if needed) has 
 * occurred. 
 *
 * strhashval = ownerstr_hashval
 */

static struct nfs4_lockowner *
alloc_init_lock_stateowner(unsigned int strhashval, struct nfs4_client *clp, struct nfs4_ol_stateid *open_stp, struct nfsd4_lock *lock) {
	struct nfs4_lockowner *lo;

	lo = alloc_stateowner(lockowner_slab, &lock->lk_new_owner, clp);
	if (!lo)
		return NULL;
	INIT_LIST_HEAD(&lo->lo_owner.so_stateids);
	lo->lo_owner.so_is_open_owner = 0;
	/* It is the openowner seqid that will be incremented in encode in the
	 * case of new lockowners; so increment the lock seqid manually: */
	lo->lo_owner.so_seqid = lock->lk_new_lock_seqid + 1;
	hash_lockowner(lo, strhashval, clp, open_stp);
	return lo;
}

static struct nfs4_ol_stateid *
alloc_init_lock_stateid(struct nfs4_lockowner *lo, struct nfs4_file *fp, struct nfs4_ol_stateid *open_stp)
{
	struct nfs4_ol_stateid *stp;
	struct nfs4_client *clp = lo->lo_owner.so_client;

	stp = nfs4_alloc_stateid(clp);
	if (stp == NULL)
		return NULL;
	init_stid(&stp->st_stid, clp, NFS4_LOCK_STID);
	list_add(&stp->st_perfile, &fp->fi_stateids);
	list_add(&stp->st_perstateowner, &lo->lo_owner.so_stateids);
	stp->st_stateowner = &lo->lo_owner;
	get_nfs4_file(fp);
	stp->st_file = fp;
	stp->st_access_bmap = 0;
	stp->st_deny_bmap = open_stp->st_deny_bmap;
	stp->st_openstp = open_stp;
	return stp;
}

static int
check_lock_length(u64 offset, u64 length)
{
	return ((length == 0)  || ((length != NFS4_MAX_UINT64) &&
	     LOFF_OVERFLOW(offset, length)));
}

static void get_lock_access(struct nfs4_ol_stateid *lock_stp, u32 access)
{
	struct nfs4_file *fp = lock_stp->st_file;
	int oflag = nfs4_access_to_omode(access);

	if (test_access(access, lock_stp))
		return;
	nfs4_file_get_access(fp, oflag);
	set_access(access, lock_stp);
}

static __be32 lookup_or_create_lock_state(struct nfsd4_compound_state *cstate, struct nfs4_ol_stateid *ost, struct nfsd4_lock *lock, struct nfs4_ol_stateid **lst, bool *new)
{
	struct nfs4_file *fi = ost->st_file;
	struct nfs4_openowner *oo = openowner(ost->st_stateowner);
	struct nfs4_client *cl = oo->oo_owner.so_client;
	struct nfs4_lockowner *lo;
	unsigned int strhashval;
	struct nfsd_net *nn = net_generic(cl->net, nfsd_net_id);

	lo = find_lockowner_str(fi->fi_inode, &cl->cl_clientid,
				&lock->v.new.owner, nn);
	if (lo) {
		if (!cstate->minorversion)
			return nfserr_bad_seqid;
		/* XXX: a lockowner always has exactly one stateid: */
		*lst = list_first_entry(&lo->lo_owner.so_stateids,
				struct nfs4_ol_stateid, st_perstateowner);
		return nfs_ok;
	}
	strhashval = ownerstr_hashval(cl->cl_clientid.cl_id,
			&lock->v.new.owner);
	lo = alloc_init_lock_stateowner(strhashval, cl, ost, lock);
	if (lo == NULL)
		return nfserr_jukebox;
	*lst = alloc_init_lock_stateid(lo, fi, ost);
	if (*lst == NULL) {
		release_lockowner(lo);
		return nfserr_jukebox;
	}
	*new = true;
	return nfs_ok;
}

/*
 *  LOCK operation 
 */
__be32
nfsd4_lock(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	   struct nfsd4_lock *lock)
{
	struct nfs4_openowner *open_sop = NULL;
	struct nfs4_lockowner *lock_sop = NULL;
	struct nfs4_ol_stateid *lock_stp;
	struct file *filp = NULL;
	struct file_lock *file_lock = NULL;
	struct file_lock *conflock = NULL;
	__be32 status = 0;
	bool new_state = false;
	int lkflg;
	int err;
	struct net *net = SVC_NET(rqstp);
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	dprintk("NFSD: nfsd4_lock: start=%Ld length=%Ld\n",
		(long long) lock->lk_offset,
		(long long) lock->lk_length);

	if (check_lock_length(lock->lk_offset, lock->lk_length))
		 return nfserr_inval;

	if ((status = fh_verify(rqstp, &cstate->current_fh,
				S_IFREG, NFSD_MAY_LOCK))) {
		dprintk("NFSD: nfsd4_lock: permission denied!\n");
		return status;
	}

	nfs4_lock_state();

	if (lock->lk_is_new) {
		struct nfs4_ol_stateid *open_stp = NULL;

		if (nfsd4_has_session(cstate))
			/* See rfc 5661 18.10.3: given clientid is ignored: */
			memcpy(&lock->v.new.clientid,
				&cstate->session->se_client->cl_clientid,
				sizeof(clientid_t));

		status = nfserr_stale_clientid;
		if (STALE_CLIENTID(&lock->lk_new_clientid, nn))
			goto out;

		/* validate and update open stateid and open seqid */
		status = nfs4_preprocess_confirmed_seqid_op(cstate,
				        lock->lk_new_open_seqid,
		                        &lock->lk_new_open_stateid,
					&open_stp, nn);
		if (status)
			goto out;
		open_sop = openowner(open_stp->st_stateowner);
		status = nfserr_bad_stateid;
		if (!same_clid(&open_sop->oo_owner.so_client->cl_clientid,
						&lock->v.new.clientid))
			goto out;
		status = lookup_or_create_lock_state(cstate, open_stp, lock,
							&lock_stp, &new_state);
	} else
		status = nfs4_preprocess_seqid_op(cstate,
				       lock->lk_old_lock_seqid,
				       &lock->lk_old_lock_stateid,
				       NFS4_LOCK_STID, &lock_stp, nn);
	if (status)
		goto out;
	lock_sop = lockowner(lock_stp->st_stateowner);

	lkflg = setlkflg(lock->lk_type);
	status = nfs4_check_openmode(lock_stp, lkflg);
	if (status)
		goto out;

	status = nfserr_grace;
	if (locks_in_grace(net) && !lock->lk_reclaim)
		goto out;
	status = nfserr_no_grace;
	if (!locks_in_grace(net) && lock->lk_reclaim)
		goto out;

	file_lock = locks_alloc_lock();
	if (!file_lock) {
		dprintk("NFSD: %s: unable to allocate lock!\n", __func__);
		status = nfserr_jukebox;
		goto out;
	}

	locks_init_lock(file_lock);
	switch (lock->lk_type) {
		case NFS4_READ_LT:
		case NFS4_READW_LT:
			filp = find_readable_file(lock_stp->st_file);
			if (filp)
				get_lock_access(lock_stp, NFS4_SHARE_ACCESS_READ);
			file_lock->fl_type = F_RDLCK;
			break;
		case NFS4_WRITE_LT:
		case NFS4_WRITEW_LT:
			filp = find_writeable_file(lock_stp->st_file);
			if (filp)
				get_lock_access(lock_stp, NFS4_SHARE_ACCESS_WRITE);
			file_lock->fl_type = F_WRLCK;
			break;
		default:
			status = nfserr_inval;
		goto out;
	}
	if (!filp) {
		status = nfserr_openmode;
		goto out;
	}
	file_lock->fl_owner = (fl_owner_t)lock_sop;
	file_lock->fl_pid = current->tgid;
	file_lock->fl_file = filp;
	file_lock->fl_flags = FL_POSIX;
	file_lock->fl_lmops = &nfsd_posix_mng_ops;
	file_lock->fl_start = lock->lk_offset;
	file_lock->fl_end = last_byte_offset(lock->lk_offset, lock->lk_length);
	nfs4_transform_lock_offset(file_lock);

	conflock = locks_alloc_lock();
	if (!conflock) {
		dprintk("NFSD: %s: unable to allocate lock!\n", __func__);
		status = nfserr_jukebox;
		goto out;
	}

	err = vfs_lock_file(filp, F_SETLK, file_lock, conflock);
	switch (-err) {
	case 0: /* success! */
		update_stateid(&lock_stp->st_stid.sc_stateid);
		memcpy(&lock->lk_resp_stateid, &lock_stp->st_stid.sc_stateid, 
				sizeof(stateid_t));
		status = 0;
		break;
	case (EAGAIN):		/* conflock holds conflicting lock */
		status = nfserr_denied;
		dprintk("NFSD: nfsd4_lock: conflicting lock found!\n");
		nfs4_set_lock_denied(conflock, &lock->lk_denied);
		break;
	case (EDEADLK):
		status = nfserr_deadlock;
		break;
	default:
		dprintk("NFSD: nfsd4_lock: vfs_lock_file() failed! status %d\n",err);
		status = nfserrno(err);
		break;
	}
out:
	if (status && new_state)
		release_lockowner(lock_sop);
	if (!cstate->replay_owner)
		nfs4_unlock_state();
	if (file_lock)
		locks_free_lock(file_lock);
	if (conflock)
		locks_free_lock(conflock);
	return status;
}

/*
 * The NFSv4 spec allows a client to do a LOCKT without holding an OPEN,
 * so we do a temporary open here just to get an open file to pass to
 * vfs_test_lock.  (Arguably perhaps test_lock should be done with an
 * inode operation.)
 */
static __be32 nfsd_test_lock(struct svc_rqst *rqstp, struct svc_fh *fhp, struct file_lock *lock)
{
	struct file *file;
	__be32 err = nfsd_open(rqstp, fhp, S_IFREG, NFSD_MAY_READ, &file);
	if (!err) {
		err = nfserrno(vfs_test_lock(file, lock));
		nfsd_close(file);
	}
	return err;
}

/*
 * LOCKT operation
 */
__be32
nfsd4_lockt(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	    struct nfsd4_lockt *lockt)
{
	struct inode *inode;
	struct file_lock *file_lock = NULL;
	struct nfs4_lockowner *lo;
	__be32 status;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	if (locks_in_grace(SVC_NET(rqstp)))
		return nfserr_grace;

	if (check_lock_length(lockt->lt_offset, lockt->lt_length))
		 return nfserr_inval;

	nfs4_lock_state();

	status = nfserr_stale_clientid;
	if (!nfsd4_has_session(cstate) && STALE_CLIENTID(&lockt->lt_clientid, nn))
		goto out;

	if ((status = fh_verify(rqstp, &cstate->current_fh, S_IFREG, 0)))
		goto out;

	inode = cstate->current_fh.fh_dentry->d_inode;
	file_lock = locks_alloc_lock();
	if (!file_lock) {
		dprintk("NFSD: %s: unable to allocate lock!\n", __func__);
		status = nfserr_jukebox;
		goto out;
	}
	locks_init_lock(file_lock);
	switch (lockt->lt_type) {
		case NFS4_READ_LT:
		case NFS4_READW_LT:
			file_lock->fl_type = F_RDLCK;
		break;
		case NFS4_WRITE_LT:
		case NFS4_WRITEW_LT:
			file_lock->fl_type = F_WRLCK;
		break;
		default:
			dprintk("NFSD: nfs4_lockt: bad lock type!\n");
			status = nfserr_inval;
		goto out;
	}

	lo = find_lockowner_str(inode, &lockt->lt_clientid, &lockt->lt_owner, nn);
	if (lo)
		file_lock->fl_owner = (fl_owner_t)lo;
	file_lock->fl_pid = current->tgid;
	file_lock->fl_flags = FL_POSIX;

	file_lock->fl_start = lockt->lt_offset;
	file_lock->fl_end = last_byte_offset(lockt->lt_offset, lockt->lt_length);

	nfs4_transform_lock_offset(file_lock);

	status = nfsd_test_lock(rqstp, &cstate->current_fh, file_lock);
	if (status)
		goto out;

	if (file_lock->fl_type != F_UNLCK) {
		status = nfserr_denied;
		nfs4_set_lock_denied(file_lock, &lockt->lt_denied);
	}
out:
	nfs4_unlock_state();
	if (file_lock)
		locks_free_lock(file_lock);
	return status;
}

__be32
nfsd4_locku(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	    struct nfsd4_locku *locku)
{
	struct nfs4_ol_stateid *stp;
	struct file *filp = NULL;
	struct file_lock *file_lock = NULL;
	__be32 status;
	int err;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	dprintk("NFSD: nfsd4_locku: start=%Ld length=%Ld\n",
		(long long) locku->lu_offset,
		(long long) locku->lu_length);

	if (check_lock_length(locku->lu_offset, locku->lu_length))
		 return nfserr_inval;

	nfs4_lock_state();
									        
	status = nfs4_preprocess_seqid_op(cstate, locku->lu_seqid,
					&locku->lu_stateid, NFS4_LOCK_STID,
					&stp, nn);
	if (status)
		goto out;
	filp = find_any_file(stp->st_file);
	if (!filp) {
		status = nfserr_lock_range;
		goto out;
	}
	file_lock = locks_alloc_lock();
	if (!file_lock) {
		dprintk("NFSD: %s: unable to allocate lock!\n", __func__);
		status = nfserr_jukebox;
		goto out;
	}
	locks_init_lock(file_lock);
	file_lock->fl_type = F_UNLCK;
	file_lock->fl_owner = (fl_owner_t)lockowner(stp->st_stateowner);
	file_lock->fl_pid = current->tgid;
	file_lock->fl_file = filp;
	file_lock->fl_flags = FL_POSIX;
	file_lock->fl_lmops = &nfsd_posix_mng_ops;
	file_lock->fl_start = locku->lu_offset;

	file_lock->fl_end = last_byte_offset(locku->lu_offset,
						locku->lu_length);
	nfs4_transform_lock_offset(file_lock);

	/*
	*  Try to unlock the file in the VFS.
	*/
	err = vfs_lock_file(filp, F_SETLK, file_lock, NULL);
	if (err) {
		dprintk("NFSD: nfs4_locku: vfs_lock_file failed!\n");
		goto out_nfserr;
	}
	/*
	* OK, unlock succeeded; the only thing left to do is update the stateid.
	*/
	update_stateid(&stp->st_stid.sc_stateid);
	memcpy(&locku->lu_stateid, &stp->st_stid.sc_stateid, sizeof(stateid_t));

out:
	if (!cstate->replay_owner)
		nfs4_unlock_state();
	if (file_lock)
		locks_free_lock(file_lock);
	return status;

out_nfserr:
	status = nfserrno(err);
	goto out;
}

/*
 * returns
 * 	1: locks held by lockowner
 * 	0: no locks held by lockowner
 */
static int
check_for_locks(struct nfs4_file *filp, struct nfs4_lockowner *lowner)
{
	struct file_lock **flpp;
	struct inode *inode = filp->fi_inode;
	int status = 0;

	lock_flocks();
	for (flpp = &inode->i_flock; *flpp != NULL; flpp = &(*flpp)->fl_next) {
		if ((*flpp)->fl_owner == (fl_owner_t)lowner) {
			status = 1;
			goto out;
		}
	}
out:
	unlock_flocks();
	return status;
}

__be32
nfsd4_release_lockowner(struct svc_rqst *rqstp,
			struct nfsd4_compound_state *cstate,
			struct nfsd4_release_lockowner *rlockowner)
{
	clientid_t *clid = &rlockowner->rl_clientid;
	struct nfs4_stateowner *sop;
	struct nfs4_lockowner *lo;
	struct nfs4_ol_stateid *stp;
	struct xdr_netobj *owner = &rlockowner->rl_owner;
	struct list_head matches;
	unsigned int hashval = ownerstr_hashval(clid->cl_id, owner);
	__be32 status;
	struct nfsd_net *nn = net_generic(SVC_NET(rqstp), nfsd_net_id);

	dprintk("nfsd4_release_lockowner clientid: (%08x/%08x):\n",
		clid->cl_boot, clid->cl_id);

	/* XXX check for lease expiration */

	status = nfserr_stale_clientid;
	if (STALE_CLIENTID(clid, nn))
		return status;

	nfs4_lock_state();

	status = nfserr_locks_held;
	INIT_LIST_HEAD(&matches);

	list_for_each_entry(sop, &nn->ownerstr_hashtbl[hashval], so_strhash) {
		if (sop->so_is_open_owner)
			continue;
		if (!same_owner_str(sop, owner, clid))
			continue;
		list_for_each_entry(stp, &sop->so_stateids,
				st_perstateowner) {
			lo = lockowner(sop);
			if (check_for_locks(stp->st_file, lo))
				goto out;
			list_add(&lo->lo_list, &matches);
		}
	}
	/* Clients probably won't expect us to return with some (but not all)
	 * of the lockowner state released; so don't release any until all
	 * have been checked. */
	status = nfs_ok;
	while (!list_empty(&matches)) {
		lo = list_entry(matches.next, struct nfs4_lockowner,
								lo_list);
		/* unhash_stateowner deletes so_perclient only
		 * for openowners. */
		list_del(&lo->lo_list);
		release_lockowner(lo);
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

bool
nfs4_has_reclaimed_state(const char *name, struct nfsd_net *nn)
{
	struct nfs4_client_reclaim *crp;

	crp = nfsd4_find_reclaim_client(name, nn);
	return (crp && crp->cr_clp);
}

/*
 * failure => all reset bets are off, nfserr_no_grace...
 */
struct nfs4_client_reclaim *
nfs4_client_to_reclaim(const char *name, struct nfsd_net *nn)
{
	unsigned int strhashval;
	struct nfs4_client_reclaim *crp;

	dprintk("NFSD nfs4_client_to_reclaim NAME: %.*s\n", HEXDIR_LEN, name);
	crp = alloc_reclaim();
	if (crp) {
		strhashval = clientstr_hashval(name);
		INIT_LIST_HEAD(&crp->cr_strhash);
		list_add(&crp->cr_strhash, &nn->reclaim_str_hashtbl[strhashval]);
		memcpy(crp->cr_recdir, name, HEXDIR_LEN);
		crp->cr_clp = NULL;
		nn->reclaim_str_hashtbl_size++;
	}
	return crp;
}

void
nfs4_remove_reclaim_record(struct nfs4_client_reclaim *crp, struct nfsd_net *nn)
{
	list_del(&crp->cr_strhash);
	kfree(crp);
	nn->reclaim_str_hashtbl_size--;
}

void
nfs4_release_reclaim(struct nfsd_net *nn)
{
	struct nfs4_client_reclaim *crp = NULL;
	int i;

	for (i = 0; i < CLIENT_HASH_SIZE; i++) {
		while (!list_empty(&nn->reclaim_str_hashtbl[i])) {
			crp = list_entry(nn->reclaim_str_hashtbl[i].next,
			                struct nfs4_client_reclaim, cr_strhash);
			nfs4_remove_reclaim_record(crp, nn);
		}
	}
	BUG_ON(nn->reclaim_str_hashtbl_size);
}

/*
 * called from OPEN, CLAIM_PREVIOUS with a new clientid. */
struct nfs4_client_reclaim *
nfsd4_find_reclaim_client(const char *recdir, struct nfsd_net *nn)
{
	unsigned int strhashval;
	struct nfs4_client_reclaim *crp = NULL;

	dprintk("NFSD: nfs4_find_reclaim_client for recdir %s\n", recdir);

	strhashval = clientstr_hashval(recdir);
	list_for_each_entry(crp, &nn->reclaim_str_hashtbl[strhashval], cr_strhash) {
		if (same_name(crp->cr_recdir, recdir)) {
			return crp;
		}
	}
	return NULL;
}

/*
* Called from OPEN. Look for clientid in reclaim list.
*/
__be32
nfs4_check_open_reclaim(clientid_t *clid, bool sessions, struct nfsd_net *nn)
{
	struct nfs4_client *clp;

	/* find clientid in conf_id_hashtbl */
	clp = find_confirmed_client(clid, sessions, nn);
	if (clp == NULL)
		return nfserr_reclaim_bad;

	return nfsd4_client_record_check(clp) ? nfserr_reclaim_bad : nfs_ok;
}

#ifdef CONFIG_NFSD_FAULT_INJECTION

void nfsd_forget_clients(u64 num)
{
	struct nfs4_client *clp, *next;
	int count = 0;
	struct nfsd_net *nn = net_generic(current->nsproxy->net_ns, nfsd_net_id);

	nfs4_lock_state();
	list_for_each_entry_safe(clp, next, &nn->client_lru, cl_lru) {
		expire_client(clp);
		if (++count == num)
			break;
	}
	nfs4_unlock_state();

	printk(KERN_INFO "NFSD: Forgot %d clients", count);
}

static void release_lockowner_sop(struct nfs4_stateowner *sop)
{
	release_lockowner(lockowner(sop));
}

static void release_openowner_sop(struct nfs4_stateowner *sop)
{
	release_openowner(openowner(sop));
}

static int nfsd_release_n_owners(u64 num, bool is_open_owner,
				void (*release_sop)(struct nfs4_stateowner *),
				struct nfsd_net *nn)
{
	int i, count = 0;
	struct nfs4_stateowner *sop, *next;

	for (i = 0; i < OWNER_HASH_SIZE; i++) {
		list_for_each_entry_safe(sop, next, &nn->ownerstr_hashtbl[i], so_strhash) {
			if (sop->so_is_open_owner != is_open_owner)
				continue;
			release_sop(sop);
			if (++count == num)
				return count;
		}
	}
	return count;
}

void nfsd_forget_locks(u64 num)
{
	int count;
	struct nfsd_net *nn = net_generic(&init_net, nfsd_net_id);

	nfs4_lock_state();
	count = nfsd_release_n_owners(num, false, release_lockowner_sop, nn);
	nfs4_unlock_state();

	printk(KERN_INFO "NFSD: Forgot %d locks", count);
}

void nfsd_forget_openowners(u64 num)
{
	int count;
	struct nfsd_net *nn = net_generic(&init_net, nfsd_net_id);

	nfs4_lock_state();
	count = nfsd_release_n_owners(num, true, release_openowner_sop, nn);
	nfs4_unlock_state();

	printk(KERN_INFO "NFSD: Forgot %d open owners", count);
}

static int nfsd_process_n_delegations(u64 num, struct list_head *list)
{
	int i, count = 0;
	struct nfs4_file *fp, *fnext;
	struct nfs4_delegation *dp, *dnext;

	for (i = 0; i < FILE_HASH_SIZE; i++) {
		list_for_each_entry_safe(fp, fnext, &file_hashtbl[i], fi_hash) {
			list_for_each_entry_safe(dp, dnext, &fp->fi_delegations, dl_perfile) {
				list_move(&dp->dl_recall_lru, list);
				if (++count == num)
					return count;
			}
		}
	}

	return count;
}

void nfsd_forget_delegations(u64 num)
{
	unsigned int count;
	LIST_HEAD(victims);
	struct nfs4_delegation *dp, *dnext;

	spin_lock(&recall_lock);
	count = nfsd_process_n_delegations(num, &victims);
	spin_unlock(&recall_lock);

	nfs4_lock_state();
	list_for_each_entry_safe(dp, dnext, &victims, dl_recall_lru)
		unhash_delegation(dp);
	nfs4_unlock_state();

	printk(KERN_INFO "NFSD: Forgot %d delegations", count);
}

void nfsd_recall_delegations(u64 num)
{
	unsigned int count;
	LIST_HEAD(victims);
	struct nfs4_delegation *dp, *dnext;

	spin_lock(&recall_lock);
	count = nfsd_process_n_delegations(num, &victims);
	list_for_each_entry_safe(dp, dnext, &victims, dl_recall_lru) {
		list_del(&dp->dl_recall_lru);
		nfsd_break_one_deleg(dp);
	}
	spin_unlock(&recall_lock);

	printk(KERN_INFO "NFSD: Recalled %d delegations", count);
}

#endif /* CONFIG_NFSD_FAULT_INJECTION */

/* initialization to perform at module load time: */

void
nfs4_state_init(void)
{
	int i;

	for (i = 0; i < FILE_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&file_hashtbl[i]);
	}
	INIT_LIST_HEAD(&del_recall_lru);
}

/*
 * Since the lifetime of a delegation isn't limited to that of an open, a
 * client may quite reasonably hang on to a delegation as long as it has
 * the inode cached.  This becomes an obvious problem the first time a
 * client's inode cache approaches the size of the server's total memory.
 *
 * For now we avoid this problem by imposing a hard limit on the number
 * of delegations, which varies according to the server's memory size.
 */
static void
set_max_delegations(void)
{
	/*
	 * Allow at most 4 delegations per megabyte of RAM.  Quick
	 * estimates suggest that in the worst case (where every delegation
	 * is for a different inode), a delegation could take about 1.5K,
	 * giving a worst case usage of about 6% of memory.
	 */
	max_delegations = nr_free_buffer_pages() >> (20 - 2 - PAGE_SHIFT);
}

static int nfs4_state_start_net(struct net *net)
{
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	int i;

	nn->conf_id_hashtbl = kmalloc(sizeof(struct list_head) *
			CLIENT_HASH_SIZE, GFP_KERNEL);
	if (!nn->conf_id_hashtbl)
		goto err;
	nn->unconf_id_hashtbl = kmalloc(sizeof(struct list_head) *
			CLIENT_HASH_SIZE, GFP_KERNEL);
	if (!nn->unconf_id_hashtbl)
		goto err_unconf_id;
	nn->ownerstr_hashtbl = kmalloc(sizeof(struct list_head) *
			OWNER_HASH_SIZE, GFP_KERNEL);
	if (!nn->ownerstr_hashtbl)
		goto err_ownerstr;
	nn->lockowner_ino_hashtbl = kmalloc(sizeof(struct list_head) *
			LOCKOWNER_INO_HASH_SIZE, GFP_KERNEL);
	if (!nn->lockowner_ino_hashtbl)
		goto err_lockowner_ino;
	nn->sessionid_hashtbl = kmalloc(sizeof(struct list_head) *
			SESSION_HASH_SIZE, GFP_KERNEL);
	if (!nn->sessionid_hashtbl)
		goto err_sessionid;

	for (i = 0; i < CLIENT_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&nn->conf_id_hashtbl[i]);
		INIT_LIST_HEAD(&nn->unconf_id_hashtbl[i]);
	}
	for (i = 0; i < OWNER_HASH_SIZE; i++)
		INIT_LIST_HEAD(&nn->ownerstr_hashtbl[i]);
	for (i = 0; i < LOCKOWNER_INO_HASH_SIZE; i++)
		INIT_LIST_HEAD(&nn->lockowner_ino_hashtbl[i]);
	for (i = 0; i < SESSION_HASH_SIZE; i++)
		INIT_LIST_HEAD(&nn->sessionid_hashtbl[i]);
	nn->conf_name_tree = RB_ROOT;
	nn->unconf_name_tree = RB_ROOT;
	INIT_LIST_HEAD(&nn->client_lru);
	INIT_LIST_HEAD(&nn->close_lru);

	return 0;

err_sessionid:
	kfree(nn->lockowner_ino_hashtbl);
err_lockowner_ino:
	kfree(nn->ownerstr_hashtbl);
err_ownerstr:
	kfree(nn->unconf_id_hashtbl);
err_unconf_id:
	kfree(nn->conf_id_hashtbl);
err:
	return -ENOMEM;
}

static void
__nfs4_state_shutdown_net(struct net *net)
{
	int i;
	struct nfs4_client *clp = NULL;
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	struct rb_node *node, *tmp;

	for (i = 0; i < CLIENT_HASH_SIZE; i++) {
		while (!list_empty(&nn->conf_id_hashtbl[i])) {
			clp = list_entry(nn->conf_id_hashtbl[i].next, struct nfs4_client, cl_idhash);
			destroy_client(clp);
		}
	}

	node = rb_first(&nn->unconf_name_tree);
	while (node != NULL) {
		tmp = node;
		node = rb_next(tmp);
		clp = rb_entry(tmp, struct nfs4_client, cl_namenode);
		rb_erase(tmp, &nn->unconf_name_tree);
		destroy_client(clp);
	}

	kfree(nn->sessionid_hashtbl);
	kfree(nn->lockowner_ino_hashtbl);
	kfree(nn->ownerstr_hashtbl);
	kfree(nn->unconf_id_hashtbl);
	kfree(nn->conf_id_hashtbl);
}

/* initialization to perform when the nfsd service is started: */

int
nfs4_state_start(void)
{
	struct net *net = &init_net;
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);
	int ret;

	/*
	 * FIXME: For now, we hang most of the pernet global stuff off of
	 * init_net until nfsd is fully containerized. Eventually, we'll
	 * need to pass a net pointer into this function, take a reference
	 * to that instead and then do most of the rest of this on a per-net
	 * basis.
	 */
	get_net(net);
	ret = nfs4_state_start_net(net);
	if (ret)
		return ret;
	nfsd4_client_tracking_init(net);
	nn->boot_time = get_seconds();
	locks_start_grace(net, &nn->nfsd4_manager);
	nn->grace_ended = false;
	printk(KERN_INFO "NFSD: starting %ld-second grace period\n",
	       nfsd4_grace);
	ret = set_callback_cred();
	if (ret) {
		ret = -ENOMEM;
		goto out_recovery;
	}
	laundry_wq = create_singlethread_workqueue("nfsd4");
	if (laundry_wq == NULL) {
		ret = -ENOMEM;
		goto out_recovery;
	}
	ret = nfsd4_create_callback_queue();
	if (ret)
		goto out_free_laundry;
	queue_delayed_work(laundry_wq, &laundromat_work, nfsd4_grace * HZ);
	set_max_delegations();
	return 0;
out_free_laundry:
	destroy_workqueue(laundry_wq);
out_recovery:
	nfsd4_client_tracking_exit(net);
	__nfs4_state_shutdown_net(net);
	put_net(net);
	return ret;
}

/* should be called with the state lock held */
static void
__nfs4_state_shutdown(struct net *net)
{
	struct nfs4_delegation *dp = NULL;
	struct list_head *pos, *next, reaplist;

	__nfs4_state_shutdown_net(net);

	INIT_LIST_HEAD(&reaplist);
	spin_lock(&recall_lock);
	list_for_each_safe(pos, next, &del_recall_lru) {
		dp = list_entry (pos, struct nfs4_delegation, dl_recall_lru);
		list_move(&dp->dl_recall_lru, &reaplist);
	}
	spin_unlock(&recall_lock);
	list_for_each_safe(pos, next, &reaplist) {
		dp = list_entry (pos, struct nfs4_delegation, dl_recall_lru);
		unhash_delegation(dp);
	}

	nfsd4_client_tracking_exit(net);
	put_net(net);
}

void
nfs4_state_shutdown(void)
{
	struct net *net = &init_net;
	struct nfsd_net *nn = net_generic(net, nfsd_net_id);

	cancel_delayed_work_sync(&laundromat_work);
	destroy_workqueue(laundry_wq);
	locks_end_grace(&nn->nfsd4_manager);
	nfs4_lock_state();
	__nfs4_state_shutdown(net);
	nfs4_unlock_state();
	nfsd4_destroy_callback_queue();
}

static void
get_stateid(struct nfsd4_compound_state *cstate, stateid_t *stateid)
{
	if (HAS_STATE_ID(cstate, CURRENT_STATE_ID_FLAG) && CURRENT_STATEID(stateid))
		memcpy(stateid, &cstate->current_stateid, sizeof(stateid_t));
}

static void
put_stateid(struct nfsd4_compound_state *cstate, stateid_t *stateid)
{
	if (cstate->minorversion) {
		memcpy(&cstate->current_stateid, stateid, sizeof(stateid_t));
		SET_STATE_ID(cstate, CURRENT_STATE_ID_FLAG);
	}
}

void
clear_current_stateid(struct nfsd4_compound_state *cstate)
{
	CLEAR_STATE_ID(cstate, CURRENT_STATE_ID_FLAG);
}

/*
 * functions to set current state id
 */
void
nfsd4_set_opendowngradestateid(struct nfsd4_compound_state *cstate, struct nfsd4_open_downgrade *odp)
{
	put_stateid(cstate, &odp->od_stateid);
}

void
nfsd4_set_openstateid(struct nfsd4_compound_state *cstate, struct nfsd4_open *open)
{
	put_stateid(cstate, &open->op_stateid);
}

void
nfsd4_set_closestateid(struct nfsd4_compound_state *cstate, struct nfsd4_close *close)
{
	put_stateid(cstate, &close->cl_stateid);
}

void
nfsd4_set_lockstateid(struct nfsd4_compound_state *cstate, struct nfsd4_lock *lock)
{
	put_stateid(cstate, &lock->lk_resp_stateid);
}

/*
 * functions to consume current state id
 */

void
nfsd4_get_opendowngradestateid(struct nfsd4_compound_state *cstate, struct nfsd4_open_downgrade *odp)
{
	get_stateid(cstate, &odp->od_stateid);
}

void
nfsd4_get_delegreturnstateid(struct nfsd4_compound_state *cstate, struct nfsd4_delegreturn *drp)
{
	get_stateid(cstate, &drp->dr_stateid);
}

void
nfsd4_get_freestateid(struct nfsd4_compound_state *cstate, struct nfsd4_free_stateid *fsp)
{
	get_stateid(cstate, &fsp->fr_stateid);
}

void
nfsd4_get_setattrstateid(struct nfsd4_compound_state *cstate, struct nfsd4_setattr *setattr)
{
	get_stateid(cstate, &setattr->sa_stateid);
}

void
nfsd4_get_closestateid(struct nfsd4_compound_state *cstate, struct nfsd4_close *close)
{
	get_stateid(cstate, &close->cl_stateid);
}

void
nfsd4_get_lockustateid(struct nfsd4_compound_state *cstate, struct nfsd4_locku *locku)
{
	get_stateid(cstate, &locku->lu_stateid);
}

void
nfsd4_get_readstateid(struct nfsd4_compound_state *cstate, struct nfsd4_read *read)
{
	get_stateid(cstate, &read->rd_stateid);
}

void
nfsd4_get_writestateid(struct nfsd4_compound_state *cstate, struct nfsd4_write *write)
{
	get_stateid(cstate, &write->wr_stateid);
}
