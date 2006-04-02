/*
 *   Generic Instrument routines for ALSA sequencer
 *   Copyright (c) 1999 by Jaroslav Kysela <perex@suse.cz>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
 
#include <sound/driver.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <sound/core.h>
#include "seq_clientmgr.h"
#include <sound/seq_instr.h>
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Advanced Linux Sound Architecture sequencer instrument library.");
MODULE_LICENSE("GPL");


static void snd_instr_lock_ops(struct snd_seq_kinstr_list *list)
{
	if (!(list->flags & SNDRV_SEQ_INSTR_FLG_DIRECT)) {
		spin_lock_irqsave(&list->ops_lock, list->ops_flags);
	} else {
		mutex_lock(&list->ops_mutex);
	}
}

static void snd_instr_unlock_ops(struct snd_seq_kinstr_list *list)
{
	if (!(list->flags & SNDRV_SEQ_INSTR_FLG_DIRECT)) {
		spin_unlock_irqrestore(&list->ops_lock, list->ops_flags);
	} else {
		mutex_unlock(&list->ops_mutex);
	}
}

static struct snd_seq_kinstr *snd_seq_instr_new(int add_len, int atomic)
{
	struct snd_seq_kinstr *instr;
	
	instr = kzalloc(sizeof(struct snd_seq_kinstr) + add_len, atomic ? GFP_ATOMIC : GFP_KERNEL);
	if (instr == NULL)
		return NULL;
	instr->add_len = add_len;
	return instr;
}

static int snd_seq_instr_free(struct snd_seq_kinstr *instr, int atomic)
{
	int result = 0;

	if (instr == NULL)
		return -EINVAL;
	if (instr->ops && instr->ops->remove)
		result = instr->ops->remove(instr->ops->private_data, instr, 1);
	if (!result)
		kfree(instr);
	return result;
}

struct snd_seq_kinstr_list *snd_seq_instr_list_new(void)
{
	struct snd_seq_kinstr_list *list;

	list = kzalloc(sizeof(struct snd_seq_kinstr_list), GFP_KERNEL);
	if (list == NULL)
		return NULL;
	spin_lock_init(&list->lock);
	spin_lock_init(&list->ops_lock);
	mutex_init(&list->ops_mutex);
	list->owner = -1;
	return list;
}

void snd_seq_instr_list_free(struct snd_seq_kinstr_list **list_ptr)
{
	struct snd_seq_kinstr_list *list;
	struct snd_seq_kinstr *instr;
	struct snd_seq_kcluster *cluster;
	int idx;
	unsigned long flags;

	if (list_ptr == NULL)
		return;
	list = *list_ptr;
	*list_ptr = NULL;
	if (list == NULL)
		return;
	
	for (idx = 0; idx < SNDRV_SEQ_INSTR_HASH_SIZE; idx++) {		
		while ((instr = list->hash[idx]) != NULL) {
			list->hash[idx] = instr->next;
			list->count--;
			spin_lock_irqsave(&list->lock, flags);
			while (instr->use) {
				spin_unlock_irqrestore(&list->lock, flags);
				schedule_timeout_interruptible(1);
				spin_lock_irqsave(&list->lock, flags);
			}				
			spin_unlock_irqrestore(&list->lock, flags);
			if (snd_seq_instr_free(instr, 0)<0)
				snd_printk(KERN_WARNING "instrument free problem\n");
		}
		while ((cluster = list->chash[idx]) != NULL) {
			list->chash[idx] = cluster->next;
			list->ccount--;
			kfree(cluster);
		}
	}
	kfree(list);
}

static int instr_free_compare(struct snd_seq_kinstr *instr,
			      struct snd_seq_instr_header *ifree,
			      unsigned int client)
{
	switch (ifree->cmd) {
	case SNDRV_SEQ_INSTR_FREE_CMD_ALL:
		/* all, except private for other clients */
		if ((instr->instr.std & 0xff000000) == 0)
			return 0;
		if (((instr->instr.std >> 24) & 0xff) == client)
			return 0;
		return 1;
	case SNDRV_SEQ_INSTR_FREE_CMD_PRIVATE:
		/* all my private instruments */
		if ((instr->instr.std & 0xff000000) == 0)
			return 1;
		if (((instr->instr.std >> 24) & 0xff) == client)
			return 0;
		return 1;
	case SNDRV_SEQ_INSTR_FREE_CMD_CLUSTER:
		/* all my private instruments */
		if ((instr->instr.std & 0xff000000) == 0) {
			if (instr->instr.cluster == ifree->id.cluster)
				return 0;
			return 1;
		}
		if (((instr->instr.std >> 24) & 0xff) == client) {
			if (instr->instr.cluster == ifree->id.cluster)
				return 0;
		}
		return 1;
	}
	return 1;
}

