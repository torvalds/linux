#include <linux/types.h>
#include <linux/atmmpc.h>
#include <linux/time.h>

#include "mpoa_caches.h"
#include "mpc.h"

/*
 * mpoa_caches.c: Implementation of ingress and egress cache
 * handling functions
 */

#if 0
#define dprintk printk    /* debug */
#else
#define dprintk(format,args...)
#endif

#if 0
#define ddprintk printk  /* more debug */
#else
#define ddprintk(format,args...)
#endif

static in_cache_entry *in_cache_get(__be32 dst_ip,
				    struct mpoa_client *client)
{
	in_cache_entry *entry;

	read_lock_bh(&client->ingress_lock);
	entry = client->in_cache;
	while(entry != NULL){
		if( entry->ctrl_info.in_dst_ip == dst_ip ){
			atomic_inc(&entry->use);
			read_unlock_bh(&client->ingress_lock);
			return entry;
		}
		entry = entry->next;
	}
	read_unlock_bh(&client->ingress_lock);

	return NULL;
}

static in_cache_entry *in_cache_get_with_mask(__be32 dst_ip,
					      struct mpoa_client *client,
					      __be32 mask)
{
	in_cache_entry *entry;

	read_lock_bh(&client->ingress_lock);
	entry = client->in_cache;
	while(entry != NULL){
		if((entry->ctrl_info.in_dst_ip & mask)  == (dst_ip & mask )){
			atomic_inc(&entry->use);
			read_unlock_bh(&client->ingress_lock);
			return entry;
		}
		entry = entry->next;
	}
	read_unlock_bh(&client->ingress_lock);

	return NULL;

}

static in_cache_entry *in_cache_get_by_vcc(struct atm_vcc *vcc,
					   struct mpoa_client *client )
{
	in_cache_entry *entry;

	read_lock_bh(&client->ingress_lock);
	entry = client->in_cache;
	while(entry != NULL){
		if(entry->shortcut == vcc) {
			atomic_inc(&entry->use);
			read_unlock_bh(&client->ingress_lock);
			return entry;
		}
		entry = entry->next;
	}
	read_unlock_bh(&client->ingress_lock);

	return NULL;
}

static in_cache_entry *in_cache_add_entry(__be32 dst_ip,
					  struct mpoa_client *client)
{
	in_cache_entry *entry = kzalloc(sizeof(in_cache_entry), GFP_KERNEL);

	if (entry == NULL) {
		printk("mpoa: mpoa_caches.c: new_in_cache_entry: out of memory\n");
		return NULL;
	}

	dprintk("mpoa: mpoa_caches.c: adding an ingress entry, ip = %u.%u.%u.%u\n", NIPQUAD(dst_ip));

	atomic_set(&entry->use, 1);
	dprintk("mpoa: mpoa_caches.c: new_in_cache_entry: about to lock\n");
	write_lock_bh(&client->ingress_lock);
	entry->next = client->in_cache;
	entry->prev = NULL;
	if (client->in_cache != NULL)
		client->in_cache->prev = entry;
	client->in_cache = entry;

	memcpy(entry->MPS_ctrl_ATM_addr, client->mps_ctrl_addr, ATM_ESA_LEN);
	entry->ctrl_info.in_dst_ip = dst_ip;
	do_gettimeofday(&(entry->tv));
	entry->retry_time = client->parameters.mpc_p4;
	entry->count = 1;
	entry->entry_state = INGRESS_INVALID;
	entry->ctrl_info.holding_time = HOLDING_TIME_DEFAULT;
	atomic_inc(&entry->use);

	write_unlock_bh(&client->ingress_lock);
	dprintk("mpoa: mpoa_caches.c: new_in_cache_entry: unlocked\n");

	return entry;
}

