/*
 * ixfr.c -- generating IXFR responses.
 *
 * Copyright (c) 2021, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#include "config.h"

#include <errno.h>
#include <string.h>
#include <ctype.h>
#ifdef HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#  include <sys/stat.h>
#endif
#include <unistd.h>

#include "ixfr.h"
#include "packet.h"
#include "rdata.h"
#include "axfr.h"
#include "options.h"
#include "zonec.h"
#include "zone.h"

/*
 * For optimal compression IXFR response packets are limited in size
 * to MAX_COMPRESSION_OFFSET.
 */
#define IXFR_MAX_MESSAGE_LEN MAX_COMPRESSION_OFFSET

/* draft-ietf-dnsop-rfc2845bis-06, section 5.3.1 says to sign every packet */
#define IXFR_TSIG_SIGN_EVERY_NTH	0	/* tsig sign every N packets. */

/* initial space in rrs data for storing records */
#define IXFR_STORE_INITIAL_SIZE 4096

/* store compression for one name */
struct rrcompress_entry {
	/* rbtree node, key is this struct */
	struct rbnode node;
	/* the uncompressed domain name */
	const uint8_t* dname;
	/* the length of the dname, includes terminating 0 label */
	uint16_t len;
	/* the offset of the dname in the packet */
	uint16_t offset;
};

/* structure to store compression data for the packet */
struct pktcompression {
	/* rbtree of rrcompress_entry. sorted by dname */
	struct rbtree tree;
	/* allocation information, how many bytes allocated now */
	size_t alloc_now;
	/* allocation information, total size in block */
	size_t alloc_max;
	/* region to use if block full, this is NULL if unused */
	struct region* region;
	/* block of temp data for allocation */
	uint8_t block[sizeof(struct rrcompress_entry)*1024];
};

/* compare two elements in the compression tree. Returns -1, 0, or 1. */
static int compression_cmp(const void* a, const void* b)
{
	struct rrcompress_entry* rra = (struct rrcompress_entry*)a;
	struct rrcompress_entry* rrb = (struct rrcompress_entry*)b;
	if(rra->len != rrb->len) {
		if(rra->len < rrb->len)
			return -1;
		return 1;
	}
	return memcmp(rra->dname, rrb->dname, rra->len);
}

/* init the pktcompression to a new packet */
static void pktcompression_init(struct pktcompression* pcomp)
{
	pcomp->alloc_now = 0;
	pcomp->alloc_max = sizeof(pcomp->block);
	pcomp->region = NULL;
	pcomp->tree.root = RBTREE_NULL;
	pcomp->tree.count = 0;
	pcomp->tree.region = NULL;
	pcomp->tree.cmp = &compression_cmp;
}

/* freeup the pktcompression data */
static void pktcompression_freeup(struct pktcompression* pcomp)
{
	if(pcomp->region) {
		region_destroy(pcomp->region);
		pcomp->region = NULL;
	}
	pcomp->alloc_now = 0;
	pcomp->tree.root = RBTREE_NULL;
	pcomp->tree.count = 0;
}

/* alloc data in pktcompression */
static void* pktcompression_alloc(struct pktcompression* pcomp, size_t s)
{
	/* first attempt to allocate in the fixed block,
	 * that is very fast and on the stack in the pcomp struct */
	if(pcomp->alloc_now + s <= pcomp->alloc_max) {
		void* ret = pcomp->block + pcomp->alloc_now;
		pcomp->alloc_now += s;
		return ret;
	}

	/* if that fails, create a region to allocate in,
	 * it is freed in the freeup */
	if(!pcomp->region) {
		pcomp->region = region_create(xalloc, free);
		if(!pcomp->region)
			return NULL;
	}
	return region_alloc(pcomp->region, s);
}

/* find a pktcompression name, return offset if found */
static uint16_t pktcompression_find(struct pktcompression* pcomp,
	const uint8_t* dname, size_t len)
{
	struct rrcompress_entry key, *found;
	key.node.key = &key;
	key.dname = dname;
	key.len = len;
	found = (struct rrcompress_entry*)rbtree_search(&pcomp->tree, &key);
	if(found) return found->offset;
	return 0;
}

/* insert a new domain name into the compression tree.
 * it fails silently, no need to compress then. */
static void pktcompression_insert(struct pktcompression* pcomp,
	const uint8_t* dname, size_t len, uint16_t offset)
{
	struct rrcompress_entry* entry;
	if(len > 65535)
		return;
	if(offset > MAX_COMPRESSION_OFFSET)
		return; /* too far for a compression pointer */
	entry = pktcompression_alloc(pcomp, sizeof(*entry));
	if(!entry)
		return;
	memset(&entry->node, 0, sizeof(entry->node));
	entry->node.key = entry;
	entry->dname = dname;
	entry->len = len;
	entry->offset = offset;
	(void)rbtree_insert(&pcomp->tree, &entry->node);
}

/* insert all the labels of a domain name */
static void pktcompression_insert_with_labels(struct pktcompression* pcomp,
	uint8_t* dname, size_t len, uint16_t offset)
{
	if(!dname)
		return;
	if(offset > MAX_COMPRESSION_OFFSET)
		return;

	/* while we have not seen the end root label */
	while(len > 0 && dname[0] != 0) {
		size_t lablen;
		pktcompression_insert(pcomp, dname, len, offset);
		lablen = (size_t)(dname[0]);
		if( (lablen&0xc0) )
			return; /* the dname should be uncompressed */
		if(lablen+1 > len)
			return; /* len should be uncompressed wireformat len */
		if(offset > MAX_COMPRESSION_OFFSET - lablen - 1)
			return; /* offset moves too far for compression */
		/* skip label */
		len -= lablen+1;
		dname += lablen+1;
		offset += lablen+1;
	}
}

/* calculate length of dname in uncompressed wireformat in buffer */
static size_t dname_length(const uint8_t* buf, size_t len)
{
	size_t l = 0;
	if(!buf || len == 0)
		return l;
	while(len > 0 && buf[0] != 0) {
		size_t lablen = (size_t)(buf[0]);
		if( (lablen&0xc0) )
			return 0; /* the name should be uncompressed */
		if(lablen+1 > len)
			return 0; /* should fit in the buffer */
		l += lablen+1;
		len -= lablen+1;
		buf += lablen+1;
		if(l > MAXDOMAINLEN)
			return 0;
	}
	if(len == 0)
		return 0; /* end label should fit in buffer */
	if(buf[0] != 0)
		return 0; /* must end in root label */
	l += 1; /* for the end root label */
	if(l > MAXDOMAINLEN)
		return 0;
	return l;
}

/* write a compressed domain name into the packet,
 * returns uncompressed wireformat length,
 * 0 if it does not fit and -1 on failure, bad dname. */
static int pktcompression_write_dname(struct buffer* packet,
	struct pktcompression* pcomp, const uint8_t* rr, size_t rrlen)
{
	size_t wirelen = 0;
	size_t dname_len = dname_length(rr, rrlen);
	if(!rr || rrlen == 0 || dname_len == 0)
		return 0;
	while(rrlen > 0 && rr[0] != 0) {
		size_t lablen = (size_t)(rr[0]);
		uint16_t offset;
		if( (lablen&0xc0) )
			return -1; /* name should be uncompressed */
		if(lablen+1 > rrlen)
			return -1; /* name should fit */

		/* see if the domain name has a compression pointer */
		if((offset=pktcompression_find(pcomp, rr, dname_len))!=0) {
			if(!buffer_available(packet, 2))
				return 0;
			buffer_write_u16(packet, (uint16_t)(0xc000 | offset));
			wirelen += dname_len;
			return wirelen;
		} else {
			if(!buffer_available(packet, lablen+1))
				return 0;
			/* insert the domain name at this position */
			pktcompression_insert(pcomp, rr, dname_len,
				buffer_position(packet));
			/* write it */
			buffer_write(packet, rr, lablen+1);
		}

		wirelen += lablen+1;
		rr += lablen+1;
		rrlen -= lablen+1;
		dname_len -= lablen+1;
	}
	if(rrlen > 0 && rr[0] == 0) {
		/* write end root label */
		if(!buffer_available(packet, 1))
			return 0;
		buffer_write_u8(packet, 0);
		wirelen += 1;
	}
	return wirelen;
}

/* write an RR into the packet with compression for domain names,
 * return 0 and resets position if it does not fit in the packet. */
static int ixfr_write_rr_pkt(struct query* query, struct buffer* packet,
	struct pktcompression* pcomp, const uint8_t* rr, size_t rrlen,
	uint16_t total_added)
{
	size_t oldpos = buffer_position(packet);
	size_t rdpos;
	uint16_t tp;
	int dname_len;
	size_t rdlen;
	size_t i;
	rrtype_descriptor_type* descriptor;

	if(total_added == 0) {
		size_t oldmaxlen = query->maxlen;
		/* RR > 16K can be first RR */
		query->maxlen = (query->tcp?TCP_MAX_MESSAGE_LEN:UDP_MAX_MESSAGE_LEN);
		if(query_overflow(query)) {
			query->maxlen = oldmaxlen;
			return 0;
		}
		query->maxlen = oldmaxlen;
	} else {
		if(buffer_position(packet) > MAX_COMPRESSION_OFFSET
			|| query_overflow(query)) {
			/* we are past the maximum length */
			return 0;
		}
	}

	/* write owner */
	dname_len = pktcompression_write_dname(packet, pcomp, rr, rrlen);
	if(dname_len == -1)
		return 1; /* attempt to skip this malformed rr, could assert */
	if(dname_len == 0) {
		buffer_set_position(packet, oldpos);
		return 0;
	}
	rr += dname_len;
	rrlen -= dname_len;

	/* type, class, ttl, rdatalen */
	if(!buffer_available(packet, 10)) {
		buffer_set_position(packet, oldpos);
		return 0;
	}
	if(10 > rrlen)
		return 1; /* attempt to skip this malformed rr, could assert */
	tp = read_uint16(rr);
	buffer_write(packet, rr, 8);
	rr += 8;
	rrlen -= 8;
	rdlen = read_uint16(rr);
	rr += 2;
	rrlen -= 2;
	rdpos = buffer_position(packet);
	buffer_write_u16(packet, 0);
	if(rdlen > rrlen)
		return 1; /* attempt to skip this malformed rr, could assert */

	/* rdata */
	descriptor = rrtype_descriptor_by_type(tp);
	for(i=0; i<descriptor->maximum; i++) {
		size_t copy_len = 0;
		if(rdlen == 0)
			break;

		switch(rdata_atom_wireformat_type(tp, i)) {
		case RDATA_WF_COMPRESSED_DNAME:
			dname_len = pktcompression_write_dname(packet, pcomp,
				rr, rdlen);
			if(dname_len == -1)
				return 1; /* attempt to skip malformed rr */
			if(dname_len == 0) {
				buffer_set_position(packet, oldpos);
				return 0;
			}
			rr += dname_len;
			rdlen -= dname_len;
			break;
		case RDATA_WF_UNCOMPRESSED_DNAME:
		case RDATA_WF_LITERAL_DNAME:
			copy_len = rdlen;
			break;
		case RDATA_WF_BYTE:
			copy_len = 1;
			break;
		case RDATA_WF_SHORT:
			copy_len = 2;
			break;
		case RDATA_WF_LONG:
			copy_len = 4;
			break;
		case RDATA_WF_LONGLONG:
			copy_len = 8;
			break;
		case RDATA_WF_TEXTS:
		case RDATA_WF_LONG_TEXT:
			copy_len = rdlen;
			break;
		case RDATA_WF_TEXT:
		case RDATA_WF_BINARYWITHLENGTH:
			copy_len = 1;
			if(rdlen > copy_len)
				copy_len += rr[0];
			break;
		case RDATA_WF_A:
			copy_len = 4;
			break;
		case RDATA_WF_AAAA:
			copy_len = 16;
			break;
		case RDATA_WF_ILNP64:
			copy_len = 8;
			break;
		case RDATA_WF_EUI48:
			copy_len = EUI48ADDRLEN;
			break;
		case RDATA_WF_EUI64:
			copy_len = EUI64ADDRLEN;
			break;
		case RDATA_WF_BINARY:
			copy_len = rdlen;
			break;
		case RDATA_WF_APL:
			copy_len = (sizeof(uint16_t)    /* address family */
                                  + sizeof(uint8_t)   /* prefix */
                                  + sizeof(uint8_t)); /* length */
			if(copy_len <= rdlen)
				copy_len += (rr[copy_len-1]&APL_LENGTH_MASK);
			break;
		case RDATA_WF_IPSECGATEWAY:
			copy_len = rdlen;
			break;
		case RDATA_WF_SVCPARAM:
			copy_len = 4;
			if(copy_len <= rdlen)
				copy_len += read_uint16(rr+2);
			break;
		default:
			copy_len = rdlen;
			break;
		}
		if(copy_len) {
			if(!buffer_available(packet, copy_len)) {
				buffer_set_position(packet, oldpos);
				return 0;
			}
			if(copy_len > rdlen)
				return 1; /* assert of skip malformed */
			buffer_write(packet, rr, copy_len);
			rr += copy_len;
			rdlen -= copy_len;
		}
	}
	/* write compressed rdata length */
	buffer_write_u16_at(packet, rdpos, buffer_position(packet)-rdpos-2);
	if(total_added == 0) {
		size_t oldmaxlen = query->maxlen;
		query->maxlen = (query->tcp?TCP_MAX_MESSAGE_LEN:UDP_MAX_MESSAGE_LEN);
		if(query_overflow(query)) {
			query->maxlen = oldmaxlen;
			buffer_set_position(packet, oldpos);
			return 0;
		}
		query->maxlen = oldmaxlen;
	} else {
		if(query_overflow(query)) {
			/* we are past the maximum length */
			buffer_set_position(packet, oldpos);
			return 0;
		}
	}
	return 1;
}

