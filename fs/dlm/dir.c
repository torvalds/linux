// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**
*******************************************************************************
******************************************************************************/

#include "dlm_internal.h"
#include "lockspace.h"
#include "member.h"
#include "lowcomms.h"
#include "rcom.h"
#include "config.h"
#include "memory.h"
#include "recover.h"
#include "util.h"
#include "lock.h"
#include "dir.h"

/*
 * We use the upper 16 bits of the hash value to select the directory node.
 * Low bits are used for distribution of rsb's among hash buckets on each node.
 *
 * To give the exact range wanted (0 to num_nodes-1), we apply a modulus of
 * num_nodes to the hash value.  This value in the desired range is used as an
 * offset into the sorted list of nodeid's to give the particular nodeid.
 */

int dlm_hash2nodeid(struct dlm_ls *ls, uint32_t hash)
{
	uint32_t node;

	if (ls->ls_num_nodes == 1)
		return dlm_our_nodeid();
	else {
		node = (hash >> 16) % ls->ls_total_weight;
		return ls->ls_node_array[node];
	}
}

int dlm_dir_nodeid(struct dlm_rsb *r)
{
	return r->res_dir_nodeid;
}

void dlm_recover_dir_nodeid(struct dlm_ls *ls, const struct list_head *root_list)
{
	struct dlm_rsb *r;

	list_for_each_entry(r, root_list, res_root_list) {
		r->res_dir_nodeid = dlm_hash2nodeid(ls, r->res_hash);
	}
}

int dlm_recover_directory(struct dlm_ls *ls, uint64_t seq)
{
	struct dlm_member *memb;
	char *b, *last_name = NULL;
	int error = -ENOMEM, last_len, nodeid, result;
	uint16_t namelen;
	unsigned int count = 0, count_match = 0, count_bad = 0, count_add = 0;

	log_rinfo(ls, "dlm_recover_directory");

	if (dlm_no_directory(ls))
		goto out_status;

	last_name = kmalloc(DLM_RESNAME_MAXLEN, GFP_NOFS);
	if (!last_name)
		goto out;

	list_for_each_entry(memb, &ls->ls_nodes, list) {
		if (memb->nodeid == dlm_our_nodeid())
			continue;

		memset(last_name, 0, DLM_RESNAME_MAXLEN);
		last_len = 0;

		for (;;) {
			int left;
			if (dlm_recovery_stopped(ls)) {
				error = -EINTR;
				goto out_free;
			}

			error = dlm_rcom_names(ls, memb->nodeid,
					       last_name, last_len, seq);
			if (error)
				goto out_free;

			cond_resched();

			/*
			 * pick namelen/name pairs out of received buffer
			 */

			b = ls->ls_recover_buf->rc_buf;
			left = le16_to_cpu(ls->ls_recover_buf->rc_header.h_length);
			left -= sizeof(struct dlm_rcom);

			for (;;) {
				__be16 v;

				error = -EINVAL;
				if (left < sizeof(__be16))
					goto out_free;

				memcpy(&v, b, sizeof(__be16));
				namelen = be16_to_cpu(v);
				b += sizeof(__be16);
				left -= sizeof(__be16);

				/* namelen of 0xFFFFF marks end of names for
				   this node; namelen of 0 marks end of the
				   buffer */

				if (namelen == 0xFFFF)
					goto done;
				if (!namelen)
					break;

				if (namelen > left)
					goto out_free;

				if (namelen > DLM_RESNAME_MAXLEN)
					goto out_free;

				error = dlm_master_lookup(ls, memb->nodeid,
							  b, namelen,
							  DLM_LU_RECOVER_DIR,
							  &nodeid, &result);
				if (error) {
					log_error(ls, "recover_dir lookup %d",
						  error);
					goto out_free;
				}

				/* The name was found in rsbtbl, but the
				 * master nodeid is different from
				 * memb->nodeid which says it is the master.
				 * This should not happen. */

				if (result == DLM_LU_MATCH &&
				    nodeid != memb->nodeid) {
					count_bad++;
					log_error(ls, "recover_dir lookup %d "
						  "nodeid %d memb %d bad %u",
						  result, nodeid, memb->nodeid,
						  count_bad);
					print_hex_dump_bytes("dlm_recover_dir ",
							     DUMP_PREFIX_NONE,
							     b, namelen);
				}

				/* The name was found in rsbtbl, and the
				 * master nodeid matches memb->nodeid. */

				if (result == DLM_LU_MATCH &&
				    nodeid == memb->nodeid) {
					count_match++;
				}

				/* The name was not found in rsbtbl and was
				 * added with memb->nodeid as the master. */

				if (result == DLM_LU_ADD) {
					count_add++;
				}

				last_len = namelen;
				memcpy(last_name, b, namelen);
				b += namelen;
				left -= namelen;
				count++;
			}
		}
	 done:
		;
	}

 out_status:
	error = 0;
	dlm_set_recover_status(ls, DLM_RS_DIR);

	log_rinfo(ls, "dlm_recover_directory %u in %u new",
		  count, count_add);
 out_free:
	kfree(last_name);
 out:
	return error;
}