static int cache_hit(in_cache_entry *entry, struct mpoa_client *mpc)
{
	struct atm_mpoa_qos *qos;
	struct k_message msg;

	entry->count++;
	if(entry->entry_state == INGRESS_RESOLVED && entry->shortcut != NULL)
		return OPEN;

	if(entry->entry_state == INGRESS_REFRESHING){
		if(entry->count > mpc->parameters.mpc_p1){
			msg.type = SND_MPOA_RES_RQST;
			msg.content.in_info = entry->ctrl_info;
			memcpy(msg.MPS_ctrl, mpc->mps_ctrl_addr, ATM_ESA_LEN);
			qos = atm_mpoa_search_qos(entry->ctrl_info.in_dst_ip);
			if (qos != NULL) msg.qos = qos->qos;
			msg_to_mpoad(&msg, mpc);
			do_gettimeofday(&(entry->reply_wait));
			entry->entry_state = INGRESS_RESOLVING;
		}
		if(entry->shortcut != NULL)
			return OPEN;
		return CLOSED;
	}

	if(entry->entry_state == INGRESS_RESOLVING && entry->shortcut != NULL)
		return OPEN;

	if( entry->count > mpc->parameters.mpc_p1 &&
	    entry->entry_state == INGRESS_INVALID){
		dprintk("mpoa: (%s) mpoa_caches.c: threshold exceeded for ip %u.%u.%u.%u, sending MPOA res req\n", mpc->dev->name, NIPQUAD(entry->ctrl_info.in_dst_ip));
		entry->entry_state = INGRESS_RESOLVING;
		msg.type =  SND_MPOA_RES_RQST;
		memcpy(msg.MPS_ctrl, mpc->mps_ctrl_addr, ATM_ESA_LEN );
		msg.content.in_info = entry->ctrl_info;
		qos = atm_mpoa_search_qos(entry->ctrl_info.in_dst_ip);
		if (qos != NULL) msg.qos = qos->qos;
		msg_to_mpoad( &msg, mpc);
		do_gettimeofday(&(entry->reply_wait));
	}

	return CLOSED;
}

static void in_cache_put(in_cache_entry *entry)
{
	if (atomic_dec_and_test(&entry->use)) {
		memset(entry, 0, sizeof(in_cache_entry));
		kfree(entry);
	}

	return;
}

/*
 * This should be called with write lock on
 */
static void in_cache_remove_entry(in_cache_entry *entry,
				  struct mpoa_client *client)
{
	struct atm_vcc *vcc;
	struct k_message msg;

	vcc = entry->shortcut;
	dprintk("mpoa: mpoa_caches.c: removing an ingress entry, ip = %u.%u.%u.%u\n",NIPQUAD(entry->ctrl_info.in_dst_ip));

	if (entry->prev != NULL)
		entry->prev->next = entry->next;
	else
		client->in_cache = entry->next;
	if (entry->next != NULL)
		entry->next->prev = entry->prev;
	client->in_ops->put(entry);
	if(client->in_cache == NULL && client->eg_cache == NULL){
		msg.type = STOP_KEEP_ALIVE_SM;
		msg_to_mpoad(&msg,client);
	}

	/* Check if the egress side still uses this VCC */
	if (vcc != NULL) {
		eg_cache_entry *eg_entry = client->eg_ops->get_by_vcc(vcc, client);
		if (eg_entry != NULL) {
			client->eg_ops->put(eg_entry);
			return;
		}
		vcc_release_async(vcc, -EPIPE);
	}

	return;
}


/* Call this every MPC-p2 seconds... Not exactly correct solution,
   but an easy one... */
static void clear_count_and_expired(struct mpoa_client *client)
{
	in_cache_entry *entry, *next_entry;
	struct timeval now;

	do_gettimeofday(&now);

	write_lock_bh(&client->ingress_lock);
	entry = client->in_cache;
	while(entry != NULL){
		entry->count=0;
		next_entry = entry->next;
		if((now.tv_sec - entry->tv.tv_sec)
		   > entry->ctrl_info.holding_time){
			dprintk("mpoa: mpoa_caches.c: holding time expired, ip = %u.%u.%u.%u\n", NIPQUAD(entry->ctrl_info.in_dst_ip));
			client->in_ops->remove_entry(entry, client);
		}
		entry = next_entry;
	}
	write_unlock_bh(&client->ingress_lock);

	return;
}

