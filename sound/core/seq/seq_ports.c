/*
 *   ALSA sequencer Ports
 *   Copyright (c) 1998 by Frank van de Pol <fvdpol@coil.demon.nl>
 *                         Jaroslav Kysela <perex@suse.cz>
 *
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
#include <sound/core.h>
#include <linux/slab.h>
#include "seq_system.h"
#include "seq_ports.h"
#include "seq_clientmgr.h"

/*

   registration of client ports

 */


/* 

NOTE: the current implementation of the port structure as a linked list is
not optimal for clients that have many ports. For sending messages to all
subscribers of a port we first need to find the address of the port
structure, which means we have to traverse the list. A direct access table
(array) would be better, but big preallocated arrays waste memory.

Possible actions:

1) leave it this way, a client does normaly does not have more than a few
ports

2) replace the linked list of ports by a array of pointers which is
dynamicly kmalloced. When a port is added or deleted we can simply allocate
a new array, copy the corresponding pointers, and delete the old one. We
then only need a pointer to this array, and an integer that tells us how
much elements are in array.

*/

/* return pointer to port structure - port is locked if found */
struct snd_seq_client_port *snd_seq_port_use_ptr(struct snd_seq_client *client,
						 int num)
{
	struct list_head *p;
	struct snd_seq_client_port *port;

	if (client == NULL)
		return NULL;
	read_lock(&client->ports_lock);
	list_for_each(p, &client->ports_list_head) {
		port = list_entry(p, struct snd_seq_client_port, list);
		if (port->addr.port == num) {
			if (port->closing)
				break; /* deleting now */
			snd_use_lock_use(&port->use_lock);
			read_unlock(&client->ports_lock);
			return port;
		}
	}
	read_unlock(&client->ports_lock);
	return NULL;		/* not found */
}


/* search for the next port - port is locked if found */
struct snd_seq_client_port *snd_seq_port_query_nearest(struct snd_seq_client *client,
						       struct snd_seq_port_info *pinfo)
{
	int num;
	struct list_head *p;
	struct snd_seq_client_port *port, *found;

	num = pinfo->addr.port;
	found = NULL;
	read_lock(&client->ports_lock);
	list_for_each(p, &client->ports_list_head) {
		port = list_entry(p, struct snd_seq_client_port, list);
		if (port->addr.port < num)
			continue;
		if (port->addr.port == num) {
			found = port;
			break;
		}
		if (found == NULL || port->addr.port < found->addr.port)
			found = port;
	}
	if (found) {
		if (found->closing)
			found = NULL;
		else
			snd_use_lock_use(&found->use_lock);
	}
	read_unlock(&client->ports_lock);
	return found;
}


/* initialize snd_seq_port_subs_info */
static void port_subs_info_init(struct snd_seq_port_subs_info *grp)
{
	INIT_LIST_HEAD(&grp->list_head);
	grp->count = 0;
	grp->exclusive = 0;
	rwlock_init(&grp->list_lock);
	init_rwsem(&grp->list_mutex);
	grp->open = NULL;
	grp->close = NULL;
}


/* create a port, port number is returned (-1 on failure) */
struct snd_seq_client_port *snd_seq_create_port(struct snd_seq_client *client,
						int port)
{
	unsigned long flags;
	struct snd_seq_client_port *new_port;
	struct list_head *l;
	int num = -1;
	
	/* sanity check */
	snd_assert(client, return NULL);

	if (client->num_ports >= SNDRV_SEQ_MAX_PORTS - 1) {
		snd_printk(KERN_WARNING "too many ports for client %d\n", client->number);
		return NULL;
	}

	/* create a new port */
	new_port = kzalloc(sizeof(*new_port), GFP_KERNEL);
	if (! new_port) {
		snd_printd("malloc failed for registering client port\n");
		return NULL;	/* failure, out of memory */
	}
	/* init port data */
	new_port->addr.client = client->number;
	new_port->addr.port = -1;
	new_port->owner = THIS_MODULE;
	sprintf(new_port->name, "port-%d", num);
	snd_use_lock_init(&new_port->use_lock);
	port_subs_info_init(&new_port->c_src);
	port_subs_info_init(&new_port->c_dest);

	num = port >= 0 ? port : 0;
	mutex_lock(&client->ports_mutex);
	write_lock_irqsave(&client->ports_lock, flags);
	list_for_each(l, &client->ports_list_head) {
		struct snd_seq_client_port *p = list_entry(l, struct snd_seq_client_port, list);
		if (p->addr.port > num)
			break;
		if (port < 0) /* auto-probe mode */
			num = p->addr.port + 1;
	}
	/* insert the new port */
	list_add_tail(&new_port->list, l);
	client->num_ports++;
	new_port->addr.port = num;	/* store the port number in the port */
	write_unlock_irqrestore(&client->ports_lock, flags);
	mutex_unlock(&client->ports_mutex);
	sprintf(new_port->name, "port-%d", num);

	return new_port;
}