/* parse the serial number from the IXFR query */
static int parse_qserial(struct buffer* packet, uint32_t* qserial,
	size_t* snip_pos)
{
	unsigned int i;
	uint16_t type, rdlen;
	/* we must have a SOA in the authority section */
	if(NSCOUNT(packet) == 0)
		return 0;
	/* skip over the question section, we want only one */
	buffer_set_position(packet, QHEADERSZ);
	if(QDCOUNT(packet) != 1)
		return 0;
	if(!packet_skip_rr(packet, 1))
		return 0;
	/* set position to snip off the authority section */
	*snip_pos = buffer_position(packet);
	/* skip over the authority section RRs until we find the SOA */
	for(i=0; i<NSCOUNT(packet); i++) {
		/* is this the SOA record? */
		if(!packet_skip_dname(packet))
			return 0; /* malformed name */
		if(!buffer_available(packet, 10))
			return 0; /* no type,class,ttl,rdatalen */
		type = buffer_read_u16(packet);
		buffer_skip(packet, 6);
		rdlen = buffer_read_u16(packet);
		if(!buffer_available(packet, rdlen))
			return 0;
		if(type == TYPE_SOA) {
			/* read serial from rdata, skip two dnames, then
			 * read the 32bit value */
			if(!packet_skip_dname(packet))
				return 0; /* malformed nsname */
			if(!packet_skip_dname(packet))
				return 0; /* malformed rname */
			if(!buffer_available(packet, 4))
				return 0;
			*qserial = buffer_read_u32(packet);
			return 1;
		}
		buffer_skip(packet, rdlen);
	}
	return 0;
}

/* get serial from SOA RR */
static uint32_t soa_rr_get_serial(struct rr* rr)
{
	if(rr->rdata_count < 3)
		return 0;
	if(rr->rdatas[2].data[0] < 4)
		return 0;
	return read_uint32(&rr->rdatas[2].data[1]);
}

/* get the current serial from the zone */
uint32_t zone_get_current_serial(struct zone* zone)
{
	if(!zone || !zone->soa_rrset)
		return 0;
	if(zone->soa_rrset->rr_count == 0)
		return 0;
	if(zone->soa_rrset->rrs[0].rdata_count < 3)
		return 0;
	if(zone->soa_rrset->rrs[0].rdatas[2].data[0] < 4)
		return 0;
	return read_uint32(&zone->soa_rrset->rrs[0].rdatas[2].data[1]);
}

/* iterator over ixfr data. find first element, eg. oldest zone version
 * change.
 * The iterator can be started with the ixfr_data_first, but also with
 * ixfr_data_last, or with an existing ixfr_data element to start from.
 * Continue by using ixfr_data_next or ixfr_data_prev to ask for more elements
 * until that returns NULL. NULL because end of list or loop was detected.
 * The ixfr_data_prev uses a counter, start it at 0, it returns NULL when
 * a loop is detected.
 */
static struct ixfr_data* ixfr_data_first(struct zone_ixfr* ixfr)
{
	struct ixfr_data* n;
	if(!ixfr || !ixfr->data || ixfr->data->count==0)
		return NULL;
	n = (struct ixfr_data*)rbtree_search(ixfr->data, &ixfr->oldest_serial);
	if(!n || n == (struct ixfr_data*)RBTREE_NULL)
		return NULL;
	return n;
}

/* iterator over ixfr data. find last element, eg. newest zone version
 * change. */
static struct ixfr_data* ixfr_data_last(struct zone_ixfr* ixfr)
{
	struct ixfr_data* n;
	if(!ixfr || !ixfr->data || ixfr->data->count==0)
		return NULL;
	n = (struct ixfr_data*)rbtree_search(ixfr->data, &ixfr->newest_serial);
	if(!n || n == (struct ixfr_data*)RBTREE_NULL)
		return NULL;
	return n;
}

/* iterator over ixfr data. fetch next item. If loop or nothing, NULL */
static struct ixfr_data* ixfr_data_next(struct zone_ixfr* ixfr,
	struct ixfr_data* cur)
{
	struct ixfr_data* n;
	if(!cur || cur == (struct ixfr_data*)RBTREE_NULL)
		return NULL;
	if(cur->oldserial == ixfr->newest_serial)
		return NULL; /* that was the last element */
	n = (struct ixfr_data*)rbtree_next(&cur->node);
	if(n && n != (struct ixfr_data*)RBTREE_NULL &&
		cur->newserial == n->oldserial) {
		/* the next rbtree item is the next ixfr data item */
		return n;
	}
	/* If the next item is last of tree, and we have to loop around,
	 * the search performs the lookup for the next item we need.
	 * If the next item exists, but also is not connected, the search
	 * finds the correct connected ixfr in the sorted tree. */
	/* try searching for the correct ixfr data item */
	n = (struct ixfr_data*)rbtree_search(ixfr->data, &cur->newserial);
	if(!n || n == (struct ixfr_data*)RBTREE_NULL)
		return NULL;
	return n;
}

/* iterator over ixfr data. fetch the previous item. If loop or nothing NULL.*/
static struct ixfr_data* ixfr_data_prev(struct zone_ixfr* ixfr,
	struct ixfr_data* cur, size_t* prevcount)
{
	struct ixfr_data* prev;
	if(!cur || cur == (struct ixfr_data*)RBTREE_NULL)
		return NULL;
	if(cur->oldserial == ixfr->oldest_serial)
		return NULL; /* this was the first element */
	prev = (struct ixfr_data*)rbtree_previous(&cur->node);
	if(!prev || prev == (struct ixfr_data*)RBTREE_NULL) {
		/* We hit the first element in the tree, go again
		 * at the last one. Wrap around. */
		prev = (struct ixfr_data*)rbtree_last(ixfr->data);
	}
	while(prev && prev != (struct ixfr_data*)RBTREE_NULL) {
		if(prev->newserial == cur->oldserial) {
			/* This is the correct matching previous ixfr data */
			/* Increase the prevcounter every time the routine
			 * returns an item, and if that becomes too large, we
			 * are in a loop. in that case, stop. */
			if(prevcount) {
				(*prevcount)++;
				if(*prevcount > ixfr->data->count + 12) {
					/* Larger than the max number of items
					 * plus a small margin. The longest
					 * chain is all the ixfr elements in
					 * the tree. It loops. */
					return NULL;
				}
			}
			return prev;
		}
		prev = (struct ixfr_data*)rbtree_previous(&prev->node);
		if(!prev || prev == (struct ixfr_data*)RBTREE_NULL) {
			/* We hit the first element in the tree, go again
			 * at the last one. Wrap around. */
			prev = (struct ixfr_data*)rbtree_last(ixfr->data);
		}
	}
	/* no elements in list */
	return NULL;
}

/* connect IXFRs, return true if connected, false if not. Return last serial */
static int connect_ixfrs(struct zone_ixfr* ixfr, struct ixfr_data* data,
	uint32_t* end_serial)
{
	struct ixfr_data* p = data;
	while(p != NULL) {
		struct ixfr_data* next = ixfr_data_next(ixfr, p);
		if(next) {
			if(p->newserial != next->oldserial) {
				/* These ixfrs are not connected,
				 * during IXFR processing that could already
				 * have been deleted, but we check here
				 * in any case */
				return 0;
			}
		} else {
			/* the chain of IXFRs ends in this serial number */
			*end_serial = p->newserial;
		}
		p = next;
	}
	return 1;
}

/* Count length of next record in data */
static size_t count_rr_length(const uint8_t* data, size_t data_len,
	size_t current)
{
	uint8_t label_size;
	uint16_t rdlen;
	size_t i = current;
	if(current >= data_len)
		return 0;
	/* pass the owner dname */
	while(1) {
		if(i+1 > data_len)
			return 0;
		label_size = data[i++];
		if(label_size == 0) {
			break;
		} else if((label_size &0xc0) != 0) {
			return 0; /* uncompressed dnames in IXFR store */
		} else if(i+label_size > data_len) {
			return 0;
		} else {
			i += label_size;
		}
	}
	/* after dname, we pass type, class, ttl, rdatalen */
	if(i+10 > data_len)
		return 0;
	i += 8;
	rdlen = read_uint16(data+i);
	i += 2;
	/* pass over the rdata */
	if(i+((size_t)rdlen) > data_len)
		return 0;
	i += ((size_t)rdlen);
	return i-current;
}

/* Copy RRs into packet until packet full, return number RRs added */
static uint16_t ixfr_copy_rrs_into_packet(struct query* query,
	struct pktcompression* pcomp)
{
	uint16_t total_added = 0;

	/* Copy RRs into the packet until the answer is full,
	 * when an RR does not fit, we return and add no more. */

	/* Add first SOA */
	if(query->ixfr_count_newsoa < query->ixfr_end_data->newsoa_len) {
		/* the new SOA is added from the end_data segment, it is
		 * the final SOA of the result of the IXFR */
		if(ixfr_write_rr_pkt(query, query->packet, pcomp,
			query->ixfr_end_data->newsoa,
			query->ixfr_end_data->newsoa_len, total_added)) {
			query->ixfr_count_newsoa = query->ixfr_end_data->newsoa_len;
			total_added++;
			query->ixfr_pos_of_newsoa = buffer_position(query->packet);
		} else {
			/* cannot add another RR, so return */
			return total_added;
		}
	}

	/* Add second SOA */
	if(query->ixfr_count_oldsoa < query->ixfr_data->oldsoa_len) {
		if(ixfr_write_rr_pkt(query, query->packet, pcomp,
			query->ixfr_data->oldsoa,
			query->ixfr_data->oldsoa_len, total_added)) {
			query->ixfr_count_oldsoa = query->ixfr_data->oldsoa_len;
			total_added++;
		} else {
			/* cannot add another RR, so return */
			return total_added;
		}
	}

	/* Add del data, with deleted RRs and a SOA */
	while(query->ixfr_count_del < query->ixfr_data->del_len) {
		size_t rrlen = count_rr_length(query->ixfr_data->del,
			query->ixfr_data->del_len, query->ixfr_count_del);
		if(rrlen && ixfr_write_rr_pkt(query, query->packet, pcomp,
			query->ixfr_data->del + query->ixfr_count_del,
			rrlen, total_added)) {
			query->ixfr_count_del += rrlen;
			total_added++;
		} else {
			/* the next record does not fit in the remaining
			 * space of the packet */
			return total_added;
		}
	}