/* Call this every MPC-p4 seconds. */
static void check_resolving_entries(struct mpoa_client *client)
{

	struct atm_mpoa_qos *qos;
	in_cache_entry *entry;
	struct timeval now;
	struct k_message msg;

	do_gettimeofday( &now );

	read_lock_bh(&client->ingress_lock);
	entry = client->in_cache;
	while( entry != NULL ){
		if(entry->entry_state == INGRESS_RESOLVING){
			if(now.tv_sec - entry->hold_down.tv_sec < client->parameters.mpc_p6){
				entry = entry->next;                      /* Entry in hold down */
				continue;
			}
			if( (now.tv_sec - entry->reply_wait.tv_sec) >
			    entry->retry_time ){
				entry->retry_time = MPC_C1*( entry->retry_time );
				if(entry->retry_time > client->parameters.mpc_p5){
					/* Retry time maximum exceeded, put entry in hold down. */
					do_gettimeofday(&(entry->hold_down));
					entry->retry_time = client->parameters.mpc_p4;
					entry = entry->next;
					continue;
				}
				/* Ask daemon to send a resolution request. */
				memset(&(entry->hold_down),0,sizeof(struct timeval));
				msg.type = SND_MPOA_RES_RTRY;
				memcpy(msg.MPS_ctrl, client->mps_ctrl_addr, ATM_ESA_LEN);
				msg.content.in_info = entry->ctrl_info;
				qos = atm_mpoa_search_qos(entry->ctrl_info.in_dst_ip);
				if (qos != NULL) msg.qos = qos->qos;
				msg_to_mpoad(&msg, client);
				do_gettimeofday(&(entry->reply_wait));
			}
		}
		entry = entry->next;
	}
	read_unlock_bh(&client->ingress_lock);
}

/* Call this every MPC-p5 seconds. */
static void refresh_entries(struct mpoa_client *client)
{
	struct timeval now;
	struct in_cache_entry *entry = client->in_cache;

	ddprintk("mpoa: mpoa_caches.c: refresh_entries\n");
	do_gettimeofday(&now);

	read_lock_bh(&client->ingress_lock);
	while( entry != NULL ){
		if( entry->entry_state == INGRESS_RESOLVED ){
			if(!(entry->refresh_time))
				entry->refresh_time = (2*(entry->ctrl_info.holding_time))/3;
			if( (now.tv_sec - entry->reply_wait.tv_sec) > entry->refresh_time ){
				dprintk("mpoa: mpoa_caches.c: refreshing an entry.\n");
				entry->entry_state = INGRESS_REFRESHING;

			}
		}
		entry = entry->next;
	}
	read_unlock_bh(&client->ingress_lock);
}

static void in_destroy_cache(struct mpoa_client *mpc)
{
	write_lock_irq(&mpc->ingress_lock);
	while(mpc->in_cache != NULL)
		mpc->in_ops->remove_entry(mpc->in_cache, mpc);
	write_unlock_irq(&mpc->ingress_lock);

	return;
}

static eg_cache_entry *eg_cache_get_by_cache_id(__be32 cache_id, struct mpoa_client *mpc)
{
	eg_cache_entry *entry;

	read_lock_irq(&mpc->egress_lock);
	entry = mpc->eg_cache;
	while(entry != NULL){
		if(entry->ctrl_info.cache_id == cache_id){
			atomic_inc(&entry->use);
			read_unlock_irq(&mpc->egress_lock);
			return entry;
		}
		entry = entry->next;
	}
	read_unlock_irq(&mpc->egress_lock);

	return NULL;
}

