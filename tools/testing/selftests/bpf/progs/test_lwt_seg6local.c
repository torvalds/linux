#include <stddef.h>
#include <inttypes.h>
#include <errno.h>
#include <linux/seg6_local.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

/* Packet parsing state machine helpers. */
#define cursor_advance(_cursor, _len) \
	({ void *_tmp = _cursor; _cursor += _len; _tmp; })

#define SR6_FLAG_ALERT (1 << 4)

#define BPF_PACKET_HEADER __attribute__((packed))

struct ip6_t {
	unsigned int ver:4;
	unsigned int priority:8;
	unsigned int flow_label:20;
	unsigned short payload_len;
	unsigned char next_header;
	unsigned char hop_limit;
	unsigned long long src_hi;
	unsigned long long src_lo;
	unsigned long long dst_hi;
	unsigned long long dst_lo;
} BPF_PACKET_HEADER;

struct ip6_addr_t {
	unsigned long long hi;
	unsigned long long lo;
} BPF_PACKET_HEADER;

struct ip6_srh_t {
	unsigned char nexthdr;
	unsigned char hdrlen;
	unsigned char type;
	unsigned char segments_left;
	unsigned char first_segment;
	unsigned char flags;
	unsigned short tag;

	struct ip6_addr_t segments[0];
} BPF_PACKET_HEADER;

struct sr6_tlv_t {
	unsigned char type;
	unsigned char len;
	unsigned char value[0];
} BPF_PACKET_HEADER;

static __always_inline struct ip6_srh_t *get_srh(struct __sk_buff *skb)
{
	void *cursor, *data_end;
	struct ip6_srh_t *srh;
	struct ip6_t *ip;
	uint8_t *ipver;

	data_end = (void *)(long)skb->data_end;
	cursor = (void *)(long)skb->data;
	ipver = (uint8_t *)cursor;

	if ((void *)ipver + sizeof(*ipver) > data_end)
		return NULL;

	if ((*ipver >> 4) != 6)
		return NULL;

	ip = cursor_advance(cursor, sizeof(*ip));
	if ((void *)ip + sizeof(*ip) > data_end)
		return NULL;

	if (ip->next_header != 43)
		return NULL;

	srh = cursor_advance(cursor, sizeof(*srh));
	if ((void *)srh + sizeof(*srh) > data_end)
		return NULL;

	if (srh->type != 4)
		return NULL;

	return srh;
}

static __always_inline
int update_tlv_pad(struct __sk_buff *skb, uint32_t new_pad,
		   uint32_t old_pad, uint32_t pad_off)
{
	int err;

	if (new_pad != old_pad) {
		err = bpf_lwt_seg6_adjust_srh(skb, pad_off,
					  (int) new_pad - (int) old_pad);
		if (err)
			return err;
	}

	if (new_pad > 0) {
		char pad_tlv_buf[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
					0, 0, 0};
		struct sr6_tlv_t *pad_tlv = (struct sr6_tlv_t *) pad_tlv_buf;

		pad_tlv->type = SR6_TLV_PADDING;
		pad_tlv->len = new_pad - 2;

		err = bpf_lwt_seg6_store_bytes(skb, pad_off,
					       (void *)pad_tlv_buf, new_pad);
		if (err)
			return err;
	}

	return 0;
}

static __always_inline
int is_valid_tlv_boundary(struct __sk_buff *skb, struct ip6_srh_t *srh,
			  uint32_t *tlv_off, uint32_t *pad_size,
			  uint32_t *pad_off)
{
	uint32_t srh_off, cur_off;
	int offset_valid = 0;
	int err;

	srh_off = (char *)srh - (char *)(long)skb->data;
	// cur_off = end of segments, start of possible TLVs
	cur_off = srh_off + sizeof(*srh) +
		sizeof(struct ip6_addr_t) * (srh->first_segment + 1);

	*pad_off = 0;

	// we can only go as far as ~10 TLVs due to the BPF max stack size
	#pragma clang loop unroll(full)
	for (int i = 0; i < 10; i++) {
		struct sr6_tlv_t tlv;

		if (cur_off == *tlv_off)
			offset_valid = 1;

		if (cur_off >= srh_off + ((srh->hdrlen + 1) << 3))
			break;

		err = bpf_skb_load_bytes(skb, cur_off, &tlv, sizeof(tlv));
		if (err)
			return err;

		if (tlv.type == SR6_TLV_PADDING) {
			*pad_size = tlv.len + sizeof(tlv);
			*pad_off = cur_off;

			if (*tlv_off == srh_off) {
				*tlv_off = cur_off;
				offset_valid = 1;
			}
			break;

		} else if (tlv.type == SR6_TLV_HMAC) {
			break;
		}

		cur_off += sizeof(tlv) + tlv.len;
	} // we reached the padding or HMAC TLVs, or the end of the SRH

	if (*pad_off == 0)
		*pad_off = cur_off;

	if (*tlv_off == -1)
		*tlv_off = cur_off;
	else if (!offset_valid)
		return -EINVAL;

	return 0;
}

