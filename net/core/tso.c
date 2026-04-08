// SPDX-License-Identifier: GPL-2.0
#include <linux/export.h>
#include <linux/if_vlan.h>
#include <net/ip.h>
#include <net/tso.h>
#include <linux/dma-mapping.h>
#include <linux/unaligned.h>

void tso_build_hdr(const struct sk_buff *skb, char *hdr, struct tso_t *tso,
		   int size, bool is_last)
{
	int hdr_len = skb_transport_offset(skb) + tso->tlen;
	int mac_hdr_len = skb_network_offset(skb);

	memcpy(hdr, skb->data, hdr_len);
	if (!tso->ipv6) {
		struct iphdr *iph = (void *)(hdr + mac_hdr_len);

		iph->id = htons(tso->ip_id);
		iph->tot_len = htons(size + hdr_len - mac_hdr_len);
		tso->ip_id++;
	} else {
		struct ipv6hdr *iph = (void *)(hdr + mac_hdr_len);

		iph->payload_len = htons(size + tso->tlen);
	}
	hdr += skb_transport_offset(skb);
	if (tso->tlen != sizeof(struct udphdr)) {
		struct tcphdr *tcph = (struct tcphdr *)hdr;

		put_unaligned_be32(tso->tcp_seq, &tcph->seq);

		if (!is_last) {
			/* Clear all special flags for not last packet */
			tcph->psh = 0;
			tcph->fin = 0;
			tcph->rst = 0;
		}
	} else {
		struct udphdr *uh = (struct udphdr *)hdr;

		uh->len = htons(sizeof(*uh) + size);
	}
}
EXPORT_SYMBOL(tso_build_hdr);

void tso_build_data(const struct sk_buff *skb, struct tso_t *tso, int size)
{
	tso->tcp_seq += size; /* not worth avoiding this operation for UDP */
	tso->size -= size;
	tso->data += size;

	if ((tso->size == 0) &&
	    (tso->next_frag_idx < skb_shinfo(skb)->nr_frags)) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[tso->next_frag_idx];

		/* Move to next segment */
		tso->size = skb_frag_size(frag);
		tso->data = skb_frag_address(frag);
		tso->next_frag_idx++;
	}
}
EXPORT_SYMBOL(tso_build_data);

int tso_start(struct sk_buff *skb, struct tso_t *tso)
{
	int tlen = skb_is_gso_tcp(skb) ? tcp_hdrlen(skb) : sizeof(struct udphdr);
	int hdr_len = skb_transport_offset(skb) + tlen;

	tso->tlen = tlen;
	tso->ip_id = ntohs(ip_hdr(skb)->id);
	tso->tcp_seq = (tlen != sizeof(struct udphdr)) ? ntohl(tcp_hdr(skb)->seq) : 0;
	tso->next_frag_idx = 0;
	tso->ipv6 = vlan_get_protocol(skb) == htons(ETH_P_IPV6);

	/* Build first data */
	tso->size = skb_headlen(skb) - hdr_len;
	tso->data = skb->data + hdr_len;
	if ((tso->size == 0) &&
	    (tso->next_frag_idx < skb_shinfo(skb)->nr_frags)) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[tso->next_frag_idx];

		/* Move to next segment */
		tso->size = skb_frag_size(frag);
		tso->data = skb_frag_address(frag);
		tso->next_frag_idx++;
	}
	return hdr_len;
}
EXPORT_SYMBOL(tso_start);

