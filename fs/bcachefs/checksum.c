// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "checksum.h"
#include "super.h"
#include "super-io.h"

#include <linux/crc32c.h>
#include <linux/crypto.h>
#include <linux/key.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <crypto/algapi.h>
#include <crypto/chacha.h>
#include <crypto/hash.h>
#include <crypto/poly1305.h>
#include <crypto/skcipher.h>
#include <keys/user-type.h>

/*
 * Portions Copyright (c) 1996-2001, PostgreSQL Global Development Group (Any
 * use permitted, subject to terms of PostgreSQL license; see.)

 * If we have a 64-bit integer type, then a 64-bit CRC looks just like the
 * usual sort of implementation. (See Ross Williams' excellent introduction
 * A PAINLESS GUIDE TO CRC ERROR DETECTION ALGORITHMS, available from
 * ftp://ftp.rocksoft.com/papers/crc_v3.txt or several other net sites.)
 * If we have no working 64-bit type, then fake it with two 32-bit registers.
 *
 * The present implementation is a normal (not "reflected", in Williams'
 * terms) 64-bit CRC, using initial all-ones register contents and a final
 * bit inversion. The chosen polynomial is borrowed from the DLT1 spec
 * (ECMA-182, available from http://www.ecma.ch/ecma1/STAND/ECMA-182.HTM):
 *
 * x^64 + x^62 + x^57 + x^55 + x^54 + x^53 + x^52 + x^47 + x^46 + x^45 +
 * x^40 + x^39 + x^38 + x^37 + x^35 + x^33 + x^32 + x^31 + x^29 + x^27 +
 * x^24 + x^23 + x^22 + x^21 + x^19 + x^17 + x^13 + x^12 + x^10 + x^9 +
 * x^7 + x^4 + x + 1
*/