static __always_inline
int add_tlv(struct __sk_buff *skb, struct ip6_srh_t *srh, uint32_t tlv_off,
	    struct sr6_tlv_t *itlv, uint8_t tlv_size)
{
	uint32_t srh_off = (char *)srh - (char *)(long)skb->data;
	uint8_t len_remaining, new_pad;
	uint32_t pad_off = 0;
	uint32_t pad_size = 0;
	uint32_t partial_srh_len;
	int err;

	if (tlv_off != -1)
		tlv_off += srh_off;

	if (itlv->type == SR6_TLV_PADDING || itlv->type == SR6_TLV_HMAC)
		return -EINVAL;

	err = is_valid_tlv_boundary(skb, srh, &tlv_off, &pad_size, &pad_off);
	if (err)
		return err;

	err = bpf_lwt_seg6_adjust_srh(skb, tlv_off, sizeof(*itlv) + itlv->len);
	if (err)
		return err;

	err = bpf_lwt_seg6_store_bytes(skb, tlv_off, (void *)itlv, tlv_size);
	if (err)
		return err;

	// the following can't be moved inside update_tlv_pad because the
	// bpf verifier has some issues with it
	pad_off += sizeof(*itlv) + itlv->len;
	partial_srh_len = pad_off - srh_off;
	len_remaining = partial_srh_len % 8;
	new_pad = 8 - len_remaining;

	if (new_pad == 1) // cannot pad for 1 byte only
		new_pad = 9;
	else if (new_pad == 8)
		new_pad = 0;

	return update_tlv_pad(skb, new_pad, pad_size, pad_off);
}

static __always_inline
int delete_tlv(struct __sk_buff *skb, struct ip6_srh_t *srh,
	       uint32_t tlv_off)
{
	uint32_t srh_off = (char *)srh - (char *)(long)skb->data;
	uint8_t len_remaining, new_pad;
	uint32_t partial_srh_len;
	uint32_t pad_off = 0;
	uint32_t pad_size = 0;
	struct sr6_tlv_t tlv;
	int err;

	tlv_off += srh_off;

	err = is_valid_tlv_boundary(skb, srh, &tlv_off, &pad_size, &pad_off);
	if (err)
		return err;

	err = bpf_skb_load_bytes(skb, tlv_off, &tlv, sizeof(tlv));
	if (err)
		return err;

	err = bpf_lwt_seg6_adjust_srh(skb, tlv_off, -(sizeof(tlv) + tlv.len));
	if (err)
		return err;

	pad_off -= sizeof(tlv) + tlv.len;
	partial_srh_len = pad_off - srh_off;
	len_remaining = partial_srh_len % 8;
	new_pad = 8 - len_remaining;
	if (new_pad == 1) // cannot pad for 1 byte only
		new_pad = 9;
	else if (new_pad == 8)
		new_pad = 0;

	return update_tlv_pad(skb, new_pad, pad_size, pad_off);
}

static __always_inline
int has_egr_tlv(struct __sk_buff *skb, struct ip6_srh_t *srh)
{
	int tlv_offset = sizeof(struct ip6_t) + sizeof(struct ip6_srh_t) +
		((srh->first_segment + 1) << 4);
	struct sr6_tlv_t tlv;

	if (bpf_skb_load_bytes(skb, tlv_offset, &tlv, sizeof(struct sr6_tlv_t)))
		return 0;

	if (tlv.type == SR6_TLV_EGRESS && tlv.len == 18) {
		struct ip6_addr_t egr_addr;

		if (bpf_skb_load_bytes(skb, tlv_offset + 4, &egr_addr, 16))
			return 0;

		// check if egress TLV value is correct
		if (bpf_be64_to_cpu(egr_addr.hi) == 0xfd00000000000000 &&
		    bpf_be64_to_cpu(egr_addr.lo) == 0x4)
			return 1;
	}

	return 0;
}

