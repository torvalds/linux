/*-
 * Copyright (c) 2002-2003 Luigi Rizzo
 * Copyright (c) 1996 Alex Nash, Paul Traina, Poul-Henning Kamp
 * Copyright (c) 1994 Ugen J.S.Antsilevich
 *
 * Idea and grammar partially left from:
 * Copyright (c) 1993 Daniel Boulet
 *
 * Redistribution and use in source forms, with and without modification,
 * are permitted provided that this entire comment appears intact.
 *
 * Redistribution in binary form may occur without any restrictions.
 * Obviously, it would be nice if you gave credit where credit is due
 * but requiring it would be too onerous.
 *
 * This software is provided ``AS IS'' without any warranties of any kind.
 *
 * NEW command line interface for IP firewall facility
 *
 * $FreeBSD$
 *
 * ipv6 support
 */

#include <sys/types.h>
#include <sys/socket.h>

#include "ipfw2.h"

#include <err.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/icmp6.h>
#include <netinet/ip_fw.h>
#include <arpa/inet.h>

#define	CHECK_LENGTH(v, len) do {			\
	if ((v) < (len))				\
		errx(EX_DATAERR, "Rule too long");	\
	} while (0)

static struct _s_x icmp6codes[] = {
	{ "no-route",		ICMP6_DST_UNREACH_NOROUTE },
	{ "admin-prohib",		ICMP6_DST_UNREACH_ADMIN },
	{ "address",		ICMP6_DST_UNREACH_ADDR },
	{ "port",			ICMP6_DST_UNREACH_NOPORT },
	{ NULL, 0 }
};

void
fill_unreach6_code(u_short *codep, char *str)
{
	int val;
	char *s;

	val = strtoul(str, &s, 0);
	if (s == str || *s != '\0' || val >= 0x100)
		val = match_token(icmp6codes, str);
	if (val < 0)
		errx(EX_DATAERR, "unknown ICMPv6 unreachable code ``%s''", str);
	*codep = val;
	return;
}

void
print_unreach6_code(struct buf_pr *bp, uint16_t code)
{
	char const *s = match_value(icmp6codes, code);

	if (s != NULL)
		bprintf(bp, "unreach6 %s", s);
	else
		bprintf(bp, "unreach6 %u", code);
}

/*
 * Print the ip address contained in a command.
 */
void
print_ip6(struct buf_pr *bp, ipfw_insn_ip6 *cmd)
{
	char trad[255];
	struct hostent *he = NULL;
	struct in6_addr *a = &(cmd->addr6);
	int len, mb;

	len = F_LEN((ipfw_insn *) cmd) - 1;
	if (cmd->o.opcode == O_IP6_SRC_ME || cmd->o.opcode == O_IP6_DST_ME) {
		bprintf(bp, " me6");
		return;
	}
	if (cmd->o.opcode == O_IP6) {
		bprintf(bp, " ip6");
		return;
	}

	/*
	 * len == 4 indicates a single IP, whereas lists of 1 or more
	 * addr/mask pairs have len = (2n+1). We convert len to n so we
	 * use that to count the number of entries.
	 */
	bprintf(bp, " ");
	for (len = len / 4; len > 0; len -= 2, a += 2) {
		/* mask length */
		mb = (cmd->o.opcode == O_IP6_SRC ||
		    cmd->o.opcode == O_IP6_DST) ?  128:
		    contigmask((uint8_t *)&(a[1]), 128);

		if (mb == 128 && co.do_resolv)
			he = gethostbyaddr((char *)a, sizeof(*a), AF_INET6);

		if (he != NULL)	     /* resolved to name */
			bprintf(bp, "%s", he->h_name);
		else if (mb == 0)	   /* any */
			bprintf(bp, "any");
		else {	  /* numeric IP followed by some kind of mask */
			if (inet_ntop(AF_INET6,  a, trad,
			    sizeof(trad)) == NULL)
				bprintf(bp, "Error ntop in print_ip6\n");
			bprintf(bp, "%s",  trad );
			if (mb < 0) /* mask not contiguous */
				bprintf(bp, "/%s", inet_ntop(AF_INET6, &a[1],
				    trad, sizeof(trad)));
			else if (mb < 128)
				bprintf(bp, "/%d", mb);
		}
		if (len > 2)
			bprintf(bp, ",");
	}
}