static const u64 crc_table[256] = {
	0x0000000000000000ULL, 0x42F0E1EBA9EA3693ULL, 0x85E1C3D753D46D26ULL,
	0xC711223CFA3E5BB5ULL, 0x493366450E42ECDFULL, 0x0BC387AEA7A8DA4CULL,
	0xCCD2A5925D9681F9ULL, 0x8E224479F47CB76AULL, 0x9266CC8A1C85D9BEULL,
	0xD0962D61B56FEF2DULL, 0x17870F5D4F51B498ULL, 0x5577EEB6E6BB820BULL,
	0xDB55AACF12C73561ULL, 0x99A54B24BB2D03F2ULL, 0x5EB4691841135847ULL,
	0x1C4488F3E8F96ED4ULL, 0x663D78FF90E185EFULL, 0x24CD9914390BB37CULL,
	0xE3DCBB28C335E8C9ULL, 0xA12C5AC36ADFDE5AULL, 0x2F0E1EBA9EA36930ULL,
	0x6DFEFF5137495FA3ULL, 0xAAEFDD6DCD770416ULL, 0xE81F3C86649D3285ULL,
	0xF45BB4758C645C51ULL, 0xB6AB559E258E6AC2ULL, 0x71BA77A2DFB03177ULL,
	0x334A9649765A07E4ULL, 0xBD68D2308226B08EULL, 0xFF9833DB2BCC861DULL,
	0x388911E7D1F2DDA8ULL, 0x7A79F00C7818EB3BULL, 0xCC7AF1FF21C30BDEULL,
	0x8E8A101488293D4DULL, 0x499B3228721766F8ULL, 0x0B6BD3C3DBFD506BULL,
	0x854997BA2F81E701ULL, 0xC7B97651866BD192ULL, 0x00A8546D7C558A27ULL,
	0x4258B586D5BFBCB4ULL, 0x5E1C3D753D46D260ULL, 0x1CECDC9E94ACE4F3ULL,
	0xDBFDFEA26E92BF46ULL, 0x990D1F49C77889D5ULL, 0x172F5B3033043EBFULL,
	0x55DFBADB9AEE082CULL, 0x92CE98E760D05399ULL, 0xD03E790CC93A650AULL,
	0xAA478900B1228E31ULL, 0xE8B768EB18C8B8A2ULL, 0x2FA64AD7E2F6E317ULL,
	0x6D56AB3C4B1CD584ULL, 0xE374EF45BF6062EEULL, 0xA1840EAE168A547DULL,
	0x66952C92ECB40FC8ULL, 0x2465CD79455E395BULL, 0x3821458AADA7578FULL,
	0x7AD1A461044D611CULL, 0xBDC0865DFE733AA9ULL, 0xFF3067B657990C3AULL,
	0x711223CFA3E5BB50ULL, 0x33E2C2240A0F8DC3ULL, 0xF4F3E018F031D676ULL,
	0xB60301F359DBE0E5ULL, 0xDA050215EA6C212FULL, 0x98F5E3FE438617BCULL,
	0x5FE4C1C2B9B84C09ULL, 0x1D14202910527A9AULL, 0x93366450E42ECDF0ULL,
	0xD1C685BB4DC4FB63ULL, 0x16D7A787B7FAA0D6ULL, 0x5427466C1E109645ULL,
	0x4863CE9FF6E9F891ULL, 0x0A932F745F03CE02ULL, 0xCD820D48A53D95B7ULL,
	0x8F72ECA30CD7A324ULL, 0x0150A8DAF8AB144EULL, 0x43A04931514122DDULL,
	0x84B16B0DAB7F7968ULL, 0xC6418AE602954FFBULL, 0xBC387AEA7A8DA4C0ULL,
	0xFEC89B01D3679253ULL, 0x39D9B93D2959C9E6ULL, 0x7B2958D680B3FF75ULL,
	0xF50B1CAF74CF481FULL, 0xB7FBFD44DD257E8CULL, 0x70EADF78271B2539ULL,
	0x321A3E938EF113AAULL, 0x2E5EB66066087D7EULL, 0x6CAE578BCFE24BEDULL,
	0xABBF75B735DC1058ULL, 0xE94F945C9C3626CBULL, 0x676DD025684A91A1ULL,
	0x259D31CEC1A0A732ULL, 0xE28C13F23B9EFC87ULL, 0xA07CF2199274CA14ULL,
	0x167FF3EACBAF2AF1ULL, 0x548F120162451C62ULL, 0x939E303D987B47D7ULL,
	0xD16ED1D631917144ULL, 0x5F4C95AFC5EDC62EULL, 0x1DBC74446C07F0BDULL,
	0xDAAD56789639AB08ULL, 0x985DB7933FD39D9BULL, 0x84193F60D72AF34FULL,
	0xC6E9DE8B7EC0C5DCULL, 0x01F8FCB784FE9E69ULL, 0x43081D5C2D14A8FAULL,
	0xCD2A5925D9681F90ULL, 0x8FDAB8CE70822903ULL, 0x48CB9AF28ABC72B6ULL,
	0x0A3B7B1923564425ULL, 0x70428B155B4EAF1EULL, 0x32B26AFEF2A4998DULL,
	0xF5A348C2089AC238ULL, 0xB753A929A170F4ABULL, 0x3971ED50550C43C1ULL,
	0x7B810CBBFCE67552ULL, 0xBC902E8706D82EE7ULL, 0xFE60CF6CAF321874ULL,
	0xE224479F47CB76A0ULL, 0xA0D4A674EE214033ULL, 0x67C58448141F1B86ULL,
	0x253565A3BDF52D15ULL, 0xAB1721DA49899A7FULL, 0xE9E7C031E063ACECULL,
	0x2EF6E20D1A5DF759ULL, 0x6C0603E6B3B7C1CAULL, 0xF6FAE5C07D3274CDULL,
	0xB40A042BD4D8425EULL, 0x731B26172EE619EBULL, 0x31EBC7FC870C2F78ULL,
	0xBFC9838573709812ULL, 0xFD39626EDA9AAE81ULL, 0x3A28405220A4F534ULL,
	0x78D8A1B9894EC3A7ULL, 0x649C294A61B7AD73ULL, 0x266CC8A1C85D9BE0ULL,
	0xE17DEA9D3263C055ULL, 0xA38D0B769B89F6C6ULL, 0x2DAF4F0F6FF541ACULL,
	0x6F5FAEE4C61F773FULL, 0xA84E8CD83C212C8AULL, 0xEABE6D3395CB1A19ULL,
	0x90C79D3FEDD3F122ULL, 0xD2377CD44439C7B1ULL, 0x15265EE8BE079C04ULL,
	0x57D6BF0317EDAA97ULL, 0xD9F4FB7AE3911DFDULL, 0x9B041A914A7B2B6EULL,
	0x5C1538ADB04570DBULL, 0x1EE5D94619AF4648ULL, 0x02A151B5F156289CULL,
	0x4051B05E58BC1E0FULL, 0x87409262A28245BAULL, 0xC5B073890B687329ULL,
	0x4B9237F0FF14C443ULL, 0x0962D61B56FEF2D0ULL, 0xCE73F427ACC0A965ULL,
	0x8C8315CC052A9FF6ULL, 0x3A80143F5CF17F13ULL, 0x7870F5D4F51B4980ULL,
	0xBF61D7E80F251235ULL, 0xFD913603A6CF24A6ULL, 0x73B3727A52B393CCULL,
	0x31439391FB59A55FULL, 0xF652B1AD0167FEEAULL, 0xB4A25046A88DC879ULL,
	0xA8E6D8B54074A6ADULL, 0xEA16395EE99E903EULL, 0x2D071B6213A0CB8BULL,
	0x6FF7FA89BA4AFD18ULL, 0xE1D5BEF04E364A72ULL, 0xA3255F1BE7DC7CE1ULL,
	0x64347D271DE22754ULL, 0x26C49CCCB40811C7ULL, 0x5CBD6CC0CC10FAFCULL,
	0x1E4D8D2B65FACC6FULL, 0xD95CAF179FC497DAULL, 0x9BAC4EFC362EA149ULL,
	0x158E0A85C2521623ULL, 0x577EEB6E6BB820B0ULL, 0x906FC95291867B05ULL,
	0xD29F28B9386C4D96ULL, 0xCEDBA04AD0952342ULL, 0x8C2B41A1797F15D1ULL,
	0x4B3A639D83414E64ULL, 0x09CA82762AAB78F7ULL, 0x87E8C60FDED7CF9DULL,
	0xC51827E4773DF90EULL, 0x020905D88D03A2BBULL, 0x40F9E43324E99428ULL,
	0x2CFFE7D5975E55E2ULL, 0x6E0F063E3EB46371ULL, 0xA91E2402C48A38C4ULL,
	0xEBEEC5E96D600E57ULL, 0x65CC8190991CB93DULL, 0x273C607B30F68FAEULL,
	0xE02D4247CAC8D41BULL, 0xA2DDA3AC6322E288ULL, 0xBE992B5F8BDB8C5CULL,
	0xFC69CAB42231BACFULL, 0x3B78E888D80FE17AULL, 0x7988096371E5D7E9ULL,
	0xF7AA4D1A85996083ULL, 0xB55AACF12C735610ULL, 0x724B8ECDD64D0DA5ULL,
	0x30BB6F267FA73B36ULL, 0x4AC29F2A07BFD00DULL, 0x08327EC1AE55E69EULL,
	0xCF235CFD546BBD2BULL, 0x8DD3BD16FD818BB8ULL, 0x03F1F96F09FD3CD2ULL,
	0x41011884A0170A41ULL, 0x86103AB85A2951F4ULL, 0xC4E0DB53F3C36767ULL,
	0xD8A453A01B3A09B3ULL, 0x9A54B24BB2D03F20ULL, 0x5D45907748EE6495ULL,
	0x1FB5719CE1045206ULL, 0x919735E51578E56CULL, 0xD367D40EBC92D3FFULL,
	0x1476F63246AC884AULL, 0x568617D9EF46BED9ULL, 0xE085162AB69D5E3CULL,
	0xA275F7C11F7768AFULL, 0x6564D5FDE549331AULL, 0x279434164CA30589ULL,
	0xA9B6706FB8DFB2E3ULL, 0xEB46918411358470ULL, 0x2C57B3B8EB0BDFC5ULL,
	0x6EA7525342E1E956ULL, 0x72E3DAA0AA188782ULL, 0x30133B4B03F2B111ULL,
	0xF7021977F9CCEAA4ULL, 0xB5F2F89C5026DC37ULL, 0x3BD0BCE5A45A6B5DULL,
	0x79205D0E0DB05DCEULL, 0xBE317F32F78E067BULL, 0xFCC19ED95E6430E8ULL,
	0x86B86ED5267CDBD3ULL, 0xC4488F3E8F96ED40ULL, 0x0359AD0275A8B6F5ULL,
	0x41A94CE9DC428066ULL, 0xCF8B0890283E370CULL, 0x8D7BE97B81D4019FULL,
	0x4A6ACB477BEA5A2AULL, 0x089A2AACD2006CB9ULL, 0x14DEA25F3AF9026DULL,
	0x562E43B4931334FEULL, 0x913F6188692D6F4BULL, 0xD3CF8063C0C759D8ULL,
	0x5DEDC41A34BBEEB2ULL, 0x1F1D25F19D51D821ULL, 0xD80C07CD676F8394ULL,
	0x9AFCE626CE85B507ULL,
};

