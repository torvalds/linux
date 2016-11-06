#ifndef _NET_NF_TABLES_CORE_H
#define _NET_NF_TABLES_CORE_H

extern struct nft_expr_type nft_imm_type;
extern struct nft_expr_type nft_cmp_type;
extern struct nft_expr_type nft_lookup_type;
extern struct nft_expr_type nft_bitwise_type;
extern struct nft_expr_type nft_byteorder_type;
extern struct nft_expr_type nft_payload_type;
extern struct nft_expr_type nft_dynset_type;
extern struct nft_expr_type nft_range_type;

int nf_tables_core_module_init(void);
void nf_tables_core_module_exit(void);

struct nft_cmp_fast_expr {
	u32			data;
	enum nft_registers	sreg:8;
	u8			len;
};

/* Calculate the mask for the nft_cmp_fast expression. On big endian the
 * mask needs to include the *upper* bytes when interpreting that data as
 * something smaller than the full u32, therefore a cpu_to_le32 is done.
 */
static inline u32 nft_cmp_fast_mask(unsigned int len)
{
	return cpu_to_le32(~0U >> (FIELD_SIZEOF(struct nft_cmp_fast_expr,
						data) * BITS_PER_BYTE - len));
}

extern const struct nft_expr_ops nft_cmp_fast_ops;

struct nft_payload {
	enum nft_payload_bases	base:8;
	u8			offset;
	u8			len;
	enum nft_registers	dreg:8;
};

struct nft_payload_set {
	enum nft_payload_bases	base:8;
	u8			offset;
	u8			len;
	enum nft_registers	sreg:8;
	u8			csum_type;
	u8			csum_offset;
};

extern const struct nft_expr_ops nft_payload_fast_ops;
extern struct static_key_false nft_trace_enabled;

#endif /* _NET_NF_TABLES_CORE_H */