static struct dlm_rsb *find_rsb_root(struct dlm_ls *ls, const char *name,
				     int len)
{
	struct dlm_rsb *r;
	int rv;

	read_lock_bh(&ls->ls_rsbtbl_lock);
	rv = dlm_search_rsb_tree(&ls->ls_rsbtbl, name, len, &r);
	read_unlock_bh(&ls->ls_rsbtbl_lock);
	if (!rv)
		return r;

	list_for_each_entry(r, &ls->ls_masters_list, res_masters_list) {
		if (len == r->res_length && !memcmp(name, r->res_name, len)) {
			log_debug(ls, "find_rsb_root revert to root_list %s",
				  r->res_name);
			return r;
		}
	}
	return NULL;
}

struct dlm_dir_dump {
	/* init values to match if whole
	 * dump fits to one seq. Sanity check only.
	 */
	uint64_t seq_init;
	uint64_t nodeid_init;
	/* compare local pointer with last lookup,
	 * just a sanity check.
	 */
	struct list_head *last;

	unsigned int sent_res; /* for log info */
	unsigned int sent_msg; /* for log info */

	struct list_head list;
};

static void drop_dir_ctx(struct dlm_ls *ls, int nodeid)
{
	struct dlm_dir_dump *dd, *safe;

	write_lock_bh(&ls->ls_dir_dump_lock);
	list_for_each_entry_safe(dd, safe, &ls->ls_dir_dump_list, list) {
		if (dd->nodeid_init == nodeid) {
			log_error(ls, "drop dump seq %llu",
				 (unsigned long long)dd->seq_init);
			list_del(&dd->list);
			kfree(dd);
		}
	}
	write_unlock_bh(&ls->ls_dir_dump_lock);
}

static struct dlm_dir_dump *lookup_dir_dump(struct dlm_ls *ls, int nodeid)
{
	struct dlm_dir_dump *iter, *dd = NULL;

	read_lock_bh(&ls->ls_dir_dump_lock);
	list_for_each_entry(iter, &ls->ls_dir_dump_list, list) {
		if (iter->nodeid_init == nodeid) {
			dd = iter;
			break;
		}
	}
	read_unlock_bh(&ls->ls_dir_dump_lock);

	return dd;
}

static struct dlm_dir_dump *init_dir_dump(struct dlm_ls *ls, int nodeid)
{
	struct dlm_dir_dump *dd;

	dd = lookup_dir_dump(ls, nodeid);
	if (dd) {
		log_error(ls, "found ongoing dir dump for node %d, will drop it",
			  nodeid);
		drop_dir_ctx(ls, nodeid);
	}

	dd = kzalloc(sizeof(*dd), GFP_ATOMIC);
	if (!dd)
		return NULL;

	dd->seq_init = ls->ls_recover_seq;
	dd->nodeid_init = nodeid;