u64 bch2_crc64_update(u64 crc, const void *_data, size_t len)
{
	const unsigned char *data = _data;

	while (len--) {
		int i = ((int) (crc >> 56) ^ *data++) & 0xFF;
		crc = crc_table[i] ^ (crc << 8);
	}

	return crc;
}

static u64 bch2_checksum_init(unsigned type)
{
	switch (type) {
	case BCH_CSUM_NONE:
		return 0;
	case BCH_CSUM_CRC32C_NONZERO:
		return U32_MAX;
	case BCH_CSUM_CRC64_NONZERO:
		return U64_MAX;
	case BCH_CSUM_CRC32C:
		return 0;
	case BCH_CSUM_CRC64:
		return 0;
	default:
		BUG();
	}
}

static u64 bch2_checksum_final(unsigned type, u64 crc)
{
	switch (type) {
	case BCH_CSUM_NONE:
		return 0;
	case BCH_CSUM_CRC32C_NONZERO:
		return crc ^ U32_MAX;
	case BCH_CSUM_CRC64_NONZERO:
		return crc ^ U64_MAX;
	case BCH_CSUM_CRC32C:
		return crc;
	case BCH_CSUM_CRC64:
		return crc;
	default:
		BUG();
	}
}

static u64 bch2_checksum_update(unsigned type, u64 crc, const void *data, size_t len)
{
	switch (type) {
	case BCH_CSUM_NONE:
		return 0;
	case BCH_CSUM_CRC32C_NONZERO:
	case BCH_CSUM_CRC32C:
		return crc32c(crc, data, len);
	case BCH_CSUM_CRC64_NONZERO:
	case BCH_CSUM_CRC64:
		return bch2_crc64_update(crc, data, len);
	default:
		BUG();
	}
}

