/*
  Kernel module to match application layer (OSI layer 7) data in connections.

  http://l7-filter.sf.net

  (C) 2003-2009 Matthew Strait and Ethan Sommer.

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version
  2 of the License, or (at your option) any later version.
  http://www.gnu.org/licenses/gpl.txt

  Based on ipt_string.c (C) 2000 Emmanuel Roger <winfield@freegates.be>,
  xt_helper.c (C) 2002 Harald Welte and cls_layer7.c (C) 2003 Matthew Strait,
  Ethan Sommer, Justin Levandoski.
*/

#include <linux/spinlock.h>
#include <linux/version.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netfilter.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
#include <net/netfilter/nf_conntrack_extend.h>
#include <net/netfilter/nf_conntrack_acct.h>
#endif
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_layer7.h>
#include <linux/ctype.h>
#include <linux/proc_fs.h>

#include "regexp/regexp.c"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Matthew Strait <quadong@users.sf.net>, Ethan Sommer <sommere@users.sf.net>");
MODULE_DESCRIPTION("iptables application layer match module");
MODULE_ALIAS("ipt_layer7");
MODULE_VERSION("2.21");

static int maxdatalen = 2048; // this is the default
module_param(maxdatalen, int, 0444);
MODULE_PARM_DESC(maxdatalen, "maximum bytes of data looked at by l7-filter");
#ifdef CONFIG_NETFILTER_XT_MATCH_LAYER7_DEBUG
	#define DPRINTK(format,args...) printk(format,##args)
#else
	#define DPRINTK(format,args...)
#endif

/* Number of packets whose data we look at.
This can be modified through /proc/net/layer7_numpackets */
static int num_packets = 10;

static struct pattern_cache {
	char * regex_string;
	regexp * pattern;
	struct pattern_cache * next;
} * first_pattern_cache = NULL;

DEFINE_SPINLOCK(l7_lock);

static int total_acct_packets(struct nf_conn *ct)
{
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 26)
	BUG_ON(ct == NULL);
	return (ct->counters[IP_CT_DIR_ORIGINAL].packets + ct->counters[IP_CT_DIR_REPLY].packets);
#else
	struct nf_conn_counter *acct;

	BUG_ON(ct == NULL);
	acct = nf_conn_acct_find(ct);
	if (!acct)
		return 0;
	return (atomic64_read(&acct[IP_CT_DIR_ORIGINAL].packets) + atomic64_read(&acct[IP_CT_DIR_REPLY].packets));
#endif
}

#ifdef CONFIG_IP_NF_MATCH_LAYER7_DEBUG
/* Converts an unfriendly string into a friendly one by
replacing unprintables with periods and all whitespace with " ". */
static char * friendly_print(unsigned char * s)
{
	char * f = kmalloc(strlen(s) + 1, GFP_ATOMIC);
	int i;

	if(!f) {
		if (net_ratelimit())
			printk(KERN_ERR "layer7: out of memory in "
					"friendly_print, bailing.\n");
		return NULL;
	}

	for(i = 0; i < strlen(s); i++){
		if(isprint(s[i]) && s[i] < 128)	f[i] = s[i];
		else if(isspace(s[i]))		f[i] = ' ';
		else 				f[i] = '.';
	}
	f[i] = '\0';
	return f;
}

static char dec2hex(int i)
{
	switch (i) {
		case 0 ... 9:
			return (i + '0');
			break;
		case 10 ... 15:
			return (i - 10 + 'a');
			break;
		default:
			if (net_ratelimit())
				printk("layer7: Problem in dec2hex\n");
			return '\0';
	}
}

static char * hex_print(unsigned char * s)
{
	char * g = kmalloc(strlen(s)*3 + 1, GFP_ATOMIC);
	int i;

	if(!g) {
	       if (net_ratelimit())
			printk(KERN_ERR "layer7: out of memory in hex_print, "
					"bailing.\n");
	       return NULL;
	}

	for(i = 0; i < strlen(s); i++) {
		g[i*3    ] = dec2hex(s[i]/16);
		g[i*3 + 1] = dec2hex(s[i]%16);
		g[i*3 + 2] = ' ';
	}
	g[i*3] = '\0';

	return g;
}
#endif // DEBUG