	/* Add add data, with added RRs and a SOA */
	while(query->ixfr_count_add < query->ixfr_data->add_len) {
		size_t rrlen = count_rr_length(query->ixfr_data->add,
			query->ixfr_data->add_len, query->ixfr_count_add);
		if(rrlen && ixfr_write_rr_pkt(query, query->packet, pcomp,
			query->ixfr_data->add + query->ixfr_count_add,
			rrlen, total_added)) {
			query->ixfr_count_add += rrlen;
			total_added++;
		} else {
			/* the next record does not fit in the remaining
			 * space of the packet */
			return total_added;
		}
	}
	return total_added;
}

query_state_type query_ixfr(struct nsd *nsd, struct query *query)
{
	uint16_t total_added = 0;
	struct pktcompression pcomp;

	if (query->ixfr_is_done)
		return QUERY_PROCESSED;

	pktcompression_init(&pcomp);
	if (query->maxlen > IXFR_MAX_MESSAGE_LEN)
		query->maxlen = IXFR_MAX_MESSAGE_LEN;

	assert(!query_overflow(query));
	/* only keep running values for most packets */
	query->tsig_prepare_it = 0;
	query->tsig_update_it = 1;
	if(query->tsig_sign_it) {
		/* prepare for next updates */
		query->tsig_prepare_it = 1;
		query->tsig_sign_it = 0;
	}

	if (query->ixfr_data == NULL) {
		/* This is the first packet, process the query further */
		uint32_t qserial = 0, current_serial = 0, end_serial = 0;
		struct zone* zone;
		struct ixfr_data* ixfr_data;
		size_t oldpos;

		STATUP(nsd, rixfr);
		/* parse the serial number from the IXFR request */
		oldpos = QHEADERSZ;
		if(!parse_qserial(query->packet, &qserial, &oldpos)) {
			NSCOUNT_SET(query->packet, 0);
			ARCOUNT_SET(query->packet, 0);
			buffer_set_position(query->packet, oldpos);
			RCODE_SET(query->packet, RCODE_FORMAT);
			return QUERY_PROCESSED;
		}
		NSCOUNT_SET(query->packet, 0);
		ARCOUNT_SET(query->packet, 0);
		buffer_set_position(query->packet, oldpos);
		DEBUG(DEBUG_XFRD,1, (LOG_INFO, "ixfr query routine, %s IXFR=%u",
			dname_to_string(query->qname, NULL), (unsigned)qserial));

		/* do we have an IXFR with this serial number? If not, serve AXFR */
		zone = namedb_find_zone(nsd->db, query->qname);
		if(!zone) {
			/* no zone is present */
			RCODE_SET(query->packet, RCODE_NOTAUTH);
			return QUERY_PROCESSED;
		}
		ZTATUP(nsd, zone, rixfr);

		/* if the query is for same or newer serial than our current
		 * serial, then serve a single SOA with our current serial */
		current_serial = zone_get_current_serial(zone);
		if(compare_serial(qserial, current_serial) >= 0) {
			if(!zone->soa_rrset || zone->soa_rrset->rr_count != 1){
				RCODE_SET(query->packet, RCODE_SERVFAIL);
				return QUERY_PROCESSED;
			}
			query_add_compression_domain(query, zone->apex,
				QHEADERSZ);
			if(packet_encode_rr(query, zone->apex,
				&zone->soa_rrset->rrs[0],
				zone->soa_rrset->rrs[0].ttl)) {
				ANCOUNT_SET(query->packet, 1);
			} else {
				RCODE_SET(query->packet, RCODE_SERVFAIL);
			}
			AA_SET(query->packet);
			query_clear_compression_tables(query);
			if(query->tsig.status == TSIG_OK)
				query->tsig_sign_it = 1;
			return QUERY_PROCESSED;
		}

		if(!zone->ixfr) {
			/* we have no ixfr information for the zone, make an AXFR */
			if(query->tsig_prepare_it)
				query->tsig_sign_it = 1;
			VERBOSITY(2, (LOG_INFO, "ixfr fallback to axfr, no ixfr info for zone: %s",
				dname_to_string(query->qname, NULL)));
			return query_axfr(nsd, query, 0);
		}
		ixfr_data = zone_ixfr_find_serial(zone->ixfr, qserial);
		if(!ixfr_data) {
			/* the specific version is not available, make an AXFR */
			if(query->tsig_prepare_it)
				query->tsig_sign_it = 1;
			VERBOSITY(2, (LOG_INFO, "ixfr fallback to axfr, no history for serial for zone: %s",
				dname_to_string(query->qname, NULL)));
			return query_axfr(nsd, query, 0);
		}
		/* see if the IXFRs connect to the next IXFR, and if it ends
		 * at the current served zone, if not, AXFR */
		if(!connect_ixfrs(zone->ixfr, ixfr_data, &end_serial) ||
			end_serial != current_serial) {
			if(query->tsig_prepare_it)
				query->tsig_sign_it = 1;
			VERBOSITY(2, (LOG_INFO, "ixfr fallback to axfr, incomplete history from this serial for zone: %s",
				dname_to_string(query->qname, NULL)));
			return query_axfr(nsd, query, 0);
		}

		query->zone = zone;
		query->ixfr_data = ixfr_data;
		query->ixfr_is_done = 0;
		/* set up to copy the last version's SOA as first SOA */
		query->ixfr_end_data = ixfr_data_last(zone->ixfr);
		query->ixfr_count_newsoa = 0;
		query->ixfr_count_oldsoa = 0;
		query->ixfr_count_del = 0;
		query->ixfr_count_add = 0;
		query->ixfr_pos_of_newsoa = 0;
		/* the query name can be compressed to */
		pktcompression_insert_with_labels(&pcomp,
			buffer_at(query->packet, QHEADERSZ),
			query->qname->name_size, QHEADERSZ);
		if(query->tsig.status == TSIG_OK) {
			query->tsig_sign_it = 1; /* sign first packet in stream */
		}
	} else {
		/*
		 * Query name need not be repeated after the
		 * first response packet.
		 */
		buffer_set_limit(query->packet, QHEADERSZ);
		QDCOUNT_SET(query->packet, 0);
		query_prepare_response(query);
	}

	total_added = ixfr_copy_rrs_into_packet(query, &pcomp);

	while(query->ixfr_count_add >= query->ixfr_data->add_len) {
		struct ixfr_data* next = ixfr_data_next(query->zone->ixfr,
			query->ixfr_data);
		/* finished the ixfr_data */
		if(next) {
			/* move to the next IXFR */
			query->ixfr_data = next;
			/* we need to skip the SOA records, set len to done*/
			/* the newsoa count is already done, at end_data len */
			query->ixfr_count_oldsoa = next->oldsoa_len;
			/* and then set up to copy the del and add sections */
			query->ixfr_count_del = 0;
			query->ixfr_count_add = 0;
			total_added += ixfr_copy_rrs_into_packet(query, &pcomp);
		} else {
			/* we finished the IXFR */
			/* sign the last packet */
			query->tsig_sign_it = 1;
			query->ixfr_is_done = 1;
			break;
		}
	}

	/* return the answer */
	AA_SET(query->packet);
	ANCOUNT_SET(query->packet, total_added);
	NSCOUNT_SET(query->packet, 0);
	ARCOUNT_SET(query->packet, 0);

	if(!query->tcp && !query->ixfr_is_done) {
		TC_SET(query->packet);
		if(query->ixfr_pos_of_newsoa) {
			/* if we recorded the newsoa in the result, snip off
			 * the rest of the response, the RFC1995 response for
			 * when it does not fit is only the latest SOA */
			buffer_set_position(query->packet, query->ixfr_pos_of_newsoa);
			ANCOUNT_SET(query->packet, 1);
		}
		query->ixfr_is_done = 1;
	}

	/* check if it needs tsig signatures */
	if(query->tsig.status == TSIG_OK) {
#if IXFR_TSIG_SIGN_EVERY_NTH > 0
		if(query->tsig.updates_since_last_prepare >= IXFR_TSIG_SIGN_EVERY_NTH) {
#endif
			query->tsig_sign_it = 1;
#if IXFR_TSIG_SIGN_EVERY_NTH > 0
		}
#endif
	}
	pktcompression_freeup(&pcomp);
	return QUERY_IN_IXFR;
}

/* free ixfr_data structure */
static void ixfr_data_free(struct ixfr_data* data)
{
	if(!data)
		return;
	free(data->newsoa);
	free(data->oldsoa);
	free(data->del);
	free(data->add);
	free(data->log_str);
	free(data);
}

size_t ixfr_data_size(struct ixfr_data* data)
{
	return sizeof(struct ixfr_data) + data->newsoa_len + data->oldsoa_len
		+ data->del_len + data->add_len;
}

struct ixfr_store* ixfr_store_start(struct zone* zone,
	struct ixfr_store* ixfr_store_mem)
{
	struct ixfr_store* ixfr_store = ixfr_store_mem;
	memset(ixfr_store, 0, sizeof(*ixfr_store));
	ixfr_store->zone = zone;
	ixfr_store->data = xalloc_zero(sizeof(*ixfr_store->data));
	return ixfr_store;
}

void ixfr_store_cancel(struct ixfr_store* ixfr_store)
{
	ixfr_store->cancelled = 1;
	ixfr_data_free(ixfr_store->data);
	ixfr_store->data = NULL;
}

void ixfr_store_free(struct ixfr_store* ixfr_store)
{
	if(!ixfr_store)
		return;
	ixfr_data_free(ixfr_store->data);
}

/* make space in record data for the new size, grows the allocation */
static void ixfr_rrs_make_space(uint8_t** rrs, size_t* len, size_t* capacity,
	size_t added)
{
	size_t newsize = 0;
	if(*rrs == NULL) {
		newsize = IXFR_STORE_INITIAL_SIZE;
	} else {
		if(*len + added <= *capacity)
			return; /* already enough space */
		newsize = (*capacity)*2;
	}
	if(*len + added > newsize)
		newsize = *len + added;
	if(*rrs == NULL) {
		*rrs = xalloc(newsize);
	} else {
		*rrs = xrealloc(*rrs, newsize);
	}
	*capacity = newsize;
}

/* put new SOA record after delrrs and addrrs */
static void ixfr_put_newsoa(struct ixfr_store* ixfr_store, uint8_t** rrs,
	size_t* len, size_t* capacity)
{
	uint8_t* soa;
	size_t soa_len;
	if(!ixfr_store->data)
		return; /* data should be nonNULL, we are not cancelled */
	soa = ixfr_store->data->newsoa;
	soa_len= ixfr_store->data->newsoa_len;
	ixfr_rrs_make_space(rrs, len, capacity, soa_len);
	if(!*rrs || *len + soa_len > *capacity) {
		log_msg(LOG_ERR, "ixfr_store addrr: cannot allocate space");
		ixfr_store_cancel(ixfr_store);
		return;
	}
	memmove(*rrs + *len, soa, soa_len);
	*len += soa_len;
}

/* trim unused storage from the rrs data */
static void ixfr_trim_capacity(uint8_t** rrs, size_t* len, size_t* capacity)
{
	if(*rrs == NULL)
		return;
	if(*capacity == *len)
		return;
	*rrs = xrealloc(*rrs, *len);
	*capacity = *len;
}

void ixfr_store_finish_data(struct ixfr_store* ixfr_store)
{
	if(ixfr_store->data_trimmed)
		return;
	ixfr_store->data_trimmed = 1;

	/* put new serial SOA record after delrrs and addrrs */
	ixfr_put_newsoa(ixfr_store, &ixfr_store->data->del,
		&ixfr_store->data->del_len, &ixfr_store->del_capacity);
	ixfr_put_newsoa(ixfr_store, &ixfr_store->data->add,
		&ixfr_store->data->add_len, &ixfr_store->add_capacity);

	/* trim the data in the store, the overhead from capacity is
	 * removed */
	if(!ixfr_store->data)
		return; /* data should be nonNULL, we are not cancelled */
	ixfr_trim_capacity(&ixfr_store->data->del,
		&ixfr_store->data->del_len, &ixfr_store->del_capacity);
	ixfr_trim_capacity(&ixfr_store->data->add,
		&ixfr_store->data->add_len, &ixfr_store->add_capacity);
}