static inline void do_encrypt_sg(struct crypto_sync_skcipher *tfm,
				 struct nonce nonce,
				 struct scatterlist *sg, size_t len)
{
	SYNC_SKCIPHER_REQUEST_ON_STACK(req, tfm);
	int ret;

	skcipher_request_set_sync_tfm(req, tfm);
	skcipher_request_set_crypt(req, sg, sg, len, nonce.d);

	ret = crypto_skcipher_encrypt(req);
	BUG_ON(ret);
}

static inline void do_encrypt(struct crypto_sync_skcipher *tfm,
			      struct nonce nonce,
			      void *buf, size_t len)
{
	struct scatterlist sg;

	sg_init_one(&sg, buf, len);
	do_encrypt_sg(tfm, nonce, &sg, len);
}

int bch2_chacha_encrypt_key(struct bch_key *key, struct nonce nonce,
			    void *buf, size_t len)
{
	struct crypto_sync_skcipher *chacha20 =
		crypto_alloc_sync_skcipher("chacha20", 0, 0);
	int ret;

	if (!chacha20) {
		pr_err("error requesting chacha20 module: %li", PTR_ERR(chacha20));
		return PTR_ERR(chacha20);
	}

	ret = crypto_skcipher_setkey(&chacha20->base,
				     (void *) key, sizeof(*key));
	if (ret) {
		pr_err("crypto_skcipher_setkey() error: %i", ret);
		goto err;
	}

	do_encrypt(chacha20, nonce, buf, len);
err:
	crypto_free_sync_skcipher(chacha20);
	return ret;
}