/* Use instead of regcomp.  As we expect to be seeing the same regexps over and
over again, it make sense to cache the results. */
static regexp * compile_and_cache(const char * regex_string, 
                                  const char * protocol)
{
	struct pattern_cache * node               = first_pattern_cache;
	struct pattern_cache * last_pattern_cache = first_pattern_cache;
	struct pattern_cache * tmp;
	unsigned int len;

	while (node != NULL) {
		if (!strcmp(node->regex_string, regex_string))
		return node->pattern;

		last_pattern_cache = node;/* points at the last non-NULL node */
		node = node->next;
	}

	/* If we reach the end of the list, then we have not yet cached
	   the pattern for this regex. Let's do that now.
	   Be paranoid about running out of memory to avoid list corruption. */
	tmp = kmalloc(sizeof(struct pattern_cache), GFP_ATOMIC);

	if(!tmp) {
		if (net_ratelimit())
			printk(KERN_ERR "layer7: out of memory in "
					"compile_and_cache, bailing.\n");
		return NULL;
	}

	tmp->regex_string  = kmalloc(strlen(regex_string) + 1, GFP_ATOMIC);
	tmp->pattern       = kmalloc(sizeof(struct regexp),    GFP_ATOMIC);
	tmp->next = NULL;

	if(!tmp->regex_string || !tmp->pattern) {
		if (net_ratelimit())
			printk(KERN_ERR "layer7: out of memory in "
					"compile_and_cache, bailing.\n");
		kfree(tmp->regex_string);
		kfree(tmp->pattern);
		kfree(tmp);
		return NULL;
	}

	/* Ok.  The new node is all ready now. */
	node = tmp;

	if(first_pattern_cache == NULL) /* list is empty */
		first_pattern_cache = node; /* make node the beginning */
	else
		last_pattern_cache->next = node; /* attach node to the end */

	/* copy the string and compile the regex */
	len = strlen(regex_string);
	DPRINTK("About to compile this: \"%s\"\n", regex_string);
	node->pattern = regcomp((char *)regex_string, &len);
	if ( !node->pattern ) {
		if (net_ratelimit())
			printk(KERN_ERR "layer7: Error compiling regexp "
					"\"%s\" (%s)\n", 
					regex_string, protocol);
		/* pattern is now cached as NULL, so we won't try again. */
	}

	strcpy(node->regex_string, regex_string);
	return node->pattern;
}

static int can_handle(const struct sk_buff *skb)
{
	if(!ip_hdr(skb)) /* not IP */
		return 0;
	if(ip_hdr(skb)->protocol != IPPROTO_TCP &&
	   ip_hdr(skb)->protocol != IPPROTO_UDP &&
	   ip_hdr(skb)->protocol != IPPROTO_ICMP)
		return 0;
	return 1;
}

/* Returns offset the into the skb->data that the application data starts */
static int app_data_offset(const struct sk_buff *skb)
{
	/* In case we are ported somewhere (ebtables?) where ip_hdr(skb)
	isn't set, this can be gotten from 4*(skb->data[0] & 0x0f) as well. */
	int ip_hl = 4*ip_hdr(skb)->ihl;

	if( ip_hdr(skb)->protocol == IPPROTO_TCP ) {
		/* 12 == offset into TCP header for the header length field.
		Can't get this with skb->h.th->doff because the tcphdr
		struct doesn't get set when routing (this is confirmed to be
		true in Netfilter as well as QoS.) */
		int tcp_hl = 4*(skb->data[ip_hl + 12] >> 4);

		return ip_hl + tcp_hl;
	} else if( ip_hdr(skb)->protocol == IPPROTO_UDP  ) {
		return ip_hl + 8; /* UDP header is always 8 bytes */
	} else if( ip_hdr(skb)->protocol == IPPROTO_ICMP ) {
		return ip_hl + 8; /* ICMP header is 8 bytes */
	} else {
		if (net_ratelimit())
			printk(KERN_ERR "layer7: tried to handle unknown "
					"protocol!\n");
		return ip_hl + 8; /* something reasonable */
	}
}

