/* Kernel module to check if the source address has been seen recently. */
/* Copyright 2002-2003, Stephen Frost, 2.5.x port by laforge@netfilter.org */
/* Author: Stephen Frost <sfrost@snowman.net> */
/* Project Page: http://snowman.net/projects/ipt_recent/ */
/* This software is distributed under the terms of the GPL, Version 2 */
/* This copyright does not cover user programs that use kernel services
 * by normal system calls. */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>
#include <linux/ctype.h>
#include <linux/ip.h>
#include <linux/vmalloc.h>
#include <linux/moduleparam.h>

#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_recent.h>

#undef DEBUG
#define HASH_LOG 9

/* Defaults, these can be overridden on the module command-line. */
static unsigned int ip_list_tot = 100;
static unsigned int ip_pkt_list_tot = 20;
static unsigned int ip_list_hash_size = 0;
static unsigned int ip_list_perms = 0644;
#ifdef DEBUG
static int debug = 1;
#endif

static char version[] =
KERN_INFO RECENT_NAME " " RECENT_VER ": Stephen Frost <sfrost@snowman.net>.  http://snowman.net/projects/ipt_recent/\n";

MODULE_AUTHOR("Stephen Frost <sfrost@snowman.net>");
MODULE_DESCRIPTION("IP tables recently seen matching module " RECENT_VER);
MODULE_LICENSE("GPL");
module_param(ip_list_tot, uint, 0400);
module_param(ip_pkt_list_tot, uint, 0400);
module_param(ip_list_hash_size, uint, 0400);
module_param(ip_list_perms, uint, 0400);
#ifdef DEBUG
module_param(debug, bool, 0600);
MODULE_PARM_DESC(debug,"enable debugging output");
#endif
MODULE_PARM_DESC(ip_list_tot,"number of IPs to remember per list");
MODULE_PARM_DESC(ip_pkt_list_tot,"number of packets per IP to remember");
MODULE_PARM_DESC(ip_list_hash_size,"size of hash table used to look up IPs");
MODULE_PARM_DESC(ip_list_perms,"permissions on /proc/net/ipt_recent/* files");

/* Structure of our list of recently seen addresses. */
struct recent_ip_list {
	u_int32_t addr;
	u_int8_t  ttl;
	unsigned long last_seen;
	unsigned long *last_pkts;
	u_int32_t oldest_pkt;
	u_int32_t hash_entry;
	u_int32_t time_pos;
};

struct time_info_list {
	u_int32_t position;
	u_int32_t time;
};

/* Structure of our linked list of tables of recent lists. */
struct recent_ip_tables {
	char name[IPT_RECENT_NAME_LEN];
	int count;
	int time_pos;
	struct recent_ip_list *table;
	struct recent_ip_tables *next;
	spinlock_t list_lock;
	int *hash_table;
	struct time_info_list *time_info;
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *status_proc;
#endif /* CONFIG_PROC_FS */
};

/* Our current list of addresses we have recently seen.
 * Only added to on a --set, and only updated on --set || --update 
 */
static struct recent_ip_tables *r_tables = NULL;

/* We protect r_list with this spinlock so two processors are not modifying
 * the list at the same time. 
 */
static DEFINE_SPINLOCK(recent_lock);

#ifdef CONFIG_PROC_FS
/* Our /proc/net/ipt_recent entry */
static struct proc_dir_entry *proc_net_ipt_recent = NULL;
#endif

/* Function declaration for later. */
static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const struct xt_match *match,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      int *hotdrop);

/* Function to hash a given address into the hash table of table_size size */
static int hash_func(unsigned int addr, int table_size)
{
	int result = 0;
	unsigned int value = addr;
	do { result ^= value; } while((value >>= HASH_LOG));

#ifdef DEBUG
	if(debug) printk(KERN_INFO RECENT_NAME ": %d = hash_func(%u,%d)\n",
			 result & (table_size - 1),
			 addr,
			 table_size);
#endif

	return(result & (table_size - 1));
}

#ifdef CONFIG_PROC_FS
/* This is the function which produces the output for our /proc output
 * interface which lists each IP address, the last seen time and the 
 * other recent times the address was seen.
 */

static int ip_recent_get_info(char *buffer, char **start, off_t offset, int length, int *eof, void *data)
{
	int len = 0, count, last_len = 0, pkt_count;
	off_t pos = 0;
	off_t begin = 0;
	struct recent_ip_tables *curr_table;

	curr_table = (struct recent_ip_tables*) data;

	spin_lock_bh(&curr_table->list_lock);
	for(count = 0; count < ip_list_tot; count++) {
		if(!curr_table->table[count].addr) continue;
		last_len = len;
		len += sprintf(buffer+len,"src=%u.%u.%u.%u ",NIPQUAD(curr_table->table[count].addr));
		len += sprintf(buffer+len,"ttl: %u ",curr_table->table[count].ttl);
		len += sprintf(buffer+len,"last_seen: %lu ",curr_table->table[count].last_seen);
		len += sprintf(buffer+len,"oldest_pkt: %u ",curr_table->table[count].oldest_pkt);
		len += sprintf(buffer+len,"last_pkts: %lu",curr_table->table[count].last_pkts[0]);
		for(pkt_count = 1; pkt_count < ip_pkt_list_tot; pkt_count++) {
			if(!curr_table->table[count].last_pkts[pkt_count]) break;
			len += sprintf(buffer+len,", %lu",curr_table->table[count].last_pkts[pkt_count]);
		}
		len += sprintf(buffer+len,"\n");
		pos = begin + len;
		if(pos < offset) { len = 0; begin = pos; }
		if(pos > offset + length) { len = last_len; break; }
	}

	*start = buffer + (offset - begin);
	len -= (offset - begin);
	if(len > length) len = length;

	spin_unlock_bh(&curr_table->list_lock);
	return len;
}