/* */
enum group_type {
	SRC_LIST, DEST_LIST
};

static int subscribe_port(struct snd_seq_client *client,
			  struct snd_seq_client_port *port,
			  struct snd_seq_port_subs_info *grp,
			  struct snd_seq_port_subscribe *info, int send_ack);
static int unsubscribe_port(struct snd_seq_client *client,
			    struct snd_seq_client_port *port,
			    struct snd_seq_port_subs_info *grp,
			    struct snd_seq_port_subscribe *info, int send_ack);


static struct snd_seq_client_port *get_client_port(struct snd_seq_addr *addr,
						   struct snd_seq_client **cp)
{
	struct snd_seq_client_port *p;
	*cp = snd_seq_client_use_ptr(addr->client);
	if (*cp) {
		p = snd_seq_port_use_ptr(*cp, addr->port);
		if (! p) {
			snd_seq_client_unlock(*cp);
			*cp = NULL;
		}
		return p;
	}
	return NULL;
}

/*
 * remove all subscribers on the list
 * this is called from port_delete, for each src and dest list.
 */
static void clear_subscriber_list(struct snd_seq_client *client,
				  struct snd_seq_client_port *port,
				  struct snd_seq_port_subs_info *grp,
				  int grptype)
{
	struct list_head *p, *n;

	down_write(&grp->list_mutex);
	list_for_each_safe(p, n, &grp->list_head) {
		struct snd_seq_subscribers *subs;
		struct snd_seq_client *c;
		struct snd_seq_client_port *aport;

		if (grptype == SRC_LIST) {
			subs = list_entry(p, struct snd_seq_subscribers, src_list);
			aport = get_client_port(&subs->info.dest, &c);
		} else {
			subs = list_entry(p, struct snd_seq_subscribers, dest_list);
			aport = get_client_port(&subs->info.sender, &c);
		}
		list_del(p);
		unsubscribe_port(client, port, grp, &subs->info, 0);
		if (!aport) {
			/* looks like the connected port is being deleted.
			 * we decrease the counter, and when both ports are deleted
			 * remove the subscriber info
			 */
			if (atomic_dec_and_test(&subs->ref_count))
				kfree(subs);
		} else {
			/* ok we got the connected port */
			struct snd_seq_port_subs_info *agrp;
			agrp = (grptype == SRC_LIST) ? &aport->c_dest : &aport->c_src;
			down_write(&agrp->list_mutex);
			if (grptype == SRC_LIST)
				list_del(&subs->dest_list);
			else
				list_del(&subs->src_list);
			unsubscribe_port(c, aport, agrp, &subs->info, 1);
			kfree(subs);
			up_write(&agrp->list_mutex);
			snd_seq_port_unlock(aport);
			snd_seq_client_unlock(c);
		}
	}
	up_write(&grp->list_mutex);
}

/* delete port data */
static int port_delete(struct snd_seq_client *client,
		       struct snd_seq_client_port *port)
{
	/* set closing flag and wait for all port access are gone */
	port->closing = 1;
	snd_use_lock_sync(&port->use_lock); 

	/* clear subscribers info */
	clear_subscriber_list(client, port, &port->c_src, SRC_LIST);
	clear_subscriber_list(client, port, &port->c_dest, DEST_LIST);

	if (port->private_free)
		port->private_free(port->private_data);

	snd_assert(port->c_src.count == 0,);
	snd_assert(port->c_dest.count == 0,);

	kfree(port);
	return 0;
}