void ixfr_store_finish(struct ixfr_store* ixfr_store, struct nsd* nsd,
	char* log_buf)
{
	if(ixfr_store->cancelled) {
		ixfr_store_free(ixfr_store);
		return;
	}

	ixfr_store_finish_data(ixfr_store);

	if(ixfr_store->cancelled) {
		ixfr_store_free(ixfr_store);
		return;
	}

	if(log_buf && !ixfr_store->data->log_str)
		ixfr_store->data->log_str = strdup(log_buf);

	/* store the data in the zone */
	if(!ixfr_store->zone->ixfr)
		ixfr_store->zone->ixfr = zone_ixfr_create(nsd);
	zone_ixfr_make_space(ixfr_store->zone->ixfr, ixfr_store->zone,
		ixfr_store->data, ixfr_store);
	if(ixfr_store->cancelled) {
		ixfr_store_free(ixfr_store);
		return;
	}
	zone_ixfr_add(ixfr_store->zone->ixfr, ixfr_store->data, 1);
	ixfr_store->data = NULL;

	/* free structure */
	ixfr_store_free(ixfr_store);
}

/* read SOA rdata section for SOA storage */
static int read_soa_rdata(struct buffer* packet, uint8_t* primns,
	int* primns_len, uint8_t* email, int* email_len,
	uint32_t* serial, uint32_t* refresh, uint32_t* retry,
	uint32_t* expire, uint32_t* minimum, size_t* sz)
{
	if(!(*primns_len = dname_make_wire_from_packet(primns, packet, 1))) {
		log_msg(LOG_ERR, "ixfr_store: cannot parse soa nsname in packet");
		return 0;
	}
	*sz += *primns_len;
	if(!(*email_len = dname_make_wire_from_packet(email, packet, 1))) {
		log_msg(LOG_ERR, "ixfr_store: cannot parse soa maintname in packet");
		return 0;
	}
	*sz += *email_len;
	*serial = buffer_read_u32(packet);
	*sz += 4;
	*refresh = buffer_read_u32(packet);
	*sz += 4;
	*retry = buffer_read_u32(packet);
	*sz += 4;
	*expire = buffer_read_u32(packet);
	*sz += 4;
	*minimum = buffer_read_u32(packet);
	*sz += 4;
	return 1;
}

/* store SOA record data in memory buffer */
static void store_soa(uint8_t* soa, struct zone* zone, uint32_t ttl,
	uint16_t rdlen_uncompressed, uint8_t* primns, int primns_len,
	uint8_t* email, int email_len, uint32_t serial, uint32_t refresh,
	uint32_t retry, uint32_t expire, uint32_t minimum)
{
	uint8_t* sp = soa;
	memmove(sp, dname_name(domain_dname(zone->apex)),
		domain_dname(zone->apex)->name_size);
	sp += domain_dname(zone->apex)->name_size;
	write_uint16(sp, TYPE_SOA);
	sp += 2;
	write_uint16(sp, CLASS_IN);
	sp += 2;
	write_uint32(sp, ttl);
	sp += 4;
	write_uint16(sp, rdlen_uncompressed);
	sp += 2;
	memmove(sp, primns, primns_len);
	sp += primns_len;
	memmove(sp, email, email_len);
	sp += email_len;
	write_uint32(sp, serial);
	sp += 4;
	write_uint32(sp, refresh);
	sp += 4;
	write_uint32(sp, retry);
	sp += 4;
	write_uint32(sp, expire);
	sp += 4;
	write_uint32(sp, minimum);
}

void ixfr_store_add_newsoa(struct ixfr_store* ixfr_store, uint32_t ttl,
	struct buffer* packet, size_t rrlen)
{
	size_t oldpos, sz = 0;
	uint32_t serial, refresh, retry, expire, minimum;
	uint16_t rdlen_uncompressed;
	int primns_len = 0, email_len = 0;
	uint8_t primns[MAXDOMAINLEN + 1], email[MAXDOMAINLEN + 1];

	if(ixfr_store->cancelled)
		return;
	if(ixfr_store->data->newsoa) {
		free(ixfr_store->data->newsoa);
		ixfr_store->data->newsoa = NULL;
		ixfr_store->data->newsoa_len = 0;
	}
	oldpos = buffer_position(packet);

	/* calculate the length */
	sz = domain_dname(ixfr_store->zone->apex)->name_size;
	sz += 2 /* type */ + 2 /* class */ + 4 /* ttl */ + 2 /* rdlen */;
	if(!buffer_available(packet, rrlen)) {
		/* not possible already parsed, but fail nicely anyway */
		log_msg(LOG_ERR, "ixfr_store: not enough rdata space in packet");
		ixfr_store_cancel(ixfr_store);
		buffer_set_position(packet, oldpos);
		return;
	}
	if(!read_soa_rdata(packet, primns, &primns_len, email, &email_len,
		&serial, &refresh, &retry, &expire, &minimum, &sz)) {
		log_msg(LOG_ERR, "ixfr_store newsoa: cannot parse packet");
		ixfr_store_cancel(ixfr_store);
		buffer_set_position(packet, oldpos);
		return;
	}
	rdlen_uncompressed = primns_len + email_len + 4 + 4 + 4 + 4 + 4;

	ixfr_store->data->newserial = serial;

	/* store the soa record */
	ixfr_store->data->newsoa = xalloc(sz);
	ixfr_store->data->newsoa_len = sz;
	store_soa(ixfr_store->data->newsoa, ixfr_store->zone, ttl,
		rdlen_uncompressed, primns, primns_len, email, email_len,
		serial, refresh, retry, expire, minimum);

	buffer_set_position(packet, oldpos);
}

void ixfr_store_add_oldsoa(struct ixfr_store* ixfr_store, uint32_t ttl,
	struct buffer* packet, size_t rrlen)
{
	size_t oldpos, sz = 0;
	uint32_t serial, refresh, retry, expire, minimum;
	uint16_t rdlen_uncompressed;
	int primns_len = 0, email_len = 0;
	uint8_t primns[MAXDOMAINLEN + 1], email[MAXDOMAINLEN + 1];

	if(ixfr_store->cancelled)
		return;
	if(ixfr_store->data->oldsoa) {
		free(ixfr_store->data->oldsoa);
		ixfr_store->data->oldsoa = NULL;
		ixfr_store->data->oldsoa_len = 0;
	}
	/* we have the old SOA and thus we are sure it is an IXFR, make space*/
	zone_ixfr_make_space(ixfr_store->zone->ixfr, ixfr_store->zone,
		ixfr_store->data, ixfr_store);
	if(ixfr_store->cancelled)
		return;
	oldpos = buffer_position(packet);

	/* calculate the length */
	sz = domain_dname(ixfr_store->zone->apex)->name_size;
	sz += 2 /*type*/ + 2 /*class*/ + 4 /*ttl*/ + 2 /*rdlen*/;
	if(!buffer_available(packet, rrlen)) {
		/* not possible already parsed, but fail nicely anyway */
		log_msg(LOG_ERR, "ixfr_store oldsoa: not enough rdata space in packet");
		ixfr_store_cancel(ixfr_store);
		buffer_set_position(packet, oldpos);
		return;
	}
	if(!read_soa_rdata(packet, primns, &primns_len, email, &email_len,
		&serial, &refresh, &retry, &expire, &minimum, &sz)) {
		log_msg(LOG_ERR, "ixfr_store oldsoa: cannot parse packet");
		ixfr_store_cancel(ixfr_store);
		buffer_set_position(packet, oldpos);
		return;
	}
	rdlen_uncompressed = primns_len + email_len + 4 + 4 + 4 + 4 + 4;

	ixfr_store->data->oldserial = serial;

	/* store the soa record */
	ixfr_store->data->oldsoa = xalloc(sz);
	ixfr_store->data->oldsoa_len = sz;
	store_soa(ixfr_store->data->oldsoa, ixfr_store->zone, ttl,
		rdlen_uncompressed, primns, primns_len, email, email_len,
		serial, refresh, retry, expire, minimum);

	buffer_set_position(packet, oldpos);
}

/* store RR in data segment */
static int ixfr_putrr(const struct dname* dname, uint16_t type, uint16_t klass,
	uint32_t ttl, rdata_atom_type* rdatas, ssize_t rdata_num,
	uint8_t** rrs, size_t* rrs_len, size_t* rrs_capacity)
{
	size_t rdlen_uncompressed, sz;
	uint8_t* sp;
	int i;

	/* find rdatalen */
	rdlen_uncompressed = 0;
	for(i=0; i<rdata_num; i++) {
		if(rdata_atom_is_domain(type, i)) {
			rdlen_uncompressed += domain_dname(rdatas[i].domain)
				->name_size;
		} else {
			rdlen_uncompressed += rdatas[i].data[0];
		}
	}
	sz = dname->name_size + 2 /*type*/ + 2 /*class*/ + 4 /*ttl*/ +
		2 /*rdlen*/ + rdlen_uncompressed;

	/* store RR in IXFR data */
	ixfr_rrs_make_space(rrs, rrs_len, rrs_capacity, sz);
	if(!*rrs || *rrs_len + sz > *rrs_capacity) {
		return 0;
	}
	/* copy data into add */
	sp = *rrs + *rrs_len;
	*rrs_len += sz;
	memmove(sp, dname_name(dname), dname->name_size);
	sp += dname->name_size;
	write_uint16(sp, type);
	sp += 2;
	write_uint16(sp, klass);
	sp += 2;
	write_uint32(sp, ttl);
	sp += 4;
	write_uint16(sp, rdlen_uncompressed);
	sp += 2;
	for(i=0; i<rdata_num; i++) {
		if(rdata_atom_is_domain(type, i)) {
			memmove(sp, dname_name(domain_dname(rdatas[i].domain)),
				domain_dname(rdatas[i].domain)->name_size);
			sp += domain_dname(rdatas[i].domain)->name_size;
		} else {
			memmove(sp, &rdatas[i].data[1], rdatas[i].data[0]);
			sp += rdatas[i].data[0];
		}
	}
	return 1;
}

void ixfr_store_putrr(struct ixfr_store* ixfr_store, const struct dname* dname,
	uint16_t type, uint16_t klass, uint32_t ttl, struct buffer* packet,
	uint16_t rrlen, struct region* temp_region, uint8_t** rrs,
	size_t* rrs_len, size_t* rrs_capacity)
{
	domain_table_type *temptable;
	rdata_atom_type *rdatas;
	ssize_t rdata_num;
	size_t oldpos;

	if(ixfr_store->cancelled)
		return;

	/* The SOA data is stored with separate calls. And then appended
	 * during the finish operation. We do not have to store it here
	 * when called from difffile's IXFR processing with type SOA. */
	if(type == TYPE_SOA)
		return;
	/* make space for these RRs we have now; basically once we
	 * grow beyond the current allowed amount an older IXFR is deleted. */
	zone_ixfr_make_space(ixfr_store->zone->ixfr, ixfr_store->zone,
		ixfr_store->data, ixfr_store);
	if(ixfr_store->cancelled)
		return;

	/* parse rdata */
	oldpos = buffer_position(packet);
	temptable = domain_table_create(temp_region);
	rdata_num = rdata_wireformat_to_rdata_atoms(temp_region, temptable,
		type, rrlen, packet, &rdatas);
	buffer_set_position(packet, oldpos);
	if(rdata_num == -1) {
		log_msg(LOG_ERR, "ixfr_store addrr: cannot parse packet");
		ixfr_store_cancel(ixfr_store);
		return;
	}

	if(!ixfr_putrr(dname, type, klass, ttl, rdatas, rdata_num,
		rrs, rrs_len, rrs_capacity)) {
		log_msg(LOG_ERR, "ixfr_store addrr: cannot allocate space");
		ixfr_store_cancel(ixfr_store);
		return;
	}
}

void ixfr_store_delrr(struct ixfr_store* ixfr_store, const struct dname* dname,
	uint16_t type, uint16_t klass, uint32_t ttl, struct buffer* packet,
	uint16_t rrlen, struct region* temp_region)
{
	if(ixfr_store->cancelled)
		return;
	ixfr_store_putrr(ixfr_store, dname, type, klass, ttl, packet, rrlen,
		temp_region, &ixfr_store->data->del,
		&ixfr_store->data->del_len, &ixfr_store->del_capacity);
}

void ixfr_store_addrr(struct ixfr_store* ixfr_store, const struct dname* dname,
	uint16_t type, uint16_t klass, uint32_t ttl, struct buffer* packet,
	uint16_t rrlen, struct region* temp_region)
{
	if(ixfr_store->cancelled)
		return;
	ixfr_store_putrr(ixfr_store, dname, type, klass, ttl, packet, rrlen,
		temp_region, &ixfr_store->data->add,
		&ixfr_store->data->add_len, &ixfr_store->add_capacity);
}