void
fill_icmp6types(ipfw_insn_icmp6 *cmd, char *av, int cblen)
{
       uint8_t type;

       CHECK_LENGTH(cblen, F_INSN_SIZE(ipfw_insn_icmp6));
       while (*av) {
	       if (*av == ',')
		       av++;
	       type = strtoul(av, &av, 0);
	       if (*av != ',' && *av != '\0')
		       errx(EX_DATAERR, "invalid ICMP6 type");
	       /*
		* XXX: shouldn't this be 0xFF?  I can't see any reason why
		* we shouldn't be able to filter all possiable values
		* regardless of the ability of the rest of the kernel to do
		* anything useful with them.
		*/
	       if (type > ICMP6_MAXTYPE)
		       errx(EX_DATAERR, "ICMP6 type out of range");
	       cmd->d[type / 32] |= ( 1 << (type % 32));
       }
       cmd->o.opcode = O_ICMP6TYPE;
       cmd->o.len |= F_INSN_SIZE(ipfw_insn_icmp6);
}

void
print_icmp6types(struct buf_pr *bp, ipfw_insn_u32 *cmd)
{
	int i, j;
	char sep= ' ';

	bprintf(bp, " icmp6types");
	for (i = 0; i < 7; i++)
		for (j=0; j < 32; ++j) {
			if ( (cmd->d[i] & (1 << (j))) == 0)
				continue;
			bprintf(bp, "%c%d", sep, (i*32 + j));
			sep = ',';
		}
}

void
print_flow6id(struct buf_pr *bp, ipfw_insn_u32 *cmd)
{
	uint16_t i, limit = cmd->o.arg1;
	char sep = ',';

	bprintf(bp, " flow-id ");
	for( i=0; i < limit; ++i) {
		if (i == limit - 1)
			sep = ' ';
		bprintf(bp, "%d%c", cmd->d[i], sep);
	}
}

/* structure and define for the extension header in ipv6 */
static struct _s_x ext6hdrcodes[] = {
	{ "frag",       EXT_FRAGMENT },
	{ "hopopt",     EXT_HOPOPTS },
	{ "route",      EXT_ROUTING },
	{ "dstopt",     EXT_DSTOPTS },
	{ "ah",	 EXT_AH },
	{ "esp",	EXT_ESP },
	{ "rthdr0",     EXT_RTHDR0 },
	{ "rthdr2",     EXT_RTHDR2 },
	{ NULL,	 0 }
};

/* fills command for the extension header filtering */
int
fill_ext6hdr( ipfw_insn *cmd, char *av)
{
	int tok;
	char *s = av;

	cmd->arg1 = 0;
	while(s) {
		av = strsep( &s, ",") ;
		tok = match_token(ext6hdrcodes, av);
		switch (tok) {
		case EXT_FRAGMENT:
			cmd->arg1 |= EXT_FRAGMENT;
			break;
		case EXT_HOPOPTS:
			cmd->arg1 |= EXT_HOPOPTS;
			break;
		case EXT_ROUTING:
			cmd->arg1 |= EXT_ROUTING;
			break;
		case EXT_DSTOPTS:
			cmd->arg1 |= EXT_DSTOPTS;
			break;
		case EXT_AH:
			cmd->arg1 |= EXT_AH;
			break;
		case EXT_ESP:
			cmd->arg1 |= EXT_ESP;
			break;
		case EXT_RTHDR0:
			cmd->arg1 |= EXT_RTHDR0;
			break;
		case EXT_RTHDR2:
			cmd->arg1 |= EXT_RTHDR2;
			break;
		default:
			errx(EX_DATAERR,
			    "invalid option for ipv6 exten header");
			break;
		}
	}
	if (cmd->arg1 == 0)
		return (0);
	cmd->opcode = O_EXT_HDR;
	cmd->len |= F_INSN_SIZE(ipfw_insn);
	return (1);
}