static void gen_poly_key(struct bch_fs *c, struct shash_desc *desc,
			 struct nonce nonce)
{
	u8 key[POLY1305_KEY_SIZE];

	nonce.d[3] ^= BCH_NONCE_POLY;

	memset(key, 0, sizeof(key));
	do_encrypt(c->chacha20, nonce, key, sizeof(key));

	desc->tfm = c->poly1305;
	crypto_shash_init(desc);
	crypto_shash_update(desc, key, sizeof(key));
}

struct bch_csum bch2_checksum(struct bch_fs *c, unsigned type,
			      struct nonce nonce, const void *data, size_t len)
{
	switch (type) {
	case BCH_CSUM_NONE:
	case BCH_CSUM_CRC32C_NONZERO:
	case BCH_CSUM_CRC64_NONZERO:
	case BCH_CSUM_CRC32C:
	case BCH_CSUM_CRC64: {
		u64 crc = bch2_checksum_init(type);

		crc = bch2_checksum_update(type, crc, data, len);
		crc = bch2_checksum_final(type, crc);

		return (struct bch_csum) { .lo = cpu_to_le64(crc) };
	}

	case BCH_CSUM_CHACHA20_POLY1305_80:
	case BCH_CSUM_CHACHA20_POLY1305_128: {
		SHASH_DESC_ON_STACK(desc, c->poly1305);
		u8 digest[POLY1305_DIGEST_SIZE];
		struct bch_csum ret = { 0 };

		gen_poly_key(c, desc, nonce);

		crypto_shash_update(desc, data, len);
		crypto_shash_final(desc, digest);

		memcpy(&ret, digest, bch_crc_bytes[type]);
		return ret;
	}
	default:
		BUG();
	}
}

void bch2_encrypt(struct bch_fs *c, unsigned type,
		  struct nonce nonce, void *data, size_t len)
{
	if (!bch2_csum_type_is_encryption(type))
		return;

	do_encrypt(c->chacha20, nonce, data, len);
}

static struct bch_csum __bch2_checksum_bio(struct bch_fs *c, unsigned type,
					   struct nonce nonce, struct bio *bio,
					   struct bvec_iter *iter)
{
	struct bio_vec bv;

	switch (type) {
	case BCH_CSUM_NONE:
		return (struct bch_csum) { 0 };
	case BCH_CSUM_CRC32C_NONZERO:
	case BCH_CSUM_CRC64_NONZERO:
	case BCH_CSUM_CRC32C:
	case BCH_CSUM_CRC64: {
		u64 crc = bch2_checksum_init(type);

#ifdef CONFIG_HIGHMEM
		__bio_for_each_segment(bv, bio, *iter, *iter) {
			void *p = kmap_atomic(bv.bv_page) + bv.bv_offset;
			crc = bch2_checksum_update(type,
				crc, p, bv.bv_len);
			kunmap_atomic(p);
		}
#else
		__bio_for_each_contig_segment(bv, bio, *iter, *iter)
			crc = bch2_checksum_update(type, crc,
				page_address(bv.bv_page) + bv.bv_offset,
				bv.bv_len);
#endif
		crc = bch2_checksum_final(type, crc);
		return (struct bch_csum) { .lo = cpu_to_le64(crc) };
	}

	case BCH_CSUM_CHACHA20_POLY1305_80:
	case BCH_CSUM_CHACHA20_POLY1305_128: {
		SHASH_DESC_ON_STACK(desc, c->poly1305);
		u8 digest[POLY1305_DIGEST_SIZE];
		struct bch_csum ret = { 0 };

		gen_poly_key(c, desc, nonce);

#ifdef CONFIG_HIGHMEM
		__bio_for_each_segment(bv, bio, *iter, *iter) {
			void *p = kmap_atomic(bv.bv_page) + bv.bv_offset;

			crypto_shash_update(desc, p, bv.bv_len);
			kunmap_atomic(p);
		}
#else
		__bio_for_each_contig_segment(bv, bio, *iter, *iter)
			crypto_shash_update(desc,
				page_address(bv.bv_page) + bv.bv_offset,
				bv.bv_len);
#endif
		crypto_shash_final(desc, digest);

		memcpy(&ret, digest, bch_crc_bytes[type]);
		return ret;
	}
	default:
		BUG();
	}
}

