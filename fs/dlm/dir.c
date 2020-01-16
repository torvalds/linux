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
 * We use the upper 16 bits of the hash value to select the directory yesde.
 * Low bits are used for distribution of rsb's among hash buckets on each yesde.
 *
 * To give the exact range wanted (0 to num_yesdes-1), we apply a modulus of
 * num_yesdes to the hash value.  This value in the desired range is used as an
 * offset into the sorted list of yesdeid's to give the particular yesdeid.
 */

int dlm_hash2yesdeid(struct dlm_ls *ls, uint32_t hash)
{
	uint32_t yesde;

	if (ls->ls_num_yesdes == 1)
		return dlm_our_yesdeid();
	else {
		yesde = (hash >> 16) % ls->ls_total_weight;
		return ls->ls_yesde_array[yesde];
	}
}

int dlm_dir_yesdeid(struct dlm_rsb *r)
{
	return r->res_dir_yesdeid;
}

void dlm_recover_dir_yesdeid(struct dlm_ls *ls)
{
	struct dlm_rsb *r;

	down_read(&ls->ls_root_sem);
	list_for_each_entry(r, &ls->ls_root_list, res_root_list) {
		r->res_dir_yesdeid = dlm_hash2yesdeid(ls, r->res_hash);
	}
	up_read(&ls->ls_root_sem);
}

int dlm_recover_directory(struct dlm_ls *ls)
{
	struct dlm_member *memb;
	char *b, *last_name = NULL;
	int error = -ENOMEM, last_len, yesdeid, result;
	uint16_t namelen;
	unsigned int count = 0, count_match = 0, count_bad = 0, count_add = 0;

	log_rinfo(ls, "dlm_recover_directory");

	if (dlm_yes_directory(ls))
		goto out_status;

	last_name = kmalloc(DLM_RESNAME_MAXLEN, GFP_NOFS);
	if (!last_name)
		goto out;

	list_for_each_entry(memb, &ls->ls_yesdes, list) {
		if (memb->yesdeid == dlm_our_yesdeid())
			continue;

		memset(last_name, 0, DLM_RESNAME_MAXLEN);
		last_len = 0;

		for (;;) {
			int left;
			error = dlm_recovery_stopped(ls);
			if (error)
				goto out_free;

			error = dlm_rcom_names(ls, memb->yesdeid,
					       last_name, last_len);
			if (error)
				goto out_free;

			cond_resched();

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
				   this yesde; namelen of 0 marks end of the
				   buffer */

				if (namelen == 0xFFFF)
					goto done;
				if (!namelen)
					break;

				if (namelen > left)
					goto out_free;

				if (namelen > DLM_RESNAME_MAXLEN)
					goto out_free;

				error = dlm_master_lookup(ls, memb->yesdeid,
							  b, namelen,
							  DLM_LU_RECOVER_DIR,
							  &yesdeid, &result);
				if (error) {
					log_error(ls, "recover_dir lookup %d",
						  error);
					goto out_free;
				}

				/* The name was found in rsbtbl, but the
				 * master yesdeid is different from
				 * memb->yesdeid which says it is the master.
				 * This should yest happen. */

				if (result == DLM_LU_MATCH &&
				    yesdeid != memb->yesdeid) {
					count_bad++;
					log_error(ls, "recover_dir lookup %d "
						  "yesdeid %d memb %d bad %u",
						  result, yesdeid, memb->yesdeid,
						  count_bad);
					print_hex_dump_bytes("dlm_recover_dir ",
							     DUMP_PREFIX_NONE,
							     b, namelen);
				}

				/* The name was found in rsbtbl, and the
				 * master yesdeid matches memb->yesdeid. */

				if (result == DLM_LU_MATCH &&
				    yesdeid == memb->yesdeid) {
					count_match++;
				}

				/* The name was yest found in rsbtbl and was
				 * added with memb->yesdeid as the master. */

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

static struct dlm_rsb *find_rsb_root(struct dlm_ls *ls, char *name, int len)
{
	struct dlm_rsb *r;
	uint32_t hash, bucket;
	int rv;

	hash = jhash(name, len, 0);
	bucket = hash & (ls->ls_rsbtbl_size - 1);

	spin_lock(&ls->ls_rsbtbl[bucket].lock);
	rv = dlm_search_rsb_tree(&ls->ls_rsbtbl[bucket].keep, name, len, &r);
	if (rv)
		rv = dlm_search_rsb_tree(&ls->ls_rsbtbl[bucket].toss,
					 name, len, &r);
	spin_unlock(&ls->ls_rsbtbl[bucket].lock);

	if (!rv)
		return r;

	down_read(&ls->ls_root_sem);
	list_for_each_entry(r, &ls->ls_root_list, res_root_list) {
		if (len == r->res_length && !memcmp(name, r->res_name, len)) {
			up_read(&ls->ls_root_sem);
			log_debug(ls, "find_rsb_root revert to root_list %s",
				  r->res_name);
			return r;
		}
	}
	up_read(&ls->ls_root_sem);
	return NULL;
}

/* Find the rsb where we left off (or start again), then send rsb names
   for rsb's we're master of and whose directory yesde matches the requesting
   yesde.  inbuf is the rsb name last sent, inlen is the name's length */

void dlm_copy_master_names(struct dlm_ls *ls, char *inbuf, int inlen,
 			   char *outbuf, int outlen, int yesdeid)
{
	struct list_head *list;
	struct dlm_rsb *r;
	int offset = 0, dir_yesdeid;
	__be16 be_namelen;

	down_read(&ls->ls_root_sem);

	if (inlen > 1) {
		r = find_rsb_root(ls, inbuf, inlen);
		if (!r) {
			inbuf[inlen - 1] = '\0';
			log_error(ls, "copy_master_names from %d start %d %s",
				  yesdeid, inlen, inbuf);
			goto out;
		}
		list = r->res_root_list.next;
	} else {
		list = ls->ls_root_list.next;
	}

	for (offset = 0; list != &ls->ls_root_list; list = list->next) {
		r = list_entry(list, struct dlm_rsb, res_root_list);
		if (r->res_yesdeid)
			continue;

		dir_yesdeid = dlm_dir_yesdeid(r);
		if (dir_yesdeid != yesdeid)
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
			ls->ls_recover_dir_sent_msg++;
			goto out;
		}

		be_namelen = cpu_to_be16(r->res_length);
		memcpy(outbuf + offset, &be_namelen, sizeof(__be16));
		offset += sizeof(__be16);
		memcpy(outbuf + offset, r->res_name, r->res_length);
		offset += r->res_length;
		ls->ls_recover_dir_sent_res++;
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
		ls->ls_recover_dir_sent_msg++;
	}
 out:
	up_read(&ls->ls_root_sem);
}