/* ip_recent_ctrl provides an interface for users to modify the table
 * directly.  This allows adding entries, removing entries, and
 * flushing the entire table.
 * This is done by opening up the appropriate table for writing and
 * sending one of:
 * xx.xx.xx.xx   -- Add entry to table with current time
 * +xx.xx.xx.xx  -- Add entry to table with current time
 * -xx.xx.xx.xx  -- Remove entry from table
 * clear         -- Flush table, remove all entries
 */

static int ip_recent_ctrl(struct file *file, const char __user *input, unsigned long size, void *data)
{
	static const u_int32_t max[4] = { 0xffffffff, 0xffffff, 0xffff, 0xff };
	u_int32_t val;
	int base, used = 0;
	char c, *cp;
	union iaddr {
		uint8_t bytes[4];
		uint32_t word;
	} res;
	uint8_t *pp = res.bytes;
	int digit;

	char buffer[20];
	int len, check_set = 0, count;
	u_int32_t addr = 0;
	struct sk_buff *skb;
	struct ipt_recent_info *info;
	struct recent_ip_tables *curr_table;

	curr_table = (struct recent_ip_tables*) data;

	if(size > 20) len = 20; else len = size;

	if(copy_from_user(buffer,input,len)) return -EFAULT;

	if(len < 20) buffer[len] = '\0';

#ifdef DEBUG
	if(debug) printk(KERN_INFO RECENT_NAME ": ip_recent_ctrl len: %d, input: `%.20s'\n",len,buffer);
#endif

	cp = buffer;
	while(isspace(*cp)) { cp++; used++; if(used >= len-5) return used; }

	/* Check if we are asked to flush the entire table */
	if(!memcmp(cp,"clear",5)) {
		used += 5;
		spin_lock_bh(&curr_table->list_lock);
		curr_table->time_pos = 0;
		for(count = 0; count < ip_list_hash_size; count++) {
			curr_table->hash_table[count] = -1;
		}
		for(count = 0; count < ip_list_tot; count++) {
			curr_table->table[count].last_seen = 0;
			curr_table->table[count].addr = 0;
			curr_table->table[count].ttl = 0;
			memset(curr_table->table[count].last_pkts,0,ip_pkt_list_tot*sizeof(unsigned long));
			curr_table->table[count].oldest_pkt = 0;
			curr_table->table[count].time_pos = 0;
			curr_table->time_info[count].position = count;
			curr_table->time_info[count].time = 0;
		}
		spin_unlock_bh(&curr_table->list_lock);
		return used;
	}

        check_set = IPT_RECENT_SET;
	switch(*cp) {
		case '+': check_set = IPT_RECENT_SET; cp++; used++; break;
		case '-': check_set = IPT_RECENT_REMOVE; cp++; used++; break;
		default: if(!isdigit(*cp)) return (used+1); break;
	}

#ifdef DEBUG
	if(debug) printk(KERN_INFO RECENT_NAME ": ip_recent_ctrl cp: `%c', check_set: %d\n",*cp,check_set);
#endif
	/* Get addr (effectively inet_aton()) */
	/* Shamelessly stolen from libc, a function in the kernel for doing
	 * this would, of course, be greatly preferred, but our options appear
	 * to be rather limited, so we will just do it ourselves here.
	 */
	res.word = 0;

	c = *cp;
	for(;;) {
		if(!isdigit(c)) return used;
		val = 0; base = 10; digit = 0;
		if(c == '0') {
			c = *++cp;
			if(c == 'x' || c == 'X') base = 16, c = *++cp;
			else { base = 8; digit = 1; }
		}
		for(;;) {
			if(isascii(c) && isdigit(c)) {
				if(base == 8 && (c == '8' || c == '0')) return used;
				val = (val * base) + (c - '0');
				c = *++cp;
				digit = 1;
			} else if(base == 16 && isascii(c) && isxdigit(c)) {
				val = (val << 4) | (c + 10 - (islower(c) ? 'a' : 'A'));
				c = *++cp;
				digit = 1;
			} else break;
		}
		if(c == '.') {
			if(pp > res.bytes + 2 || val > 0xff) return used;
			*pp++ = val;
			c = *++cp;
		} else break;
	}
	used = cp - buffer;
	if(c != '\0' && (!isascii(c) || !isspace(c))) return used;
	if(c == '\n') used++;
	if(!digit) return used;

	if(val > max[pp - res.bytes]) return used;
	addr = res.word | htonl(val);

	if(!addr && check_set == IPT_RECENT_SET) return used;

#ifdef DEBUG
	if(debug) printk(KERN_INFO RECENT_NAME ": ip_recent_ctrl c: %c, addr: %u used: %d\n",c,addr,used);
#endif

	/* Set up and just call match */
	info = kmalloc(sizeof(struct ipt_recent_info),GFP_KERNEL);
	if(!info) { return -ENOMEM; }
	info->seconds = 0;
	info->hit_count = 0;
	info->check_set = check_set;
	info->invert = 0;
	info->side = IPT_RECENT_SOURCE;
	strncpy(info->name,curr_table->name,IPT_RECENT_NAME_LEN);
	info->name[IPT_RECENT_NAME_LEN-1] = '\0';

	skb = kmalloc(sizeof(struct sk_buff),GFP_KERNEL);
	if (!skb) {
		used = -ENOMEM;
		goto out_free_info;
	}
	skb->nh.iph = kmalloc(sizeof(struct iphdr),GFP_KERNEL);
	if (!skb->nh.iph) {
		used = -ENOMEM;
		goto out_free_skb;
	}

	skb->nh.iph->saddr = addr;
	skb->nh.iph->daddr = 0;
	/* Clear ttl since we have no way of knowing it */
	skb->nh.iph->ttl = 0;
	match(skb,NULL,NULL,NULL,info,0,0,NULL);

	kfree(skb->nh.iph);
out_free_skb:
	kfree(skb);
out_free_info:
	kfree(info);

#ifdef DEBUG
	if(debug) printk(KERN_INFO RECENT_NAME ": Leaving ip_recent_ctrl addr: %u used: %d\n",addr,used);
#endif
	return used;
}

