/* FTP extension for connection tracking. */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2003,2004 USAGI/WIDE Project <http://www.linux-ipv6.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/netfilter.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/ctype.h>
#include <linux/inet.h>
#include <net/checksum.h>
#include <net/tcp.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_conntrack_ecache.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <linux/netfilter/nf_conntrack_ftp.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rusty Russell <rusty@rustcorp.com.au>");
MODULE_DESCRIPTION("ftp connection tracking helper");
MODULE_ALIAS("ip_conntrack_ftp");

/* This is slow, but it's simple. --RR */
static char *ftp_buffer;

static DEFINE_SPINLOCK(nf_ftp_lock);

#define MAX_PORTS 8
static u_int16_t ports[MAX_PORTS];
static unsigned int ports_c;
module_param_array(ports, ushort, &ports_c, 0400);

static int loose;
module_param(loose, bool, 0600);

unsigned int (*nf_nat_ftp_hook)(struct sk_buff **pskb,
				enum ip_conntrack_info ctinfo,
				enum nf_ct_ftp_type type,
				unsigned int matchoff,
				unsigned int matchlen,
				struct nf_conntrack_expect *exp,
				u32 *seq);
EXPORT_SYMBOL_GPL(nf_nat_ftp_hook);

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

static int try_rfc959(const char *, size_t, struct nf_conntrack_man *, char);
static int try_eprt(const char *, size_t, struct nf_conntrack_man *, char);
static int try_epsv_response(const char *, size_t, struct nf_conntrack_man *,
			     char);

static struct ftp_search {
	const char *pattern;
	size_t plen;
	char skip;
	char term;
	enum nf_ct_ftp_type ftptype;
	int (*getnum)(const char *, size_t, struct nf_conntrack_man *, char);
} search[IP_CT_DIR_MAX][2] = {
	[IP_CT_DIR_ORIGINAL] = {
		{
			.pattern	= "PORT",
			.plen		= sizeof("PORT") - 1,
			.skip		= ' ',
			.term		= '\r',
			.ftptype	= NF_CT_FTP_PORT,
			.getnum		= try_rfc959,
		},
		{
			.pattern	= "EPRT",
			.plen		= sizeof("EPRT") - 1,
			.skip		= ' ',
			.term		= '\r',
			.ftptype	= NF_CT_FTP_EPRT,
			.getnum		= try_eprt,
		},
	},
	[IP_CT_DIR_REPLY] = {
		{
			.pattern	= "227 ",
			.plen		= sizeof("227 ") - 1,
			.skip		= '(',
			.term		= ')',
			.ftptype	= NF_CT_FTP_PASV,
			.getnum		= try_rfc959,
		},
		{
			.pattern	= "229 ",
			.plen		= sizeof("229 ") - 1,
			.skip		= '(',
			.term		= ')',
			.ftptype	= NF_CT_FTP_EPSV,
			.getnum		= try_epsv_response,
		},
	},
};

static int
get_ipv6_addr(const char *src, size_t dlen, struct in6_addr *dst, u_int8_t term)
{
	const char *end;
	int ret = in6_pton(src, min_t(size_t, dlen, 0xffff), (u8 *)dst, term, &end);
	if (ret > 0)
		return (int)(end - src);
	return 0;
}

static int try_number(const char *data, size_t dlen, u_int32_t array[],
		      int array_size, char sep, char term)
{
	u_int32_t i, len;

	memset(array, 0, sizeof(array[0])*array_size);

	/* Keep data pointing at next char. */
	for (i = 0, len = 0; len < dlen && i < array_size; len++, data++) {
		if (*data >= '0' && *data <= '9') {
			array[i] = array[i]*10 + *data - '0';
		}
		else if (*data == sep)
			i++;
		else {
			/* Unexpected character; true if it's the
			   terminator and we're finished. */
			if (*data == term && i == array_size - 1)
				return len;

			DEBUGP("Char %u (got %u nums) `%u' unexpected\n",
			       len, i, *data);
			return 0;
		}
	}
	DEBUGP("Failed to fill %u numbers separated by %c\n", array_size, sep);

	return 0;
}

/* Returns 0, or length of numbers: 192,168,1,1,5,6 */
static int try_rfc959(const char *data, size_t dlen,
		      struct nf_conntrack_man *cmd, char term)
{
	int length;
	u_int32_t array[6];

	length = try_number(data, dlen, array, 6, ',', term);
	if (length == 0)
		return 0;