struct bch_csum bch2_checksum_bio(struct bch_fs *c, unsigned type,
				  struct nonce nonce, struct bio *bio)
{
	struct bvec_iter iter = bio->bi_iter;

	return __bch2_checksum_bio(c, type, nonce, bio, &iter);
}

void bch2_encrypt_bio(struct bch_fs *c, unsigned type,
		      struct nonce nonce, struct bio *bio)
{
	struct bio_vec bv;
	struct bvec_iter iter;
	struct scatterlist sgl[16], *sg = sgl;
	size_t bytes = 0;

	if (!bch2_csum_type_is_encryption(type))
		return;

	sg_init_table(sgl, ARRAY_SIZE(sgl));

	bio_for_each_segment(bv, bio, iter) {
		if (sg == sgl + ARRAY_SIZE(sgl)) {
			sg_mark_end(sg - 1);
			do_encrypt_sg(c->chacha20, nonce, sgl, bytes);

			nonce = nonce_add(nonce, bytes);
			bytes = 0;

			sg_init_table(sgl, ARRAY_SIZE(sgl));
			sg = sgl;
		}

		sg_set_page(sg++, bv.bv_page, bv.bv_len, bv.bv_offset);
		bytes += bv.bv_len;
	}

	sg_mark_end(sg - 1);
	do_encrypt_sg(c->chacha20, nonce, sgl, bytes);
}

static inline bool bch2_checksum_mergeable(unsigned type)
{

	switch (type) {
	case BCH_CSUM_NONE:
	case BCH_CSUM_CRC32C:
	case BCH_CSUM_CRC64:
		return true;
	default:
		return false;
	}
}

static struct bch_csum bch2_checksum_merge(unsigned type,
					   struct bch_csum a,
					   struct bch_csum b, size_t b_len)
{
	BUG_ON(!bch2_checksum_mergeable(type));

	while (b_len) {
		unsigned b = min_t(unsigned, b_len, PAGE_SIZE);

		a.lo = bch2_checksum_update(type, a.lo,
				page_address(ZERO_PAGE(0)), b);
		b_len -= b;
	}

	a.lo ^= b.lo;
	a.hi ^= b.hi;
	return a;
}

int bch2_rechecksum_bio(struct bch_fs *c, struct bio *bio,
			struct bversion version,
			struct bch_extent_crc_unpacked crc_old,
			struct bch_extent_crc_unpacked *crc_a,
			struct bch_extent_crc_unpacked *crc_b,
			unsigned len_a, unsigned len_b,
			unsigned new_csum_type)
{
	struct bvec_iter iter = bio->bi_iter;
	struct nonce nonce = extent_nonce(version, crc_old);
	struct bch_csum merged = { 0 };
	struct crc_split {
		struct bch_extent_crc_unpacked	*crc;
		unsigned			len;
		unsigned			csum_type;
		struct bch_csum			csum;
	} splits[3] = {
		{ crc_a, len_a, new_csum_type },
		{ crc_b, len_b, new_csum_type },
		{ NULL,	 bio_sectors(bio) - len_a - len_b, new_csum_type },
	}, *i;
	bool mergeable = crc_old.csum_type == new_csum_type &&
		bch2_checksum_mergeable(new_csum_type);
	unsigned crc_nonce = crc_old.nonce;

	BUG_ON(len_a + len_b > bio_sectors(bio));
	BUG_ON(crc_old.uncompressed_size != bio_sectors(bio));
	BUG_ON(crc_old.compression_type);
	BUG_ON(bch2_csum_type_is_encryption(crc_old.csum_type) !=
	       bch2_csum_type_is_encryption(new_csum_type));

	for (i = splits; i < splits + ARRAY_SIZE(splits); i++) {
		iter.bi_size = i->len << 9;
		if (mergeable || i->crc)
			i->csum = __bch2_checksum_bio(c, i->csum_type,
						      nonce, bio, &iter);
		else
			bio_advance_iter(bio, &iter, i->len << 9);
		nonce = nonce_add(nonce, i->len << 9);
	}

	if (mergeable)
		for (i = splits; i < splits + ARRAY_SIZE(splits); i++)
			merged = bch2_checksum_merge(new_csum_type, merged,
						     i->csum, i->len << 9);
	else
		merged = bch2_checksum_bio(c, crc_old.csum_type,
				extent_nonce(version, crc_old), bio);

	if (bch2_crc_cmp(merged, crc_old.csum))
		return -EIO;

	for (i = splits; i < splits + ARRAY_SIZE(splits); i++) {
		if (i->crc)
			*i->crc = (struct bch_extent_crc_unpacked) {
				.csum_type		= i->csum_type,
				.compressed_size	= i->len,
				.uncompressed_size	= i->len,
				.offset			= 0,
				.live_size		= i->len,
				.nonce			= crc_nonce,
				.csum			= i->csum,
			};

		if (bch2_csum_type_is_encryption(new_csum_type))
			crc_nonce += i->len;
	}

	return 0;
}