#endif /* CONFIG_PROC_FS */

/* 'match' is our primary function, called by the kernel whenever a rule is
 * hit with our module as an option to it.
 * What this function does depends on what was specifically asked of it by
 * the user:
 * --set -- Add or update last seen time of the source address of the packet
 *   -- matchinfo->check_set == IPT_RECENT_SET
 * --rcheck -- Just check if the source address is in the list
 *   -- matchinfo->check_set == IPT_RECENT_CHECK
 * --update -- If the source address is in the list, update last_seen
 *   -- matchinfo->check_set == IPT_RECENT_UPDATE
 * --remove -- If the source address is in the list, remove it
 *   -- matchinfo->check_set == IPT_RECENT_REMOVE
 * --seconds -- Option to --rcheck/--update, only match if last_seen within seconds
 *   -- matchinfo->seconds
 * --hitcount -- Option to --rcheck/--update, only match if seen hitcount times
 *   -- matchinfo->hit_count
 * --seconds and --hitcount can be combined
 */
static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const struct xt_match *match,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      int *hotdrop)
{
	int pkt_count, hits_found, ans;
	unsigned long now;
	const struct ipt_recent_info *info = matchinfo;
	u_int32_t addr = 0, time_temp;
	u_int8_t ttl = skb->nh.iph->ttl;
	int *hash_table;
	int orig_hash_result, hash_result, temp, location = 0, time_loc, end_collision_chain = -1;
	struct time_info_list *time_info;
	struct recent_ip_tables *curr_table;
	struct recent_ip_tables *last_table;
	struct recent_ip_list *r_list;

#ifdef DEBUG
	if(debug) printk(KERN_INFO RECENT_NAME ": match() called\n");
#endif

	/* Default is false ^ info->invert */
	ans = info->invert;

#ifdef DEBUG
	if(debug) printk(KERN_INFO RECENT_NAME ": match(): name = '%s'\n",info->name);
#endif

	/* if out != NULL then routing has been done and TTL changed.
	 * We change it back here internally for match what came in before routing. */
	if(out) ttl++;

	/* Find the right table */
	spin_lock_bh(&recent_lock);
	curr_table = r_tables;
	while( (last_table = curr_table) && strncmp(info->name,curr_table->name,IPT_RECENT_NAME_LEN) && (curr_table = curr_table->next) );

#ifdef DEBUG
	if(debug) printk(KERN_INFO RECENT_NAME ": match(): table found('%s')\n",info->name);
#endif

	spin_unlock_bh(&recent_lock);

	/* Table with this name not found, match impossible */
	if(!curr_table) { return ans; }

	/* Make sure no one is changing the list while we work with it */
	spin_lock_bh(&curr_table->list_lock);

	r_list = curr_table->table;
	if(info->side == IPT_RECENT_DEST) addr = skb->nh.iph->daddr; else addr = skb->nh.iph->saddr;

	if(!addr) { 
#ifdef DEBUG
		if(debug) printk(KERN_INFO RECENT_NAME ": match() address (%u) invalid, leaving.\n",addr);
#endif
		spin_unlock_bh(&curr_table->list_lock);
		return ans;
	}

#ifdef DEBUG
	if(debug) printk(KERN_INFO RECENT_NAME ": match(): checking table, addr: %u, ttl: %u, orig_ttl: %u\n",addr,ttl,skb->nh.iph->ttl);
#endif

	/* Get jiffies now in case they changed while we were waiting for a lock */
	now = jiffies;
	hash_table = curr_table->hash_table;
	time_info = curr_table->time_info;

	orig_hash_result = hash_result = hash_func(addr,ip_list_hash_size);
	/* Hash entry at this result used */
	/* Check for TTL match if requested.  If TTL is zero then a match would never
	 * happen, so match regardless of existing TTL in that case.  Zero means the
	 * entry was added via the /proc interface anyway, so we will just use the
	 * first TTL we get for that IP address. */
	if(info->check_set & IPT_RECENT_TTL) {
		while(hash_table[hash_result] != -1 && !(r_list[hash_table[hash_result]].addr == addr &&
			(!r_list[hash_table[hash_result]].ttl || r_list[hash_table[hash_result]].ttl == ttl))) {
			/* Collision in hash table */
			hash_result = (hash_result + 1) % ip_list_hash_size;
		}
	} else {
		while(hash_table[hash_result] != -1 && r_list[hash_table[hash_result]].addr != addr) {
			/* Collision in hash table */
			hash_result = (hash_result + 1) % ip_list_hash_size;
		}
	}

	if(hash_table[hash_result] == -1 && !(info->check_set & IPT_RECENT_SET)) {
		/* IP not in list and not asked to SET */
		spin_unlock_bh(&curr_table->list_lock);
		return ans;
	}

	/* Check if we need to handle the collision, do not need to on REMOVE */
	if(orig_hash_result != hash_result && !(info->check_set & IPT_RECENT_REMOVE)) {
#ifdef DEBUG
		if(debug) printk(KERN_INFO RECENT_NAME ": match(): Collision in hash table. (or: %d,hr: %d,oa: %u,ha: %u)\n",
				 orig_hash_result,
				 hash_result,
				 r_list[hash_table[orig_hash_result]].addr,
				 addr);
#endif

		/* We had a collision.
		 * orig_hash_result is where we started, hash_result is where we ended up.
		 * So, swap them because we are likely to see the same guy again sooner */
#ifdef DEBUG
		if(debug) {
		  printk(KERN_INFO RECENT_NAME ": match(): Collision; hash_table[orig_hash_result] = %d\n",hash_table[orig_hash_result]);
		  printk(KERN_INFO RECENT_NAME ": match(): Collision; r_list[hash_table[orig_hash_result]].hash_entry = %d\n",
				r_list[hash_table[orig_hash_result]].hash_entry);
		}
#endif

		r_list[hash_table[orig_hash_result]].hash_entry = hash_result;


		temp = hash_table[orig_hash_result];
#ifdef DEBUG
		if(debug) printk(KERN_INFO RECENT_NAME ": match(): Collision; hash_table[hash_result] = %d\n",hash_table[hash_result]);
#endif
		hash_table[orig_hash_result] = hash_table[hash_result];
		hash_table[hash_result] = temp;
		temp = hash_result;
		hash_result = orig_hash_result;
		orig_hash_result = temp;
		time_info[r_list[hash_table[orig_hash_result]].time_pos].position = hash_table[orig_hash_result];
		if(hash_table[hash_result] != -1) {
			r_list[hash_table[hash_result]].hash_entry = hash_result;
			time_info[r_list[hash_table[hash_result]].time_pos].position = hash_table[hash_result];
		}

#ifdef DEBUG
		if(debug) printk(KERN_INFO RECENT_NAME ": match(): Collision handled.\n");
#endif
	}

	if(hash_table[hash_result] == -1) {
#ifdef DEBUG
		if(debug) printk(KERN_INFO RECENT_NAME ": match(): New table entry. (hr: %d,ha: %u)\n",
				 hash_result, addr);
#endif

		/* New item found and IPT_RECENT_SET, so we need to add it */
		location = time_info[curr_table->time_pos].position;
		hash_table[r_list[location].hash_entry] = -1;
		hash_table[hash_result] = location;
		memset(r_list[location].last_pkts,0,ip_pkt_list_tot*sizeof(unsigned long));
		r_list[location].time_pos = curr_table->time_pos;
		r_list[location].addr = addr;
		r_list[location].ttl = ttl;
		r_list[location].last_seen = now;
		r_list[location].oldest_pkt = 1;
		r_list[location].last_pkts[0] = now;
		r_list[location].hash_entry = hash_result;
		time_info[curr_table->time_pos].time = r_list[location].last_seen;
		curr_table->time_pos = (curr_table->time_pos + 1) % ip_list_tot;

		ans = !info->invert;
	} else {
#ifdef DEBUG
		if(debug) printk(KERN_INFO RECENT_NAME ": match(): Existing table entry. (hr: %d,ha: %u)\n",
				 hash_result,
				 addr);
#endif

		/* Existing item found */
		location = hash_table[hash_result];
		/* We have a match on address, now to make sure it meets all requirements for a
		 * full match. */
		if(info->check_set & IPT_RECENT_CHECK || info->check_set & IPT_RECENT_UPDATE) {
			if(!info->seconds && !info->hit_count) ans = !info->invert; else ans = info->invert;
			if(info->seconds && !info->hit_count) {
				if(time_before_eq(now,r_list[location].last_seen+info->seconds*HZ)) ans = !info->invert; else ans = info->invert;
			}
			if(info->seconds && info->hit_count) {
				for(pkt_count = 0, hits_found = 0; pkt_count < ip_pkt_list_tot; pkt_count++) {
					if(r_list[location].last_pkts[pkt_count] == 0) break;
					if(time_before_eq(now,r_list[location].last_pkts[pkt_count]+info->seconds*HZ)) hits_found++;
				}
				if(hits_found >= info->hit_count) ans = !info->invert; else ans = info->invert;
			}
			if(info->hit_count && !info->seconds) {
				for(pkt_count = 0, hits_found = 0; pkt_count < ip_pkt_list_tot; pkt_count++) {
					if(r_list[location].last_pkts[pkt_count] == 0) break;
					hits_found++;
				}
				if(hits_found >= info->hit_count) ans = !info->invert; else ans = info->invert;
			}
		}
#ifdef DEBUG
		if(debug) {
			if(ans)
				printk(KERN_INFO RECENT_NAME ": match(): match addr: %u\n",addr);
			else
				printk(KERN_INFO RECENT_NAME ": match(): no match addr: %u\n",addr);
		}
#endif

		/* If and only if we have been asked to SET, or to UPDATE (on match) do we add the
		 * current timestamp to the last_seen. */
		if((info->check_set & IPT_RECENT_SET && (ans = !info->invert)) || (info->check_set & IPT_RECENT_UPDATE && ans)) {
#ifdef DEBUG
			if(debug) printk(KERN_INFO RECENT_NAME ": match(): SET or UPDATE; updating time info.\n");
#endif
			/* Have to update our time info */
			time_loc = r_list[location].time_pos;
			time_info[time_loc].time = now;
			time_info[time_loc].position = location;
			while((time_info[(time_loc+1) % ip_list_tot].time < time_info[time_loc].time) && ((time_loc+1) % ip_list_tot) != curr_table->time_pos) {
				time_temp = time_info[time_loc].time;
				time_info[time_loc].time = time_info[(time_loc+1)%ip_list_tot].time;
				time_info[(time_loc+1)%ip_list_tot].time = time_temp;
				time_temp = time_info[time_loc].position;
				time_info[time_loc].position = time_info[(time_loc+1)%ip_list_tot].position;
				time_info[(time_loc+1)%ip_list_tot].position = time_temp;
				r_list[time_info[time_loc].position].time_pos = time_loc;
				r_list[time_info[(time_loc+1)%ip_list_tot].position].time_pos = (time_loc+1)%ip_list_tot;
				time_loc = (time_loc+1) % ip_list_tot;
			}
			r_list[location].time_pos = time_loc;
			r_list[location].ttl = ttl;
			r_list[location].last_pkts[r_list[location].oldest_pkt] = now;
			r_list[location].oldest_pkt = ++r_list[location].oldest_pkt % ip_pkt_list_tot;
			r_list[location].last_seen = now;
		}
		/* If we have been asked to remove the entry from the list, just set it to 0 */
		if(info->check_set & IPT_RECENT_REMOVE) {
#ifdef DEBUG
			if(debug) printk(KERN_INFO RECENT_NAME ": match(): REMOVE; clearing entry (or: %d, hr: %d).\n",orig_hash_result,hash_result);
#endif
			/* Check if this is part of a collision chain */
			while(hash_table[(orig_hash_result+1) % ip_list_hash_size] != -1) {
				orig_hash_result++;
				if(hash_func(r_list[hash_table[orig_hash_result]].addr,ip_list_hash_size) == hash_result) {
					/* Found collision chain, how deep does this rabbit hole go? */
#ifdef DEBUG
					if(debug) printk(KERN_INFO RECENT_NAME ": match(): REMOVE; found collision chain.\n");
#endif
					end_collision_chain = orig_hash_result;
				}
			}
			if(end_collision_chain != -1) {
#ifdef DEBUG
				if(debug) printk(KERN_INFO RECENT_NAME ": match(): REMOVE; part of collision chain, moving to end.\n");
#endif
				/* Part of a collision chain, swap it with the end of the chain
				 * before removing. */
				r_list[hash_table[end_collision_chain]].hash_entry = hash_result;
				temp = hash_table[end_collision_chain];
				hash_table[end_collision_chain] = hash_table[hash_result];
				hash_table[hash_result] = temp;
				time_info[r_list[hash_table[hash_result]].time_pos].position = hash_table[hash_result];
				hash_result = end_collision_chain;
				r_list[hash_table[hash_result]].hash_entry = hash_result;
				time_info[r_list[hash_table[hash_result]].time_pos].position = hash_table[hash_result];
			}
			location = hash_table[hash_result];
			hash_table[r_list[location].hash_entry] = -1;
			time_loc = r_list[location].time_pos;
			time_info[time_loc].time = 0;
			time_info[time_loc].position = location;
			while((time_info[(time_loc+1) % ip_list_tot].time < time_info[time_loc].time) && ((time_loc+1) % ip_list_tot) != curr_table->time_pos) {
				time_temp = time_info[time_loc].time;
				time_info[time_loc].time = time_info[(time_loc+1)%ip_list_tot].time;
				time_info[(time_loc+1)%ip_list_tot].time = time_temp;
				time_temp = time_info[time_loc].position;
				time_info[time_loc].position = time_info[(time_loc+1)%ip_list_tot].position;
				time_info[(time_loc+1)%ip_list_tot].position = time_temp;
				r_list[time_info[time_loc].position].time_pos = time_loc;
				r_list[time_info[(time_loc+1)%ip_list_tot].position].time_pos = (time_loc+1)%ip_list_tot;
				time_loc = (time_loc+1) % ip_list_tot;
			}
			r_list[location].time_pos = time_loc;
			r_list[location].last_seen = 0;
			r_list[location].addr = 0;
			r_list[location].ttl = 0;
			memset(r_list[location].last_pkts,0,ip_pkt_list_tot*sizeof(unsigned long));
			r_list[location].oldest_pkt = 0;
			ans = !info->invert;
		}
		spin_unlock_bh(&curr_table->list_lock);
		return ans;
	}

	spin_unlock_bh(&curr_table->list_lock);
#ifdef DEBUG
	if(debug) printk(KERN_INFO RECENT_NAME ": match() left.\n");
#endif
	return ans;
}