	cmd->u3.ip =  htonl((array[0] << 24) | (array[1] << 16) |
				    (array[2] << 8) | array[3]);
	cmd->u.tcp.port = htons((array[4] << 8) | array[5]);
	return length;
}

/* Grab port: number up to delimiter */
static int get_port(const char *data, int start, size_t dlen, char delim,
		    __be16 *port)
{
	u_int16_t tmp_port = 0;
	int i;

	for (i = start; i < dlen; i++) {
		/* Finished? */
		if (data[i] == delim) {
			if (tmp_port == 0)
				break;
			*port = htons(tmp_port);
			DEBUGP("get_port: return %d\n", tmp_port);
			return i + 1;
		}
		else if (data[i] >= '0' && data[i] <= '9')
			tmp_port = tmp_port*10 + data[i] - '0';
		else { /* Some other crap */
			DEBUGP("get_port: invalid char.\n");
			break;
		}
	}
	return 0;
}

/* Returns 0, or length of numbers: |1|132.235.1.2|6275| or |2|3ffe::1|6275| */
static int try_eprt(const char *data, size_t dlen, struct nf_conntrack_man *cmd,
		    char term)
{
	char delim;
	int length;

	/* First character is delimiter, then "1" for IPv4 or "2" for IPv6,
	   then delimiter again. */
	if (dlen <= 3) {
		DEBUGP("EPRT: too short\n");
		return 0;
	}
	delim = data[0];
	if (isdigit(delim) || delim < 33 || delim > 126 || data[2] != delim) {
		DEBUGP("try_eprt: invalid delimitter.\n");
		return 0;
	}

	if ((cmd->l3num == PF_INET && data[1] != '1') ||
	    (cmd->l3num == PF_INET6 && data[1] != '2')) {
		DEBUGP("EPRT: invalid protocol number.\n");
		return 0;
	}

	DEBUGP("EPRT: Got %c%c%c\n", delim, data[1], delim);

	if (data[1] == '1') {
		u_int32_t array[4];

		/* Now we have IP address. */
		length = try_number(data + 3, dlen - 3, array, 4, '.', delim);
		if (length != 0)
			cmd->u3.ip = htonl((array[0] << 24) | (array[1] << 16)
					   | (array[2] << 8) | array[3]);
	} else {
		/* Now we have IPv6 address. */
		length = get_ipv6_addr(data + 3, dlen - 3,
				       (struct in6_addr *)cmd->u3.ip6, delim);
	}

	if (length == 0)
		return 0;
	DEBUGP("EPRT: Got IP address!\n");
	/* Start offset includes initial "|1|", and trailing delimiter */
	return get_port(data, 3 + length + 1, dlen, delim, &cmd->u.tcp.port);
}

/* Returns 0, or length of numbers: |||6446| */
static int try_epsv_response(const char *data, size_t dlen,
			     struct nf_conntrack_man *cmd, char term)
{
	char delim;

	/* Three delimiters. */
	if (dlen <= 3) return 0;
	delim = data[0];
	if (isdigit(delim) || delim < 33 || delim > 126
	    || data[1] != delim || data[2] != delim)
		return 0;

	return get_port(data, 3, dlen, delim, &cmd->u.tcp.port);
}

/* Return 1 for match, 0 for accept, -1 for partial. */
static int find_pattern(const char *data, size_t dlen,
			const char *pattern, size_t plen,
			char skip, char term,
			unsigned int *numoff,
			unsigned int *numlen,
			struct nf_conntrack_man *cmd,
			int (*getnum)(const char *, size_t,
				      struct nf_conntrack_man *, char))
{
	size_t i;

	DEBUGP("find_pattern `%s': dlen = %u\n", pattern, dlen);
	if (dlen == 0)
		return 0;

	if (dlen <= plen) {
		/* Short packet: try for partial? */
		if (strnicmp(data, pattern, dlen) == 0)
			return -1;
		else return 0;
	}

	if (strnicmp(data, pattern, plen) != 0) {
#if 0
		size_t i;

		DEBUGP("ftp: string mismatch\n");
		for (i = 0; i < plen; i++) {
			DEBUGP("ftp:char %u `%c'(%u) vs `%c'(%u)\n",
				i, data[i], data[i],
				pattern[i], pattern[i]);
		}
#endif
		return 0;
	}