int snd_seq_instr_list_free_cond(struct snd_seq_kinstr_list *list,
			         struct snd_seq_instr_header *ifree,
			         int client,
			         int atomic)
{
	struct snd_seq_kinstr *instr, *prev, *next, *flist;
	int idx;
	unsigned long flags;

	snd_instr_lock_ops(list);
	for (idx = 0; idx < SNDRV_SEQ_INSTR_HASH_SIZE; idx++) {
		spin_lock_irqsave(&list->lock, flags);
		instr = list->hash[idx];
		prev = flist = NULL;
		while (instr) {
			while (instr && instr_free_compare(instr, ifree, (unsigned int)client)) {
				prev = instr;
				instr = instr->next;
			}
			if (instr == NULL)
				continue;
			if (instr->ops && instr->ops->notify)
				instr->ops->notify(instr->ops->private_data, instr, SNDRV_SEQ_INSTR_NOTIFY_REMOVE);
			next = instr->next;
			if (prev == NULL) {
				list->hash[idx] = next;
			} else {
				prev->next = next;
			}
			list->count--;
			instr->next = flist;
			flist = instr;
			instr = next;
		}
		spin_unlock_irqrestore(&list->lock, flags);
		while (flist) {
			instr = flist;
			flist = instr->next;
			while (instr->use)
				schedule_timeout_interruptible(1);
			if (snd_seq_instr_free(instr, atomic)<0)
				snd_printk(KERN_WARNING "instrument free problem\n");
			instr = next;
		}
	}
	snd_instr_unlock_ops(list);
	return 0;	
}

static int compute_hash_instr_key(struct snd_seq_instr *instr)
{
	int result;
	
	result = instr->bank | (instr->prg << 16);
	result += result >> 24;
	result += result >> 16;
	result += result >> 8;
	return result & (SNDRV_SEQ_INSTR_HASH_SIZE-1);
}

#if 0
static int compute_hash_cluster_key(snd_seq_instr_cluster_t cluster)
{
	int result;
	
	result = cluster;
	result += result >> 24;
	result += result >> 16;
	result += result >> 8;
	return result & (SNDRV_SEQ_INSTR_HASH_SIZE-1);
}
#endif

static int compare_instr(struct snd_seq_instr *i1, struct snd_seq_instr *i2, int exact)
{
	if (exact) {
		if (i1->cluster != i2->cluster ||
		    i1->bank != i2->bank ||
		    i1->prg != i2->prg)
			return 1;
		if ((i1->std & 0xff000000) != (i2->std & 0xff000000))
			return 1;
		if (!(i1->std & i2->std))
			return 1;
		return 0;
	} else {
		unsigned int client_check;
		
		if (i2->cluster && i1->cluster != i2->cluster)
			return 1;
		client_check = i2->std & 0xff000000;
		if (client_check) {
			if ((i1->std & 0xff000000) != client_check)
				return 1;
		} else {
			if ((i1->std & i2->std) != i2->std)
				return 1;
		}
		return i1->bank != i2->bank || i1->prg != i2->prg;
	}
}

struct snd_seq_kinstr *snd_seq_instr_find(struct snd_seq_kinstr_list *list,
					  struct snd_seq_instr *instr,
					  int exact,
					  int follow_alias)
{
	unsigned long flags;
	int depth = 0;
	struct snd_seq_kinstr *result;

	if (list == NULL || instr == NULL)
		return NULL;
	spin_lock_irqsave(&list->lock, flags);
      __again:
	result = list->hash[compute_hash_instr_key(instr)];
	while (result) {
		if (!compare_instr(&result->instr, instr, exact)) {
			if (follow_alias && (result->type == SNDRV_SEQ_INSTR_ATYPE_ALIAS)) {
				instr = (struct snd_seq_instr *)KINSTR_DATA(result);
				if (++depth > 10)
					goto __not_found;
				goto __again;
			}
			result->use++;
			spin_unlock_irqrestore(&list->lock, flags);
			return result;
		}
		result = result->next;
	}
      __not_found:
	spin_unlock_irqrestore(&list->lock, flags);
	return NULL;
}

void snd_seq_instr_free_use(struct snd_seq_kinstr_list *list,
			    struct snd_seq_kinstr *instr)
{
	unsigned long flags;

