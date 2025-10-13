// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   ALSA sequencer Ports
 *   Copyright (c) 1998 by Frank van de Pol <fvdpol@coil.demon.nl>
 *                         Jaroslav Kysela <perex@perex.cz>
 */

#include <sound/core.h>
#include <linux/slab.h>
#include <linux/module.h>
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
	struct snd_seq_client_port *port;

	if (client == NULL)
		return NULL;
	guard(read_lock)(&client->ports_lock);
	list_for_each_entry(port, &client->ports_list_head, list) {
		if (port->addr.port == num) {
			if (port->closing)
				break; /* deleting now */
			snd_use_lock_use(&port->use_lock);
			return port;
		}
	}
	return NULL;		/* not found */
}


/* search for the next port - port is locked if found */
struct snd_seq_client_port *snd_seq_port_query_nearest(struct snd_seq_client *client,
						       struct snd_seq_port_info *pinfo)
{
	int num;
	struct snd_seq_client_port *port, *found;
	bool check_inactive = (pinfo->capability & SNDRV_SEQ_PORT_CAP_INACTIVE);

	num = pinfo->addr.port;
	found = NULL;
	guard(read_lock)(&client->ports_lock);
	list_for_each_entry(port, &client->ports_list_head, list) {
		if ((port->capability & SNDRV_SEQ_PORT_CAP_INACTIVE) &&
		    !check_inactive)
			continue; /* skip inactive ports */
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


/* create a port, port number or a negative error code is returned
 * the caller needs to unref the port via snd_seq_port_unlock() appropriately
 */
int snd_seq_create_port(struct snd_seq_client *client, int port,
			struct snd_seq_client_port **port_ret)
{
	struct snd_seq_client_port *new_port, *p;
	int num;
	
	*port_ret = NULL;

	/* sanity check */
	if (snd_BUG_ON(!client))
		return -EINVAL;

	if (client->num_ports >= SNDRV_SEQ_MAX_PORTS) {
		pr_warn("ALSA: seq: too many ports for client %d\n", client->number);
		return -EINVAL;
	}

	/* create a new port */
	new_port = kzalloc(sizeof(*new_port), GFP_KERNEL);
	if (!new_port)
		return -ENOMEM;	/* failure, out of memory */
	/* init port data */
	new_port->addr.client = client->number;
	new_port->addr.port = -1;
	new_port->owner = THIS_MODULE;
	snd_use_lock_init(&new_port->use_lock);
	port_subs_info_init(&new_port->c_src);
	port_subs_info_init(&new_port->c_dest);
	snd_use_lock_use(&new_port->use_lock);

	num = max(port, 0);
	guard(mutex)(&client->ports_mutex);
	guard(write_lock_irq)(&client->ports_lock);
	list_for_each_entry(p, &client->ports_list_head, list) {
		if (p->addr.port == port) {
			kfree(new_port);
			return -EBUSY;
		}
		if (p->addr.port > num)
			break;
		if (port < 0) /* auto-probe mode */
			num = p->addr.port + 1;
	}
	/* insert the new port */
	list_add_tail(&new_port->list, &p->list);
	client->num_ports++;
	new_port->addr.port = num;	/* store the port number in the port */
	sprintf(new_port->name, "port-%d", num);
	*port_ret = new_port;

	return num;
}

/* */
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
	*cp = snd_seq_client_use_ptr(addr->client);
	if (!*cp)
		return NULL;
	return snd_seq_port_use_ptr(*cp, addr->port);
}

static void delete_and_unsubscribe_port(struct snd_seq_client *client,
					struct snd_seq_client_port *port,
					struct snd_seq_subscribers *subs,
					bool is_src, bool ack);

static inline struct snd_seq_subscribers *
get_subscriber(struct list_head *p, bool is_src)
{
	if (is_src)
		return list_entry(p, struct snd_seq_subscribers, src_list);
	else
		return list_entry(p, struct snd_seq_subscribers, dest_list);
}

/*
 * remove all subscribers on the list
 * this is called from port_delete, for each src and dest list.
 */
static void clear_subscriber_list(struct snd_seq_client *client,
				  struct snd_seq_client_port *port,
				  struct snd_seq_port_subs_info *grp,
				  int is_src)
{
	struct list_head *p, *n;

	list_for_each_safe(p, n, &grp->list_head) {
		struct snd_seq_subscribers *subs;
		struct snd_seq_client *c __free(snd_seq_client) = NULL;
		struct snd_seq_client_port *aport __free(snd_seq_port) = NULL;

		subs = get_subscriber(p, is_src);
		if (is_src)
			aport = get_client_port(&subs->info.dest, &c);
		else
			aport = get_client_port(&subs->info.sender, &c);
		delete_and_unsubscribe_port(client, port, subs, is_src, false);

		if (!aport) {
			/* looks like the connected port is being deleted.
			 * we decrease the counter, and when both ports are deleted
			 * remove the subscriber info
			 */
			if (atomic_dec_and_test(&subs->ref_count))
				kfree(subs);
			continue;
		}

		/* ok we got the connected port */
		delete_and_unsubscribe_port(c, aport, subs, !is_src, true);
		kfree(subs);
	}
}

/* delete port data */
static int port_delete(struct snd_seq_client *client,
		       struct snd_seq_client_port *port)
{
	/* set closing flag and wait for all port access are gone */
	port->closing = 1;
	snd_use_lock_sync(&port->use_lock); 

