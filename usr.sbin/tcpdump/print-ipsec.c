/*	$OpenBSD: print-ipsec.c,v 1.27 2021/11/29 18:50:16 tb Exp $	*/

/*
 * Copyright (c) 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Format and print IPsec (ESP/AH) packets.
 *      By Tero Kivinen <kivinen@ssh.fi>, Tero Mononen <tmo@ssh.fi>,  
 *         Tatu Ylonen <ylo@ssh.fi> and Timo J. Rinne <tri@ssh.fi>
 *         in co-operation with SSH Communications Security, Espoo, Finland    
 */

#include <sys/time.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/ip_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/tcp.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "addrtoname.h"
#include "interface.h"
#include "extract.h"		    /* must come after interface.h */

#include <openssl/evp.h>
#include <ctype.h>

/*
 * IPsec/ESP header
 */
struct esp_hdr {
	u_int esp_spi;
	u_int esp_seq;
};

static int espinit = 0;
static int espauthlen = 12;
static EVP_CIPHER_CTX *ctx;

int
esp_init (char *espspec)
{
	const EVP_CIPHER *evp;
	char *p, *espkey, s[3], name[1024];
	u_char *key;
	int i, klen, len;

	evp = EVP_aes_128_cbc();	/* default */
	espkey = espspec;
	if ((p = strchr(espspec, ':')) != NULL) {
		len = p - espspec;
		if (len >= sizeof(name))
			error("espalg too long");
		memcpy(name, espspec, len);
		name[len] = '\0';
		espkey = p + 1;

		/* strip auth alg */
		espauthlen = 0;
		if ((p = strstr(name, "-hmac96")) != NULL) {
			espauthlen = 12;
			*p = '\0';
		}
		OpenSSL_add_all_algorithms();
		if ((evp = EVP_get_cipherbyname(name)) == NULL)
			error("espalg `%s' not supported", name);
	}
	klen = EVP_CIPHER_key_length(evp);
	if (strlen(espkey) != klen * 2)
		error("espkey size mismatch, %d bytes needed", klen);
	if ((key = malloc(klen)) == NULL)
		error("malloc failed");
	for (i = 0; i < klen; i++) {
		s[0] = espkey[2*i];
		s[1] = espkey[2*i + 1];
		s[2] = 0;
		if (!isxdigit((unsigned char)s[0]) ||
		    !isxdigit((unsigned char)s[1])) {
			free(key);
			error("espkey must be specified in hex");
		}
		key[i] = strtoul(s, NULL, 16);
	}
	if ((ctx = EVP_CIPHER_CTX_new()) == NULL) {
		free(key);
		error("espkey init failed");
	}
	if (!EVP_CipherInit(ctx, evp, key, NULL, 0)) {
		EVP_CIPHER_CTX_free(ctx);
		free(key);
		error("espkey init failed");
	}
	free(key);
	espinit = 1;
	return (0);
}

void
esp_decrypt (const u_char *bp, u_int len, const u_char *bp2)
{
	const struct ip *ip;
	u_char *data, pad, nh;
	int blocksz;

	ip = (const struct ip *)bp2;

	blocksz = EVP_CIPHER_CTX_block_size(ctx);

	/* Skip fragments and short packets */
	if (ntohs(ip->ip_off) & 0x3fff)
		return;
	if (snapend - bp < len) {
		printf(" [|esp]");
		return;
	}
	/*
	 * Skip ESP header and ignore authentication trailer.
	 * For decryption we need at least 2 blocks: IV and
	 * one cipher block.
	 */
	if (len < sizeof(struct esp_hdr) + espauthlen + 2 * blocksz) {
		printf(" [|esp]");
		return;
	}

	data = (char *)bp;
	data += sizeof(struct esp_hdr);
	len -= sizeof(struct esp_hdr);
	len -= espauthlen;

	/* the first block contains the IV */
	if (!EVP_CipherInit(ctx, NULL, NULL, data, 0))
		return;

	len -= blocksz;
	data += blocksz;

	/* decrypt remaining payload */
	if (!EVP_Cipher(ctx, data, data, len))
		return;

	nh = data[len - 1];
	pad = data[len - 2];

	/* verify padding */
	if (pad + 2 > len)
		return;
	if (data[len - 3]  != pad)
		return;
	if (vflag > 1)
		printf(" pad %d", pad);
	len -= (pad + 2);
	printf(": ");
	switch (nh) {
	case IPPROTO_TCP:
		tcp_print(data, len, bp2);
		break;
	case IPPROTO_UDP:
		udp_print(data, len, bp2);
		break;
	case IPPROTO_IPV6:
		ip6_print(data, len);
		break;
	case IPPROTO_IPV4:
		ip_print(data, len);
		break;
	case IPPROTO_ICMP:
		icmp_print(data, len, bp2);
		break;
	case IPPROTO_ICMPV6:
		icmp6_print(data, len, bp2);
		break;
	default:
		printf("ip-proto-%d %d", nh, len);
		break;
	}
	if (vflag)
		printf(" (esp)");
}