/* handles whether there's a match when we aren't appending data anymore */
static int match_no_append(struct nf_conn * conntrack, 
                           struct nf_conn * master_conntrack, 
                           enum ip_conntrack_info ctinfo,
                           enum ip_conntrack_info master_ctinfo,
                           const struct xt_layer7_info * info)
{
	/* If we're in here, throw the app data away */
	if(master_conntrack->layer7.app_data != NULL) {

	#ifdef CONFIG_IP_NF_MATCH_LAYER7_DEBUG
		if(!master_conntrack->layer7.app_proto) {
			char * f = 
			  friendly_print(master_conntrack->layer7.app_data);
			char * g = 
			  hex_print(master_conntrack->layer7.app_data);
			DPRINTK("\nl7-filter gave up after %d bytes "
				"(%d packets):\n%s\n",
				strlen(f), total_acct_packets(master_conntrack), f);
			kfree(f);
			DPRINTK("In hex: %s\n", g);
			kfree(g);
		}
	#endif

		kfree(master_conntrack->layer7.app_data);
		master_conntrack->layer7.app_data = NULL; /* don't free again */
	}

	if(master_conntrack->layer7.app_proto){
		/* Here child connections set their .app_proto (for /proc) */
		if(!conntrack->layer7.app_proto) {
			conntrack->layer7.app_proto = 
			  kmalloc(strlen(master_conntrack->layer7.app_proto)+1, 
			    GFP_ATOMIC);
			if(!conntrack->layer7.app_proto){
				if (net_ratelimit())
					printk(KERN_ERR "layer7: out of memory "
							"in match_no_append, "
							"bailing.\n");
				return 1;
			}
			strcpy(conntrack->layer7.app_proto, 
				master_conntrack->layer7.app_proto);
		}

		return (!strcmp(master_conntrack->layer7.app_proto, 
				info->protocol));
	}
	else {
		/* If not classified, set to "unknown" to distinguish from
		connections that are still being tested. */
		master_conntrack->layer7.app_proto = 
			kmalloc(strlen("unknown")+1, GFP_ATOMIC);
		if(!master_conntrack->layer7.app_proto){
			if (net_ratelimit())
				printk(KERN_ERR "layer7: out of memory in "
						"match_no_append, bailing.\n");
			return 1;
		}
		strcpy(master_conntrack->layer7.app_proto, "unknown");
		return 0;
	}
}

/* add the new app data to the conntrack.  Return number of bytes added. */
static int add_datastr(char *target, int offset, char *app_data, int len)
{
	int length = 0, i;
	if (!target) return 0;

	/* Strip nulls. Make everything lower case (our regex lib doesn't
	do case insensitivity).  Add it to the end of the current data. */
 	for(i = 0; i < maxdatalen-offset-1 && i < len; i++) {
		if(app_data[i] != '\0') {
			/* the kernel version of tolower mungs 'upper ascii' */
			target[length+offset] =
				isascii(app_data[i])? 
					tolower(app_data[i]) : app_data[i];
			length++;
		}
	}
	target[length+offset] = '\0';

	return length;
}

/* add the new app data to the conntrack.  Return number of bytes added. */
static int add_data(struct nf_conn * master_conntrack,
                    char * app_data, int appdatalen)
{
	int length;

	length = add_datastr(master_conntrack->layer7.app_data, master_conntrack->layer7.app_data_len, app_data, appdatalen);
	master_conntrack->layer7.app_data_len += length;

	return length;
}

/* taken from drivers/video/modedb.c */
static int my_atoi(const char *s)
{
	int val = 0;

	for (;; s++) {
		switch (*s) {
			case '0'...'9':
			val = 10*val+(*s-'0');
			break;
		default:
			return val;
		}
	}
}

/* write out num_packets to userland. */
static int layer7_read_proc(char* page, char ** start, off_t off, int count,
                            int* eof, void * data)
{
	if(num_packets > 99 && net_ratelimit())
		printk(KERN_ERR "layer7: NOT REACHED. num_packets too big\n");

	page[0] = num_packets/10 + '0';
	page[1] = num_packets%10 + '0';
	page[2] = '\n';
	page[3] = '\0';

	*eof=1;

	return 3;
}

/* Read in num_packets from userland */
static int layer7_write_proc(struct file* file, const char* buffer,
                             unsigned long count, void *data)
{
	char * foo = kmalloc(count, GFP_ATOMIC);

	if(!foo){
		if (net_ratelimit())
			printk(KERN_ERR "layer7: out of memory, bailing. "
					"num_packets unchanged.\n");
		return count;
	}

	if(copy_from_user(foo, buffer, count)) {
		return -EFAULT;
	}


	num_packets = my_atoi(foo);
	kfree (foo);

	/* This has an arbitrary limit to make the math easier. I'm lazy.
	But anyway, 99 is a LOT! If you want more, you're doing it wrong! */
	if(num_packets > 99) {
		printk(KERN_WARNING "layer7: num_packets can't be > 99.\n");
		num_packets = 99;
	} else if(num_packets < 1) {
		printk(KERN_WARNING "layer7: num_packets can't be < 1.\n");
		num_packets = 1;
	}

	return count;
}

static bool
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
match(const struct sk_buff *skbin, struct xt_action_param *par)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28)
match(const struct sk_buff *skbin, const struct xt_match_param *par)
#else
match(const struct sk_buff *skbin,
      const struct net_device *in,
      const struct net_device *out,
      const struct xt_match *match,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      bool *hotdrop)