	if (list == NULL || instr == NULL)
		return;
	spin_lock_irqsave(&list->lock, flags);
	if (instr->use <= 0) {
		snd_printk(KERN_ERR "free_use: fatal!!! use = %i, name = '%s'\n", instr->use, instr->name);
	} else {
		instr->use--;
	}
	spin_unlock_irqrestore(&list->lock, flags);
}

static struct snd_seq_kinstr_ops *instr_ops(struct snd_seq_kinstr_ops *ops,
					    char *instr_type)
{
	while (ops) {
		if (!strcmp(ops->instr_type, instr_type))
			return ops;
		ops = ops->next;
	}
	return NULL;
}

static int instr_result(struct snd_seq_event *ev,
			int type, int result,
			int atomic)
{
	struct snd_seq_event sev;
	
	memset(&sev, 0, sizeof(sev));
	sev.type = SNDRV_SEQ_EVENT_RESULT;
	sev.flags = SNDRV_SEQ_TIME_STAMP_REAL | SNDRV_SEQ_EVENT_LENGTH_FIXED |
	            SNDRV_SEQ_PRIORITY_NORMAL;
	sev.source = ev->dest;
	sev.dest = ev->source;
	sev.data.result.event = type;
	sev.data.result.result = result;
#if 0
	printk("instr result - type = %i, result = %i, queue = %i, source.client:port = %i:%i, dest.client:port = %i:%i\n",
				type, result,
				sev.queue,
				sev.source.client, sev.source.port,
				sev.dest.client, sev.dest.port);
#endif
	return snd_seq_kernel_client_dispatch(sev.source.client, &sev, atomic, 0);
}

static int instr_begin(struct snd_seq_kinstr_ops *ops,
		       struct snd_seq_kinstr_list *list,
		       struct snd_seq_event *ev,
		       int atomic, int hop)
{
	unsigned long flags;

	spin_lock_irqsave(&list->lock, flags);
	if (list->owner >= 0 && list->owner != ev->source.client) {
		spin_unlock_irqrestore(&list->lock, flags);
		return instr_result(ev, SNDRV_SEQ_EVENT_INSTR_BEGIN, -EBUSY, atomic);
	}
	list->owner = ev->source.client;
	spin_unlock_irqrestore(&list->lock, flags);
	return instr_result(ev, SNDRV_SEQ_EVENT_INSTR_BEGIN, 0, atomic);
}

static int instr_end(struct snd_seq_kinstr_ops *ops,
		     struct snd_seq_kinstr_list *list,
		     struct snd_seq_event *ev,
		     int atomic, int hop)
{
	unsigned long flags;

	/* TODO: timeout handling */
	spin_lock_irqsave(&list->lock, flags);
	if (list->owner == ev->source.client) {
		list->owner = -1;
		spin_unlock_irqrestore(&list->lock, flags);
		return instr_result(ev, SNDRV_SEQ_EVENT_INSTR_END, 0, atomic);
	}
	spin_unlock_irqrestore(&list->lock, flags);
	return instr_result(ev, SNDRV_SEQ_EVENT_INSTR_END, -EINVAL, atomic);
}

static int instr_info(struct snd_seq_kinstr_ops *ops,
		      struct snd_seq_kinstr_list *list,
		      struct snd_seq_event *ev,
		      int atomic, int hop)
{
	return -ENXIO;
}

static int instr_format_info(struct snd_seq_kinstr_ops *ops,
			     struct snd_seq_kinstr_list *list,
			     struct snd_seq_event *ev,
			     int atomic, int hop)
{
	return -ENXIO;
}

static int instr_reset(struct snd_seq_kinstr_ops *ops,
		       struct snd_seq_kinstr_list *list,
		       struct snd_seq_event *ev,
		       int atomic, int hop)
{
	return -ENXIO;
}

static int instr_status(struct snd_seq_kinstr_ops *ops,
			struct snd_seq_kinstr_list *list,
			struct snd_seq_event *ev,
			int atomic, int hop)
{
	return -ENXIO;
}