	DEBUGP("Pattern matches!\n");
	/* Now we've found the constant string, try to skip
	   to the 'skip' character */
	for (i = plen; data[i] != skip; i++)
		if (i == dlen - 1) return -1;

	/* Skip over the last character */
	i++;

	DEBUGP("Skipped up to `%c'!\n", skip);

	*numoff = i;
	*numlen = getnum(data + i, dlen - i, cmd, term);
	if (!*numlen)
		return -1;

	DEBUGP("Match succeeded!\n");
	return 1;
}

/* Look up to see if we're just after a \n. */
static int find_nl_seq(u32 seq, const struct nf_ct_ftp_master *info, int dir)
{
	unsigned int i;

	for (i = 0; i < info->seq_aft_nl_num[dir]; i++)
		if (info->seq_aft_nl[dir][i] == seq)
			return 1;
	return 0;
}

/* We don't update if it's older than what we have. */
static void update_nl_seq(u32 nl_seq, struct nf_ct_ftp_master *info, int dir,
			  struct sk_buff *skb)
{
	unsigned int i, oldest = NUM_SEQ_TO_REMEMBER;

	/* Look for oldest: if we find exact match, we're done. */
	for (i = 0; i < info->seq_aft_nl_num[dir]; i++) {
		if (info->seq_aft_nl[dir][i] == nl_seq)
			return;

		if (oldest == info->seq_aft_nl_num[dir]
		    || before(info->seq_aft_nl[dir][i], oldest))
			oldest = i;
	}

	if (info->seq_aft_nl_num[dir] < NUM_SEQ_TO_REMEMBER) {
		info->seq_aft_nl[dir][info->seq_aft_nl_num[dir]++] = nl_seq;
		nf_conntrack_event_cache(IPCT_HELPINFO_VOLATILE, skb);
	} else if (oldest != NUM_SEQ_TO_REMEMBER) {
		info->seq_aft_nl[dir][oldest] = nl_seq;
		nf_conntrack_event_cache(IPCT_HELPINFO_VOLATILE, skb);
	}
}

static int help(struct sk_buff **pskb,
		unsigned int protoff,
		struct nf_conn *ct,
		enum ip_conntrack_info ctinfo)
{
	unsigned int dataoff, datalen;
	struct tcphdr _tcph, *th;
	char *fb_ptr;
	int ret;
	u32 seq;
	int dir = CTINFO2DIR(ctinfo);
	unsigned int matchlen, matchoff;
	struct nf_ct_ftp_master *ct_ftp_info = &nfct_help(ct)->help.ct_ftp_info;
	struct nf_conntrack_expect *exp;
	struct nf_conntrack_man cmd = {};
	unsigned int i;
	int found = 0, ends_in_nl;
	typeof(nf_nat_ftp_hook) nf_nat_ftp;

	/* Until there's been traffic both ways, don't look in packets. */
	if (ctinfo != IP_CT_ESTABLISHED
	    && ctinfo != IP_CT_ESTABLISHED+IP_CT_IS_REPLY) {
		DEBUGP("ftp: Conntrackinfo = %u\n", ctinfo);
		return NF_ACCEPT;
	}

	th = skb_header_pointer(*pskb, protoff, sizeof(_tcph), &_tcph);
	if (th == NULL)
		return NF_ACCEPT;

	dataoff = protoff + th->doff * 4;
	/* No data? */
	if (dataoff >= (*pskb)->len) {
		DEBUGP("ftp: dataoff(%u) >= skblen(%u)\n", dataoff,
			(*pskb)->len);
		return NF_ACCEPT;
	}
	datalen = (*pskb)->len - dataoff;

	spin_lock_bh(&nf_ftp_lock);
	fb_ptr = skb_header_pointer(*pskb, dataoff, datalen, ftp_buffer);
	BUG_ON(fb_ptr == NULL);

	ends_in_nl = (fb_ptr[datalen - 1] == '\n');
	seq = ntohl(th->seq) + datalen;

	/* Look up to see if we're just after a \n. */
	if (!find_nl_seq(ntohl(th->seq), ct_ftp_info, dir)) {
		/* Now if this ends in \n, update ftp info. */
		DEBUGP("nf_conntrack_ftp_help: wrong seq pos %s(%u) or %s(%u)\n",
		       ct_ftp_info->seq_aft_nl_num[dir] > 0 ? "" : "(UNSET)",
		       ct_ftp_info->seq_aft_nl[dir][0],
		       ct_ftp_info->seq_aft_nl_num[dir] > 1 ? "" : "(UNSET)",
		       ct_ftp_info->seq_aft_nl[dir][1]);
		ret = NF_ACCEPT;
		goto out_update_nl;
	}