	/* clear subscribers info */
	clear_subscriber_list(client, port, &port->c_src, true);
	clear_subscriber_list(client, port, &port->c_dest, false);

	if (port->private_free)
		port->private_free(port->private_data);

	snd_BUG_ON(port->c_src.count != 0);
	snd_BUG_ON(port->c_dest.count != 0);

	kfree(port);
	return 0;
}


/* delete a port with the given port id */
int snd_seq_delete_port(struct snd_seq_client *client, int port)
{
	struct snd_seq_client_port *found = NULL, *p;

	scoped_guard(mutex, &client->ports_mutex) {
		guard(write_lock_irq)(&client->ports_lock);
		list_for_each_entry(p, &client->ports_list_head, list) {
			if (p->addr.port == port) {
				/* ok found.  delete from the list at first */
				list_del(&p->list);
				client->num_ports--;
				found = p;
				break;
			}
		}
	}
	if (found)
		return port_delete(client, found);
	else
		return -ENOENT;
}

/* delete the all ports belonging to the given client */
int snd_seq_delete_all_ports(struct snd_seq_client *client)
{
	struct list_head deleted_list;
	struct snd_seq_client_port *port, *tmp;
	
	/* move the port list to deleted_list, and
	 * clear the port list in the client data.
	 */
	guard(mutex)(&client->ports_mutex);
	scoped_guard(write_lock_irq, &client->ports_lock) {
		if (!list_empty(&client->ports_list_head)) {
			list_add(&deleted_list, &client->ports_list_head);
			list_del_init(&client->ports_list_head);
		} else {
			INIT_LIST_HEAD(&deleted_list);
		}
		client->num_ports = 0;
	}

	/* remove each port in deleted_list */
	list_for_each_entry_safe(port, tmp, &deleted_list, list) {
		list_del(&port->list);
		snd_seq_system_client_ev_port_exit(port->addr.client, port->addr.port);
		port_delete(client, port);
	}
	return 0;
}

/* set port info fields */
int snd_seq_set_port_info(struct snd_seq_client_port * port,
			  struct snd_seq_port_info * info)
{
	if (snd_BUG_ON(!port || !info))
		return -EINVAL;

	/* set port name */
	if (info->name[0])
		strscpy(port->name, info->name, sizeof(port->name));
	
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

	/* UMP direction and group */
	port->direction = info->direction;
	port->ump_group = info->ump_group;
	if (port->ump_group > SNDRV_UMP_MAX_GROUPS)
		port->ump_group = 0;

	/* fill default port direction */
	if (!port->direction) {
		if (info->capability & SNDRV_SEQ_PORT_CAP_READ)
			port->direction |= SNDRV_SEQ_PORT_DIR_INPUT;
		if (info->capability & SNDRV_SEQ_PORT_CAP_WRITE)
			port->direction |= SNDRV_SEQ_PORT_DIR_OUTPUT;
	}

	port->is_midi1 = !!(info->flags & SNDRV_SEQ_PORT_FLG_IS_MIDI1);

	return 0;
}

/* get port info fields */
int snd_seq_get_port_info(struct snd_seq_client_port * port,
			  struct snd_seq_port_info * info)
{
	if (snd_BUG_ON(!port || !info))
		return -EINVAL;

	/* get port name */
	strscpy(info->name, port->name, sizeof(info->name));
	
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

	if (port->is_midi1)
		info->flags |= SNDRV_SEQ_PORT_FLG_IS_MIDI1;