int ixfr_store_addrr_rdatas(struct ixfr_store* ixfr_store,
	const struct dname* dname, uint16_t type, uint16_t klass,
	uint32_t ttl, rdata_atom_type* rdatas, ssize_t rdata_num)
{
	if(ixfr_store->cancelled)
		return 1;
	if(type == TYPE_SOA)
		return 1;
	return ixfr_putrr(dname, type, klass, ttl, rdatas, rdata_num,
		&ixfr_store->data->add, &ixfr_store->data->add_len,
		&ixfr_store->add_capacity);
}

int ixfr_store_add_newsoa_rdatas(struct ixfr_store* ixfr_store,
	const struct dname* dname, uint16_t type, uint16_t klass,
	uint32_t ttl, rdata_atom_type* rdatas, ssize_t rdata_num)
{
	size_t capacity = 0;
	uint32_t serial;
	if(ixfr_store->cancelled)
		return 1;
	if(rdata_num < 2 || rdata_atom_size(rdatas[2]) < 4)
		return 0;
	memcpy(&serial, rdata_atom_data(rdatas[2]), sizeof(serial));
	ixfr_store->data->newserial = ntohl(serial);
	if(!ixfr_putrr(dname, type, klass, ttl, rdatas, rdata_num,
		&ixfr_store->data->newsoa, &ixfr_store->data->newsoa_len,
		&ixfr_store->add_capacity))
		return 0;
	ixfr_trim_capacity(&ixfr_store->data->newsoa,
		&ixfr_store->data->newsoa_len, &capacity);
	return 1;
}

/* store rr uncompressed */
int ixfr_storerr_uncompressed(uint8_t* dname, size_t dname_len, uint16_t type,
	uint16_t klass, uint32_t ttl, uint8_t* rdata, size_t rdata_len,
	uint8_t** rrs, size_t* rrs_len, size_t* rrs_capacity)
{
	size_t sz;
	uint8_t* sp;

	/* find rdatalen */
	sz = dname_len + 2 /*type*/ + 2 /*class*/ + 4 /*ttl*/ +
		2 /*rdlen*/ + rdata_len;

	/* store RR in IXFR data */
	ixfr_rrs_make_space(rrs, rrs_len, rrs_capacity, sz);
	if(!*rrs || *rrs_len + sz > *rrs_capacity) {
		return 0;
	}
	/* copy data into add */
	sp = *rrs + *rrs_len;
	*rrs_len += sz;
	memmove(sp, dname, dname_len);
	sp += dname_len;
	write_uint16(sp, type);
	sp += 2;
	write_uint16(sp, klass);
	sp += 2;
	write_uint32(sp, ttl);
	sp += 4;
	write_uint16(sp, rdata_len);
	sp += 2;
	memmove(sp, rdata, rdata_len);
	return 1;
}

int ixfr_store_delrr_uncompressed(struct ixfr_store* ixfr_store,
	uint8_t* dname, size_t dname_len, uint16_t type, uint16_t klass,
	uint32_t ttl, uint8_t* rdata, size_t rdata_len)
{
	if(ixfr_store->cancelled)
		return 1;
	if(type == TYPE_SOA)
		return 1;
	return ixfr_storerr_uncompressed(dname, dname_len, type, klass,
		ttl, rdata, rdata_len, &ixfr_store->data->del,
		&ixfr_store->data->del_len, &ixfr_store->del_capacity);
}

static size_t skip_dname(uint8_t* rdata, size_t rdata_len)
{
	for (size_t index=0; index < rdata_len; ) {
		uint8_t label_size = rdata[index];
		if (label_size == 0) {
			return index + 1;
		} else if ((label_size & 0xc0) != 0) {
			return (index + 1 < rdata_len) ? index + 2 : 0;
		} else {
			/* loop breaks if index exceeds rdata_len */
			index += label_size + 1;
		}
	}

	return 0;
}

int ixfr_store_oldsoa_uncompressed(struct ixfr_store* ixfr_store,
	uint8_t* dname, size_t dname_len, uint16_t type, uint16_t klass,
	uint32_t ttl, uint8_t* rdata, size_t rdata_len)
{
	size_t capacity = 0;
	if(ixfr_store->cancelled)
		return 1;
	if(!ixfr_storerr_uncompressed(dname, dname_len, type, klass,
		ttl, rdata, rdata_len, &ixfr_store->data->oldsoa,
		&ixfr_store->data->oldsoa_len, &capacity))
		return 0;
	{
		uint32_t serial;
		size_t index, count = 0;
		if (!(count = skip_dname(rdata, rdata_len)))
			return 0;
		index = count;
		if (!(count = skip_dname(rdata+index, rdata_len-index)))
			return 0;
		index += count;
		if (rdata_len - index < 4)
			return 0;
		memcpy(&serial, rdata+index, sizeof(serial));
		ixfr_store->data->oldserial = ntohl(serial);
	}
	ixfr_trim_capacity(&ixfr_store->data->oldsoa,
		&ixfr_store->data->oldsoa_len, &capacity);
	return 1;
}

int zone_is_ixfr_enabled(struct zone* zone)
{
	return zone->opts->pattern->store_ixfr;
}

/* compare ixfr elements */
static int ixfrcompare(const void* x, const void* y)
{
	uint32_t* serial_x = (uint32_t*)x;
	uint32_t* serial_y = (uint32_t*)y;
	if(*serial_x < *serial_y)
		return -1;
	if(*serial_x > *serial_y)
		return 1;
	return 0;
}

struct zone_ixfr* zone_ixfr_create(struct nsd* nsd)
{
	struct zone_ixfr* ixfr = xalloc_zero(sizeof(struct zone_ixfr));
	ixfr->data = rbtree_create(nsd->region, &ixfrcompare);
	return ixfr;
}

/* traverse tree postorder */
static void ixfr_tree_del(struct rbnode* node)
{
	if(node == NULL || node == RBTREE_NULL)
		return;
	ixfr_tree_del(node->left);
	ixfr_tree_del(node->right);
	ixfr_data_free((struct ixfr_data*)node);
}

/* clear the ixfr data elements */
static void zone_ixfr_clear(struct zone_ixfr* ixfr)
{
	if(!ixfr)
		return;
	if(ixfr->data) {
		ixfr_tree_del(ixfr->data->root);
		ixfr->data->root = RBTREE_NULL;
		ixfr->data->count = 0;
	}
	ixfr->total_size = 0;
	ixfr->oldest_serial = 0;
	ixfr->newest_serial = 0;
}

void zone_ixfr_free(struct zone_ixfr* ixfr)
{
	if(!ixfr)
		return;
	if(ixfr->data) {
		ixfr_tree_del(ixfr->data->root);
		ixfr->data = NULL;
	}
	free(ixfr);
}

void ixfr_store_delixfrs(struct zone* zone)
{
	if(!zone)
		return;
	zone_ixfr_clear(zone->ixfr);
}

/* remove the oldest data entry from the ixfr versions */
static void zone_ixfr_remove_oldest(struct zone_ixfr* ixfr)
{
	if(ixfr->data->count > 0) {
		struct ixfr_data* oldest = ixfr_data_first(ixfr);
		if(ixfr->oldest_serial == oldest->oldserial) {
			if(ixfr->data->count > 1) {
				struct ixfr_data* next = ixfr_data_next(ixfr, oldest);
				assert(next);
				if(next)
					ixfr->oldest_serial = next->oldserial;
				else 	ixfr->oldest_serial = oldest->newserial;
			} else {
				ixfr->oldest_serial = 0;
			}
		}
		if(ixfr->newest_serial == oldest->oldserial) {
			ixfr->newest_serial = 0;
		}
		zone_ixfr_remove(ixfr, oldest);
	}
}

void zone_ixfr_make_space(struct zone_ixfr* ixfr, struct zone* zone,
	struct ixfr_data* data, struct ixfr_store* ixfr_store)
{
	size_t addsize;
	if(!ixfr || !data)
		return;
	if(zone->opts->pattern->ixfr_number == 0) {
		ixfr_store_cancel(ixfr_store);
		return;
	}

	/* Check the number of IXFRs allowed for this zone, if too many,
	 * shorten the number to make space for another one */
	while(ixfr->data->count >= zone->opts->pattern->ixfr_number) {
		zone_ixfr_remove_oldest(ixfr);
	}

	if(zone->opts->pattern->ixfr_size == 0) {
		/* no size limits imposed */
		return;
	}

	/* Check the size of the current added data element 'data', and
	 * see if that overflows the maximum storage size for IXFRs for
	 * this zone, and if so, delete the oldest IXFR to make space */
	addsize = ixfr_data_size(data);
	while(ixfr->data->count > 0 && ixfr->total_size + addsize >
		zone->opts->pattern->ixfr_size) {
		zone_ixfr_remove_oldest(ixfr);
	}

	/* if deleting the oldest elements does not work, then this
	 * IXFR is too big to store and we cancel it */
	if(ixfr->data->count == 0 && ixfr->total_size + addsize >
		zone->opts->pattern->ixfr_size) {
		ixfr_store_cancel(ixfr_store);
		return;
	}
}

void zone_ixfr_remove(struct zone_ixfr* ixfr, struct ixfr_data* data)
{
	rbtree_delete(ixfr->data, data->node.key);
	ixfr->total_size -= ixfr_data_size(data);
	ixfr_data_free(data);
}

void zone_ixfr_add(struct zone_ixfr* ixfr, struct ixfr_data* data, int isnew)
{
	memset(&data->node, 0, sizeof(data->node));
	if(ixfr->data->count == 0) {
		ixfr->oldest_serial = data->oldserial;
		ixfr->newest_serial = data->oldserial;
	} else if(isnew) {
		/* newest entry is last there is */
		ixfr->newest_serial = data->oldserial;
	} else {
		/* added older entry, before the others */
		ixfr->oldest_serial = data->oldserial;
	}
	data->node.key = &data->oldserial;
	rbtree_insert(ixfr->data, &data->node);
	ixfr->total_size += ixfr_data_size(data);
}

struct ixfr_data* zone_ixfr_find_serial(struct zone_ixfr* ixfr,
	uint32_t qserial)
{
	struct ixfr_data* data;
	if(!ixfr)
		return NULL;
	if(!ixfr->data)
		return NULL;
	data = (struct ixfr_data*)rbtree_search(ixfr->data, &qserial);
	if(data) {
		assert(data->oldserial == qserial);
		return data;
	}
	/* not found */
	return NULL;
}

/* calculate the number of files we want */
static int ixfr_target_number_files(struct zone* zone)
{
	int dest_num_files;
	if(!zone->ixfr || !zone->ixfr->data)
		return 0;
	if(!zone_is_ixfr_enabled(zone))
		return 0;
	/* if we store ixfr, it is the configured number of files */
	dest_num_files = (int)zone->opts->pattern->ixfr_number;
	/* but if the number of available transfers is smaller, store less */
	if(dest_num_files > (int)zone->ixfr->data->count)
		dest_num_files = (int)zone->ixfr->data->count;
	return dest_num_files;
}

/* create ixfrfile name in buffer for file_num. The num is 1 .. number. */
static void make_ixfr_name(char* buf, size_t len, const char* zfile,
	int file_num)
{
	if(file_num == 1)
		snprintf(buf, len, "%s.ixfr", zfile);
	else snprintf(buf, len, "%s.ixfr.%d", zfile, file_num);
}

/* create temp ixfrfile name in buffer for file_num. The num is 1 .. number. */
static void make_ixfr_name_temp(char* buf, size_t len, const char* zfile,
	int file_num, int temp)
{
	if(file_num == 1)
		snprintf(buf, len, "%s.ixfr%s", zfile, (temp?".temp":""));
	else snprintf(buf, len, "%s.ixfr.%d%s", zfile, file_num,
		(temp?".temp":""));
}