#endif
{
	/* sidestep const without getting a compiler warning... */
	struct sk_buff * skb = (struct sk_buff *)skbin; 

	const struct xt_layer7_info * info = 
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28)
		par->matchinfo;
	#else
		matchinfo;
	#endif

	enum ip_conntrack_info master_ctinfo, ctinfo;
	struct nf_conn *master_conntrack, *conntrack;
	unsigned char *app_data, *tmp_data;
	unsigned int pattern_result, appdatalen;
	regexp * comppattern;

	/* Be paranoid/incompetent - lock the entire match function. */
	spin_lock_bh(&l7_lock);

	if(!can_handle(skb)){
		DPRINTK("layer7: This is some protocol I can't handle.\n");
		spin_unlock_bh(&l7_lock);
		return info->invert;
	}

	/* Treat parent & all its children together as one connection, except
	for the purpose of setting conntrack->layer7.app_proto in the actual
	connection. This makes /proc/net/ip_conntrack more satisfying. */
	if(!(conntrack = nf_ct_get(skb, &ctinfo)) ||
	   !(master_conntrack=nf_ct_get(skb,&master_ctinfo))){
		DPRINTK("layer7: couldn't get conntrack.\n");
		spin_unlock_bh(&l7_lock);
		return info->invert;
	}

	/* Try to get a master conntrack (and its master etc) for FTP, etc. */
	while (master_ct(master_conntrack) != NULL)
		master_conntrack = master_ct(master_conntrack);

	/* if we've classified it or seen too many packets */
	if(!info->pkt && (total_acct_packets(master_conntrack) > num_packets ||
	   master_conntrack->layer7.app_proto)) {

		pattern_result = match_no_append(conntrack, master_conntrack, 
						 ctinfo, master_ctinfo, info);

		/* skb->cb[0] == seen. Don't do things twice if there are 
		multiple l7 rules. I'm not sure that using cb for this purpose 
		is correct, even though it says "put your private variables 
		there". But it doesn't look like it is being used for anything
		else in the skbs that make it here. */
		skb->cb[0] = 1; /* marking it seen here's probably irrelevant */

		spin_unlock_bh(&l7_lock);
		return (pattern_result ^ info->invert);
	}

	if(skb_is_nonlinear(skb)){
		if(skb_linearize(skb) != 0){
			if (net_ratelimit())
				printk(KERN_ERR "layer7: failed to linearize "
						"packet, bailing.\n");
			spin_unlock_bh(&l7_lock);
			return info->invert;
		}
	}

	/* now that the skb is linearized, it's safe to set these. */
	app_data = skb->data + app_data_offset(skb);
	appdatalen = skb_tail_pointer(skb) - app_data;

	/* the return value gets checked later, when we're ready to use it */
	comppattern = compile_and_cache(info->pattern, info->protocol);

	if (info->pkt) {
		tmp_data = kmalloc(maxdatalen, GFP_ATOMIC);
		if(!tmp_data){
			if (net_ratelimit())
				printk(KERN_ERR "layer7: out of memory in match, bailing.\n");
			return info->invert;
		}

		tmp_data[0] = '\0';
		add_datastr(tmp_data, 0, app_data, appdatalen);
		pattern_result = ((comppattern && regexec(comppattern, tmp_data)) ? 1 : 0);

		kfree(tmp_data);
		tmp_data = NULL;
		spin_unlock_bh(&l7_lock);

		return (pattern_result ^ info->invert);
	}

	/* On the first packet of a connection, allocate space for app data */
	if(total_acct_packets(master_conntrack) == 1 && !skb->cb[0] && 
	   !master_conntrack->layer7.app_data){
		master_conntrack->layer7.app_data = 
			kmalloc(maxdatalen, GFP_ATOMIC);
		if(!master_conntrack->layer7.app_data){
			if (net_ratelimit())
				printk(KERN_ERR "layer7: out of memory in "
						"match, bailing.\n");
			spin_unlock_bh(&l7_lock);
			return info->invert;
		}

		master_conntrack->layer7.app_data[0] = '\0';
	}

	/* Can be here, but unallocated, if numpackets is increased near
	the beginning of a connection */
	if(master_conntrack->layer7.app_data == NULL){
		spin_unlock_bh(&l7_lock);
		return info->invert; /* unmatched */
	}

	if(!skb->cb[0]){
		int newbytes;
		newbytes = add_data(master_conntrack, app_data, appdatalen);

		if(newbytes == 0) { /* didn't add any data */
			skb->cb[0] = 1;
			/* Didn't match before, not going to match now */
			spin_unlock_bh(&l7_lock);
			return info->invert;
		}
	}

	/* If looking for "unknown", then never match.  "Unknown" means that
	we've given up; we're still trying with these packets. */
	if(!strcmp(info->protocol, "unknown")) {
		pattern_result = 0;
	/* If looking for "unset", then always match. "Unset" means that we
	haven't yet classified the connection. */
	} else if(!strcmp(info->protocol, "unset")) {
		pattern_result = 2;
		DPRINTK("layer7: matched unset: not yet classified "
			"(%d/%d packets)\n",
                        total_acct_packets(master_conntrack), num_packets);
	/* If the regexp failed to compile, don't bother running it */
	} else if(comppattern && 
		  regexec(comppattern, master_conntrack->layer7.app_data)){
		DPRINTK("layer7: matched %s\n", info->protocol);
		pattern_result = 1;
	} else pattern_result = 0;

	if(pattern_result == 1) {
		master_conntrack->layer7.app_proto = 
			kmalloc(strlen(info->protocol)+1, GFP_ATOMIC);
		if(!master_conntrack->layer7.app_proto){
			if (net_ratelimit())
				printk(KERN_ERR "layer7: out of memory in "
						"match, bailing.\n");
			spin_unlock_bh(&l7_lock);
			return (pattern_result ^ info->invert);
		}
		strcpy(master_conntrack->layer7.app_proto, info->protocol);
	} else if(pattern_result > 1) { /* cleanup from "unset" */
		pattern_result = 1;
	}

	/* mark the packet seen */
	skb->cb[0] = 1;

	spin_unlock_bh(&l7_lock);
	return (pattern_result ^ info->invert);
}