	/* Initialize IP/IPv6 addr to expected address (it's not mentioned
	   in EPSV responses) */
	cmd.l3num = ct->tuplehash[dir].tuple.src.l3num;
	memcpy(cmd.u3.all, &ct->tuplehash[dir].tuple.src.u3.all,
	       sizeof(cmd.u3.all));

	for (i = 0; i < ARRAY_SIZE(search[dir]); i++) {
		found = find_pattern(fb_ptr, datalen,
				     search[dir][i].pattern,
				     search[dir][i].plen,
				     search[dir][i].skip,
				     search[dir][i].term,
				     &matchoff, &matchlen,
				     &cmd,
				     search[dir][i].getnum);
		if (found) break;
	}
	if (found == -1) {
		/* We don't usually drop packets.  After all, this is
		   connection tracking, not packet filtering.
		   However, it is necessary for accurate tracking in
		   this case. */
		if (net_ratelimit())
			printk("conntrack_ftp: partial %s %u+%u\n",
			       search[dir][i].pattern,
			       ntohl(th->seq), datalen);
		ret = NF_DROP;
		goto out;
	} else if (found == 0) { /* No match */
		ret = NF_ACCEPT;
		goto out_update_nl;
	}

	DEBUGP("conntrack_ftp: match `%.*s' (%u bytes at %u)\n",
	       (int)matchlen, fb_ptr + matchoff,
	       matchlen, ntohl(th->seq) + matchoff);

	exp = nf_conntrack_expect_alloc(ct);
	if (exp == NULL) {
		ret = NF_DROP;
		goto out;
	}

	/* We refer to the reverse direction ("!dir") tuples here,
	 * because we're expecting something in the other direction.
	 * Doesn't matter unless NAT is happening.  */
	exp->tuple.dst.u3 = ct->tuplehash[!dir].tuple.dst.u3;

	/* Update the ftp info */
	if ((cmd.l3num == ct->tuplehash[dir].tuple.src.l3num) &&
	    memcmp(&cmd.u3.all, &ct->tuplehash[dir].tuple.src.u3.all,
		     sizeof(cmd.u3.all))) {
		/* Enrico Scholz's passive FTP to partially RNAT'd ftp
		   server: it really wants us to connect to a
		   different IP address.  Simply don't record it for
		   NAT. */
		if (cmd.l3num == PF_INET) {
			DEBUGP("conntrack_ftp: NOT RECORDING: " NIPQUAD_FMT " != " NIPQUAD_FMT "\n",
			       NIPQUAD(cmd.u3.ip),
			       NIPQUAD(ct->tuplehash[dir].tuple.src.u3.ip));
		} else {
			DEBUGP("conntrack_ftp: NOT RECORDING: " NIP6_FMT " != " NIP6_FMT "\n",
			       NIP6(*((struct in6_addr *)cmd.u3.ip6)),
			       NIP6(*((struct in6_addr *)ct->tuplehash[dir]
							.tuple.src.u3.ip6)));
		}

		/* Thanks to Cristiano Lincoln Mattos
		   <lincoln@cesar.org.br> for reporting this potential
		   problem (DMZ machines opening holes to internal
		   networks, or the packet filter itself). */
		if (!loose) {
			ret = NF_ACCEPT;
			goto out_put_expect;
		}
		memcpy(&exp->tuple.dst.u3, &cmd.u3.all,
		       sizeof(exp->tuple.dst.u3));
	}

	exp->tuple.src.u3 = ct->tuplehash[!dir].tuple.src.u3;
	exp->tuple.src.l3num = cmd.l3num;
	exp->tuple.src.u.tcp.port = 0;
	exp->tuple.dst.u.tcp.port = cmd.u.tcp.port;
	exp->tuple.dst.protonum = IPPROTO_TCP;