/* This function is to verify that the rule given during the userspace iptables
 * command is correct.
 * If the command is valid then we check if the table name referred to by the
 * rule exists, if not it is created.
 */
static int
checkentry(const char *tablename,
           const void *ip,
	   const struct xt_match *match,
           void *matchinfo,
           unsigned int matchsize,
           unsigned int hook_mask)
{
	int flag = 0, c;
	unsigned long *hold;
	const struct ipt_recent_info *info = matchinfo;
	struct recent_ip_tables *curr_table, *find_table, *last_table;

#ifdef DEBUG
	if(debug) printk(KERN_INFO RECENT_NAME ": checkentry() entered.\n");
#endif

	/* seconds and hit_count only valid for CHECK/UPDATE */
	if(info->check_set & IPT_RECENT_SET) { flag++; if(info->seconds || info->hit_count) return 0; }
	if(info->check_set & IPT_RECENT_REMOVE) { flag++; if(info->seconds || info->hit_count) return 0; }
	if(info->check_set & IPT_RECENT_CHECK) flag++;
	if(info->check_set & IPT_RECENT_UPDATE) flag++;

	/* One and only one of these should ever be set */
	if(flag != 1) return 0;

	/* Name must be set to something */
	if(!info->name || !info->name[0]) return 0;

	/* Things look good, create a list for this if it does not exist */
	/* Lock the linked list while we play with it */
	spin_lock_bh(&recent_lock);

	/* Look for an entry with this name already created */
	/* Finds the end of the list and the entry before the end if current name does not exist */
	find_table = r_tables;
	while( (last_table = find_table) && strncmp(info->name,find_table->name,IPT_RECENT_NAME_LEN) && (find_table = find_table->next) );

	/* If a table already exists just increment the count on that table and return */
	if(find_table) { 
#ifdef DEBUG
		if(debug) printk(KERN_INFO RECENT_NAME ": checkentry: table found (%s), incrementing count.\n",info->name);
#endif
		find_table->count++;
		spin_unlock_bh(&recent_lock);
		return 1;
	}

	spin_unlock_bh(&recent_lock);

	/* Table with this name not found */
	/* Allocate memory for new linked list item */

#ifdef DEBUG
	if(debug) {
		printk(KERN_INFO RECENT_NAME ": checkentry: no table found (%s)\n",info->name);
		printk(KERN_INFO RECENT_NAME ": checkentry: Allocationg %d for link-list entry.\n",sizeof(struct recent_ip_tables));
	}
#endif

	curr_table = vmalloc(sizeof(struct recent_ip_tables));
	if(curr_table == NULL) return 0;

	spin_lock_init(&curr_table->list_lock);
	curr_table->next = NULL;
	curr_table->count = 1;
	curr_table->time_pos = 0;
	strncpy(curr_table->name,info->name,IPT_RECENT_NAME_LEN);
	curr_table->name[IPT_RECENT_NAME_LEN-1] = '\0';

	/* Allocate memory for this table and the list of packets in each entry. */
#ifdef DEBUG
	if(debug) printk(KERN_INFO RECENT_NAME ": checkentry: Allocating %d for table (%s).\n",
			sizeof(struct recent_ip_list)*ip_list_tot,
			info->name);
#endif

	curr_table->table = vmalloc(sizeof(struct recent_ip_list)*ip_list_tot);
	if(curr_table->table == NULL) { vfree(curr_table); return 0; }
	memset(curr_table->table,0,sizeof(struct recent_ip_list)*ip_list_tot);
#ifdef DEBUG
	if(debug) printk(KERN_INFO RECENT_NAME ": checkentry: Allocating %d for pkt_list.\n",
			sizeof(unsigned long)*ip_pkt_list_tot*ip_list_tot);
#endif

	hold = vmalloc(sizeof(unsigned long)*ip_pkt_list_tot*ip_list_tot);
#ifdef DEBUG
	if(debug) printk(KERN_INFO RECENT_NAME ": checkentry: After pkt_list allocation.\n");
#endif
	if(hold == NULL) { 
		printk(KERN_INFO RECENT_NAME ": checkentry: unable to allocate for pkt_list.\n");
		vfree(curr_table->table); 
		vfree(curr_table);
		return 0;
	}
	for(c = 0; c < ip_list_tot; c++) {
		curr_table->table[c].last_pkts = hold + c*ip_pkt_list_tot;
	}

	/* Allocate memory for the hash table */
#ifdef DEBUG
	if(debug) printk(KERN_INFO RECENT_NAME ": checkentry: Allocating %d for hash_table.\n",
			sizeof(int)*ip_list_hash_size);
#endif

	curr_table->hash_table = vmalloc(sizeof(int)*ip_list_hash_size);
	if(!curr_table->hash_table) {
		printk(KERN_INFO RECENT_NAME ": checkentry: unable to allocate for hash_table.\n");
		vfree(hold);
		vfree(curr_table->table); 
		vfree(curr_table);
		return 0;
	}

	for(c = 0; c < ip_list_hash_size; c++) {
		curr_table->hash_table[c] = -1;
	}

	/* Allocate memory for the time info */
#ifdef DEBUG
	if(debug) printk(KERN_INFO RECENT_NAME ": checkentry: Allocating %d for time_info.\n",
			sizeof(struct time_info_list)*ip_list_tot);
#endif

	curr_table->time_info = vmalloc(sizeof(struct time_info_list)*ip_list_tot);
	if(!curr_table->time_info) {
		printk(KERN_INFO RECENT_NAME ": checkentry: unable to allocate for time_info.\n");
		vfree(curr_table->hash_table);
		vfree(hold);
		vfree(curr_table->table); 
		vfree(curr_table);
		return 0;
	}
	for(c = 0; c < ip_list_tot; c++) {
		curr_table->time_info[c].position = c;
		curr_table->time_info[c].time = 0;
	}

	/* Put the new table in place */
	spin_lock_bh(&recent_lock);
	find_table = r_tables;
	while( (last_table = find_table) && strncmp(info->name,find_table->name,IPT_RECENT_NAME_LEN) && (find_table = find_table->next) );

	/* If a table already exists just increment the count on that table and return */
	if(find_table) { 
		find_table->count++;	
		spin_unlock_bh(&recent_lock);
#ifdef DEBUG
		if(debug) printk(KERN_INFO RECENT_NAME ": checkentry: table found (%s), created by other process.\n",info->name);
#endif
		vfree(curr_table->time_info);
		vfree(curr_table->hash_table);
		vfree(hold);
		vfree(curr_table->table);
		vfree(curr_table);
		return 1;
	}
	if(!last_table) r_tables = curr_table; else last_table->next = curr_table;

	spin_unlock_bh(&recent_lock);

#ifdef CONFIG_PROC_FS
	/* Create our proc 'status' entry. */
	curr_table->status_proc = create_proc_entry(curr_table->name, ip_list_perms, proc_net_ipt_recent);
	if (!curr_table->status_proc) {
		printk(KERN_INFO RECENT_NAME ": checkentry: unable to allocate for /proc entry.\n");
		/* Destroy the created table */
		spin_lock_bh(&recent_lock);
		last_table = NULL;
		curr_table = r_tables;
		if(!curr_table) {
#ifdef DEBUG
			if(debug) printk(KERN_INFO RECENT_NAME ": checkentry() create_proc failed, no tables.\n");
#endif
			spin_unlock_bh(&recent_lock);
			return 0;
		}
		while( strncmp(info->name,curr_table->name,IPT_RECENT_NAME_LEN) && (last_table = curr_table) && (curr_table = curr_table->next) );
		if(!curr_table) {
#ifdef DEBUG
			if(debug) printk(KERN_INFO RECENT_NAME ": checkentry() create_proc failed, table already destroyed.\n");
#endif
			spin_unlock_bh(&recent_lock);
			return 0;
		}
		if(last_table) last_table->next = curr_table->next; else r_tables = curr_table->next;
		spin_unlock_bh(&recent_lock);
		vfree(curr_table->time_info);
		vfree(curr_table->hash_table);
		vfree(hold);
		vfree(curr_table->table);
		vfree(curr_table);
		return 0;
	}
	
	curr_table->status_proc->owner = THIS_MODULE;
	curr_table->status_proc->data = curr_table;
	wmb();
	curr_table->status_proc->read_proc = ip_recent_get_info;
	curr_table->status_proc->write_proc = ip_recent_ctrl;
#endif /* CONFIG_PROC_FS */

#ifdef DEBUG
	if(debug) printk(KERN_INFO RECENT_NAME ": checkentry() left.\n");
#endif

	return 1;
}