// load nf_conntrack_ipv4
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
static int
#else
static bool
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28)
check(const struct xt_mtchk_param *par)
{
        if (nf_ct_l3proto_try_module_get(par->match->family) < 0) {
                printk(KERN_WARNING "can't load conntrack support for "
                                    "proto=%d\n", par->match->family);
#else
check(const char *tablename, const void *inf,
		 const struct xt_match *match, void *matchinfo,
		 unsigned int hook_mask)
{
        if (nf_ct_l3proto_try_module_get(match->family) < 0) {
                printk(KERN_WARNING "can't load conntrack support for "
                                    "proto=%d\n", match->family);
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
		return -EINVAL;
	}
	return 0;
#else
                return 0;
        }
	return 1;
#endif
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28)
	static void destroy(const struct xt_mtdtor_param *par)
	{
		nf_ct_l3proto_module_put(par->match->family);
	}
#else
	static void destroy(const struct xt_match *match, void *matchinfo)
	{
		nf_ct_l3proto_module_put(match->family);
	}
#endif

static struct xt_match xt_layer7_match[] __read_mostly = {
{
	.name		= "layer7",
	.family		= AF_INET,
	.checkentry	= check,
	.match		= match,
	.destroy	= destroy,
	.matchsize	= sizeof(struct xt_layer7_info),
	.me		= THIS_MODULE
}
};

static void layer7_cleanup_proc(void)
{
	remove_proc_entry("layer7_numpackets", init_net.proc_net);
}

/* register the proc file */
static void layer7_init_proc(void)
{
	struct proc_dir_entry* entry;
	entry = create_proc_entry("layer7_numpackets", 0644, init_net.proc_net);
	entry->read_proc = layer7_read_proc;
	entry->write_proc = layer7_write_proc;
}

static int __init xt_layer7_init(void)
{
	need_conntrack();

	layer7_init_proc();
	if(maxdatalen < 1) {
		printk(KERN_WARNING "layer7: maxdatalen can't be < 1, "
			"using 1\n");
		maxdatalen = 1;
	}
	/* This is not a hard limit.  It's just here to prevent people from
	bringing their slow machines to a grinding halt. */
	else if(maxdatalen > 65536) {
		printk(KERN_WARNING "layer7: maxdatalen can't be > 65536, "
			"using 65536\n");
		maxdatalen = 65536;
	}
	return xt_register_matches(xt_layer7_match,
				   ARRAY_SIZE(xt_layer7_match));
}

static void __exit xt_layer7_fini(void)
{
	layer7_cleanup_proc();
	xt_unregister_matches(xt_layer7_match, ARRAY_SIZE(xt_layer7_match));
}

module_init(xt_layer7_init);
module_exit(xt_layer7_fini);