static int instr_put(struct snd_seq_kinstr_ops *ops,
		     struct snd_seq_kinstr_list *list,
		     struct snd_seq_event *ev,
		     int atomic, int hop)
{
	unsigned long flags;
	struct snd_seq_instr_header put;
	struct snd_seq_kinstr *instr;
	int result = -EINVAL, len, key;

	if ((ev->flags & SNDRV_SEQ_EVENT_LENGTH_MASK) != SNDRV_SEQ_EVENT_LENGTH_VARUSR)
		goto __return;

	if (ev->data.ext.len < sizeof(struct snd_seq_instr_header))
		goto __return;
	if (copy_from_user(&put, (void __user *)ev->data.ext.ptr,
			   sizeof(struct snd_seq_instr_header))) {
		result = -EFAULT;
		goto __return;
	}
	snd_instr_lock_ops(list);
	if (put.id.instr.std & 0xff000000) {	/* private instrument */
		put.id.instr.std &= 0x00ffffff;
		put.id.instr.std |= (unsigned int)ev->source.client << 24;
	}
	if ((instr = snd_seq_instr_find(list, &put.id.instr, 1, 0))) {
		snd_seq_instr_free_use(list, instr);
		snd_instr_unlock_ops(list);
		result = -EBUSY;
		goto __return;
	}
	ops = instr_ops(ops, put.data.data.format);
	if (ops == NULL) {
		snd_instr_unlock_ops(list);
		goto __return;
	}
	len = ops->add_len;
	if (put.data.type == SNDRV_SEQ_INSTR_ATYPE_ALIAS)
		len = sizeof(struct snd_seq_instr);
	instr = snd_seq_instr_new(len, atomic);
	if (instr == NULL) {
		snd_instr_unlock_ops(list);
		result = -ENOMEM;
		goto __return;
	}
	instr->ops = ops;
	instr->instr = put.id.instr;
	strlcpy(instr->name, put.data.name, sizeof(instr->name));
	instr->type = put.data.type;
	if (instr->type == SNDRV_SEQ_INSTR_ATYPE_DATA) {
		result = ops->put(ops->private_data,
				  instr,
				  (void __user *)ev->data.ext.ptr + sizeof(struct snd_seq_instr_header),
				  ev->data.ext.len - sizeof(struct snd_seq_instr_header),
				  atomic,
				  put.cmd);
		if (result < 0) {
			snd_seq_instr_free(instr, atomic);
			snd_instr_unlock_ops(list);
			goto __return;
		}
	}
	key = compute_hash_instr_key(&instr->instr);
	spin_lock_irqsave(&list->lock, flags);
	instr->next = list->hash[key];
	list->hash[key] = instr;
	list->count++;
	spin_unlock_irqrestore(&list->lock, flags);
	snd_instr_unlock_ops(list);
	result = 0;
      __return:
	instr_result(ev, SNDRV_SEQ_EVENT_INSTR_PUT, result, atomic);
	return result;
}

static int instr_get(struct snd_seq_kinstr_ops *ops,
		     struct snd_seq_kinstr_list *list,
		     struct snd_seq_event *ev,
		     int atomic, int hop)
{
	return -ENXIO;
}

static int instr_free(struct snd_seq_kinstr_ops *ops,
		      struct snd_seq_kinstr_list *list,
		      struct snd_seq_event *ev,
		      int atomic, int hop)
{
	struct snd_seq_instr_header ifree;
	struct snd_seq_kinstr *instr, *prev;
	int result = -EINVAL;
	unsigned long flags;
	unsigned int hash;

	if ((ev->flags & SNDRV_SEQ_EVENT_LENGTH_MASK) != SNDRV_SEQ_EVENT_LENGTH_VARUSR)
		goto __return;

	if (ev->data.ext.len < sizeof(struct snd_seq_instr_header))
		goto __return;
	if (copy_from_user(&ifree, (void __user *)ev->data.ext.ptr,
			   sizeof(struct snd_seq_instr_header))) {
		result = -EFAULT;
		goto __return;
	}
	if (ifree.cmd == SNDRV_SEQ_INSTR_FREE_CMD_ALL ||
	    ifree.cmd == SNDRV_SEQ_INSTR_FREE_CMD_PRIVATE ||
	    ifree.cmd == SNDRV_SEQ_INSTR_FREE_CMD_CLUSTER) {
	    	result = snd_seq_instr_list_free_cond(list, &ifree, ev->dest.client, atomic);
	    	goto __return;
	}
	if (ifree.cmd == SNDRV_SEQ_INSTR_FREE_CMD_SINGLE) {
		if (ifree.id.instr.std & 0xff000000) {
			ifree.id.instr.std &= 0x00ffffff;
			ifree.id.instr.std |= (unsigned int)ev->source.client << 24;
		}
		hash = compute_hash_instr_key(&ifree.id.instr);
		snd_instr_lock_ops(list);
		spin_lock_irqsave(&list->lock, flags);
		instr = list->hash[hash];
		prev = NULL;
		while (instr) {
			if (!compare_instr(&instr->instr, &ifree.id.instr, 1))
				goto __free_single;
			prev = instr;
			instr = instr->next;
		}
		result = -ENOENT;
		spin_unlock_irqrestore(&list->lock, flags);
		snd_instr_unlock_ops(list);
		goto __return;
		
	      __free_single:
		if (prev) {
			prev->next = instr->next;
		} else {
			list->hash[hash] = instr->next;
		}
		if (instr->ops && instr->ops->notify)
			instr->ops->notify(instr->ops->private_data, instr,
					   SNDRV_SEQ_INSTR_NOTIFY_REMOVE);
		while (instr->use) {
			spin_unlock_irqrestore(&list->lock, flags);
			schedule_timeout_interruptible(1);
			spin_lock_irqsave(&list->lock, flags);
		}				
		spin_unlock_irqrestore(&list->lock, flags);
		result = snd_seq_instr_free(instr, atomic);
		snd_instr_unlock_ops(list);
		goto __return;
	}

      __return:
	instr_result(ev, SNDRV_SEQ_EVENT_INSTR_FREE, result, atomic);
	return result;
}

