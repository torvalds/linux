/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
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


static void put_free_de(struct dlm_ls *ls, struct dlm_direntry *de)
{
	spin_lock(&ls->ls_recover_list_lock);
	list_add(&de->list, &ls->ls_recover_list);
	spin_unlock(&ls->ls_recover_list_lock);
}

static struct dlm_direntry *get_free_de(struct dlm_ls *ls, int len)
{
	int found = 0;
	struct dlm_direntry *de;

	spin_lock(&ls->ls_recover_list_lock);
	list_for_each_entry(de, &ls->ls_recover_list, list) {
		if (de->length == len) {
			list_del(&de->list);
			de->master_nodeid = 0;
			memset(de->name, 0, len);
			found = 1;
			break;
		}
	}
	spin_unlock(&ls->ls_recover_list_lock);

	if (!found)
		de = kzalloc(sizeof(struct dlm_direntry) + len, GFP_KERNEL);
	return de;
}

void dlm_clear_free_entries(struct dlm_ls *ls)
{
	struct dlm_direntry *de;

	spin_lock(&ls->ls_recover_list_lock);
	while (!list_empty(&ls->ls_recover_list)) {
		de = list_entry(ls->ls_recover_list.next, struct dlm_direntry,
				list);
		list_del(&de->list);
		kfree(de);
	}
	spin_unlock(&ls->ls_recover_list_lock);
}

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
	struct list_head *tmp;
	struct dlm_member *memb = NULL;
	uint32_t node, n = 0;
	int nodeid;

	if (ls->ls_num_nodes == 1) {
		nodeid = dlm_our_nodeid();
		goto out;
	}

	if (ls->ls_node_array) {
		node = (hash >> 16) % ls->ls_total_weight;
		nodeid = ls->ls_node_array[node];
		goto out;
	}

	/* make_member_array() failed to kmalloc ls_node_array... */

	node = (hash >> 16) % ls->ls_num_nodes;

	list_for_each(tmp, &ls->ls_nodes) {
		if (n++ != node)
			continue;
		memb = list_entry(tmp, struct dlm_member, list);
		break;
	}

	DLM_ASSERT(memb , printk("num_nodes=%u n=%u node=%u\n",
				 ls->ls_num_nodes, n, node););
	nodeid = memb->nodeid;
 out:
	return nodeid;
}

int dlm_dir_nodeid(struct dlm_rsb *r)
{
	return dlm_hash2nodeid(r->res_ls, r->res_hash);
}

static inline uint32_t dir_hash(struct dlm_ls *ls, char *name, int len)
{
	uint32_t val;

	val = jhash(name, len, 0);
	val &= (ls->ls_dirtbl_size - 1);

	return val;
}

static void add_entry_to_hash(struct dlm_ls *ls, struct dlm_direntry *de)
{
	uint32_t bucket;

	bucket = dir_hash(ls, de->name, de->length);
	list_add_tail(&de->list, &ls->ls_dirtbl[bucket].list);
}

static struct dlm_direntry *search_bucket(struct dlm_ls *ls, char *name,
					  int namelen, uint32_t bucket)
{
	struct dlm_direntry *de;

	list_for_each_entry(de, &ls->ls_dirtbl[bucket].list, list) {
		if (de->length == namelen && !memcmp(name, de->name, namelen))
			goto out;
	}
	de = NULL;
 out:
	return de;
}

void dlm_dir_remove_entry(struct dlm_ls *ls, int nodeid, char *name, int namelen)
{
	struct dlm_direntry *de;
	uint32_t bucket;

	bucket = dir_hash(ls, name, namelen);

	spin_lock(&ls->ls_dirtbl[bucket].lock);

	de = search_bucket(ls, name, namelen, bucket);

	if (!de) {
		log_error(ls, "remove fr %u none", nodeid);
		goto out;
	}

	if (de->master_nodeid != nodeid) {
		log_error(ls, "remove fr %u ID %u", nodeid, de->master_nodeid);
		goto out;
	}

	list_del(&de->list);
	kfree(de);
 out:
	spin_unlock(&ls->ls_dirtbl[bucket].lock);
}

void dlm_dir_clear(struct dlm_ls *ls)
{
	struct list_head *head;
	struct dlm_direntry *de;
	int i;

	DLM_ASSERT(list_empty(&ls->ls_recover_list), );

	for (i = 0; i < ls->ls_dirtbl_size; i++) {
		spin_lock(&ls->ls_dirtbl[i].lock);
		head = &ls->ls_dirtbl[i].list;
		while (!list_empty(head)) {
			de = list_entry(head->next, struct dlm_direntry, list);
			list_del(&de->list);
			put_free_de(ls, de);
		}
		spin_unlock(&ls->ls_dirtbl[i].lock);
	}
}