/* see if ixfr file exists */
static int ixfr_file_exists_ctmp(const char* zfile, int file_num, int temp)
{
	struct stat statbuf;
	char ixfrfile[1024+24];
	make_ixfr_name_temp(ixfrfile, sizeof(ixfrfile), zfile, file_num, temp);
	memset(&statbuf, 0, sizeof(statbuf));
	if(stat(ixfrfile, &statbuf) < 0) {
		if(errno == ENOENT)
			return 0;
		/* file is not usable */
		return 0;
	}
	return 1;
}

int ixfr_file_exists(const char* zfile, int file_num)
{
	return ixfr_file_exists_ctmp(zfile, file_num, 0);
}

/* see if ixfr file exists */
static int ixfr_file_exists_temp(const char* zfile, int file_num)
{
	return ixfr_file_exists_ctmp(zfile, file_num, 1);
}

/* unlink an ixfr file */
static int ixfr_unlink_it_ctmp(const char* zname, const char* zfile,
	int file_num, int silent_enoent, int temp)
{
	char ixfrfile[1024+24];
	make_ixfr_name_temp(ixfrfile, sizeof(ixfrfile), zfile, file_num, temp);
	VERBOSITY(3, (LOG_INFO, "delete zone %s IXFR data file %s",
		zname, ixfrfile));
	if(unlink(ixfrfile) < 0) {
		if(silent_enoent && errno == ENOENT)
			return 0;
		log_msg(LOG_ERR, "error to delete file %s: %s", ixfrfile,
			strerror(errno));
		return 0;
	}
	return 1;
}

int ixfr_unlink_it(const char* zname, const char* zfile, int file_num,
	int silent_enoent)
{
	return ixfr_unlink_it_ctmp(zname, zfile, file_num, silent_enoent, 0);
}

/* unlink an ixfr file */
static int ixfr_unlink_it_temp(const char* zname, const char* zfile,
	int file_num, int silent_enoent)
{
	return ixfr_unlink_it_ctmp(zname, zfile, file_num, silent_enoent, 1);
}

/* read ixfr file header */
int ixfr_read_file_header(const char* zname, const char* zfile,
	int file_num, uint32_t* oldserial, uint32_t* newserial,
	size_t* data_size, int enoent_is_err)
{
	char ixfrfile[1024+24];
	char buf[1024];
	FILE* in;
	int num_lines = 0, got_old = 0, got_new = 0, got_datasize = 0;
	make_ixfr_name(ixfrfile, sizeof(ixfrfile), zfile, file_num);
	in = fopen(ixfrfile, "r");
	if(!in) {
		if((errno == ENOENT && enoent_is_err) || (errno != ENOENT))
			log_msg(LOG_ERR, "could not open %s: %s", ixfrfile,
				strerror(errno));
		return 0;
	}
	/* read about 10 lines, this is where the header is */
	while(!(got_old && got_new && got_datasize) && num_lines < 10) {
		buf[0]=0;
		buf[sizeof(buf)-1]=0;
		if(!fgets(buf, sizeof(buf), in)) {
			log_msg(LOG_ERR, "could not read %s: %s", ixfrfile,
				strerror(errno));
			fclose(in);
			return 0;
		}
		num_lines++;
		if(buf[0]!=0 && buf[strlen(buf)-1]=='\n')
			buf[strlen(buf)-1]=0;
		if(strncmp(buf, "; zone ", 7) == 0) {
			if(strcmp(buf+7, zname) != 0) {
				log_msg(LOG_ERR, "file has wrong zone, expected zone %s, but found %s in file %s",
					zname, buf+7, ixfrfile);
				fclose(in);
				return 0;
			}
		} else if(strncmp(buf, "; from_serial ", 14) == 0) {
			*oldserial = atoi(buf+14);
			got_old = 1;
		} else if(strncmp(buf, "; to_serial ", 12) == 0) {
			*newserial = atoi(buf+12);
			got_new = 1;
		} else if(strncmp(buf, "; data_size ", 12) == 0) {
			*data_size = (size_t)atoi(buf+12);
			got_datasize = 1;
		}
	}
	fclose(in);
	if(!got_old)
		return 0;
	if(!got_new)
		return 0;
	if(!got_datasize)
		return 0;
	return 1;
}

/* delete rest ixfr files, that are after the current item */
static void ixfr_delete_rest_files(struct zone* zone, struct ixfr_data* from,
	const char* zfile, int temp)
{
	size_t prevcount = 0;
	struct ixfr_data* data = from;
	while(data) {
		if(data->file_num != 0) {
			(void)ixfr_unlink_it_ctmp(zone->opts->name, zfile,
				data->file_num, 0, temp);
			data->file_num = 0;
		}
		data = ixfr_data_prev(zone->ixfr, data, &prevcount);
	}
}

void ixfr_delete_superfluous_files(struct zone* zone, const char* zfile,
	int dest_num_files)
{
	int i = dest_num_files + 1;
	if(!ixfr_file_exists(zfile, i))
		return;
	while(ixfr_unlink_it(zone->opts->name, zfile, i, 1)) {
		i++;
	}
}

int ixfr_rename_it(const char* zname, const char* zfile, int oldnum,
	int oldtemp, int newnum, int newtemp)
{
	char ixfrfile_old[1024+24];
	char ixfrfile_new[1024+24];
	make_ixfr_name_temp(ixfrfile_old, sizeof(ixfrfile_old), zfile, oldnum,
		oldtemp);
	make_ixfr_name_temp(ixfrfile_new, sizeof(ixfrfile_new), zfile, newnum,
		newtemp);
	VERBOSITY(3, (LOG_INFO, "rename zone %s IXFR data file %s to %s",
		zname, ixfrfile_old, ixfrfile_new));
	if(rename(ixfrfile_old, ixfrfile_new) < 0) {
		log_msg(LOG_ERR, "error to rename file %s: %s", ixfrfile_old,
			strerror(errno));
		return 0;
	}
	return 1;
}

/* delete if we have too many items in memory */
static void ixfr_delete_memory_items(struct zone* zone, int dest_num_files)
{
	if(!zone->ixfr || !zone->ixfr->data)
		return;
	if(dest_num_files == (int)zone->ixfr->data->count)
		return;
	if(dest_num_files > (int)zone->ixfr->data->count) {
		/* impossible, dest_num_files should be smaller */
		return;
	}

	/* delete oldest ixfr, until we have dest_num_files entries */
	while(dest_num_files < (int)zone->ixfr->data->count) {
		zone_ixfr_remove_oldest(zone->ixfr);
	}
}

/* rename the ixfr files that need to change name */
static int ixfr_rename_files(struct zone* zone, const char* zfile,
	int dest_num_files)
{
	struct ixfr_data* data, *startspot = NULL;
	size_t prevcount = 0;
	int destnum;
	if(!zone->ixfr || !zone->ixfr->data)
		return 1;

	/* the oldest file is at the largest number */
	data = ixfr_data_first(zone->ixfr);
	destnum = dest_num_files;
	if(!data)
		return 1; /* nothing to do */
	if(data->file_num == destnum)
		return 1; /* nothing to do for rename */

	/* rename the files to temporary files, because otherwise the
	 * items would overwrite each other when the list touches itself.
	 * On fail, the temporary files are removed and we end up with
	 * the newly written data plus the remaining files, in order.
	 * Thus, start the temporary rename at the oldest, then rename
	 * to the final names starting from the newest. */
	while(data && data->file_num != 0) {
		/* if existing file at temporary name, delete that */
		if(ixfr_file_exists_temp(zfile, data->file_num)) {
			(void)ixfr_unlink_it_temp(zone->opts->name, zfile,
				data->file_num, 0);
		}

		/* rename to temporary name */
		if(!ixfr_rename_it(zone->opts->name, zfile, data->file_num, 0,
			data->file_num, 1)) {
			/* failure, we cannot store files */
			/* delete the renamed files */
			ixfr_delete_rest_files(zone, data, zfile, 1);
			return 0;
		}

		/* the next cycle should start at the newest file that
		 * has been renamed to a temporary name */
		startspot = data;
		data = ixfr_data_next(zone->ixfr, data);
		destnum--;
	}

	/* rename the files to their final name position */
	data = startspot;
	while(data && data->file_num != 0) {
		destnum++;

		/* if there is an existing file, delete it */
		if(ixfr_file_exists(zfile, destnum)) {
			(void)ixfr_unlink_it(zone->opts->name, zfile,
				destnum, 0);
		}

		if(!ixfr_rename_it(zone->opts->name, zfile, data->file_num, 1, destnum, 0)) {
			/* failure, we cannot store files */
			ixfr_delete_rest_files(zone, data, zfile, 1);
			/* delete the previously renamed files, so in
			 * memory stays as is, on disk we have the current
			 * item (and newer transfers) okay. */
			return 0;
		}
		data->file_num = destnum;

		data = ixfr_data_prev(zone->ixfr, data, &prevcount);
	}
	return 1;
}

/* write the ixfr data file header */
static int ixfr_write_file_header(struct zone* zone, struct ixfr_data* data,
	FILE* out)
{
	if(!fprintf(out, "; IXFR data file\n"))
		return 0;
	if(!fprintf(out, "; zone %s\n", zone->opts->name))
		return 0;
	if(!fprintf(out, "; from_serial %u\n", (unsigned)data->oldserial))
		return 0;
	if(!fprintf(out, "; to_serial %u\n", (unsigned)data->newserial))
		return 0;
	if(!fprintf(out, "; data_size %u\n", (unsigned)ixfr_data_size(data)))
		return 0;
	if(data->log_str) {
		if(!fprintf(out, "; %s\n", data->log_str))
			return 0;
	}
	return 1;
}

/* print rdata on one line */
static int
oneline_print_rdata(buffer_type *output, rrtype_descriptor_type *descriptor,
	rr_type* record)
{
	size_t i;
	size_t saved_position = buffer_position(output);

	for (i = 0; i < record->rdata_count; ++i) {
		if (i == 0) {
			buffer_printf(output, "\t");
		} else {
			buffer_printf(output, " ");
		}
		if (!rdata_atom_to_string(
			    output,
			    (rdata_zoneformat_type) descriptor->zoneformat[i],
			    record->rdatas[i], record))
		{
			buffer_set_position(output, saved_position);
			return 0;
		}
	}

	return 1;
}

/* parse wireformat RR into a struct RR in temp region */
static int parse_wirerr_into_temp(struct zone* zone, char* fname,
	struct region* temp, uint8_t* buf, size_t len,
	const dname_type** dname, struct rr* rr)
{
	size_t bufpos = 0;
	uint16_t rdlen;
	ssize_t rdata_num;
	buffer_type packet;
	domain_table_type* owners;
	owners = domain_table_create(temp);
	memset(rr, 0, sizeof(*rr));
	*dname = dname_make(temp, buf, 1);
	if(!*dname) {
		log_msg(LOG_ERR, "failed to write zone %s IXFR data %s: failed to parse dname", zone->opts->name, fname);
		return 0;
	}
	bufpos = (*dname)->name_size;
	if(bufpos+10 > len) {
		log_msg(LOG_ERR, "failed to write zone %s IXFR data %s: buffer too short", zone->opts->name, fname);
		return 0;
	}
	rr->type = read_uint16(buf+bufpos);
	bufpos += 2;
	rr->klass = read_uint16(buf+bufpos);
	bufpos += 2;
	rr->ttl = read_uint32(buf+bufpos);
	bufpos += 4;
	rdlen = read_uint16(buf+bufpos);
	bufpos += 2;
	if(bufpos + rdlen > len) {
		log_msg(LOG_ERR, "failed to write zone %s IXFR data %s: buffer too short for rdatalen", zone->opts->name, fname);
		return 0;
	}
	buffer_create_from(&packet, buf+bufpos, rdlen);
	rdata_num = rdata_wireformat_to_rdata_atoms(
		temp, owners, rr->type, rdlen, &packet, &rr->rdatas);
	if(rdata_num == -1) {
		log_msg(LOG_ERR, "failed to write zone %s IXFR data %s: cannot parse rdata", zone->opts->name, fname);
		return 0;
	}
	rr->rdata_count = rdata_num;
	return 1;
}

/* print RR on one line in output buffer. caller must zeroterminate, if
 * that is needed. */