	/* UMP direction and group */
	info->direction = port->direction;
	info->ump_group = port->ump_group;

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
	if (grp->open && grp->count == 1) {
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
	if (grp->close && grp->count == 0)
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

static int check_and_subscribe_port(struct snd_seq_client *client,
				    struct snd_seq_client_port *port,
				    struct snd_seq_subscribers *subs,
				    bool is_src, bool exclusive, bool ack)
{
	struct snd_seq_port_subs_info *grp;
	struct list_head *p;
	struct snd_seq_subscribers *s;
	int err;

	grp = is_src ? &port->c_src : &port->c_dest;
	guard(rwsem_write)(&grp->list_mutex);
	if (exclusive) {
		if (!list_empty(&grp->list_head))
			return -EBUSY;
	} else {
		if (grp->exclusive)
			return -EBUSY;
		/* check whether already exists */
		list_for_each(p, &grp->list_head) {
			s = get_subscriber(p, is_src);
			if (match_subs_info(&subs->info, &s->info))
				return -EBUSY;
		}
	}

	err = subscribe_port(client, port, grp, &subs->info, ack);
	if (err < 0) {
		grp->exclusive = 0;
		return err;
	}

	/* add to list */
	guard(write_lock_irq)(&grp->list_lock);
	if (is_src)
		list_add_tail(&subs->src_list, &grp->list_head);
	else
		list_add_tail(&subs->dest_list, &grp->list_head);
	grp->exclusive = exclusive;
	atomic_inc(&subs->ref_count);

	return 0;
}

/* called with grp->list_mutex held */
static void __delete_and_unsubscribe_port(struct snd_seq_client *client,
					  struct snd_seq_client_port *port,
					  struct snd_seq_subscribers *subs,
					  bool is_src, bool ack)
{
	struct snd_seq_port_subs_info *grp;
	struct list_head *list;
	bool empty;

	grp = is_src ? &port->c_src : &port->c_dest;
	list = is_src ? &subs->src_list : &subs->dest_list;
	scoped_guard(write_lock_irq, &grp->list_lock) {
		empty = list_empty(list);
		if (!empty)
			list_del_init(list);
		grp->exclusive = 0;
	}

	if (!empty)
		unsubscribe_port(client, port, grp, &subs->info, ack);
}

static void delete_and_unsubscribe_port(struct snd_seq_client *client,
					struct snd_seq_client_port *port,
					struct snd_seq_subscribers *subs,
					bool is_src, bool ack)
{
	struct snd_seq_port_subs_info *grp;

	grp = is_src ? &port->c_src : &port->c_dest;
	guard(rwsem_write)(&grp->list_mutex);
	__delete_and_unsubscribe_port(client, port, subs, is_src, ack);
}

/* connect two ports */
int snd_seq_port_connect(struct snd_seq_client *connector,
			 struct snd_seq_client *src_client,
			 struct snd_seq_client_port *src_port,
			 struct snd_seq_client *dest_client,
			 struct snd_seq_client_port *dest_port,
			 struct snd_seq_port_subscribe *info)
{
	struct snd_seq_subscribers *subs;
	bool exclusive;
	int err;

	subs = kzalloc(sizeof(*subs), GFP_KERNEL);
	if (!subs)
		return -ENOMEM;

	subs->info = *info;
	atomic_set(&subs->ref_count, 0);
	INIT_LIST_HEAD(&subs->src_list);
	INIT_LIST_HEAD(&subs->dest_list);

	exclusive = !!(info->flags & SNDRV_SEQ_PORT_SUBS_EXCLUSIVE);

	err = check_and_subscribe_port(src_client, src_port, subs, true,
				       exclusive,
				       connector->number != src_client->number);
	if (err < 0)
		goto error;
	err = check_and_subscribe_port(dest_client, dest_port, subs, false,
				       exclusive,
				       connector->number != dest_client->number);
	if (err < 0)
		goto error_dest;

	return 0;

 error_dest:
	delete_and_unsubscribe_port(src_client, src_port, subs, true,
				    connector->number != src_client->number);
 error:
	kfree(subs);
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
	struct snd_seq_port_subs_info *dest = &dest_port->c_dest;
	struct snd_seq_subscribers *subs;
	int err = -ENOENT;

	/* always start from deleting the dest port for avoiding concurrent
	 * deletions
	 */
	scoped_guard(rwsem_write, &dest->list_mutex) {
		/* look for the connection */
		list_for_each_entry(subs, &dest->list_head, dest_list) {
			if (match_subs_info(info, &subs->info)) {
				__delete_and_unsubscribe_port(dest_client, dest_port,
							      subs, false,
							      connector->number != dest_client->number);
				err = 0;
				break;
			}
		}
	}
	if (err < 0)
		return err;

	delete_and_unsubscribe_port(src_client, src_port, subs, true,
				    connector->number != src_client->number);
	kfree(subs);
	return 0;
}


/* get matched subscriber */
int snd_seq_port_get_subscription(struct snd_seq_port_subs_info *src_grp,
				  struct snd_seq_addr *dest_addr,
				  struct snd_seq_port_subscribe *subs)
{
	struct snd_seq_subscribers *s;
	int err = -ENOENT;

	guard(rwsem_read)(&src_grp->list_mutex);
	list_for_each_entry(s, &src_grp->list_head, src_list) {
		if (addr_match(dest_addr, &s->info.dest)) {
			*subs = s->info;
			err = 0;
			break;
		}
	}
	return err;
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
	strscpy(portinfo.name, portname ? portname : "Unnamed port",
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