/* This function is called in the event that a rule matching this module is
 * removed.
 * When this happens we need to check if there are no other rules matching
 * the table given.  If that is the case then we remove the table and clean
 * up its memory.
 */
static void
destroy(const struct xt_match *match, void *matchinfo, unsigned int matchsize)
{
	const struct ipt_recent_info *info = matchinfo;
	struct recent_ip_tables *curr_table, *last_table;

#ifdef DEBUG
	if(debug) printk(KERN_INFO RECENT_NAME ": destroy() entered.\n");
#endif

	if(matchsize != IPT_ALIGN(sizeof(struct ipt_recent_info))) return;

	/* Lock the linked list while we play with it */
	spin_lock_bh(&recent_lock);

	/* Look for an entry with this name already created */
	/* Finds the end of the list and the entry before the end if current name does not exist */
	last_table = NULL;
	curr_table = r_tables;
	if(!curr_table) { 
#ifdef DEBUG
		if(debug) printk(KERN_INFO RECENT_NAME ": destroy() No tables found, leaving.\n");
#endif
		spin_unlock_bh(&recent_lock);
		return;
	}
	while( strncmp(info->name,curr_table->name,IPT_RECENT_NAME_LEN) && (last_table = curr_table) && (curr_table = curr_table->next) );

	/* If a table does not exist then do nothing and return */
	if(!curr_table) { 
#ifdef DEBUG
		if(debug) printk(KERN_INFO RECENT_NAME ": destroy() table not found, leaving.\n");
#endif
		spin_unlock_bh(&recent_lock);
		return;
	}

	curr_table->count--;

	/* If count is still non-zero then there are still rules referenceing it so we do nothing */
	if(curr_table->count) { 
#ifdef DEBUG
		if(debug) printk(KERN_INFO RECENT_NAME ": destroy() table found, non-zero count, leaving.\n");
#endif
		spin_unlock_bh(&recent_lock);
		return;
	}

#ifdef DEBUG
	if(debug) printk(KERN_INFO RECENT_NAME ": destroy() table found, zero count, removing.\n");
#endif

	/* Count must be zero so we remove this table from the list */
	if(last_table) last_table->next = curr_table->next; else r_tables = curr_table->next;

	spin_unlock_bh(&recent_lock);

	/* lock to make sure any late-runners still using this after we removed it from
	 * the list finish up then remove everything */
	spin_lock_bh(&curr_table->list_lock);
	spin_unlock_bh(&curr_table->list_lock);

#ifdef CONFIG_PROC_FS
	if(curr_table->status_proc) remove_proc_entry(curr_table->name,proc_net_ipt_recent);
#endif /* CONFIG_PROC_FS */
	vfree(curr_table->table[0].last_pkts);
	vfree(curr_table->table);
	vfree(curr_table->hash_table);
	vfree(curr_table->time_info);
	vfree(curr_table);

#ifdef DEBUG
	if(debug) printk(KERN_INFO RECENT_NAME ": destroy() left.\n");
#endif

	return;
}