void
print_ext6hdr(struct buf_pr *bp, ipfw_insn *cmd )
{
	char sep = ' ';

	bprintf(bp, " extension header:");
	if (cmd->arg1 & EXT_FRAGMENT) {
		bprintf(bp, "%cfragmentation", sep);
		sep = ',';
	}
	if (cmd->arg1 & EXT_HOPOPTS) {
		bprintf(bp, "%chop options", sep);
		sep = ',';
	}
	if (cmd->arg1 & EXT_ROUTING) {
		bprintf(bp, "%crouting options", sep);
		sep = ',';
	}
	if (cmd->arg1 & EXT_RTHDR0) {
		bprintf(bp, "%crthdr0", sep);
		sep = ',';
	}
	if (cmd->arg1 & EXT_RTHDR2) {
		bprintf(bp, "%crthdr2", sep);
		sep = ',';
	}
	if (cmd->arg1 & EXT_DSTOPTS) {
		bprintf(bp, "%cdestination options", sep);
		sep = ',';
	}
	if (cmd->arg1 & EXT_AH) {
		bprintf(bp, "%cauthentication header", sep);
		sep = ',';
	}
	if (cmd->arg1 & EXT_ESP) {
		bprintf(bp, "%cencapsulated security payload", sep);
	}
}

/* Try to find ipv6 address by hostname */
static int
lookup_host6 (char *host, struct in6_addr *ip6addr)
{
	struct hostent *he;

	if (!inet_pton(AF_INET6, host, ip6addr)) {
		if ((he = gethostbyname2(host, AF_INET6)) == NULL)
			return(-1);
		memcpy(ip6addr, he->h_addr_list[0], sizeof( struct in6_addr));
	}
	return (0);
}


/*
 * fill the addr and mask fields in the instruction as appropriate from av.
 * Update length as appropriate.
 * The following formats are allowed:
 *     any     matches any IP6. Actually returns an empty instruction.
 *     me      returns O_IP6_*_ME
 *
 *     03f1::234:123:0342			single IP6 address
 *     03f1::234:123:0342/24			address/masklen
 *     03f1::234:123:0342/ffff::ffff:ffff	address/mask
 *     03f1::234:123:0342/24,03f1::234:123:0343/	List of address
 *
 * Set of address (as in ipv6) not supported because ipv6 address
 * are typically random past the initial prefix.
 * Return 1 on success, 0 on failure.
 */
static int
fill_ip6(ipfw_insn_ip6 *cmd, char *av, int cblen, struct tidx *tstate)
{
	int len = 0;
	struct in6_addr *d = &(cmd->addr6);
	char *oav;
	/*
	 * Needed for multiple address.
	 * Note d[1] points to struct in6_add r mask6 of cmd
	 */

	cmd->o.len &= ~F_LEN_MASK;	/* zero len */

	if (strcmp(av, "any") == 0)
		return (1);


	if (strcmp(av, "me") == 0) {	/* Set the data for "me" opt*/
		cmd->o.len |= F_INSN_SIZE(ipfw_insn);
		return (1);
	}

	if (strcmp(av, "me6") == 0) {	/* Set the data for "me" opt*/
		cmd->o.len |= F_INSN_SIZE(ipfw_insn);
		return (1);
	}

	if (strncmp(av, "table(", 6) == 0) {
		fill_table(&cmd->o, av, O_IP_DST_LOOKUP, tstate);
		return (1);
	}

	oav = av = strdup(av);
	while (av) {
		/*
		 * After the address we can have '/' indicating a mask,
		 * or ',' indicating another address follows.
		 */

		char *p, *q;
		int masklen;
		char md = '\0';

		CHECK_LENGTH(cblen, 1 + len + 2 * F_INSN_SIZE(struct in6_addr));

		if ((q = strchr(av, ',')) ) {
			*q = '\0';
			q++;
		}

		if ((p = strchr(av, '/')) ) {
			md = *p;	/* save the separator */
			*p = '\0';	/* terminate address string */
			p++;		/* and skip past it */
		}
		/* now p points to NULL, mask or next entry */

		/* lookup stores address in *d as a side effect */
		if (lookup_host6(av, d) != 0) {
			/* XXX: failed. Free memory and go */
			errx(EX_DATAERR, "bad address \"%s\"", av);
		}
		/* next, look at the mask, if any */
		if (md == '/' && strchr(p, ':')) {
			if (!inet_pton(AF_INET6, p, &d[1]))
				errx(EX_DATAERR, "bad mask \"%s\"", p);

			masklen = contigmask((uint8_t *)&(d[1]), 128);
		} else {
			masklen = (md == '/') ? atoi(p) : 128;
			if (masklen > 128 || masklen < 0)
				errx(EX_DATAERR, "bad width \"%s\''", p);
			else
				n2mask(&d[1], masklen);
		}

		APPLY_MASK(d, &d[1]);   /* mask base address with mask */

		av = q;

		/* Check this entry */
		if (masklen == 0) {
			/*
			 * 'any' turns the entire list into a NOP.
			 * 'not any' never matches, so it is removed from the
			 * list unless it is the only item, in which case we
			 * report an error.
			 */
			if (cmd->o.len & F_NOT && av == NULL && len == 0)
				errx(EX_DATAERR, "not any never matches");
			continue;
		}

		/*
		 * A single IP can be stored alone
		 */
		if (masklen == 128 && av == NULL && len == 0) {
			len = F_INSN_SIZE(struct in6_addr);
			break;
		}

		/* Update length and pointer to arguments */
		len += F_INSN_SIZE(struct in6_addr)*2;
		d += 2;
	} /* end while */

	/*
	 * Total length of the command, remember that 1 is the size of
	 * the base command.
	 */
	if (len + 1 > F_LEN_MASK)
		errx(EX_DATAERR, "address list too long");
	cmd->o.len |= len+1;
	free(oav);
	return (1);
}