/* delete a port with the given port id */
int snd_seq_delete_port(struct snd_seq_client *client, int port)
{
	unsigned long flags;
	struct list_head *l;
	struct snd_seq_client_port *found = NULL;

	mutex_lock(&client->ports_mutex);
	write_lock_irqsave(&client->ports_lock, flags);
	list_for_each(l, &client->ports_list_head) {
		struct snd_seq_client_port *p = list_entry(l, struct snd_seq_client_port, list);
		if (p->addr.port == port) {
			/* ok found.  delete from the list at first */
			list_del(l);
			client->num_ports--;
			found = p;
			break;
		}
	}
	write_unlock_irqrestore(&client->ports_lock, flags);
	mutex_unlock(&client->ports_mutex);
	if (found)
		return port_delete(client, found);
	else
		return -ENOENT;
}

/* delete the all ports belonging to the given client */
int snd_seq_delete_all_ports(struct snd_seq_client *client)
{
	unsigned long flags;
	struct list_head deleted_list, *p, *n;
	
	/* move the port list to deleted_list, and
	 * clear the port list in the client data.
	 */
	mutex_lock(&client->ports_mutex);
	write_lock_irqsave(&client->ports_lock, flags);
	if (! list_empty(&client->ports_list_head)) {
		__list_add(&deleted_list,
			   client->ports_list_head.prev,
			   client->ports_list_head.next);
		INIT_LIST_HEAD(&client->ports_list_head);
	} else {
		INIT_LIST_HEAD(&deleted_list);
	}
	client->num_ports = 0;
	write_unlock_irqrestore(&client->ports_lock, flags);

	/* remove each port in deleted_list */
	list_for_each_safe(p, n, &deleted_list) {
		struct snd_seq_client_port *port = list_entry(p, struct snd_seq_client_port, list);
		list_del(p);
		snd_seq_system_client_ev_port_exit(port->addr.client, port->addr.port);
		port_delete(client, port);
	}
	mutex_unlock(&client->ports_mutex);
	return 0;
}

/* set port info fields */
int snd_seq_set_port_info(struct snd_seq_client_port * port,
			  struct snd_seq_port_info * info)
{
	snd_assert(port && info, return -EINVAL);

	/* set port name */
	if (info->name[0])
		strlcpy(port->name, info->name, sizeof(port->name));
	
	/* set capabilities */
	port->capability = info->capability;
	
	/* get port type */
	port->type = info->type;

	/* information about supported channels/voices */
	port->midi_channels = info->midi_channels;
	port->midi_voices = info->midi_voices;
	port->synth_voices = info->synth_voices;

	/* timestamping */
	port->timestamping = (info->flags & SNDRV_SEQ_PORT_FLG_TIMESTAMP) ? 1 : 0;
	port->time_real = (info->flags & SNDRV_SEQ_PORT_FLG_TIME_REAL) ? 1 : 0;
	port->time_queue = info->time_queue;

	return 0;
}

/* get port info fields */
int snd_seq_get_port_info(struct snd_seq_client_port * port,
			  struct snd_seq_port_info * info)
{
	snd_assert(port && info, return -EINVAL);

	/* get port name */
	strlcpy(info->name, port->name, sizeof(info->name));
	
	/* get capabilities */
	info->capability = port->capability;

	/* get port type */
	info->type = port->type;

	/* information about supported channels/voices */
	info->midi_channels = port->midi_channels;
	info->midi_voices = port->midi_voices;
	info->synth_voices = port->synth_voices;

	/* get subscriber counts */
	info->read_use = port->c_src.count;
	info->write_use = port->c_dest.count;
	
	/* timestamping */
	info->flags = 0;
	if (port->timestamping) {
		info->flags |= SNDRV_SEQ_PORT_FLG_TIMESTAMP;
		if (port->time_real)
			info->flags |= SNDRV_SEQ_PORT_FLG_TIME_REAL;
		info->time_queue = port->time_queue;
	}

	return 0;
}



/*
 * call callback functions (if any):
 * the callbacks are invoked only when the first (for connection) or
 * the last subscription (for disconnection) is done.  Second or later
 * subscription results in increment of counter, but no callback is
 * invoked.
 * This feature is useful if these callbacks are associated with
 * initialization or termination of devices (see seq_midi.c).
 *
 * If callback_all option is set, the callback function is invoked
 * at each connnection/disconnection. 
 */