/* This can be called from any context since it saves CPU flags */
static eg_cache_entry *eg_cache_get_by_tag(__be32 tag, struct mpoa_client *mpc)
{
	unsigned long flags;
	eg_cache_entry *entry;

	read_lock_irqsave(&mpc->egress_lock, flags);
	entry = mpc->eg_cache;
	while (entry != NULL){
		if (entry->ctrl_info.tag == tag) {
			atomic_inc(&entry->use);
			read_unlock_irqrestore(&mpc->egress_lock, flags);
			return entry;
		}
		entry = entry->next;
	}
	read_unlock_irqrestore(&mpc->egress_lock, flags);

	return NULL;
}

/* This can be called from any context since it saves CPU flags */
static eg_cache_entry *eg_cache_get_by_vcc(struct atm_vcc *vcc, struct mpoa_client *mpc)
{
	unsigned long flags;
	eg_cache_entry *entry;

	read_lock_irqsave(&mpc->egress_lock, flags);
	entry = mpc->eg_cache;
	while (entry != NULL){
		if (entry->shortcut == vcc) {
			atomic_inc(&entry->use);
			read_unlock_irqrestore(&mpc->egress_lock, flags);
			return entry;
		}
		entry = entry->next;
	}
	read_unlock_irqrestore(&mpc->egress_lock, flags);

	return NULL;
}

static eg_cache_entry *eg_cache_get_by_src_ip(__be32 ipaddr, struct mpoa_client *mpc)
{
	eg_cache_entry *entry;

	read_lock_irq(&mpc->egress_lock);
	entry = mpc->eg_cache;
	while(entry != NULL){
		if(entry->latest_ip_addr == ipaddr) {
			atomic_inc(&entry->use);
			read_unlock_irq(&mpc->egress_lock);
			return entry;
		}
		entry = entry->next;
	}
	read_unlock_irq(&mpc->egress_lock);

	return NULL;
}

static void eg_cache_put(eg_cache_entry *entry)
{
	if (atomic_dec_and_test(&entry->use)) {
		memset(entry, 0, sizeof(eg_cache_entry));
		kfree(entry);
	}

	return;
}

/*
 * This should be called with write lock on
 */
static void eg_cache_remove_entry(eg_cache_entry *entry,
				  struct mpoa_client *client)
{
	struct atm_vcc *vcc;
	struct k_message msg;

	vcc = entry->shortcut;
	dprintk("mpoa: mpoa_caches.c: removing an egress entry.\n");
	if (entry->prev != NULL)
		entry->prev->next = entry->next;
	else
		client->eg_cache = entry->next;
	if (entry->next != NULL)
		entry->next->prev = entry->prev;
	client->eg_ops->put(entry);
	if(client->in_cache == NULL && client->eg_cache == NULL){
		msg.type = STOP_KEEP_ALIVE_SM;
		msg_to_mpoad(&msg,client);
	}

	/* Check if the ingress side still uses this VCC */
	if (vcc != NULL) {
		in_cache_entry *in_entry = client->in_ops->get_by_vcc(vcc, client);
		if (in_entry != NULL) {
			client->in_ops->put(in_entry);
			return;
		}
		vcc_release_async(vcc, -EPIPE);
	}

	return;
}