#ifdef __KERNEL__
int bch2_request_key(struct bch_sb *sb, struct bch_key *key)
{
	char key_description[60];
	struct key *keyring_key;
	const struct user_key_payload *ukp;
	int ret;

	snprintf(key_description, sizeof(key_description),
		 "bcachefs:%pUb", &sb->user_uuid);

	keyring_key = request_key(&key_type_logon, key_description, NULL);
	if (IS_ERR(keyring_key))
		return PTR_ERR(keyring_key);

	down_read(&keyring_key->sem);
	ukp = dereference_key_locked(keyring_key);
	if (ukp->datalen == sizeof(*key)) {
		memcpy(key, ukp->data, ukp->datalen);
		ret = 0;
	} else {
		ret = -EINVAL;
	}
	up_read(&keyring_key->sem);
	key_put(keyring_key);

	return ret;
}
#else
#include <keyutils.h>
#include <uuid/uuid.h>

int bch2_request_key(struct bch_sb *sb, struct bch_key *key)
{
	key_serial_t key_id;
	char key_description[60];
	char uuid[40];

	uuid_unparse_lower(sb->user_uuid.b, uuid);
	sprintf(key_description, "bcachefs:%s", uuid);

	key_id = request_key("user", key_description, NULL,
			     KEY_SPEC_USER_KEYRING);
	if (key_id < 0)
		return -errno;

	if (keyctl_read(key_id, (void *) key, sizeof(*key)) != sizeof(*key))
		return -1;

	return 0;
}
#endif

int bch2_decrypt_sb_key(struct bch_fs *c,
			struct bch_sb_field_crypt *crypt,
			struct bch_key *key)
{
	struct bch_encrypted_key sb_key = crypt->key;
	struct bch_key user_key;
	int ret = 0;

	/* is key encrypted? */
	if (!bch2_key_is_encrypted(&sb_key))
		goto out;

	ret = bch2_request_key(c->disk_sb.sb, &user_key);
	if (ret) {
		bch_err(c, "error requesting encryption key: %i", ret);
		goto err;
	}

	/* decrypt real key: */
	ret = bch2_chacha_encrypt_key(&user_key, bch2_sb_key_nonce(c),
			     &sb_key, sizeof(sb_key));
	if (ret)
		goto err;

	if (bch2_key_is_encrypted(&sb_key)) {
		bch_err(c, "incorrect encryption key");
		ret = -EINVAL;
		goto err;
	}
out:
	*key = sb_key.key;
err:
	memzero_explicit(&sb_key, sizeof(sb_key));
	memzero_explicit(&user_key, sizeof(user_key));
	return ret;
}

static int bch2_alloc_ciphers(struct bch_fs *c)
{
	if (!c->chacha20)
		c->chacha20 = crypto_alloc_sync_skcipher("chacha20", 0, 0);
	if (IS_ERR(c->chacha20)) {
		bch_err(c, "error requesting chacha20 module: %li",
			PTR_ERR(c->chacha20));
		return PTR_ERR(c->chacha20);
	}

	if (!c->poly1305)
		c->poly1305 = crypto_alloc_shash("poly1305", 0, 0);
	if (IS_ERR(c->poly1305)) {
		bch_err(c, "error requesting poly1305 module: %li",
			PTR_ERR(c->poly1305));
		return PTR_ERR(c->poly1305);
	}

	return 0;
}