static int subscribe_port(struct snd_seq_client *client,
			  struct snd_seq_client_port *port,
			  struct snd_seq_port_subs_info *grp,
			  struct snd_seq_port_subscribe *info,
			  int send_ack)
{
	int err = 0;

	if (!try_module_get(port->owner))
		return -EFAULT;
	grp->count++;
	if (grp->open && (port->callback_all || grp->count == 1)) {
		err = grp->open(port->private_data, info);
		if (err < 0) {
			module_put(port->owner);
			grp->count--;
		}
	}
	if (err >= 0 && send_ack && client->type == USER_CLIENT)
		snd_seq_client_notify_subscription(port->addr.client, port->addr.port,
						   info, SNDRV_SEQ_EVENT_PORT_SUBSCRIBED);

	return err;
}

static int unsubscribe_port(struct snd_seq_client *client,
			    struct snd_seq_client_port *port,
			    struct snd_seq_port_subs_info *grp,
			    struct snd_seq_port_subscribe *info,
			    int send_ack)
{
	int err = 0;

	if (! grp->count)
		return -EINVAL;
	grp->count--;
	if (grp->close && (port->callback_all || grp->count == 0))
		err = grp->close(port->private_data, info);
	if (send_ack && client->type == USER_CLIENT)
		snd_seq_client_notify_subscription(port->addr.client, port->addr.port,
						   info, SNDRV_SEQ_EVENT_PORT_UNSUBSCRIBED);
	module_put(port->owner);
	return err;
}



/* check if both addresses are identical */
static inline int addr_match(struct snd_seq_addr *r, struct snd_seq_addr *s)
{
	return (r->client == s->client) && (r->port == s->port);
}

/* check the two subscribe info match */
/* if flags is zero, checks only sender and destination addresses */
static int match_subs_info(struct snd_seq_port_subscribe *r,
			   struct snd_seq_port_subscribe *s)
{
	if (addr_match(&r->sender, &s->sender) &&
	    addr_match(&r->dest, &s->dest)) {
		if (r->flags && r->flags == s->flags)
			return r->queue == s->queue;
		else if (! r->flags)
			return 1;
	}
	return 0;
}


/* connect two ports */
int snd_seq_port_connect(struct snd_seq_client *connector,
			 struct snd_seq_client *src_client,
			 struct snd_seq_client_port *src_port,
			 struct snd_seq_client *dest_client,
			 struct snd_seq_client_port *dest_port,
			 struct snd_seq_port_subscribe *info)
{
	struct snd_seq_port_subs_info *src = &src_port->c_src;
	struct snd_seq_port_subs_info *dest = &dest_port->c_dest;
	struct snd_seq_subscribers *subs;
	struct list_head *p;
	int err, src_called = 0;
	unsigned long flags;
	int exclusive;

	subs = kzalloc(sizeof(*subs), GFP_KERNEL);
	if (! subs)
		return -ENOMEM;

	subs->info = *info;
	atomic_set(&subs->ref_count, 2);

	down_write(&src->list_mutex);
	down_write(&dest->list_mutex);

	exclusive = info->flags & SNDRV_SEQ_PORT_SUBS_EXCLUSIVE ? 1 : 0;
	err = -EBUSY;
	if (exclusive) {
		if (! list_empty(&src->list_head) || ! list_empty(&dest->list_head))
			goto __error;
	} else {
		if (src->exclusive || dest->exclusive)
			goto __error;
		/* check whether already exists */
		list_for_each(p, &src->list_head) {
			struct snd_seq_subscribers *s = list_entry(p, struct snd_seq_subscribers, src_list);
			if (match_subs_info(info, &s->info))
				goto __error;
		}
		list_for_each(p, &dest->list_head) {
			struct snd_seq_subscribers *s = list_entry(p, struct snd_seq_subscribers, dest_list);
			if (match_subs_info(info, &s->info))
				goto __error;
		}
	}

	if ((err = subscribe_port(src_client, src_port, src, info,
				  connector->number != src_client->number)) < 0)
		goto __error;
	src_called = 1;

	if ((err = subscribe_port(dest_client, dest_port, dest, info,
				  connector->number != dest_client->number)) < 0)
		goto __error;

	/* add to list */
	write_lock_irqsave(&src->list_lock, flags);
	// write_lock(&dest->list_lock); // no other lock yet
	list_add_tail(&subs->src_list, &src->list_head);
	list_add_tail(&subs->dest_list, &dest->list_head);
	// write_unlock(&dest->list_lock); // no other lock yet
	write_unlock_irqrestore(&src->list_lock, flags);