// This function will push a SRH with segments fd00::1, fd00::2, fd00::3,
// fd00::4
SEC("encap_srh")
int __encap_srh(struct __sk_buff *skb)
{
	unsigned long long hi = 0xfd00000000000000;
	struct ip6_addr_t *seg;
	struct ip6_srh_t *srh;
	char srh_buf[72]; // room for 4 segments
	int err;

	srh = (struct ip6_srh_t *)srh_buf;
	srh->nexthdr = 0;
	srh->hdrlen = 8;
	srh->type = 4;
	srh->segments_left = 3;
	srh->first_segment = 3;
	srh->flags = 0;
	srh->tag = 0;

	seg = (struct ip6_addr_t *)((char *)srh + sizeof(*srh));

	#pragma clang loop unroll(full)
	for (unsigned long long lo = 0; lo < 4; lo++) {
		seg->lo = bpf_cpu_to_be64(4 - lo);
		seg->hi = bpf_cpu_to_be64(hi);
		seg = (struct ip6_addr_t *)((char *)seg + sizeof(*seg));
	}

	err = bpf_lwt_push_encap(skb, 0, (void *)srh, sizeof(srh_buf));
	if (err)
		return BPF_DROP;

	return BPF_REDIRECT;
}

// Add an Egress TLV fc00::4, add the flag A,
// and apply End.X action to fc42::1
SEC("add_egr_x")
int __add_egr_x(struct __sk_buff *skb)
{
	unsigned long long hi = 0xfc42000000000000;
	unsigned long long lo = 0x1;
	struct ip6_srh_t *srh = get_srh(skb);
	uint8_t new_flags = SR6_FLAG_ALERT;
	struct ip6_addr_t addr;
	int err, offset;

	if (srh == NULL)
		return BPF_DROP;

	uint8_t tlv[20] = {2, 18, 0, 0, 0xfd, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
			   0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4};

	err = add_tlv(skb, srh, (srh->hdrlen+1) << 3,
		      (struct sr6_tlv_t *)&tlv, 20);
	if (err)
		return BPF_DROP;

	offset = sizeof(struct ip6_t) + offsetof(struct ip6_srh_t, flags);
	err = bpf_lwt_seg6_store_bytes(skb, offset,
				       (void *)&new_flags, sizeof(new_flags));
	if (err)
		return BPF_DROP;

	addr.lo = bpf_cpu_to_be64(lo);
	addr.hi = bpf_cpu_to_be64(hi);
	err = bpf_lwt_seg6_action(skb, SEG6_LOCAL_ACTION_END_X,
				  (void *)&addr, sizeof(addr));
	if (err)
		return BPF_DROP;
	return BPF_REDIRECT;
}

// Pop the Egress TLV, reset the flags, change the tag 2442 and finally do a
// simple End action
SEC("pop_egr")
int __pop_egr(struct __sk_buff *skb)
{
	struct ip6_srh_t *srh = get_srh(skb);
	uint16_t new_tag = bpf_htons(2442);
	uint8_t new_flags = 0;
	int err, offset;

	if (srh == NULL)
		return BPF_DROP;

	if (srh->flags != SR6_FLAG_ALERT)
		return BPF_DROP;

	if (srh->hdrlen != 11) // 4 segments + Egress TLV + Padding TLV
		return BPF_DROP;

	if (!has_egr_tlv(skb, srh))
		return BPF_DROP;

	err = delete_tlv(skb, srh, 8 + (srh->first_segment + 1) * 16);
	if (err)
		return BPF_DROP;

	offset = sizeof(struct ip6_t) + offsetof(struct ip6_srh_t, flags);
	if (bpf_lwt_seg6_store_bytes(skb, offset, (void *)&new_flags,
				     sizeof(new_flags)))
		return BPF_DROP;

	offset = sizeof(struct ip6_t) + offsetof(struct ip6_srh_t, tag);
	if (bpf_lwt_seg6_store_bytes(skb, offset, (void *)&new_tag,
				     sizeof(new_tag)))
		return BPF_DROP;

	return BPF_OK;
}

// Inspect if the Egress TLV and flag have been removed, if the tag is correct,
// then apply a End.T action to reach the last segment
SEC("inspect_t")
int __inspect_t(struct __sk_buff *skb)
{
	struct ip6_srh_t *srh = get_srh(skb);
	int table = 117;
	int err;

	if (srh == NULL)
		return BPF_DROP;

	if (srh->flags != 0)
		return BPF_DROP;

	if (srh->tag != bpf_htons(2442))
		return BPF_DROP;

	if (srh->hdrlen != 8) // 4 segments
		return BPF_DROP;

	err = bpf_lwt_seg6_action(skb, SEG6_LOCAL_ACTION_END_T,
				  (void *)&table, sizeof(table));

	if (err)
		return BPF_DROP;

	return BPF_REDIRECT;
}

char __license[] SEC("license") = "GPL";