/*
 * fills command for ipv6 flow-id filtering
 * note that the 20 bit flow number is stored in a array of u_int32_t
 * it's supported lists of flow-id, so in the o.arg1 we store how many
 * additional flow-id we want to filter, the basic is 1
 */
void
fill_flow6( ipfw_insn_u32 *cmd, char *av, int cblen)
{
	u_int32_t type;	 /* Current flow number */
	u_int16_t nflow = 0;    /* Current flow index */
	char *s = av;
	cmd->d[0] = 0;	  /* Initializing the base number*/

	while (s) {
		CHECK_LENGTH(cblen, F_INSN_SIZE(ipfw_insn_u32) + nflow + 1);

		av = strsep( &s, ",") ;
		type = strtoul(av, &av, 0);
		if (*av != ',' && *av != '\0')
			errx(EX_DATAERR, "invalid ipv6 flow number %s", av);
		if (type > 0xfffff)
			errx(EX_DATAERR, "flow number out of range %s", av);
		cmd->d[nflow] |= type;
		nflow++;
	}
	if( nflow > 0 ) {
		cmd->o.opcode = O_FLOW6ID;
		cmd->o.len |= F_INSN_SIZE(ipfw_insn_u32) + nflow;
		cmd->o.arg1 = nflow;
	}
	else {
		errx(EX_DATAERR, "invalid ipv6 flow number %s", av);
	}
}

ipfw_insn *
add_srcip6(ipfw_insn *cmd, char *av, int cblen, struct tidx *tstate)
{

	fill_ip6((ipfw_insn_ip6 *)cmd, av, cblen, tstate);
	if (cmd->opcode == O_IP_DST_SET)			/* set */
		cmd->opcode = O_IP_SRC_SET;
	else if (cmd->opcode == O_IP_DST_LOOKUP)		/* table */
		cmd->opcode = O_IP_SRC_LOOKUP;
	else if (F_LEN(cmd) == 0) {				/* any */
	} else if (F_LEN(cmd) == F_INSN_SIZE(ipfw_insn)) {	/* "me" */
		cmd->opcode = O_IP6_SRC_ME;
	} else if (F_LEN(cmd) ==
	    (F_INSN_SIZE(struct in6_addr) + F_INSN_SIZE(ipfw_insn))) {
		/* single IP, no mask*/
		cmd->opcode = O_IP6_SRC;
	} else {					/* addr/mask opt */
		cmd->opcode = O_IP6_SRC_MASK;
	}
	return cmd;
}

ipfw_insn *
add_dstip6(ipfw_insn *cmd, char *av, int cblen, struct tidx *tstate)
{

	fill_ip6((ipfw_insn_ip6 *)cmd, av, cblen, tstate);
	if (cmd->opcode == O_IP_DST_SET)			/* set */
		;
	else if (cmd->opcode == O_IP_DST_LOOKUP)		/* table */
		;
	else if (F_LEN(cmd) == 0) {				/* any */
	} else if (F_LEN(cmd) == F_INSN_SIZE(ipfw_insn)) {	/* "me" */
		cmd->opcode = O_IP6_DST_ME;
	} else if (F_LEN(cmd) ==
	    (F_INSN_SIZE(struct in6_addr) + F_INSN_SIZE(ipfw_insn))) {
		/* single IP, no mask*/
		cmd->opcode = O_IP6_DST;
	} else {					/* addr/mask opt */
		cmd->opcode = O_IP6_DST_MASK;
	}
	return cmd;
}