static int tso_dma_iova_try(struct device *dev, struct tso_dma_map *map,
			    phys_addr_t phys, size_t linear_len,
			    size_t total_len, size_t *offset)
{
	const struct sk_buff *skb;
	unsigned int nr_frags;
	int i;

	if (!dma_iova_try_alloc(dev, &map->iova_state, phys, total_len))
		return 1;

	skb = map->skb;
	nr_frags = skb_shinfo(skb)->nr_frags;

	if (linear_len) {
		if (dma_iova_link(dev, &map->iova_state,
				  phys, *offset, linear_len,
				  DMA_TO_DEVICE, 0))
			goto iova_fail;
		map->linear_len = linear_len;
		*offset += linear_len;
	}

	for (i = 0; i < nr_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		unsigned int frag_len = skb_frag_size(frag);

		if (dma_iova_link(dev, &map->iova_state,
				  skb_frag_phys(frag), *offset,
				  frag_len, DMA_TO_DEVICE, 0)) {
			map->nr_frags = i;
			goto iova_fail;
		}
		map->frags[i].len = frag_len;
		*offset += frag_len;
		map->nr_frags = i + 1;
	}

	if (dma_iova_sync(dev, &map->iova_state, 0, total_len))
		goto iova_fail;

	return 0;

iova_fail:
	dma_iova_destroy(dev, &map->iova_state, *offset,
			 DMA_TO_DEVICE, 0);
	memset(&map->iova_state, 0, sizeof(map->iova_state));

	/* reset map state */
	map->frag_idx = -1;
	map->offset = 0;
	map->linear_len = 0;
	map->nr_frags = 0;

	return 1;
}

/**
 * tso_dma_map_init - DMA-map GSO payload regions
 * @map: map struct to initialize
 * @dev: device for DMA mapping
 * @skb: the GSO skb
 * @hdr_len: per-segment header length in bytes
 *
 * DMA-maps the linear payload (after headers) and all frags.
 * Prefers the DMA IOVA API (one contiguous mapping, one IOTLB sync);
 * falls back to per-region dma_map_phys() when IOVA is not available.
 * Positions the iterator at byte 0 of the payload.
 *
 * Return: 0 on success, -ENOMEM on DMA mapping failure (partial mappings
 * are cleaned up internally).
 */
int tso_dma_map_init(struct tso_dma_map *map, struct device *dev,
		     const struct sk_buff *skb, unsigned int hdr_len)
{
	unsigned int linear_len = skb_headlen(skb) - hdr_len;
	unsigned int nr_frags = skb_shinfo(skb)->nr_frags;
	size_t total_len = skb->len - hdr_len;
	size_t offset = 0;
	phys_addr_t phys;
	int i;

	map->dev = dev;
	map->skb = skb;
	map->hdr_len = hdr_len;
	map->frag_idx = -1;
	map->offset = 0;
	map->iova_offset = 0;
	map->total_len = total_len;
	map->linear_len = 0;
	map->nr_frags = 0;
	memset(&map->iova_state, 0, sizeof(map->iova_state));

	if (!total_len)
		return 0;

	if (linear_len)
		phys = virt_to_phys(skb->data + hdr_len);
	else
		phys = skb_frag_phys(&skb_shinfo(skb)->frags[0]);

	if (tso_dma_iova_try(dev, map, phys, linear_len, total_len, &offset)) {
		/* IOVA path failed, map state was reset. Fallback to
		 * per-region dma_map_phys()
		 */
		if (linear_len) {
			map->linear_dma = dma_map_phys(dev, phys, linear_len,
						       DMA_TO_DEVICE, 0);
			if (dma_mapping_error(dev, map->linear_dma))
				return -ENOMEM;
			map->linear_len = linear_len;
		}

		for (i = 0; i < nr_frags; i++) {
			skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
			unsigned int frag_len = skb_frag_size(frag);

			map->frags[i].len = frag_len;
			map->frags[i].dma = dma_map_phys(dev, skb_frag_phys(frag),
							 frag_len, DMA_TO_DEVICE, 0);
			if (dma_mapping_error(dev, map->frags[i].dma)) {
				tso_dma_map_cleanup(map);
				return -ENOMEM;
			}
			map->nr_frags = i + 1;
		}
	}

	if (linear_len == 0 && nr_frags > 0)
		map->frag_idx = 0;

	return 0;
}
EXPORT_SYMBOL(tso_dma_map_init);

/**
 * tso_dma_map_cleanup - unmap all DMA regions in a tso_dma_map
 * @map: the map to clean up
 *
 * Handles both IOVA and fallback paths. For IOVA, calls
 * dma_iova_destroy(). For fallback, unmaps each region individually.
 */