	write_lock_bh(&ls->ls_dir_dump_lock);
	list_add(&dd->list, &ls->ls_dir_dump_list);
	write_unlock_bh(&ls->ls_dir_dump_lock);

	return dd;
}

/* Find the rsb where we left off (or start again), then send rsb names
   for rsb's we're master of and whose directory node matches the requesting
   node.  inbuf is the rsb name last sent, inlen is the name's length */

void dlm_copy_master_names(struct dlm_ls *ls, const char *inbuf, int inlen,
 			   char *outbuf, int outlen, int nodeid)
{
	struct list_head *list;
	struct dlm_rsb *r;
	int offset = 0, dir_nodeid;
	struct dlm_dir_dump *dd;
	__be16 be_namelen;

	read_lock_bh(&ls->ls_masters_lock);

	if (inlen > 1) {
		dd = lookup_dir_dump(ls, nodeid);
		if (!dd) {
			log_error(ls, "failed to lookup dir dump context nodeid: %d",
				  nodeid);
			goto out;
		}

		/* next chunk in dump */
		r = find_rsb_root(ls, inbuf, inlen);
		if (!r) {
			log_error(ls, "copy_master_names from %d start %d %.*s",
				  nodeid, inlen, inlen, inbuf);
			goto out;
		}
		list = r->res_masters_list.next;

		/* sanity checks */
		if (dd->last != &r->res_masters_list ||
		    dd->seq_init != ls->ls_recover_seq) {
			log_error(ls, "failed dir dump sanity check seq_init: %llu seq: %llu",
				  (unsigned long long)dd->seq_init,
				  (unsigned long long)ls->ls_recover_seq);
			goto out;
		}
	} else {
		dd = init_dir_dump(ls, nodeid);
		if (!dd) {
			log_error(ls, "failed to allocate dir dump context");
			goto out;
		}

		/* start dump */
		list = ls->ls_masters_list.next;
		dd->last = list;
	}

	for (offset = 0; list != &ls->ls_masters_list; list = list->next) {
		r = list_entry(list, struct dlm_rsb, res_masters_list);
		dir_nodeid = dlm_dir_nodeid(r);
		if (dir_nodeid != nodeid)
			continue;

		/*
		 * The block ends when we can't fit the following in the
		 * remaining buffer space:
		 * namelen (uint16_t) +
		 * name (r->res_length) +
		 * end-of-block record 0x0000 (uint16_t)
		 */

		if (offset + sizeof(uint16_t)*2 + r->res_length > outlen) {
			/* Write end-of-block record */
			be_namelen = cpu_to_be16(0);
			memcpy(outbuf + offset, &be_namelen, sizeof(__be16));
			offset += sizeof(__be16);
			dd->sent_msg++;
			goto out;
		}

		be_namelen = cpu_to_be16(r->res_length);
		memcpy(outbuf + offset, &be_namelen, sizeof(__be16));
		offset += sizeof(__be16);
		memcpy(outbuf + offset, r->res_name, r->res_length);
		offset += r->res_length;
		dd->sent_res++;
		dd->last = list;
	}

	/*
	 * If we've reached the end of the list (and there's room) write a
	 * terminating record.
	 */

	if ((list == &ls->ls_masters_list) &&
	    (offset + sizeof(uint16_t) <= outlen)) {
		/* end dump */
		be_namelen = cpu_to_be16(0xFFFF);
		memcpy(outbuf + offset, &be_namelen, sizeof(__be16));
		offset += sizeof(__be16);
		dd->sent_msg++;
		log_rinfo(ls, "dlm_recover_directory nodeid %d sent %u res out %u messages",
			  nodeid, dd->sent_res, dd->sent_msg);

		write_lock_bh(&ls->ls_dir_dump_lock);
		list_del_init(&dd->list);
		write_unlock_bh(&ls->ls_dir_dump_lock);
		kfree(dd);
	}
 out:
	read_unlock_bh(&ls->ls_masters_lock);
}