int dlm_recover_directory(struct dlm_ls *ls)
{
	struct dlm_member *memb;
	struct dlm_direntry *de;
	char *b, *last_name = NULL;
	int error = -ENOMEM, last_len, count = 0;
	uint16_t namelen;

	log_debug(ls, "dlm_recover_directory");

	if (dlm_no_directory(ls))
		goto out_status;

	dlm_dir_clear(ls);

	last_name = kmalloc(DLM_RESNAME_MAXLEN, GFP_KERNEL);
	if (!last_name)
		goto out;

	list_for_each_entry(memb, &ls->ls_nodes, list) {
		memset(last_name, 0, DLM_RESNAME_MAXLEN);
		last_len = 0;

		for (;;) {
			int left;
			error = dlm_recovery_stopped(ls);
			if (error)
				goto out_free;

			error = dlm_rcom_names(ls, memb->nodeid,
					       last_name, last_len);
			if (error)
				goto out_free;

			schedule();

			/*
			 * pick namelen/name pairs out of received buffer
			 */

			b = ls->ls_recover_buf->rc_buf;
			left = ls->ls_recover_buf->rc_header.h_length;
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

				error = -ENOMEM;
				de = get_free_de(ls, namelen);
				if (!de)
					goto out_free;

				de->master_nodeid = memb->nodeid;
				de->length = namelen;
				last_len = namelen;
				memcpy(de->name, b, namelen);
				memcpy(last_name, b, namelen);
				b += namelen;
				left -= namelen;

				add_entry_to_hash(ls, de);
				count++;
			}
		}
         done:
		;
	}

 out_status:
	error = 0;
	dlm_set_recover_status(ls, DLM_RS_DIR);
	log_debug(ls, "dlm_recover_directory %d entries", count);
 out_free:
	kfree(last_name);
 out:
	dlm_clear_free_entries(ls);
	return error;
}

static int get_entry(struct dlm_ls *ls, int nodeid, char *name,
		     int namelen, int *r_nodeid)
{
	struct dlm_direntry *de, *tmp;
	uint32_t bucket;

	bucket = dir_hash(ls, name, namelen);

	spin_lock(&ls->ls_dirtbl[bucket].lock);
	de = search_bucket(ls, name, namelen, bucket);
	if (de) {
		*r_nodeid = de->master_nodeid;
		spin_unlock(&ls->ls_dirtbl[bucket].lock);
		if (*r_nodeid == nodeid)
			return -EEXIST;
		return 0;
	}

	spin_unlock(&ls->ls_dirtbl[bucket].lock);

	if (namelen > DLM_RESNAME_MAXLEN)
		return -EINVAL;

	de = kzalloc(sizeof(struct dlm_direntry) + namelen, GFP_KERNEL);
	if (!de)
		return -ENOMEM;

	de->master_nodeid = nodeid;
	de->length = namelen;
	memcpy(de->name, name, namelen);

	spin_lock(&ls->ls_dirtbl[bucket].lock);
	tmp = search_bucket(ls, name, namelen, bucket);
	if (tmp) {
		kfree(de);
		de = tmp;
	} else {
		list_add_tail(&de->list, &ls->ls_dirtbl[bucket].list);
	}
	*r_nodeid = de->master_nodeid;
	spin_unlock(&ls->ls_dirtbl[bucket].lock);
	return 0;
}

int dlm_dir_lookup(struct dlm_ls *ls, int nodeid, char *name, int namelen,
		   int *r_nodeid)
{
	return get_entry(ls, nodeid, name, namelen, r_nodeid);
}

static struct dlm_rsb *find_rsb_root(struct dlm_ls *ls, char *name, int len)
{
	struct dlm_rsb *r;

	down_read(&ls->ls_root_sem);
	list_for_each_entry(r, &ls->ls_root_list, res_root_list) {
		if (len == r->res_length && !memcmp(name, r->res_name, len)) {
			up_read(&ls->ls_root_sem);
			return r;
		}
	}
	up_read(&ls->ls_root_sem);
	return NULL;
}

/* Find the rsb where we left off (or start again), then send rsb names
   for rsb's we're master of and whose directory node matches the requesting
   node.  inbuf is the rsb name last sent, inlen is the name's length */

void dlm_copy_master_names(struct dlm_ls *ls, char *inbuf, int inlen,
 			   char *outbuf, int outlen, int nodeid)
{
	struct list_head *list;
	struct dlm_rsb *r;
	int offset = 0, dir_nodeid;
	__be16 be_namelen;

	down_read(&ls->ls_root_sem);

	if (inlen > 1) {
		r = find_rsb_root(ls, inbuf, inlen);
		if (!r) {
			inbuf[inlen - 1] = '\0';
			log_error(ls, "copy_master_names from %d start %d %s",
				  nodeid, inlen, inbuf);
			goto out;
		}
		list = r->res_root_list.next;
	} else {
		list = ls->ls_root_list.next;
	}

	for (offset = 0; list != &ls->ls_root_list; list = list->next) {
		r = list_entry(list, struct dlm_rsb, res_root_list);
		if (r->res_nodeid)
			continue;

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
			goto out;
		}

		be_namelen = cpu_to_be16(r->res_length);
		memcpy(outbuf + offset, &be_namelen, sizeof(__be16));
		offset += sizeof(__be16);
		memcpy(outbuf + offset, r->res_name, r->res_length);
		offset += r->res_length;
	}

	/*
	 * If we've reached the end of the list (and there's room) write a
	 * terminating record.
	 */

	if ((list == &ls->ls_root_list) &&
	    (offset + sizeof(uint16_t) <= outlen)) {
		be_namelen = cpu_to_be16(0xFFFF);
		memcpy(outbuf + offset, &be_namelen, sizeof(__be16));
		offset += sizeof(__be16);
	}

 out:
	up_read(&ls->ls_root_sem);
}