static int instr_list(struct snd_seq_kinstr_ops *ops,
		      struct snd_seq_kinstr_list *list,
		      struct snd_seq_event *ev,
		      int atomic, int hop)
{
	return -ENXIO;
}

static int instr_cluster(struct snd_seq_kinstr_ops *ops,
			 struct snd_seq_kinstr_list *list,
			 struct snd_seq_event *ev,
			 int atomic, int hop)
{
	return -ENXIO;
}

int snd_seq_instr_event(struct snd_seq_kinstr_ops *ops,
			struct snd_seq_kinstr_list *list,
			struct snd_seq_event *ev,
			int client,
			int atomic,
			int hop)
{
	int direct = 0;

	snd_assert(ops != NULL && list != NULL && ev != NULL, return -EINVAL);
	if (snd_seq_ev_is_direct(ev)) {
		direct = 1;
		switch (ev->type) {
		case SNDRV_SEQ_EVENT_INSTR_BEGIN:
			return instr_begin(ops, list, ev, atomic, hop);
		case SNDRV_SEQ_EVENT_INSTR_END:
			return instr_end(ops, list, ev, atomic, hop);
		}
	}
	if ((list->flags & SNDRV_SEQ_INSTR_FLG_DIRECT) && !direct)
		return -EINVAL;
	switch (ev->type) {
	case SNDRV_SEQ_EVENT_INSTR_INFO:
		return instr_info(ops, list, ev, atomic, hop);
	case SNDRV_SEQ_EVENT_INSTR_FINFO:
		return instr_format_info(ops, list, ev, atomic, hop);
	case SNDRV_SEQ_EVENT_INSTR_RESET:
		return instr_reset(ops, list, ev, atomic, hop);
	case SNDRV_SEQ_EVENT_INSTR_STATUS:
		return instr_status(ops, list, ev, atomic, hop);
	case SNDRV_SEQ_EVENT_INSTR_PUT:
		return instr_put(ops, list, ev, atomic, hop);
	case SNDRV_SEQ_EVENT_INSTR_GET:
		return instr_get(ops, list, ev, atomic, hop);
	case SNDRV_SEQ_EVENT_INSTR_FREE:
		return instr_free(ops, list, ev, atomic, hop);
	case SNDRV_SEQ_EVENT_INSTR_LIST:
		return instr_list(ops, list, ev, atomic, hop);
	case SNDRV_SEQ_EVENT_INSTR_CLUSTER:
		return instr_cluster(ops, list, ev, atomic, hop);
	}
	return -EINVAL;
}
			
/*
 *  Init part
 */

static int __init alsa_seq_instr_init(void)
{
	return 0;
}

static void __exit alsa_seq_instr_exit(void)
{
}

module_init(alsa_seq_instr_init)
module_exit(alsa_seq_instr_exit)

EXPORT_SYMBOL(snd_seq_instr_list_new);
EXPORT_SYMBOL(snd_seq_instr_list_free);
EXPORT_SYMBOL(snd_seq_instr_list_free_cond);
EXPORT_SYMBOL(snd_seq_instr_find);
EXPORT_SYMBOL(snd_seq_instr_free_use);
EXPORT_SYMBOL(snd_seq_instr_event);