void tso_dma_map_cleanup(struct tso_dma_map *map)
{
	int i;

	if (dma_use_iova(&map->iova_state)) {
		dma_iova_destroy(map->dev, &map->iova_state, map->total_len,
				 DMA_TO_DEVICE, 0);
		memset(&map->iova_state, 0, sizeof(map->iova_state));
	} else {
		if (map->linear_len)
			dma_unmap_phys(map->dev, map->linear_dma,
				       map->linear_len, DMA_TO_DEVICE, 0);

		for (i = 0; i < map->nr_frags; i++)
			dma_unmap_phys(map->dev, map->frags[i].dma,
				       map->frags[i].len, DMA_TO_DEVICE, 0);
	}

	map->linear_len = 0;
	map->nr_frags = 0;
}
EXPORT_SYMBOL(tso_dma_map_cleanup);

/**
 * tso_dma_map_count - count descriptors for a payload range
 * @map: the payload map
 * @len: number of payload bytes in this segment
 *
 * Counts how many contiguous DMA region chunks the next @len bytes
 * will span, without advancing the iterator. On the IOVA path this
 * is always 1 (contiguous). On the fallback path, uses region sizes
 * from the current position.
 *
 * Return: the number of descriptors needed for @len bytes of payload.
 */
unsigned int tso_dma_map_count(struct tso_dma_map *map, unsigned int len)
{
	unsigned int offset = map->offset;
	int idx = map->frag_idx;
	unsigned int count = 0;

	if (!len)
		return 0;

	if (dma_use_iova(&map->iova_state))
		return 1;

	while (len > 0) {
		unsigned int region_len, chunk;

		if (idx == -1)
			region_len = map->linear_len;
		else
			region_len = map->frags[idx].len;

		chunk = min(len, region_len - offset);
		len -= chunk;
		count++;
		offset = 0;
		idx++;
	}

	return count;
}
EXPORT_SYMBOL(tso_dma_map_count);

/**
 * tso_dma_map_next - yield the next DMA address range
 * @map: the payload map
 * @addr: output DMA address
 * @chunk_len: output chunk length
 * @mapping_len: full DMA mapping length when this chunk starts a new
 *               mapping region, or 0 when continuing a previous one.
 *               On the IOVA path this is always 0 (driver must not
 *               do per-region unmaps; use tso_dma_map_cleanup instead).
 * @seg_remaining: bytes left in current segment
 *
 * Yields the next (dma_addr, chunk_len) pair and advances the iterator.
 * On the IOVA path, the entire payload is contiguous so each segment
 * is always a single chunk.
 *
 * Return: true if a chunk was yielded, false when @seg_remaining is 0.
 */
bool tso_dma_map_next(struct tso_dma_map *map, dma_addr_t *addr,
		      unsigned int *chunk_len, unsigned int *mapping_len,
		      unsigned int seg_remaining)
{
	unsigned int region_len, chunk;

	if (!seg_remaining)
		return false;

	/* IOVA path: contiguous DMA range, no region boundaries */
	if (dma_use_iova(&map->iova_state)) {
		*addr = map->iova_state.addr + map->iova_offset;
		*chunk_len = seg_remaining;
		*mapping_len = 0;
		map->iova_offset += seg_remaining;
		return true;
	}

	/* Fallback path: per-region iteration */

	if (map->frag_idx == -1) {
		region_len = map->linear_len;
		chunk = min(seg_remaining, region_len - map->offset);
		*addr = map->linear_dma + map->offset;
	} else {
		region_len = map->frags[map->frag_idx].len;
		chunk = min(seg_remaining, region_len - map->offset);
		*addr = map->frags[map->frag_idx].dma + map->offset;
	}

	*mapping_len = (map->offset == 0) ? region_len : 0;
	*chunk_len = chunk;
	map->offset += chunk;

	if (map->offset >= region_len) {
		map->frag_idx++;
		map->offset = 0;
	}

	return true;
}
EXPORT_SYMBOL(tso_dma_map_next);