static eg_cache_entry *eg_cache_add_entry(struct k_message *msg, struct mpoa_client *client)
{
	eg_cache_entry *entry = kzalloc(sizeof(eg_cache_entry), GFP_KERNEL);

	if (entry == NULL) {
		printk("mpoa: mpoa_caches.c: new_eg_cache_entry: out of memory\n");
		return NULL;
	}

	dprintk("mpoa: mpoa_caches.c: adding an egress entry, ip = %u.%u.%u.%u, this should be our IP\n", NIPQUAD(msg->content.eg_info.eg_dst_ip));

	atomic_set(&entry->use, 1);
	dprintk("mpoa: mpoa_caches.c: new_eg_cache_entry: about to lock\n");
	write_lock_irq(&client->egress_lock);
	entry->next = client->eg_cache;
	entry->prev = NULL;
	if (client->eg_cache != NULL)
		client->eg_cache->prev = entry;
	client->eg_cache = entry;

	memcpy(entry->MPS_ctrl_ATM_addr, client->mps_ctrl_addr, ATM_ESA_LEN);
	entry->ctrl_info = msg->content.eg_info;
	do_gettimeofday(&(entry->tv));
	entry->entry_state = EGRESS_RESOLVED;
	dprintk("mpoa: mpoa_caches.c: new_eg_cache_entry cache_id %lu\n", ntohl(entry->ctrl_info.cache_id));
	dprintk("mpoa: mpoa_caches.c: mps_ip = %u.%u.%u.%u\n",
		NIPQUAD(entry->ctrl_info.mps_ip));
	atomic_inc(&entry->use);

	write_unlock_irq(&client->egress_lock);
	dprintk("mpoa: mpoa_caches.c: new_eg_cache_entry: unlocked\n");

	return entry;
}

static void update_eg_cache_entry(eg_cache_entry * entry, uint16_t holding_time)
{
	do_gettimeofday(&(entry->tv));
	entry->entry_state = EGRESS_RESOLVED;
	entry->ctrl_info.holding_time = holding_time;

	return;
}

static void clear_expired(struct mpoa_client *client)
{
	eg_cache_entry *entry, *next_entry;
	struct timeval now;
	struct k_message msg;

	do_gettimeofday(&now);

	write_lock_irq(&client->egress_lock);
	entry = client->eg_cache;
	while(entry != NULL){
		next_entry = entry->next;
		if((now.tv_sec - entry->tv.tv_sec)
		   > entry->ctrl_info.holding_time){
			msg.type = SND_EGRESS_PURGE;
			msg.content.eg_info = entry->ctrl_info;
			dprintk("mpoa: mpoa_caches.c: egress_cache: holding time expired, cache_id = %lu.\n",ntohl(entry->ctrl_info.cache_id));
			msg_to_mpoad(&msg, client);
			client->eg_ops->remove_entry(entry, client);
		}
		entry = next_entry;
	}
	write_unlock_irq(&client->egress_lock);

	return;
}

static void eg_destroy_cache(struct mpoa_client *mpc)
{
	write_lock_irq(&mpc->egress_lock);
	while(mpc->eg_cache != NULL)
		mpc->eg_ops->remove_entry(mpc->eg_cache, mpc);
	write_unlock_irq(&mpc->egress_lock);

	return;
}



static struct in_cache_ops ingress_ops = {
	in_cache_add_entry,               /* add_entry       */
	in_cache_get,                     /* get             */
	in_cache_get_with_mask,           /* get_with_mask   */
	in_cache_get_by_vcc,              /* get_by_vcc      */
	in_cache_put,                     /* put             */
	in_cache_remove_entry,            /* remove_entry    */
	cache_hit,                        /* cache_hit       */
	clear_count_and_expired,          /* clear_count     */
	check_resolving_entries,          /* check_resolving */
	refresh_entries,                  /* refresh         */
	in_destroy_cache                  /* destroy_cache   */
};

static struct eg_cache_ops egress_ops = {
	eg_cache_add_entry,               /* add_entry        */
	eg_cache_get_by_cache_id,         /* get_by_cache_id  */
	eg_cache_get_by_tag,              /* get_by_tag       */
	eg_cache_get_by_vcc,              /* get_by_vcc       */
	eg_cache_get_by_src_ip,           /* get_by_src_ip    */
	eg_cache_put,                     /* put              */
	eg_cache_remove_entry,            /* remove_entry     */
	update_eg_cache_entry,            /* update           */
	clear_expired,                    /* clear_expired    */
	eg_destroy_cache                  /* destroy_cache    */
};


void atm_mpoa_init_cache(struct mpoa_client *mpc)
{
	mpc->in_ops = &ingress_ops;
	mpc->eg_ops = &egress_ops;

	return;
}