/* This is the structure we pass to ipt_register to register our
 * module with iptables.
 */
static struct ipt_match recent_match = {
	.name		= "recent",
	.match		= match,
	.matchsize	= sizeof(struct ipt_recent_info),
	.checkentry	= checkentry,
	.destroy	= destroy,
	.me		= THIS_MODULE
};

/* Kernel module initialization. */
static int __init ipt_recent_init(void)
{
	int err, count;

	printk(version);
#ifdef CONFIG_PROC_FS
	proc_net_ipt_recent = proc_mkdir("ipt_recent",proc_net);
	if(!proc_net_ipt_recent) return -ENOMEM;
#endif

	if(ip_list_hash_size && ip_list_hash_size <= ip_list_tot) {
	  printk(KERN_WARNING RECENT_NAME ": ip_list_hash_size too small, resetting to default.\n");
	  ip_list_hash_size = 0;
	}

	if(!ip_list_hash_size) {
		ip_list_hash_size = ip_list_tot*3;
		count = 2*2;
		while(ip_list_hash_size > count) count = count*2;
		ip_list_hash_size = count;
	}

#ifdef DEBUG
	if(debug) printk(KERN_INFO RECENT_NAME ": ip_list_hash_size: %d\n",ip_list_hash_size);
#endif

	err = ipt_register_match(&recent_match);
	if (err)
		remove_proc_entry("ipt_recent", proc_net);
	return err;
}

/* Kernel module destruction. */
static void __exit ipt_recent_fini(void)
{
	ipt_unregister_match(&recent_match);

	remove_proc_entry("ipt_recent",proc_net);
}

/* Register our module with the kernel. */
module_init(ipt_recent_init);
module_exit(ipt_recent_fini);