	src->exclusive = dest->exclusive = exclusive;

	up_write(&dest->list_mutex);
	up_write(&src->list_mutex);
	return 0;

 __error:
	if (src_called)
		unsubscribe_port(src_client, src_port, src, info,
				 connector->number != src_client->number);
	kfree(subs);
	up_write(&dest->list_mutex);
	up_write(&src->list_mutex);
	return err;
}


/* remove the connection */
int snd_seq_port_disconnect(struct snd_seq_client *connector,
			    struct snd_seq_client *src_client,
			    struct snd_seq_client_port *src_port,
			    struct snd_seq_client *dest_client,
			    struct snd_seq_client_port *dest_port,
			    struct snd_seq_port_subscribe *info)
{
	struct snd_seq_port_subs_info *src = &src_port->c_src;
	struct snd_seq_port_subs_info *dest = &dest_port->c_dest;
	struct snd_seq_subscribers *subs;
	struct list_head *p;
	int err = -ENOENT;
	unsigned long flags;

	down_write(&src->list_mutex);
	down_write(&dest->list_mutex);

	/* look for the connection */
	list_for_each(p, &src->list_head) {
		subs = list_entry(p, struct snd_seq_subscribers, src_list);
		if (match_subs_info(info, &subs->info)) {
			write_lock_irqsave(&src->list_lock, flags);
			// write_lock(&dest->list_lock);  // no lock yet
			list_del(&subs->src_list);
			list_del(&subs->dest_list);
			// write_unlock(&dest->list_lock);
			write_unlock_irqrestore(&src->list_lock, flags);
			src->exclusive = dest->exclusive = 0;
			unsubscribe_port(src_client, src_port, src, info,
					 connector->number != src_client->number);
			unsubscribe_port(dest_client, dest_port, dest, info,
					 connector->number != dest_client->number);
			kfree(subs);
			err = 0;
			break;
		}
	}

	up_write(&dest->list_mutex);
	up_write(&src->list_mutex);
	return err;
}


/* get matched subscriber */
struct snd_seq_subscribers *snd_seq_port_get_subscription(struct snd_seq_port_subs_info *src_grp,
							  struct snd_seq_addr *dest_addr)
{
	struct list_head *p;
	struct snd_seq_subscribers *s, *found = NULL;

	down_read(&src_grp->list_mutex);
	list_for_each(p, &src_grp->list_head) {
		s = list_entry(p, struct snd_seq_subscribers, src_list);
		if (addr_match(dest_addr, &s->info.dest)) {
			found = s;
			break;
		}
	}
	up_read(&src_grp->list_mutex);
	return found;
}

/*
 * Attach a device driver that wants to receive events from the
 * sequencer.  Returns the new port number on success.
 * A driver that wants to receive the events converted to midi, will
 * use snd_seq_midisynth_register_port().
 */
/* exported */
int snd_seq_event_port_attach(int client,
			      struct snd_seq_port_callback *pcbp,
			      int cap, int type, int midi_channels,
			      int midi_voices, char *portname)
{
	struct snd_seq_port_info portinfo;
	int  ret;

	/* Set up the port */
	memset(&portinfo, 0, sizeof(portinfo));
	portinfo.addr.client = client;
	strlcpy(portinfo.name, portname ? portname : "Unamed port",
		sizeof(portinfo.name));

	portinfo.capability = cap;
	portinfo.type = type;
	portinfo.kernel = pcbp;
	portinfo.midi_channels = midi_channels;
	portinfo.midi_voices = midi_voices;

	/* Create it */
	ret = snd_seq_kernel_client_ctl(client,
					SNDRV_SEQ_IOCTL_CREATE_PORT,
					&portinfo);

	if (ret >= 0)
		ret = portinfo.addr.port;

	return ret;
}

EXPORT_SYMBOL(snd_seq_event_port_attach);

/*
 * Detach the driver from a port.
 */
/* exported */
int snd_seq_event_port_detach(int client, int port)
{
	struct snd_seq_port_info portinfo;
	int  err;

	memset(&portinfo, 0, sizeof(portinfo));
	portinfo.addr.client = client;
	portinfo.addr.port   = port;
	err = snd_seq_kernel_client_ctl(client,
					SNDRV_SEQ_IOCTL_DELETE_PORT,
					&portinfo);

	return err;
}

EXPORT_SYMBOL(snd_seq_event_port_detach);