static int print_rr_oneline(struct buffer* rr_buffer, const dname_type* dname,
	struct rr* rr)
{
	rrtype_descriptor_type *descriptor;
	descriptor = rrtype_descriptor_by_type(rr->type);
	buffer_printf(rr_buffer, "%s", dname_to_string(dname, NULL));
	buffer_printf(rr_buffer, "\t%lu\t%s\t%s", (unsigned long)rr->ttl,
		rrclass_to_string(rr->klass), rrtype_to_string(rr->type));
	if(!oneline_print_rdata(rr_buffer, descriptor, rr)) {
		if(!rdata_atoms_to_unknown_string(rr_buffer,
			descriptor, rr->rdata_count, rr->rdatas)) {
			return 0;
		}
	}
	return 1;
}

/* write one RR to file, on one line */
static int ixfr_write_rr(struct zone* zone, FILE* out, char* fname,
	uint8_t* buf, size_t len, struct region* temp, buffer_type* rr_buffer)
{
	const dname_type* dname;
	struct rr rr;

	if(!parse_wirerr_into_temp(zone, fname, temp, buf, len, &dname, &rr)) {
		region_free_all(temp);
		return 0;
	}

	buffer_clear(rr_buffer);
	if(!print_rr_oneline(rr_buffer, dname, &rr)) {
		log_msg(LOG_ERR, "failed to write zone %s IXFR data %s: cannot spool RR string into buffer", zone->opts->name, fname);
		region_free_all(temp);
		return 0;
	}
	buffer_write_u8(rr_buffer, 0);
	buffer_flip(rr_buffer);

	if(!fprintf(out, "%s\n", buffer_begin(rr_buffer))) {
		log_msg(LOG_ERR, "failed to write zone %s IXFR data %s: cannot print RR string to file: %s", zone->opts->name, fname, strerror(errno));
		region_free_all(temp);
		return 0;
	}
	region_free_all(temp);
	return 1;
}

/* write ixfr RRs to file */
static int ixfr_write_rrs(struct zone* zone, FILE* out, char* fname,
	uint8_t* buf, size_t len, struct region* temp, buffer_type* rr_buffer)
{
	size_t current = 0;
	if(!buf || len == 0)
		return 1;
	while(current < len) {
		size_t rrlen = count_rr_length(buf, len, current);
		if(rrlen == 0)
			return 0;
		if(current + rrlen > len)
			return 0;
		if(!ixfr_write_rr(zone, out, fname, buf+current, rrlen,
			temp, rr_buffer))
			return 0;
		current += rrlen;
	}
	return 1;
}

/* write the ixfr data file data */
static int ixfr_write_file_data(struct zone* zone, struct ixfr_data* data,
	FILE* out, char* fname)
{
	struct region* temp, *rrtemp;
	buffer_type* rr_buffer;
	temp = region_create(xalloc, free);
	rrtemp = region_create(xalloc, free);
	rr_buffer = buffer_create(rrtemp, MAX_RDLENGTH);

	if(!ixfr_write_rrs(zone, out, fname, data->newsoa, data->newsoa_len,
		temp, rr_buffer)) {
		region_destroy(temp);
		region_destroy(rrtemp);
		return 0;
	}
	if(!ixfr_write_rrs(zone, out, fname, data->oldsoa, data->oldsoa_len,
		temp, rr_buffer)) {
		region_destroy(temp);
		region_destroy(rrtemp);
		return 0;
	}
	if(!ixfr_write_rrs(zone, out, fname, data->del, data->del_len,
		temp, rr_buffer)) {
		region_destroy(temp);
		region_destroy(rrtemp);
		return 0;
	}
	if(!ixfr_write_rrs(zone, out, fname, data->add, data->add_len,
		temp, rr_buffer)) {
		region_destroy(temp);
		region_destroy(rrtemp);
		return 0;
	}
	region_destroy(temp);
	region_destroy(rrtemp);
	return 1;
}

int ixfr_write_file(struct zone* zone, struct ixfr_data* data,
	const char* zfile, int file_num)
{
	char ixfrfile[1024+24];
	FILE* out;
	make_ixfr_name(ixfrfile, sizeof(ixfrfile), zfile, file_num);
	VERBOSITY(1, (LOG_INFO, "writing zone %s IXFR data to file %s",
		zone->opts->name, ixfrfile));
	out = fopen(ixfrfile, "w");
	if(!out) {
		log_msg(LOG_ERR, "could not open for writing zone %s IXFR file %s: %s",
			zone->opts->name, ixfrfile, strerror(errno));
		return 0;
	}

	if(!ixfr_write_file_header(zone, data, out)) {
		log_msg(LOG_ERR, "could not write file header for zone %s IXFR file %s: %s",
			zone->opts->name, ixfrfile, strerror(errno));
		fclose(out);
		return 0;
	}
	if(!ixfr_write_file_data(zone, data, out, ixfrfile)) {
		fclose(out);
		return 0;
	}

	fclose(out);
	data->file_num = file_num;
	return 1;
}

/* write the ixfr files that need to be stored on disk */
static void ixfr_write_files(struct zone* zone, const char* zfile)
{
	size_t prevcount = 0;
	int num;
	struct ixfr_data* data;
	if(!zone->ixfr || !zone->ixfr->data)
		return; /* nothing to write */

	/* write unwritten files to disk */
	data = ixfr_data_last(zone->ixfr);
	num=1;
	while(data && data->file_num == 0) {
		if(!ixfr_write_file(zone, data, zfile, num)) {
			/* There could be more files that are sitting on the
			 * disk, remove them, they are not used without
			 * this ixfr file.
			 *
			 * Give this element a file num, so it can be
			 * deleted, it failed to write. It may be partial,
			 * and we do not want to read that back in.
			 * We are left with the newer transfers, that form
			 * a correct list of transfers, that are wholly
			 * written. */
			data->file_num = num;
			ixfr_delete_rest_files(zone, data, zfile, 0);
			return;
		}
		num++;
		data = ixfr_data_prev(zone->ixfr, data, &prevcount);
	}
}

void ixfr_write_to_file(struct zone* zone, const char* zfile)
{
	int dest_num_files = 0;
	/* we just wrote the zonefile zfile, and it is time to write
	 * the IXFR contents to the disk too. */
	/* find out what the target number of files is that we want on
	 * the disk */
	dest_num_files = ixfr_target_number_files(zone);

	/* delete if we have more than we need */
	ixfr_delete_superfluous_files(zone, zfile, dest_num_files);

	/* delete if we have too much in memory */
	ixfr_delete_memory_items(zone, dest_num_files);

	/* rename the transfers that we have that already have a file */
	if(!ixfr_rename_files(zone, zfile, dest_num_files))
		return;

	/* write the transfers that are not written yet */
	ixfr_write_files(zone, zfile);
}

/* delete from domain table */
static void domain_table_delete(struct domain_table* table,
	struct domain* domain)
{
	/* first adjust the number list so that domain is the last one */
	numlist_make_last(table, domain);
	/* pop off the domain from the number list */
	(void)numlist_pop_last(table);

#ifdef USE_RADIX_TREE
	radix_delete(table->nametree, domain->rnode);
#else
	rbtree_delete(table->names_to_domains, domain->node.key);
#endif
}

/* can we delete temp domain */
static int can_del_temp_domain(struct domain* domain)
{
	struct domain* n;
	/* we want to keep the zone apex */
	if(domain->is_apex)
		return 0;
	if(domain->rrsets)
		return 0;
	if(domain->usage)
		return 0;
	/* check if there are domains under it */
	n = domain_next(domain);
	if(n && domain_is_subdomain(n, domain))
		return 0;
	return 1;
}

/* delete temporary domain */
static void ixfr_temp_deldomain(struct domain_table* temptable,
	struct domain* domain, struct domain* avoid)
{
	struct domain* p;
	if(domain == avoid || !can_del_temp_domain(domain))
		return;
	p = domain->parent;
	/* see if this domain is someones wildcard-child-closest-match,
	 * which can only be the parent, and then it should use the
	 * one-smaller than this domain as closest-match. */
	if(domain->parent &&
		domain->parent->wildcard_child_closest_match == domain)
		domain->parent->wildcard_child_closest_match =
			domain_previous_existing_child(domain);
	domain_table_delete(temptable, domain);
	while(p) {
		struct domain* up = p->parent;
		if(p == avoid || !can_del_temp_domain(p))
			break;
		if(p->parent && p->parent->wildcard_child_closest_match == p)
			p->parent->wildcard_child_closest_match =
				domain_previous_existing_child(p);
		domain_table_delete(temptable, p);
		p = up;
	}
}

/* clear out the just read RR from the temp table */
static void clear_temp_table_of_rr(struct domain_table* temptable,
	struct zone* tempzone, struct rr* rr)
{
#if 0 /* clear out by removing everything, alternate for the cleanout code */
	/* clear domains from the tempzone,
	 * the only domain left is the zone apex and its parents */
	domain_type* domain;
#ifdef USE_RADIX_TREE
	struct radnode* first = radix_first(temptable->nametree);
	domain = first?(domain_type*)first->elem:NULL;
#else
	domain = (domain_type*)rbtree_first(temptable->names_to_domains);
#endif
	while(domain != (domain_type*)RBTREE_NULL && domain) {
		domain_type* next = domain_next(domain);
		if(domain != tempzone->apex &&
			!domain_is_subdomain(tempzone->apex, domain)) {
			domain_table_delete(temptable, domain);
		} else {
			if(!domain->parent /* is the root */ ||
				domain == tempzone->apex)
				domain->usage = 1;
			else	domain->usage = 0;
		}
		domain = next;
	}

	if(rr->owner == tempzone->apex) {
		tempzone->apex->rrsets = NULL;
		tempzone->soa_rrset = NULL;
		tempzone->soa_nx_rrset = NULL;
		tempzone->ns_rrset = NULL;
	}
	return;
#endif

	/* clear domains in the rdata */
	unsigned i;
	for(i=0; i<rr->rdata_count; i++) {
		if(rdata_atom_is_domain(rr->type, i)) {
			/* clear out that dname */
			struct domain* domain =
				rdata_atom_domain(rr->rdatas[i]);
			domain->usage --;
			if(domain != tempzone->apex && domain->usage == 0)
				ixfr_temp_deldomain(temptable, domain, rr->owner);
		}
	}

	/* clear domain_parsed */
	if(rr->owner == tempzone->apex) {
		tempzone->apex->rrsets = NULL;
		tempzone->soa_rrset = NULL;
		tempzone->soa_nx_rrset = NULL;
		tempzone->ns_rrset = NULL;
	} else {
		rr->owner->rrsets = NULL;
		if(rr->owner->usage == 0) {
			ixfr_temp_deldomain(temptable, rr->owner, NULL);
		}
	}
}

/* read ixfr data new SOA */
static int ixfr_data_readnewsoa(struct ixfr_data* data, struct zone* zone,
	struct rr *rr, zone_parser_t *parser, struct region* tempregion,
	struct domain_table* temptable, struct zone* tempzone,
	uint32_t dest_serial)
{
	size_t capacity = 0;
	if(rr->type != TYPE_SOA) {
		zone_error(parser, "zone %s ixfr data: IXFR data does not start with SOA",
			zone->opts->name);
		return 0;
	}
	if(rr->klass != CLASS_IN) {
		zone_error(parser, "zone %s ixfr data: IXFR data is not class IN",
			zone->opts->name);
		return 0;
	}
	if(!zone->apex) {
		zone_error(parser, "zone %s ixfr data: zone has no apex, no zone data",
			zone->opts->name);
		return 0;
	}
	if(dname_compare(domain_dname(zone->apex), domain_dname(rr->owner)) != 0) {
		zone_error(parser, "zone %s ixfr data: IXFR data wrong SOA for zone %s",
			zone->opts->name, domain_to_string(rr->owner));
		return 0;
	}
	data->newserial = soa_rr_get_serial(rr);
	if(data->newserial != dest_serial) {
		zone_error(parser, "zone %s ixfr data: IXFR data contains the wrong version, serial %u but want destination serial %u",
			zone->opts->name, data->newserial,
			dest_serial);
		return 0;
	}
	if(!ixfr_putrr(domain_dname(rr->owner), rr->type, rr->klass, rr->ttl, rr->rdatas, rr->rdata_count, &data->newsoa, &data->newsoa_len, &capacity)) {
		zone_error(parser, "zone %s ixfr data: cannot allocate space",
			zone->opts->name);
		return 0;
	}
	clear_temp_table_of_rr(temptable, tempzone, rr);
	region_free_all(tempregion);
	ixfr_trim_capacity(&data->newsoa, &data->newsoa_len, &capacity);
	return 1;
}