void 
esp_print (const u_char *bp, u_int len, const u_char *bp2)
{
	const struct esp_hdr *esp;

	if (len < sizeof(struct esp_hdr)) {
		printf("[|esp]");
		return;
	}
	esp = (const struct esp_hdr *)bp;

	printf("esp spi 0x%08x seq %u len %d",
	    ntohl(esp->esp_spi), ntohl(esp->esp_seq), len);

	if (espinit)
		esp_decrypt(bp, len, bp2);
}

/*
 * IPsec/AH header
 */
struct ah_hdr {
	u_char  ah_nxt_hdr;
	u_char  ah_pl_len;
	u_short ah_reserved;
	u_int   ah_spi;
	u_int   ah_seq;
};

void
ah_print (const u_char *bp, u_int len, const u_char *bp2)
{
	const struct ip *ip;
	const struct ah_hdr *ah;
	u_int pl_len = len;
	const struct ip6_hdr *ip6;

	ip = (const struct ip *)bp2;
	if (ip->ip_v == 6) {
		ip6 = (const struct ip6_hdr *)bp2;
		printf("ah %s > %s", ip6addr_string(&ip6->ip6_src),
		    ip6addr_string(&ip6->ip6_dst));
	} else
		printf("ah %s > %s",
	    	    ipaddr_string(&ip->ip_src), ipaddr_string(&ip->ip_dst));

	if (pl_len < sizeof(struct ah_hdr)) {
		printf("[|ah]");
		return;
	}
	ah = (const struct ah_hdr *)bp;

	printf(" spi 0x%08x seq %u len %d",
	    ntohl(ah->ah_spi), ntohl(ah->ah_seq), len);

	if (vflag) {
	        printf(" [ ");

	        pl_len = (ah->ah_pl_len + 2) << 2; /* RFC2402, sec 2.2 */

		if (len <= pl_len) {
		        printf("truncated");
			goto out;
		}
		
		switch (ah->ah_nxt_hdr) { 

		case IPPROTO_IPIP: /* Tunnel Mode, IP-in-IP */
		        ip_print(bp + pl_len, len - pl_len); 
			break;

	        case IPPROTO_ICMP: /* From here and down; Transport mode */
		        icmp_print(bp + pl_len, len - pl_len,
				  (const u_char *) ip);
			break;

	        case IPPROTO_ICMPV6:
		        icmp6_print(bp + pl_len, len - pl_len,
				  (const u_char *) ip);
			break;

	        case IPPROTO_TCP:
		        tcp_print(bp + pl_len, len - pl_len, 
				  (const u_char *) ip);
			break;

	        case IPPROTO_UDP:
		        udp_print(bp + pl_len, len - pl_len, 
				  (const u_char *) ip);
			break;

		case IPPROTO_ESP:
		        esp_print(bp + pl_len, len - pl_len, 
				  (const u_char *) ip);
			break;

		case IPPROTO_AH:
		        ah_print(bp + pl_len, len - pl_len, 
				 (const u_char *) ip);
			break;

		default:
		        printf("ip-proto-%d len %d",
			    ah->ah_nxt_hdr, len - pl_len);
		}
out:
		printf(" ]");
	}

}

struct ipcomp_hdr {
	u_char  ipcomp_nxt_hdr;
	u_char	ipcomp_flags;
	u_short	ipcomp_cpi;
};

void
ipcomp_print (const u_char *bp, u_int len, const u_char *bp2)
{
	const struct ip *ip;
	const struct ipcomp_hdr *ipc;
	u_int plen = len;
 
	ip = (const struct ip *)bp2;

	printf("ipcomp %s > %s",
	    ipaddr_string(&ip->ip_src),
	    ipaddr_string(&ip->ip_dst));

	if (plen < sizeof(struct ipcomp_hdr)) {
		printf("[|ipcomp]");
		return;
	}
	ipc = (const struct ipcomp_hdr *)bp;

	printf(" cpi 0x%04X flags %x next %x",
	    ntohs(ipc->ipcomp_cpi), ipc->ipcomp_flags, ipc->ipcomp_nxt_hdr);
}