int bch2_disable_encryption(struct bch_fs *c)
{
	struct bch_sb_field_crypt *crypt;
	struct bch_key key;
	int ret = -EINVAL;

	mutex_lock(&c->sb_lock);

	crypt = bch2_sb_get_crypt(c->disk_sb.sb);
	if (!crypt)
		goto out;

	/* is key encrypted? */
	ret = 0;
	if (bch2_key_is_encrypted(&crypt->key))
		goto out;

	ret = bch2_decrypt_sb_key(c, crypt, &key);
	if (ret)
		goto out;

	crypt->key.magic	= BCH_KEY_MAGIC;
	crypt->key.key		= key;

	SET_BCH_SB_ENCRYPTION_TYPE(c->disk_sb.sb, 0);
	bch2_write_super(c);
out:
	mutex_unlock(&c->sb_lock);

	return ret;
}

int bch2_enable_encryption(struct bch_fs *c, bool keyed)
{
	struct bch_encrypted_key key;
	struct bch_key user_key;
	struct bch_sb_field_crypt *crypt;
	int ret = -EINVAL;

	mutex_lock(&c->sb_lock);

	/* Do we already have an encryption key? */
	if (bch2_sb_get_crypt(c->disk_sb.sb))
		goto err;

	ret = bch2_alloc_ciphers(c);
	if (ret)
		goto err;

	key.magic = BCH_KEY_MAGIC;
	get_random_bytes(&key.key, sizeof(key.key));

	if (keyed) {
		ret = bch2_request_key(c->disk_sb.sb, &user_key);
		if (ret) {
			bch_err(c, "error requesting encryption key: %i", ret);
			goto err;
		}

		ret = bch2_chacha_encrypt_key(&user_key, bch2_sb_key_nonce(c),
					      &key, sizeof(key));
		if (ret)
			goto err;
	}

	ret = crypto_skcipher_setkey(&c->chacha20->base,
			(void *) &key.key, sizeof(key.key));
	if (ret)
		goto err;

	crypt = bch2_sb_resize_crypt(&c->disk_sb, sizeof(*crypt) / sizeof(u64));
	if (!crypt) {
		ret = -ENOMEM; /* XXX this technically could be -ENOSPC */
		goto err;
	}

	crypt->key = key;

	/* write superblock */
	SET_BCH_SB_ENCRYPTION_TYPE(c->disk_sb.sb, 1);
	bch2_write_super(c);
err:
	mutex_unlock(&c->sb_lock);
	memzero_explicit(&user_key, sizeof(user_key));
	memzero_explicit(&key, sizeof(key));
	return ret;
}

void bch2_fs_encryption_exit(struct bch_fs *c)
{
	if (!IS_ERR_OR_NULL(c->poly1305))
		crypto_free_shash(c->poly1305);
	if (!IS_ERR_OR_NULL(c->chacha20))
		crypto_free_sync_skcipher(c->chacha20);
	if (!IS_ERR_OR_NULL(c->sha256))
		crypto_free_shash(c->sha256);
}

int bch2_fs_encryption_init(struct bch_fs *c)
{
	struct bch_sb_field_crypt *crypt;
	struct bch_key key;
	int ret = 0;

	pr_verbose_init(c->opts, "");

	c->sha256 = crypto_alloc_shash("sha256", 0, 0);
	if (IS_ERR(c->sha256)) {
		bch_err(c, "error requesting sha256 module");
		ret = PTR_ERR(c->sha256);
		goto out;
	}

	crypt = bch2_sb_get_crypt(c->disk_sb.sb);
	if (!crypt)
		goto out;

	ret = bch2_alloc_ciphers(c);
	if (ret)
		goto out;

	ret = bch2_decrypt_sb_key(c, crypt, &key);
	if (ret)
		goto out;

	ret = crypto_skcipher_setkey(&c->chacha20->base,
			(void *) &key.key, sizeof(key.key));
	if (ret)
		goto out;
out:
	memzero_explicit(&key, sizeof(key));
	pr_verbose_init(c->opts, "ret %i", ret);
	return ret;
}