/* read ixfr data old SOA */
static int ixfr_data_readoldsoa(struct ixfr_data* data, struct zone* zone,
	struct rr *rr, zone_parser_t *parser, struct region* tempregion,
	struct domain_table* temptable, struct zone* tempzone,
	uint32_t* dest_serial)
{
	size_t capacity = 0;
	if(rr->type != TYPE_SOA) {
		zone_error(parser, "zone %s ixfr data: IXFR data 2nd RR is not SOA",
			zone->opts->name);
		return 0;
	}
	if(rr->klass != CLASS_IN) {
		zone_error(parser, "zone %s ixfr data: IXFR data 2ndSOA is not class IN",
			zone->opts->name);
		return 0;
	}
	if(!zone->apex) {
		zone_error(parser, "zone %s ixfr data: zone has no apex, no zone data",
			zone->opts->name);
		return 0;
	}
	if(dname_compare(domain_dname(zone->apex), domain_dname(rr->owner)) != 0) {
		zone_error(parser, "zone %s ixfr data: IXFR data wrong 2nd SOA for zone %s",
			zone->opts->name, domain_to_string(rr->owner));
		return 0;
	}
	data->oldserial = soa_rr_get_serial(rr);
	if(!ixfr_putrr(domain_dname(rr->owner), rr->type, rr->klass, rr->ttl, rr->rdatas, rr->rdata_count, &data->oldsoa, &data->oldsoa_len, &capacity)) {
		zone_error(parser, "zone %s ixfr data: cannot allocate space",
			zone->opts->name);
		return 0;
	}
	clear_temp_table_of_rr(temptable, tempzone, rr);
	region_free_all(tempregion);
	ixfr_trim_capacity(&data->oldsoa, &data->oldsoa_len, &capacity);
	*dest_serial = data->oldserial;
	return 1;
}

/* read ixfr data del section */
static int ixfr_data_readdel(struct ixfr_data* data, struct zone* zone,
	struct rr *rr, zone_parser_t *parser, struct region* tempregion,
	struct domain_table* temptable, struct zone* tempzone)
{
	size_t capacity = 0;
	if(!ixfr_putrr(domain_dname(rr->owner), rr->type, rr->klass, rr->ttl, rr->rdatas, rr->rdata_count, &data->del, &data->del_len, &capacity)) {
		zone_error(parser, "zone %s ixdr data: cannot allocate space",
			zone->opts->name);
		return 0;
	}
	clear_temp_table_of_rr(temptable, tempzone, rr);
	/* check SOA and also serial, because there could be other
	 * add and del sections from older versions collated, we can
	 * see this del section end when it has the serial */
	if(rr->type != TYPE_SOA && soa_rr_get_serial(rr) != data->newserial) {
		region_free_all(tempregion);
		return 1;
	}
	region_free_all(tempregion);
	ixfr_trim_capacity(&data->del, &data->del_len, &capacity);
	return 2;
}

/* read ixfr data add section */
static int ixfr_data_readadd(struct ixfr_data* data, struct zone* zone,
	struct rr *rr, zone_parser_t *parser, struct region* tempregion,
	struct domain_table* temptable, struct zone* tempzone)
{
	size_t capacity = 0;
	if(!ixfr_putrr(domain_dname(rr->owner), rr->type, rr->klass, rr->ttl, rr->rdatas, rr->rdata_count, &data->add, &data->add_len, &capacity)) {
		zone_error(parser, "zone %s ixfr data: cannot allocate space",
			zone->opts->name);
		return 0;
	}
	clear_temp_table_of_rr(temptable, tempzone, rr);
	if(rr->type != TYPE_SOA || soa_rr_get_serial(rr) != data->newserial) {
		region_free_all(tempregion);
		return 1;
	}
	region_free_all(tempregion);
	ixfr_trim_capacity(&data->add, &data->add_len, &capacity);
	return 2;
}

struct ixfr_data_state {
	struct zone *zone;
	struct ixfr_data *data;
	struct region *tempregion, *stayregion;
	struct domain_table *temptable;
	struct zone *tempzone;
	uint32_t *dest_serial;
	size_t rr_count, soa_rr_count;
};

/* read one RR from file */
static int32_t ixfr_data_accept(
	zone_parser_t *parser,
	const zone_name_t *name,
	uint16_t type,
	uint16_t class,
	uint32_t ttl,
	uint16_t rdlength,
	const uint8_t *rdata,
	void *user_data)
{
	struct rr *rr;
	const struct dname *dname;
	struct domain *domain;
	struct buffer buffer;
	union rdata_atom *rdatas;
	ssize_t rdata_count;
	struct ixfr_data_state *state = (struct ixfr_data_state *)user_data;

	assert(parser);

	buffer_create_from(&buffer, rdata, rdlength);

	dname = dname_make(state->tempregion, name->octets, 1);
	assert(dname);
	domain = domain_table_insert(state->temptable, dname);
	assert(domain);

	rdata_count = rdata_wireformat_to_rdata_atoms(
		state->tempregion, state->temptable, type, rdlength, &buffer, &rdatas);
	assert(rdata_count > 0);
	rr = region_alloc(state->tempregion, sizeof(*rr));
	assert(rr);
	rr->owner = domain;
	rr->rdatas = rdatas;
	rr->ttl = ttl;
	rr->type = type;
	rr->klass = class;
	rr->rdata_count = rdata_count;

	if (state->rr_count == 0) {
		if (!ixfr_data_readnewsoa(state->data, state->zone, rr, parser,
		                          state->tempregion, state->temptable,
		                          state->tempzone, *state->dest_serial))
			return ZONE_SEMANTIC_ERROR;
	} else if (state->rr_count == 1) {
		if(!ixfr_data_readoldsoa(state->data, state->zone, rr, parser,
		                         state->tempregion, state->temptable,
		                         state->tempzone, state->dest_serial))
			return ZONE_SEMANTIC_ERROR;
	} else if (state->soa_rr_count == 0) {
		switch (ixfr_data_readdel(state->data, state->zone, rr, parser,
		                          state->tempregion, state->temptable,
		                          state->tempzone))
		{
			case 0:
				return ZONE_SEMANTIC_ERROR;
			case 1:
				break;
			case 2:
				state->soa_rr_count++;
				break;
		}
	} else if (state->soa_rr_count == 1) {
		switch (ixfr_data_readadd(state->data, state->zone, rr, parser,
		                          state->tempregion, state->temptable,
		                          state->tempzone))
		{
			case 0:
				return ZONE_SEMANTIC_ERROR;
			case 1:
				break;
			case 2:
				state->soa_rr_count++;
				break;
		}
	}

	state->rr_count++;
	return 0;
}

static void ixfr_data_log(
	zone_parser_t *parser,
	uint32_t category,
	const char *file,
	size_t line,
	const char *message,
	void *user_data)
{
	int priority = LOG_ERR;
	(void)parser;
	(void)file;
	(void)line;
	(void)user_data;
	if (category == ZONE_WARNING)
		priority = LOG_WARNING;
	log_msg(priority, "%s", message);
}

/* read ixfr data from file */
static int ixfr_data_read(struct nsd* nsd, struct zone* zone,
	const char* ixfrfile, uint32_t* dest_serial, int file_num)
{
	struct ixfr_data_state state = { 0 };

	if(!zone->apex) {
		return 0;
	}
	if(zone->ixfr &&
		zone->ixfr->data->count == zone->opts->pattern->ixfr_number) {
		VERBOSITY(3, (LOG_INFO, "zone %s skip %s IXFR data because only %d ixfr-number configured",
			zone->opts->name, ixfrfile, (int)zone->opts->pattern->ixfr_number));
		return 0;
	}

	/* the file has header comments, new soa, old soa, delsection,
	 * addsection. The delsection and addsection end in a SOA of oldver
	 * and newver respectively. */
	state.zone = zone;
	state.data = xalloc_zero(sizeof(*state.data));
	state.data->file_num = file_num;

	state.dest_serial = dest_serial;
	/* the temp region is cleared after every RR */
	state.tempregion = region_create(xalloc, free);
	/* the stay region holds the temporary data that stays between RRs */
	state.stayregion = region_create(xalloc, free);
	state.temptable = domain_table_create(state.stayregion);
	state.tempzone = region_alloc_zero(state.stayregion, sizeof(*state.tempzone));
	if(!zone->apex) {
		ixfr_data_free(state.data);
		region_destroy(state.tempregion);
		region_destroy(state.stayregion);
		return 0;
	}
	state.tempzone->apex = domain_table_insert(state.temptable,
		domain_dname(zone->apex));
	state.temptable->root->usage++;
	state.tempzone->apex->usage++;
	state.tempzone->opts = zone->opts;
	/* switch to per RR region for new allocations in temp domain table */
	state.temptable->region = state.tempregion;

  {
		const struct dname *origin;
		zone_parser_t parser;
		zone_options_t options;
		zone_name_buffer_t name_buffer;
		zone_rdata_buffer_t rdata_buffer;
		zone_buffers_t buffers = { 1, &name_buffer, &rdata_buffer };
		memset(&options, 0, sizeof(options));

		origin = domain_dname(zone->apex);
		options.origin.octets = dname_name(origin);
		options.origin.length = origin->name_size;
		options.no_includes = true;
		options.pretty_ttls = false;
		options.default_ttl = DEFAULT_TTL;
		options.default_class = CLASS_IN;
		options.log.callback = &ixfr_data_log;
		options.accept.callback = &ixfr_data_accept;

		if(zone_parse(&parser, &options, &buffers, ixfrfile, &state) != 0) {
			ixfr_data_free(state.data);
			region_destroy(state.tempregion);
			region_destroy(state.stayregion);
			return 0;
		}
	}

	region_destroy(state.tempregion);
	region_destroy(state.stayregion);

	if(!zone->ixfr)
		zone->ixfr = zone_ixfr_create(nsd);
	if(zone->opts->pattern->ixfr_size != 0 &&
		zone->ixfr->total_size + ixfr_data_size(state.data) >
		zone->opts->pattern->ixfr_size) {
		VERBOSITY(3, (LOG_INFO, "zone %s skip %s IXFR data because only ixfr-size: %u configured, and it is %u size",
			zone->opts->name, ixfrfile, (unsigned)zone->opts->pattern->ixfr_size, (unsigned)ixfr_data_size(state.data)));
		ixfr_data_free(state.data);
		return 0;
	}
	zone_ixfr_add(zone->ixfr, state.data, 0);
	VERBOSITY(3, (LOG_INFO, "zone %s read %s IXFR data of %u bytes",
		zone->opts->name, ixfrfile, (unsigned)ixfr_data_size(state.data)));
	return 1;
}

/* try to read the next ixfr file. returns false if it fails or if it
 * does not fit in the configured sizes */
static int ixfr_read_one_more_file(struct nsd* nsd, struct zone* zone,
	const char* zfile, int num_files, uint32_t *dest_serial)
{
	char ixfrfile[1024+24];
	struct stat statbuf;
	int file_num = num_files+1;
	make_ixfr_name(ixfrfile, sizeof(ixfrfile), zfile, file_num);
	/* if the file does not exist, all transfers have been read */
	if (stat(ixfrfile, &statbuf) != 0 && errno == ENOENT)
		return 0;
	return ixfr_data_read(nsd, zone, ixfrfile, dest_serial, file_num);
}

void ixfr_read_from_file(struct nsd* nsd, struct zone* zone, const char* zfile)
{
	uint32_t serial;
	int num_files = 0;
	/* delete the existing data, the zone data in memory has likely
	 * changed, eg. due to reading a new zonefile. So that needs new
	 * IXFRs */
	zone_ixfr_clear(zone->ixfr);

	/* track the serial number that we need to end up with, and check
	 * that the IXFRs match up and result in the required version */
	serial = zone_get_current_serial(zone);

	while(ixfr_read_one_more_file(nsd, zone, zfile, num_files, &serial)) {
		num_files++;
	}
	if(num_files > 0) {
		VERBOSITY(1, (LOG_INFO, "zone %s read %d IXFR transfers with success",
			zone->opts->name, num_files));
	}
}