	exp->mask = (struct nf_conntrack_tuple)
		    { .src = { .l3num = 0xFFFF,
			       .u = { .tcp = { 0 }},
			     },
		      .dst = { .protonum = 0xFF,
			       .u = { .tcp = { __constant_htons(0xFFFF) }},
			     },
		    };
	if (cmd.l3num == PF_INET) {
		exp->mask.src.u3.ip = htonl(0xFFFFFFFF);
		exp->mask.dst.u3.ip = htonl(0xFFFFFFFF);
	} else {
		memset(exp->mask.src.u3.ip6, 0xFF,
		       sizeof(exp->mask.src.u3.ip6));
		memset(exp->mask.dst.u3.ip6, 0xFF,
		       sizeof(exp->mask.src.u3.ip6));
	}

	exp->expectfn = NULL;
	exp->helper = NULL;
	exp->flags = 0;

	/* Now, NAT might want to mangle the packet, and register the
	 * (possibly changed) expectation itself. */
	nf_nat_ftp = rcu_dereference(nf_nat_ftp_hook);
	if (nf_nat_ftp && ct->status & IPS_NAT_MASK)
		ret = nf_nat_ftp(pskb, ctinfo, search[dir][i].ftptype,
				 matchoff, matchlen, exp, &seq);
	else {
		/* Can't expect this?  Best to drop packet now. */
		if (nf_conntrack_expect_related(exp) != 0)
			ret = NF_DROP;
		else
			ret = NF_ACCEPT;
	}

out_put_expect:
	nf_conntrack_expect_put(exp);

out_update_nl:
	/* Now if this ends in \n, update ftp info.  Seq may have been
	 * adjusted by NAT code. */
	if (ends_in_nl)
		update_nl_seq(seq, ct_ftp_info, dir, *pskb);
 out:
	spin_unlock_bh(&nf_ftp_lock);
	return ret;
}

static struct nf_conntrack_helper ftp[MAX_PORTS][2];
static char ftp_names[MAX_PORTS][2][sizeof("ftp-65535")];

/* don't make this __exit, since it's called from __init ! */
static void nf_conntrack_ftp_fini(void)
{
	int i, j;
	for (i = 0; i < ports_c; i++) {
		for (j = 0; j < 2; j++) {
			if (ftp[i][j].me == NULL)
				continue;

			DEBUGP("nf_ct_ftp: unregistering helper for pf: %d "
			       "port: %d\n",
				ftp[i][j].tuple.src.l3num, ports[i]);
			nf_conntrack_helper_unregister(&ftp[i][j]);
		}
	}

	kfree(ftp_buffer);
}

static int __init nf_conntrack_ftp_init(void)
{
	int i, j = -1, ret = 0;
	char *tmpname;

	ftp_buffer = kmalloc(65536, GFP_KERNEL);
	if (!ftp_buffer)
		return -ENOMEM;

	if (ports_c == 0)
		ports[ports_c++] = FTP_PORT;

	/* FIXME should be configurable whether IPv4 and IPv6 FTP connections
		 are tracked or not - YK */
	for (i = 0; i < ports_c; i++) {
		ftp[i][0].tuple.src.l3num = PF_INET;
		ftp[i][1].tuple.src.l3num = PF_INET6;
		for (j = 0; j < 2; j++) {
			ftp[i][j].tuple.src.u.tcp.port = htons(ports[i]);
			ftp[i][j].tuple.dst.protonum = IPPROTO_TCP;
			ftp[i][j].mask.src.l3num = 0xFFFF;
			ftp[i][j].mask.src.u.tcp.port = htons(0xFFFF);
			ftp[i][j].mask.dst.protonum = 0xFF;
			ftp[i][j].max_expected = 1;
			ftp[i][j].timeout = 5 * 60;	/* 5 Minutes */
			ftp[i][j].me = THIS_MODULE;
			ftp[i][j].help = help;
			tmpname = &ftp_names[i][j][0];
			if (ports[i] == FTP_PORT)
				sprintf(tmpname, "ftp");
			else
				sprintf(tmpname, "ftp-%d", ports[i]);
			ftp[i][j].name = tmpname;

			DEBUGP("nf_ct_ftp: registering helper for pf: %d "
			       "port: %d\n",
				ftp[i][j].tuple.src.l3num, ports[i]);
			ret = nf_conntrack_helper_register(&ftp[i][j]);
			if (ret) {
				printk("nf_ct_ftp: failed to register helper "
				       " for pf: %d port: %d\n",
					ftp[i][j].tuple.src.l3num, ports[i]);
				nf_conntrack_ftp_fini();
				return ret;
			}
		}
	}

	return 0;
}

module_init(nf_conntrack_ftp_init);
module_exit(nf_conntrack_ftp_fini);
