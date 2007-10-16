/**
 * eCryptfs: Linux filesystem encryption layer
 *
 * Copyright (C) 1997-2004 Erez Zadok
 * Copyright (C) 2001-2004 Stony Brook University
 * Copyright (C) 2004-2007 International Business Machines Corp.
 *   Author(s): Michael A. Halcrow <mahalcro@us.ibm.com>
 *   		Michael C. Thompson <mcthomps@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/random.h>
#include <linux/compiler.h>
#include <linux/key.h>
#include <linux/namei.h>
#include <linux/crypto.h>
#include <linux/file.h>
#include <linux/scatterlist.h>
#include "ecryptfs_kernel.h"

static int
ecryptfs_decrypt_page_offset(struct ecryptfs_crypt_stat *crypt_stat,
			     struct page *dst_page, int dst_offset,
			     struct page *src_page, int src_offset, int size,
			     unsigned char *iv);
static int
ecryptfs_encrypt_page_offset(struct ecryptfs_crypt_stat *crypt_stat,
			     struct page *dst_page, int dst_offset,
			     struct page *src_page, int src_offset, int size,
			     unsigned char *iv);

/**
 * ecryptfs_to_hex
 * @dst: Buffer to take hex character representation of contents of
 *       src; must be at least of size (src_size * 2)
 * @src: Buffer to be converted to a hex string respresentation
 * @src_size: number of bytes to convert
 */
void ecryptfs_to_hex(char *dst, char *src, size_t src_size)
{
	int x;

	for (x = 0; x < src_size; x++)
		sprintf(&dst[x * 2], "%.2x", (unsigned char)src[x]);
}

/**
 * ecryptfs_from_hex
 * @dst: Buffer to take the bytes from src hex; must be at least of
 *       size (src_size / 2)
 * @src: Buffer to be converted from a hex string respresentation to raw value
 * @dst_size: size of dst buffer, or number of hex characters pairs to convert
 */
void ecryptfs_from_hex(char *dst, char *src, int dst_size)
{
	int x;
	char tmp[3] = { 0, };

	for (x = 0; x < dst_size; x++) {
		tmp[0] = src[x * 2];
		tmp[1] = src[x * 2 + 1];
		dst[x] = (unsigned char)simple_strtol(tmp, NULL, 16);
	}
}

/**
 * ecryptfs_calculate_md5 - calculates the md5 of @src
 * @dst: Pointer to 16 bytes of allocated memory
 * @crypt_stat: Pointer to crypt_stat struct for the current inode
 * @src: Data to be md5'd
 * @len: Length of @src
 *
 * Uses the allocated crypto context that crypt_stat references to
 * generate the MD5 sum of the contents of src.
 */
static int ecryptfs_calculate_md5(char *dst,
				  struct ecryptfs_crypt_stat *crypt_stat,
				  char *src, int len)
{
	struct scatterlist sg;
	struct hash_desc desc = {
		.tfm = crypt_stat->hash_tfm,
		.flags = CRYPTO_TFM_REQ_MAY_SLEEP
	};
	int rc = 0;

	mutex_lock(&crypt_stat->cs_hash_tfm_mutex);
	sg_init_one(&sg, (u8 *)src, len);
	if (!desc.tfm) {
		desc.tfm = crypto_alloc_hash(ECRYPTFS_DEFAULT_HASH, 0,
					     CRYPTO_ALG_ASYNC);
		if (IS_ERR(desc.tfm)) {
			rc = PTR_ERR(desc.tfm);
			ecryptfs_printk(KERN_ERR, "Error attempting to "
					"allocate crypto context; rc = [%d]\n",
					rc);
			goto out;
		}
		crypt_stat->hash_tfm = desc.tfm;
	}
	crypto_hash_init(&desc);
	crypto_hash_update(&desc, &sg, len);
	crypto_hash_final(&desc, dst);
	mutex_unlock(&crypt_stat->cs_hash_tfm_mutex);
out:
	return rc;
}

static int ecryptfs_crypto_api_algify_cipher_name(char **algified_name,
						  char *cipher_name,
						  char *chaining_modifier)
{
	int cipher_name_len = strlen(cipher_name);
	int chaining_modifier_len = strlen(chaining_modifier);
	int algified_name_len;
	int rc;

	algified_name_len = (chaining_modifier_len + cipher_name_len + 3);
	(*algified_name) = kmalloc(algified_name_len, GFP_KERNEL);
	if (!(*algified_name)) {
		rc = -ENOMEM;
		goto out;
	}
	snprintf((*algified_name), algified_name_len, "%s(%s)",
		 chaining_modifier, cipher_name);
	rc = 0;
out:
	return rc;
}

/**
 * ecryptfs_derive_iv
 * @iv: destination for the derived iv vale
 * @crypt_stat: Pointer to crypt_stat struct for the current inode
 * @offset: Offset of the page whose's iv we are to derive
 *
 * Generate the initialization vector from the given root IV and page
 * offset.
 *
 * Returns zero on success; non-zero on error.
 */
static int ecryptfs_derive_iv(char *iv, struct ecryptfs_crypt_stat *crypt_stat,
			      pgoff_t offset)
{
	int rc = 0;
	char dst[MD5_DIGEST_SIZE];
	char src[ECRYPTFS_MAX_IV_BYTES + 16];

	if (unlikely(ecryptfs_verbosity > 0)) {
		ecryptfs_printk(KERN_DEBUG, "root iv:\n");
		ecryptfs_dump_hex(crypt_stat->root_iv, crypt_stat->iv_bytes);
	}
	/* TODO: It is probably secure to just cast the least
	 * significant bits of the root IV into an unsigned long and
	 * add the offset to that rather than go through all this
	 * hashing business. -Halcrow */
	memcpy(src, crypt_stat->root_iv, crypt_stat->iv_bytes);
	memset((src + crypt_stat->iv_bytes), 0, 16);
	snprintf((src + crypt_stat->iv_bytes), 16, "%ld", offset);
	if (unlikely(ecryptfs_verbosity > 0)) {
		ecryptfs_printk(KERN_DEBUG, "source:\n");
		ecryptfs_dump_hex(src, (crypt_stat->iv_bytes + 16));
	}
	rc = ecryptfs_calculate_md5(dst, crypt_stat, src,
				    (crypt_stat->iv_bytes + 16));
	if (rc) {
		ecryptfs_printk(KERN_WARNING, "Error attempting to compute "
				"MD5 while generating IV for a page\n");
		goto out;
	}
	memcpy(iv, dst, crypt_stat->iv_bytes);
	if (unlikely(ecryptfs_verbosity > 0)) {
		ecryptfs_printk(KERN_DEBUG, "derived iv:\n");
		ecryptfs_dump_hex(iv, crypt_stat->iv_bytes);
	}
out:
	return rc;
}

/**
 * ecryptfs_init_crypt_stat
 * @crypt_stat: Pointer to the crypt_stat struct to initialize.
 *
 * Initialize the crypt_stat structure.
 */
void
ecryptfs_init_crypt_stat(struct ecryptfs_crypt_stat *crypt_stat)
{
	memset((void *)crypt_stat, 0, sizeof(struct ecryptfs_crypt_stat));
	INIT_LIST_HEAD(&crypt_stat->keysig_list);
	mutex_init(&crypt_stat->keysig_list_mutex);
	mutex_init(&crypt_stat->cs_mutex);
	mutex_init(&crypt_stat->cs_tfm_mutex);
	mutex_init(&crypt_stat->cs_hash_tfm_mutex);
	crypt_stat->flags |= ECRYPTFS_STRUCT_INITIALIZED;
}

/**
 * ecryptfs_destroy_crypt_stat
 * @crypt_stat: Pointer to the crypt_stat struct to initialize.
 *
 * Releases all memory associated with a crypt_stat struct.
 */
void ecryptfs_destroy_crypt_stat(struct ecryptfs_crypt_stat *crypt_stat)
{
	struct ecryptfs_key_sig *key_sig, *key_sig_tmp;

	if (crypt_stat->tfm)
		crypto_free_blkcipher(crypt_stat->tfm);
	if (crypt_stat->hash_tfm)
		crypto_free_hash(crypt_stat->hash_tfm);
	mutex_lock(&crypt_stat->keysig_list_mutex);
	list_for_each_entry_safe(key_sig, key_sig_tmp,
				 &crypt_stat->keysig_list, crypt_stat_list) {
		list_del(&key_sig->crypt_stat_list);
		kmem_cache_free(ecryptfs_key_sig_cache, key_sig);
	}
	mutex_unlock(&crypt_stat->keysig_list_mutex);
	memset(crypt_stat, 0, sizeof(struct ecryptfs_crypt_stat));
}

void ecryptfs_destroy_mount_crypt_stat(
	struct ecryptfs_mount_crypt_stat *mount_crypt_stat)
{
	struct ecryptfs_global_auth_tok *auth_tok, *auth_tok_tmp;

	if (!(mount_crypt_stat->flags & ECRYPTFS_MOUNT_CRYPT_STAT_INITIALIZED))
		return;
	mutex_lock(&mount_crypt_stat->global_auth_tok_list_mutex);
	list_for_each_entry_safe(auth_tok, auth_tok_tmp,
				 &mount_crypt_stat->global_auth_tok_list,
				 mount_crypt_stat_list) {
		list_del(&auth_tok->mount_crypt_stat_list);
		mount_crypt_stat->num_global_auth_toks--;
		if (auth_tok->global_auth_tok_key
		    && !(auth_tok->flags & ECRYPTFS_AUTH_TOK_INVALID))
			key_put(auth_tok->global_auth_tok_key);
		kmem_cache_free(ecryptfs_global_auth_tok_cache, auth_tok);
	}
	mutex_unlock(&mount_crypt_stat->global_auth_tok_list_mutex);
	memset(mount_crypt_stat, 0, sizeof(struct ecryptfs_mount_crypt_stat));
}

/**
 * virt_to_scatterlist
 * @addr: Virtual address
 * @size: Size of data; should be an even multiple of the block size
 * @sg: Pointer to scatterlist array; set to NULL to obtain only
 *      the number of scatterlist structs required in array
 * @sg_size: Max array size
 *
 * Fills in a scatterlist array with page references for a passed
 * virtual address.
 *
 * Returns the number of scatterlist structs in array used
 */
int virt_to_scatterlist(const void *addr, int size, struct scatterlist *sg,
			int sg_size)
{
	int i = 0;
	struct page *pg;
	int offset;
	int remainder_of_page;

	while (size > 0 && i < sg_size) {
		pg = virt_to_page(addr);
		offset = offset_in_page(addr);
		if (sg) {
			sg[i].page = pg;
			sg[i].offset = offset;
		}
		remainder_of_page = PAGE_CACHE_SIZE - offset;
		if (size >= remainder_of_page) {
			if (sg)
				sg[i].length = remainder_of_page;
			addr += remainder_of_page;
			size -= remainder_of_page;
		} else {
			if (sg)
				sg[i].length = size;
			addr += size;
			size = 0;
		}
		i++;
	}
	if (size > 0)
		return -ENOMEM;
	return i;
}

/**
 * encrypt_scatterlist
 * @crypt_stat: Pointer to the crypt_stat struct to initialize.
 * @dest_sg: Destination of encrypted data
 * @src_sg: Data to be encrypted
 * @size: Length of data to be encrypted
 * @iv: iv to use during encryption
 *
 * Returns the number of bytes encrypted; negative value on error
 */
static int encrypt_scatterlist(struct ecryptfs_crypt_stat *crypt_stat,
			       struct scatterlist *dest_sg,
			       struct scatterlist *src_sg, int size,
			       unsigned char *iv)
{
	struct blkcipher_desc desc = {
		.tfm = crypt_stat->tfm,
		.info = iv,
		.flags = CRYPTO_TFM_REQ_MAY_SLEEP
	};
	int rc = 0;

	BUG_ON(!crypt_stat || !crypt_stat->tfm
	       || !(crypt_stat->flags & ECRYPTFS_STRUCT_INITIALIZED));
	if (unlikely(ecryptfs_verbosity > 0)) {
		ecryptfs_printk(KERN_DEBUG, "Key size [%d]; key:\n",
				crypt_stat->key_size);
		ecryptfs_dump_hex(crypt_stat->key,
				  crypt_stat->key_size);
	}
	/* Consider doing this once, when the file is opened */
	mutex_lock(&crypt_stat->cs_tfm_mutex);
	rc = crypto_blkcipher_setkey(crypt_stat->tfm, crypt_stat->key,
				     crypt_stat->key_size);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error setting key; rc = [%d]\n",
				rc);
		mutex_unlock(&crypt_stat->cs_tfm_mutex);
		rc = -EINVAL;
		goto out;
	}
	ecryptfs_printk(KERN_DEBUG, "Encrypting [%d] bytes.\n", size);
	crypto_blkcipher_encrypt_iv(&desc, dest_sg, src_sg, size);
	mutex_unlock(&crypt_stat->cs_tfm_mutex);
out:
	return rc;
}

static void
ecryptfs_extent_to_lwr_pg_idx_and_offset(unsigned long *lower_page_idx,
					 int *byte_offset,
					 struct ecryptfs_crypt_stat *crypt_stat,
					 unsigned long extent_num)
{
	unsigned long lower_extent_num;
	int extents_occupied_by_headers_at_front;
	int bytes_occupied_by_headers_at_front;
	int extent_offset;
	int extents_per_page;

	bytes_occupied_by_headers_at_front =
		(crypt_stat->extent_size
		 * crypt_stat->num_header_extents_at_front);
	extents_occupied_by_headers_at_front =
		( bytes_occupied_by_headers_at_front
		  / crypt_stat->extent_size );
	lower_extent_num = extents_occupied_by_headers_at_front + extent_num;
	extents_per_page = PAGE_CACHE_SIZE / crypt_stat->extent_size;
	(*lower_page_idx) = lower_extent_num / extents_per_page;
	extent_offset = lower_extent_num % extents_per_page;
	(*byte_offset) = extent_offset * crypt_stat->extent_size;
	ecryptfs_printk(KERN_DEBUG, " * crypt_stat->extent_size = "
			"[%d]\n", crypt_stat->extent_size);
	ecryptfs_printk(KERN_DEBUG, " * crypt_stat->"
			"num_header_extents_at_front = [%d]\n",
			crypt_stat->num_header_extents_at_front);
	ecryptfs_printk(KERN_DEBUG, " * extents_occupied_by_headers_at_"
			"front = [%d]\n", extents_occupied_by_headers_at_front);
	ecryptfs_printk(KERN_DEBUG, " * lower_extent_num = [0x%.16x]\n",
			lower_extent_num);
	ecryptfs_printk(KERN_DEBUG, " * extents_per_page = [%d]\n",
			extents_per_page);
	ecryptfs_printk(KERN_DEBUG, " * (*lower_page_idx) = [0x%.16x]\n",
			(*lower_page_idx));
	ecryptfs_printk(KERN_DEBUG, " * extent_offset = [%d]\n",
			extent_offset);
	ecryptfs_printk(KERN_DEBUG, " * (*byte_offset) = [%d]\n",
			(*byte_offset));
}

static int ecryptfs_write_out_page(struct ecryptfs_page_crypt_context *ctx,
				   struct page *lower_page,
				   struct inode *lower_inode,
				   int byte_offset_in_page, int bytes_to_write)
{
	int rc = 0;

	if (ctx->mode == ECRYPTFS_PREPARE_COMMIT_MODE) {
		rc = ecryptfs_commit_lower_page(lower_page, lower_inode,
						ctx->param.lower_file,
						byte_offset_in_page,
						bytes_to_write);
		if (rc) {
			ecryptfs_printk(KERN_ERR, "Error calling lower "
					"commit; rc = [%d]\n", rc);
			goto out;
		}
	} else {
		rc = ecryptfs_writepage_and_release_lower_page(lower_page,
							       lower_inode,
							       ctx->param.wbc);
		if (rc) {
			ecryptfs_printk(KERN_ERR, "Error calling lower "
					"writepage(); rc = [%d]\n", rc);
			goto out;
		}
	}
out:
	return rc;
}

static int ecryptfs_read_in_page(struct ecryptfs_page_crypt_context *ctx,
				 struct page **lower_page,
				 struct inode *lower_inode,
				 unsigned long lower_page_idx,
				 int byte_offset_in_page)
{
	int rc = 0;

	if (ctx->mode == ECRYPTFS_PREPARE_COMMIT_MODE) {
		/* TODO: Limit this to only the data extents that are
		 * needed */
		rc = ecryptfs_get_lower_page(lower_page, lower_inode,
					     ctx->param.lower_file,
					     lower_page_idx,
					     byte_offset_in_page,
					     (PAGE_CACHE_SIZE
					      - byte_offset_in_page));
		if (rc) {
			ecryptfs_printk(
				KERN_ERR, "Error attempting to grab, map, "
				"and prepare_write lower page with index "
				"[0x%.16x]; rc = [%d]\n", lower_page_idx, rc);
			goto out;
		}
	} else {
		*lower_page = grab_cache_page(lower_inode->i_mapping,
					      lower_page_idx);
		if (!(*lower_page)) {
			rc = -EINVAL;
			ecryptfs_printk(
				KERN_ERR, "Error attempting to grab and map "
				"lower page with index [0x%.16x]; rc = [%d]\n",
				lower_page_idx, rc);
			goto out;
		}
	}
out:
	return rc;
}

/**
 * ecryptfs_lower_offset_for_extent
 *
 * Convert an eCryptfs page index into a lower byte offset
 */
void ecryptfs_lower_offset_for_extent(loff_t *offset, loff_t extent_num,
				      struct ecryptfs_crypt_stat *crypt_stat)
{
	(*offset) = ((crypt_stat->extent_size
		      * crypt_stat->num_header_extents_at_front)
		     + (crypt_stat->extent_size * extent_num));
}

/**
 * ecryptfs_encrypt_extent
 * @enc_extent_page: Allocated page into which to encrypt the data in
 *                   @page
 * @crypt_stat: crypt_stat containing cryptographic context for the
 *              encryption operation
 * @page: Page containing plaintext data extent to encrypt
 * @extent_offset: Page extent offset for use in generating IV
 *
 * Encrypts one extent of data.
 *
 * Return zero on success; non-zero otherwise
 */
static int ecryptfs_encrypt_extent(struct page *enc_extent_page,
				   struct ecryptfs_crypt_stat *crypt_stat,
				   struct page *page,
				   unsigned long extent_offset)
{
	unsigned long extent_base;
	char extent_iv[ECRYPTFS_MAX_IV_BYTES];
	int rc;

	extent_base = (page->index
		       * (PAGE_CACHE_SIZE / crypt_stat->extent_size));
	rc = ecryptfs_derive_iv(extent_iv, crypt_stat,
				(extent_base + extent_offset));
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error attempting to "
				"derive IV for extent [0x%.16x]; "
				"rc = [%d]\n", (extent_base + extent_offset),
				rc);
		goto out;
	}
	if (unlikely(ecryptfs_verbosity > 0)) {
		ecryptfs_printk(KERN_DEBUG, "Encrypting extent "
				"with iv:\n");
		ecryptfs_dump_hex(extent_iv, crypt_stat->iv_bytes);
		ecryptfs_printk(KERN_DEBUG, "First 8 bytes before "
				"encryption:\n");
		ecryptfs_dump_hex((char *)
				  (page_address(page)
				   + (extent_offset * crypt_stat->extent_size)),
				  8);
	}
	rc = ecryptfs_encrypt_page_offset(crypt_stat, enc_extent_page, 0,
					  page, (extent_offset
						 * crypt_stat->extent_size),
					  crypt_stat->extent_size, extent_iv);
	if (rc < 0) {
		printk(KERN_ERR "%s: Error attempting to encrypt page with "
		       "page->index = [%ld], extent_offset = [%ld]; "
		       "rc = [%d]\n", __FUNCTION__, page->index, extent_offset,
		       rc);
		goto out;
	}
	rc = 0;
	if (unlikely(ecryptfs_verbosity > 0)) {
		ecryptfs_printk(KERN_DEBUG, "Encrypt extent [0x%.16x]; "
				"rc = [%d]\n", (extent_base + extent_offset),
				rc);
		ecryptfs_printk(KERN_DEBUG, "First 8 bytes after "
				"encryption:\n");
		ecryptfs_dump_hex((char *)(page_address(enc_extent_page)), 8);
	}
out:
	return rc;
}

/**
 * ecryptfs_encrypt_page
 * @page: Page mapped from the eCryptfs inode for the file; contains
 *        decrypted content that needs to be encrypted (to a temporary
 *        page; not in place) and written out to the lower file
 *
 * Encrypt an eCryptfs page. This is done on a per-extent basis. Note
 * that eCryptfs pages may straddle the lower pages -- for instance,
 * if the file was created on a machine with an 8K page size
 * (resulting in an 8K header), and then the file is copied onto a
 * host with a 32K page size, then when reading page 0 of the eCryptfs
 * file, 24K of page 0 of the lower file will be read and decrypted,
 * and then 8K of page 1 of the lower file will be read and decrypted.
 *
 * Returns zero on success; negative on error
 */
int ecryptfs_encrypt_page(struct page *page)
{
	struct inode *ecryptfs_inode;
	struct ecryptfs_crypt_stat *crypt_stat;
	char *enc_extent_virt = NULL;
	struct page *enc_extent_page;
	loff_t extent_offset;
	int rc = 0;

	ecryptfs_inode = page->mapping->host;
	crypt_stat =
		&(ecryptfs_inode_to_private(ecryptfs_inode)->crypt_stat);
	if (!(crypt_stat->flags & ECRYPTFS_ENCRYPTED)) {
		rc = ecryptfs_write_lower_page_segment(ecryptfs_inode, page,
						       0, PAGE_CACHE_SIZE);
		if (rc)
			printk(KERN_ERR "%s: Error attempting to copy "
			       "page at index [%ld]\n", __FUNCTION__,
			       page->index);
		goto out;
	}
	enc_extent_virt = kmalloc(PAGE_CACHE_SIZE, GFP_USER);
	if (!enc_extent_virt) {
		rc = -ENOMEM;
		ecryptfs_printk(KERN_ERR, "Error allocating memory for "
				"encrypted extent\n");
		goto out;
	}
	enc_extent_page = virt_to_page(enc_extent_virt);
	for (extent_offset = 0;
	     extent_offset < (PAGE_CACHE_SIZE / crypt_stat->extent_size);
	     extent_offset++) {
		loff_t offset;

		rc = ecryptfs_encrypt_extent(enc_extent_page, crypt_stat, page,
					     extent_offset);
		if (rc) {
			printk(KERN_ERR "%s: Error encrypting extent; "
			       "rc = [%d]\n", __FUNCTION__, rc);
			goto out;
		}
		ecryptfs_lower_offset_for_extent(
			&offset, ((page->index * (PAGE_CACHE_SIZE
						  / crypt_stat->extent_size))
				  + extent_offset), crypt_stat);
		rc = ecryptfs_write_lower(ecryptfs_inode, enc_extent_virt,
					  offset, crypt_stat->extent_size);
		if (rc) {
			ecryptfs_printk(KERN_ERR, "Error attempting "
					"to write lower page; rc = [%d]"
					"\n", rc);
			goto out;
		}
		extent_offset++;
	}
out:
	kfree(enc_extent_virt);
	return rc;
}

static int ecryptfs_decrypt_extent(struct page *page,
				   struct ecryptfs_crypt_stat *crypt_stat,
				   struct page *enc_extent_page,
				   unsigned long extent_offset)
{
	unsigned long extent_base;
	char extent_iv[ECRYPTFS_MAX_IV_BYTES];
	int rc;

	extent_base = (page->index
		       * (PAGE_CACHE_SIZE / crypt_stat->extent_size));
	rc = ecryptfs_derive_iv(extent_iv, crypt_stat,
				(extent_base + extent_offset));
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error attempting to "
				"derive IV for extent [0x%.16x]; "
				"rc = [%d]\n", (extent_base + extent_offset),
				rc);
		goto out;
	}
	if (unlikely(ecryptfs_verbosity > 0)) {
		ecryptfs_printk(KERN_DEBUG, "Decrypting extent "
				"with iv:\n");
		ecryptfs_dump_hex(extent_iv, crypt_stat->iv_bytes);
		ecryptfs_printk(KERN_DEBUG, "First 8 bytes before "
				"decryption:\n");
		ecryptfs_dump_hex((char *)
				  (page_address(enc_extent_page)
				   + (extent_offset * crypt_stat->extent_size)),
				  8);
	}
	rc = ecryptfs_decrypt_page_offset(crypt_stat, page,
					  (extent_offset
					   * crypt_stat->extent_size),
					  enc_extent_page, 0,
					  crypt_stat->extent_size, extent_iv);
	if (rc < 0) {
		printk(KERN_ERR "%s: Error attempting to decrypt to page with "
		       "page->index = [%ld], extent_offset = [%ld]; "
		       "rc = [%d]\n", __FUNCTION__, page->index, extent_offset,
		       rc);
		goto out;
	}
	rc = 0;
	if (unlikely(ecryptfs_verbosity > 0)) {
		ecryptfs_printk(KERN_DEBUG, "Decrypt extent [0x%.16x]; "
				"rc = [%d]\n", (extent_base + extent_offset),
				rc);
		ecryptfs_printk(KERN_DEBUG, "First 8 bytes after "
				"decryption:\n");
		ecryptfs_dump_hex((char *)(page_address(page)
					   + (extent_offset
					      * crypt_stat->extent_size)), 8);
	}
out:
	return rc;
}

/**
 * ecryptfs_decrypt_page
 * @page: Page mapped from the eCryptfs inode for the file; data read
 *        and decrypted from the lower file will be written into this
 *        page
 *
 * Decrypt an eCryptfs page. This is done on a per-extent basis. Note
 * that eCryptfs pages may straddle the lower pages -- for instance,
 * if the file was created on a machine with an 8K page size
 * (resulting in an 8K header), and then the file is copied onto a
 * host with a 32K page size, then when reading page 0 of the eCryptfs
 * file, 24K of page 0 of the lower file will be read and decrypted,
 * and then 8K of page 1 of the lower file will be read and decrypted.
 *
 * Returns zero on success; negative on error
 */
int ecryptfs_decrypt_page(struct page *page)
{
	struct inode *ecryptfs_inode;
	struct ecryptfs_crypt_stat *crypt_stat;
	char *enc_extent_virt = NULL;
	struct page *enc_extent_page;
	unsigned long extent_offset;
	int rc = 0;

	ecryptfs_inode = page->mapping->host;
	crypt_stat =
		&(ecryptfs_inode_to_private(ecryptfs_inode)->crypt_stat);
	if (!(crypt_stat->flags & ECRYPTFS_ENCRYPTED)) {
		rc = ecryptfs_read_lower_page_segment(page, page->index, 0,
						      PAGE_CACHE_SIZE,
						      ecryptfs_inode);
		if (rc)
			printk(KERN_ERR "%s: Error attempting to copy "
			       "page at index [%ld]\n", __FUNCTION__,
			       page->index);
		goto out_clear_uptodate;
	}
	enc_extent_virt = kmalloc(PAGE_CACHE_SIZE, GFP_USER);
	if (!enc_extent_virt) {
		rc = -ENOMEM;
		ecryptfs_printk(KERN_ERR, "Error allocating memory for "
				"encrypted extent\n");
		goto out_clear_uptodate;
	}
	enc_extent_page = virt_to_page(enc_extent_virt);
	for (extent_offset = 0;
	     extent_offset < (PAGE_CACHE_SIZE / crypt_stat->extent_size);
	     extent_offset++) {
		loff_t offset;

		ecryptfs_lower_offset_for_extent(
			&offset, ((page->index * (PAGE_CACHE_SIZE
						  / crypt_stat->extent_size))
				  + extent_offset), crypt_stat);
		rc = ecryptfs_read_lower(enc_extent_virt, offset,
					 crypt_stat->extent_size,
					 ecryptfs_inode);
		if (rc) {
			ecryptfs_printk(KERN_ERR, "Error attempting "
					"to read lower page; rc = [%d]"
					"\n", rc);
			goto out_clear_uptodate;
		}
		rc = ecryptfs_decrypt_extent(page, crypt_stat, enc_extent_page,
					     extent_offset);
		if (rc) {
			printk(KERN_ERR "%s: Error encrypting extent; "
			       "rc = [%d]\n", __FUNCTION__, rc);
			goto out_clear_uptodate;
		}
		extent_offset++;
	}
	SetPageUptodate(page);
	goto out;
out_clear_uptodate:
	ClearPageUptodate(page);
out:
	kfree(enc_extent_virt);
	return rc;
}

/**
 * decrypt_scatterlist
 * @crypt_stat: Cryptographic context
 * @dest_sg: The destination scatterlist to decrypt into
 * @src_sg: The source scatterlist to decrypt from
 * @size: The number of bytes to decrypt
 * @iv: The initialization vector to use for the decryption
 *
 * Returns the number of bytes decrypted; negative value on error
 */
static int decrypt_scatterlist(struct ecryptfs_crypt_stat *crypt_stat,
			       struct scatterlist *dest_sg,
			       struct scatterlist *src_sg, int size,
			       unsigned char *iv)
{
	struct blkcipher_desc desc = {
		.tfm = crypt_stat->tfm,
		.info = iv,
		.flags = CRYPTO_TFM_REQ_MAY_SLEEP
	};
	int rc = 0;

	/* Consider doing this once, when the file is opened */
	mutex_lock(&crypt_stat->cs_tfm_mutex);
	rc = crypto_blkcipher_setkey(crypt_stat->tfm, crypt_stat->key,
				     crypt_stat->key_size);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error setting key; rc = [%d]\n",
				rc);
		mutex_unlock(&crypt_stat->cs_tfm_mutex);
		rc = -EINVAL;
		goto out;
	}
	ecryptfs_printk(KERN_DEBUG, "Decrypting [%d] bytes.\n", size);
	rc = crypto_blkcipher_decrypt_iv(&desc, dest_sg, src_sg, size);
	mutex_unlock(&crypt_stat->cs_tfm_mutex);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error decrypting; rc = [%d]\n",
				rc);
		goto out;
	}
	rc = size;
out:
	return rc;
}

/**
 * ecryptfs_encrypt_page_offset
 * @crypt_stat: The cryptographic context
 * @dst_page: The page to encrypt into
 * @dst_offset: The offset in the page to encrypt into
 * @src_page: The page to encrypt from
 * @src_offset: The offset in the page to encrypt from
 * @size: The number of bytes to encrypt
 * @iv: The initialization vector to use for the encryption
 *
 * Returns the number of bytes encrypted
 */
static int
ecryptfs_encrypt_page_offset(struct ecryptfs_crypt_stat *crypt_stat,
			     struct page *dst_page, int dst_offset,
			     struct page *src_page, int src_offset, int size,
			     unsigned char *iv)
{
	struct scatterlist src_sg, dst_sg;

	src_sg.page = src_page;
	src_sg.offset = src_offset;
	src_sg.length = size;
	dst_sg.page = dst_page;
	dst_sg.offset = dst_offset;
	dst_sg.length = size;
	return encrypt_scatterlist(crypt_stat, &dst_sg, &src_sg, size, iv);
}

/**
 * ecryptfs_decrypt_page_offset
 * @crypt_stat: The cryptographic context
 * @dst_page: The page to decrypt into
 * @dst_offset: The offset in the page to decrypt into
 * @src_page: The page to decrypt from
 * @src_offset: The offset in the page to decrypt from
 * @size: The number of bytes to decrypt
 * @iv: The initialization vector to use for the decryption
 *
 * Returns the number of bytes decrypted
 */
static int
ecryptfs_decrypt_page_offset(struct ecryptfs_crypt_stat *crypt_stat,
			     struct page *dst_page, int dst_offset,
			     struct page *src_page, int src_offset, int size,
			     unsigned char *iv)
{
	struct scatterlist src_sg, dst_sg;

	src_sg.page = src_page;
	src_sg.offset = src_offset;
	src_sg.length = size;
	dst_sg.page = dst_page;
	dst_sg.offset = dst_offset;
	dst_sg.length = size;
	return decrypt_scatterlist(crypt_stat, &dst_sg, &src_sg, size, iv);
}

#define ECRYPTFS_MAX_SCATTERLIST_LEN 4

/**
 * ecryptfs_init_crypt_ctx
 * @crypt_stat: Uninitilized crypt stats structure
 *
 * Initialize the crypto context.
 *
 * TODO: Performance: Keep a cache of initialized cipher contexts;
 * only init if needed
 */
int ecryptfs_init_crypt_ctx(struct ecryptfs_crypt_stat *crypt_stat)
{
	char *full_alg_name;
	int rc = -EINVAL;

	if (!crypt_stat->cipher) {
		ecryptfs_printk(KERN_ERR, "No cipher specified\n");
		goto out;
	}
	ecryptfs_printk(KERN_DEBUG,
			"Initializing cipher [%s]; strlen = [%d]; "
			"key_size_bits = [%d]\n",
			crypt_stat->cipher, (int)strlen(crypt_stat->cipher),
			crypt_stat->key_size << 3);
	if (crypt_stat->tfm) {
		rc = 0;
		goto out;
	}
	mutex_lock(&crypt_stat->cs_tfm_mutex);
	rc = ecryptfs_crypto_api_algify_cipher_name(&full_alg_name,
						    crypt_stat->cipher, "cbc");
	if (rc)
		goto out;
	crypt_stat->tfm = crypto_alloc_blkcipher(full_alg_name, 0,
						 CRYPTO_ALG_ASYNC);
	kfree(full_alg_name);
	if (IS_ERR(crypt_stat->tfm)) {
		rc = PTR_ERR(crypt_stat->tfm);
		ecryptfs_printk(KERN_ERR, "cryptfs: init_crypt_ctx(): "
				"Error initializing cipher [%s]\n",
				crypt_stat->cipher);
		mutex_unlock(&crypt_stat->cs_tfm_mutex);
		goto out;
	}
	crypto_blkcipher_set_flags(crypt_stat->tfm, CRYPTO_TFM_REQ_WEAK_KEY);
	mutex_unlock(&crypt_stat->cs_tfm_mutex);
	rc = 0;
out:
	return rc;
}

static void set_extent_mask_and_shift(struct ecryptfs_crypt_stat *crypt_stat)
{
	int extent_size_tmp;

	crypt_stat->extent_mask = 0xFFFFFFFF;
	crypt_stat->extent_shift = 0;
	if (crypt_stat->extent_size == 0)
		return;
	extent_size_tmp = crypt_stat->extent_size;
	while ((extent_size_tmp & 0x01) == 0) {
		extent_size_tmp >>= 1;
		crypt_stat->extent_mask <<= 1;
		crypt_stat->extent_shift++;
	}
}

void ecryptfs_set_default_sizes(struct ecryptfs_crypt_stat *crypt_stat)
{
	/* Default values; may be overwritten as we are parsing the
	 * packets. */
	crypt_stat->extent_size = ECRYPTFS_DEFAULT_EXTENT_SIZE;
	set_extent_mask_and_shift(crypt_stat);
	crypt_stat->iv_bytes = ECRYPTFS_DEFAULT_IV_BYTES;
	if (crypt_stat->flags & ECRYPTFS_METADATA_IN_XATTR)
		crypt_stat->num_header_extents_at_front = 0;
	else {
		if (PAGE_CACHE_SIZE <= ECRYPTFS_MINIMUM_HEADER_EXTENT_SIZE)
			crypt_stat->num_header_extents_at_front =
				(ECRYPTFS_MINIMUM_HEADER_EXTENT_SIZE
				 / crypt_stat->extent_size);
		else
			crypt_stat->num_header_extents_at_front =
				(PAGE_CACHE_SIZE / crypt_stat->extent_size);
	}
}

/**
 * ecryptfs_compute_root_iv
 * @crypt_stats
 *
 * On error, sets the root IV to all 0's.
 */
int ecryptfs_compute_root_iv(struct ecryptfs_crypt_stat *crypt_stat)
{
	int rc = 0;
	char dst[MD5_DIGEST_SIZE];

	BUG_ON(crypt_stat->iv_bytes > MD5_DIGEST_SIZE);
	BUG_ON(crypt_stat->iv_bytes <= 0);
	if (!(crypt_stat->flags & ECRYPTFS_KEY_VALID)) {
		rc = -EINVAL;
		ecryptfs_printk(KERN_WARNING, "Session key not valid; "
				"cannot generate root IV\n");
		goto out;
	}
	rc = ecryptfs_calculate_md5(dst, crypt_stat, crypt_stat->key,
				    crypt_stat->key_size);
	if (rc) {
		ecryptfs_printk(KERN_WARNING, "Error attempting to compute "
				"MD5 while generating root IV\n");
		goto out;
	}
	memcpy(crypt_stat->root_iv, dst, crypt_stat->iv_bytes);
out:
	if (rc) {
		memset(crypt_stat->root_iv, 0, crypt_stat->iv_bytes);
		crypt_stat->flags |= ECRYPTFS_SECURITY_WARNING;
	}
	return rc;
}

static void ecryptfs_generate_new_key(struct ecryptfs_crypt_stat *crypt_stat)
{
	get_random_bytes(crypt_stat->key, crypt_stat->key_size);
	crypt_stat->flags |= ECRYPTFS_KEY_VALID;
	ecryptfs_compute_root_iv(crypt_stat);
	if (unlikely(ecryptfs_verbosity > 0)) {
		ecryptfs_printk(KERN_DEBUG, "Generated new session key:\n");
		ecryptfs_dump_hex(crypt_stat->key,
				  crypt_stat->key_size);
	}
}

/**
 * ecryptfs_copy_mount_wide_flags_to_inode_flags
 * @crypt_stat: The inode's cryptographic context
 * @mount_crypt_stat: The mount point's cryptographic context
 *
 * This function propagates the mount-wide flags to individual inode
 * flags.
 */
static void ecryptfs_copy_mount_wide_flags_to_inode_flags(
	struct ecryptfs_crypt_stat *crypt_stat,
	struct ecryptfs_mount_crypt_stat *mount_crypt_stat)
{
	if (mount_crypt_stat->flags & ECRYPTFS_XATTR_METADATA_ENABLED)
		crypt_stat->flags |= ECRYPTFS_METADATA_IN_XATTR;
	if (mount_crypt_stat->flags & ECRYPTFS_ENCRYPTED_VIEW_ENABLED)
		crypt_stat->flags |= ECRYPTFS_VIEW_AS_ENCRYPTED;
}

static int ecryptfs_copy_mount_wide_sigs_to_inode_sigs(
	struct ecryptfs_crypt_stat *crypt_stat,
	struct ecryptfs_mount_crypt_stat *mount_crypt_stat)
{
	struct ecryptfs_global_auth_tok *global_auth_tok;
	int rc = 0;

	mutex_lock(&mount_crypt_stat->global_auth_tok_list_mutex);
	list_for_each_entry(global_auth_tok,
			    &mount_crypt_stat->global_auth_tok_list,
			    mount_crypt_stat_list) {
		rc = ecryptfs_add_keysig(crypt_stat, global_auth_tok->sig);
		if (rc) {
			printk(KERN_ERR "Error adding keysig; rc = [%d]\n", rc);
			mutex_unlock(
				&mount_crypt_stat->global_auth_tok_list_mutex);
			goto out;
		}
	}
	mutex_unlock(&mount_crypt_stat->global_auth_tok_list_mutex);
out:
	return rc;
}

/**
 * ecryptfs_set_default_crypt_stat_vals
 * @crypt_stat: The inode's cryptographic context
 * @mount_crypt_stat: The mount point's cryptographic context
 *
 * Default values in the event that policy does not override them.
 */
static void ecryptfs_set_default_crypt_stat_vals(
	struct ecryptfs_crypt_stat *crypt_stat,
	struct ecryptfs_mount_crypt_stat *mount_crypt_stat)
{
	ecryptfs_copy_mount_wide_flags_to_inode_flags(crypt_stat,
						      mount_crypt_stat);
	ecryptfs_set_default_sizes(crypt_stat);
	strcpy(crypt_stat->cipher, ECRYPTFS_DEFAULT_CIPHER);
	crypt_stat->key_size = ECRYPTFS_DEFAULT_KEY_BYTES;
	crypt_stat->flags &= ~(ECRYPTFS_KEY_VALID);
	crypt_stat->file_version = ECRYPTFS_FILE_VERSION;
	crypt_stat->mount_crypt_stat = mount_crypt_stat;
}

/**
 * ecryptfs_new_file_context
 * @ecryptfs_dentry: The eCryptfs dentry
 *
 * If the crypto context for the file has not yet been established,
 * this is where we do that.  Establishing a new crypto context
 * involves the following decisions:
 *  - What cipher to use?
 *  - What set of authentication tokens to use?
 * Here we just worry about getting enough information into the
 * authentication tokens so that we know that they are available.
 * We associate the available authentication tokens with the new file
 * via the set of signatures in the crypt_stat struct.  Later, when
 * the headers are actually written out, we may again defer to
 * userspace to perform the encryption of the session key; for the
 * foreseeable future, this will be the case with public key packets.
 *
 * Returns zero on success; non-zero otherwise
 */
int ecryptfs_new_file_context(struct dentry *ecryptfs_dentry)
{
	struct ecryptfs_crypt_stat *crypt_stat =
	    &ecryptfs_inode_to_private(ecryptfs_dentry->d_inode)->crypt_stat;
	struct ecryptfs_mount_crypt_stat *mount_crypt_stat =
	    &ecryptfs_superblock_to_private(
		    ecryptfs_dentry->d_sb)->mount_crypt_stat;
	int cipher_name_len;
	int rc = 0;

	ecryptfs_set_default_crypt_stat_vals(crypt_stat, mount_crypt_stat);
	crypt_stat->flags |= (ECRYPTFS_ENCRYPTED | ECRYPTFS_KEY_VALID);
	ecryptfs_copy_mount_wide_flags_to_inode_flags(crypt_stat,
						      mount_crypt_stat);
	rc = ecryptfs_copy_mount_wide_sigs_to_inode_sigs(crypt_stat,
							 mount_crypt_stat);
	if (rc) {
		printk(KERN_ERR "Error attempting to copy mount-wide key sigs "
		       "to the inode key sigs; rc = [%d]\n", rc);
		goto out;
	}
	cipher_name_len =
		strlen(mount_crypt_stat->global_default_cipher_name);
	memcpy(crypt_stat->cipher,
	       mount_crypt_stat->global_default_cipher_name,
	       cipher_name_len);
	crypt_stat->cipher[cipher_name_len] = '\0';
	crypt_stat->key_size =
		mount_crypt_stat->global_default_cipher_key_size;
	ecryptfs_generate_new_key(crypt_stat);
	rc = ecryptfs_init_crypt_ctx(crypt_stat);
	if (rc)
		ecryptfs_printk(KERN_ERR, "Error initializing cryptographic "
				"context for cipher [%s]: rc = [%d]\n",
				crypt_stat->cipher, rc);
out:
	return rc;
}

/**
 * contains_ecryptfs_marker - check for the ecryptfs marker
 * @data: The data block in which to check
 *
 * Returns one if marker found; zero if not found
 */
static int contains_ecryptfs_marker(char *data)
{
	u32 m_1, m_2;

	memcpy(&m_1, data, 4);
	m_1 = be32_to_cpu(m_1);
	memcpy(&m_2, (data + 4), 4);
	m_2 = be32_to_cpu(m_2);
	if ((m_1 ^ MAGIC_ECRYPTFS_MARKER) == m_2)
		return 1;
	ecryptfs_printk(KERN_DEBUG, "m_1 = [0x%.8x]; m_2 = [0x%.8x]; "
			"MAGIC_ECRYPTFS_MARKER = [0x%.8x]\n", m_1, m_2,
			MAGIC_ECRYPTFS_MARKER);
	ecryptfs_printk(KERN_DEBUG, "(m_1 ^ MAGIC_ECRYPTFS_MARKER) = "
			"[0x%.8x]\n", (m_1 ^ MAGIC_ECRYPTFS_MARKER));
	return 0;
}

struct ecryptfs_flag_map_elem {
	u32 file_flag;
	u32 local_flag;
};

/* Add support for additional flags by adding elements here. */
static struct ecryptfs_flag_map_elem ecryptfs_flag_map[] = {
	{0x00000001, ECRYPTFS_ENABLE_HMAC},
	{0x00000002, ECRYPTFS_ENCRYPTED},
	{0x00000004, ECRYPTFS_METADATA_IN_XATTR}
};

/**
 * ecryptfs_process_flags
 * @crypt_stat: The cryptographic context
 * @page_virt: Source data to be parsed
 * @bytes_read: Updated with the number of bytes read
 *
 * Returns zero on success; non-zero if the flag set is invalid
 */
static int ecryptfs_process_flags(struct ecryptfs_crypt_stat *crypt_stat,
				  char *page_virt, int *bytes_read)
{
	int rc = 0;
	int i;
	u32 flags;

	memcpy(&flags, page_virt, 4);
	flags = be32_to_cpu(flags);
	for (i = 0; i < ((sizeof(ecryptfs_flag_map)
			  / sizeof(struct ecryptfs_flag_map_elem))); i++)
		if (flags & ecryptfs_flag_map[i].file_flag) {
			crypt_stat->flags |= ecryptfs_flag_map[i].local_flag;
		} else
			crypt_stat->flags &= ~(ecryptfs_flag_map[i].local_flag);
	/* Version is in top 8 bits of the 32-bit flag vector */
	crypt_stat->file_version = ((flags >> 24) & 0xFF);
	(*bytes_read) = 4;
	return rc;
}

/**
 * write_ecryptfs_marker
 * @page_virt: The pointer to in a page to begin writing the marker
 * @written: Number of bytes written
 *
 * Marker = 0x3c81b7f5
 */
static void write_ecryptfs_marker(char *page_virt, size_t *written)
{
	u32 m_1, m_2;

	get_random_bytes(&m_1, (MAGIC_ECRYPTFS_MARKER_SIZE_BYTES / 2));
	m_2 = (m_1 ^ MAGIC_ECRYPTFS_MARKER);
	m_1 = cpu_to_be32(m_1);
	memcpy(page_virt, &m_1, (MAGIC_ECRYPTFS_MARKER_SIZE_BYTES / 2));
	m_2 = cpu_to_be32(m_2);
	memcpy(page_virt + (MAGIC_ECRYPTFS_MARKER_SIZE_BYTES / 2), &m_2,
	       (MAGIC_ECRYPTFS_MARKER_SIZE_BYTES / 2));
	(*written) = MAGIC_ECRYPTFS_MARKER_SIZE_BYTES;
}

static void
write_ecryptfs_flags(char *page_virt, struct ecryptfs_crypt_stat *crypt_stat,
		     size_t *written)
{
	u32 flags = 0;
	int i;

	for (i = 0; i < ((sizeof(ecryptfs_flag_map)
			  / sizeof(struct ecryptfs_flag_map_elem))); i++)
		if (crypt_stat->flags & ecryptfs_flag_map[i].local_flag)
			flags |= ecryptfs_flag_map[i].file_flag;
	/* Version is in top 8 bits of the 32-bit flag vector */
	flags |= ((((u8)crypt_stat->file_version) << 24) & 0xFF000000);
	flags = cpu_to_be32(flags);
	memcpy(page_virt, &flags, 4);
	(*written) = 4;
}

struct ecryptfs_cipher_code_str_map_elem {
	char cipher_str[16];
	u16 cipher_code;
};

/* Add support for additional ciphers by adding elements here. The
 * cipher_code is whatever OpenPGP applicatoins use to identify the
 * ciphers. List in order of probability. */
static struct ecryptfs_cipher_code_str_map_elem
ecryptfs_cipher_code_str_map[] = {
	{"aes",RFC2440_CIPHER_AES_128 },
	{"blowfish", RFC2440_CIPHER_BLOWFISH},
	{"des3_ede", RFC2440_CIPHER_DES3_EDE},
	{"cast5", RFC2440_CIPHER_CAST_5},
	{"twofish", RFC2440_CIPHER_TWOFISH},
	{"cast6", RFC2440_CIPHER_CAST_6},
	{"aes", RFC2440_CIPHER_AES_192},
	{"aes", RFC2440_CIPHER_AES_256}
};

/**
 * ecryptfs_code_for_cipher_string
 * @crypt_stat: The cryptographic context
 *
 * Returns zero on no match, or the cipher code on match
 */
u16 ecryptfs_code_for_cipher_string(struct ecryptfs_crypt_stat *crypt_stat)
{
	int i;
	u16 code = 0;
	struct ecryptfs_cipher_code_str_map_elem *map =
		ecryptfs_cipher_code_str_map;

	if (strcmp(crypt_stat->cipher, "aes") == 0) {
		switch (crypt_stat->key_size) {
		case 16:
			code = RFC2440_CIPHER_AES_128;
			break;
		case 24:
			code = RFC2440_CIPHER_AES_192;
			break;
		case 32:
			code = RFC2440_CIPHER_AES_256;
		}
	} else {
		for (i = 0; i < ARRAY_SIZE(ecryptfs_cipher_code_str_map); i++)
			if (strcmp(crypt_stat->cipher, map[i].cipher_str) == 0){
				code = map[i].cipher_code;
				break;
			}
	}
	return code;
}

/**
 * ecryptfs_cipher_code_to_string
 * @str: Destination to write out the cipher name
 * @cipher_code: The code to convert to cipher name string
 *
 * Returns zero on success
 */
int ecryptfs_cipher_code_to_string(char *str, u16 cipher_code)
{
	int rc = 0;
	int i;

	str[0] = '\0';
	for (i = 0; i < ARRAY_SIZE(ecryptfs_cipher_code_str_map); i++)
		if (cipher_code == ecryptfs_cipher_code_str_map[i].cipher_code)
			strcpy(str, ecryptfs_cipher_code_str_map[i].cipher_str);
	if (str[0] == '\0') {
		ecryptfs_printk(KERN_WARNING, "Cipher code not recognized: "
				"[%d]\n", cipher_code);
		rc = -EINVAL;
	}
	return rc;
}

/**
 * ecryptfs_read_header_region
 * @data: The virtual address to write header region data into
 * @dentry: The lower dentry
 * @mnt: The lower VFS mount
 *
 * Returns zero on success; non-zero otherwise
 */
static int ecryptfs_read_header_region(char *data, struct dentry *dentry,
				       struct vfsmount *mnt)
{
	struct file *lower_file;
	mm_segment_t oldfs;
	int rc;

	rc = ecryptfs_open_lower_file(&lower_file, dentry, mnt, O_RDONLY);
	if (rc) {
		printk(KERN_ERR
		       "Error opening lower_file to read header region\n");
		goto out;
	}
	lower_file->f_pos = 0;
	oldfs = get_fs();
	set_fs(get_ds());
	rc = lower_file->f_op->read(lower_file, (char __user *)data,
			      ECRYPTFS_DEFAULT_EXTENT_SIZE, &lower_file->f_pos);
	set_fs(oldfs);
	rc = ecryptfs_close_lower_file(lower_file);
	if (rc) {
		printk(KERN_ERR "Error closing lower_file\n");
		goto out;
	}
	rc = 0;
out:
	return rc;
}

int ecryptfs_read_and_validate_header_region(char *data,
					     struct inode *ecryptfs_inode)
{
	struct ecryptfs_crypt_stat *crypt_stat =
		&(ecryptfs_inode_to_private(ecryptfs_inode)->crypt_stat);
	int rc;

	rc = ecryptfs_read_lower(data, 0, crypt_stat->extent_size,
				 ecryptfs_inode);
	if (rc) {
		printk(KERN_ERR "%s: Error reading header region; rc = [%d]\n",
		       __FUNCTION__, rc);
		goto out;
	}
	if (!contains_ecryptfs_marker(data + ECRYPTFS_FILE_SIZE_BYTES)) {
		rc = -EINVAL;
		ecryptfs_printk(KERN_DEBUG, "Valid marker not found\n");
	}
out:
	return rc;
}

void
ecryptfs_write_header_metadata(char *virt,
			       struct ecryptfs_crypt_stat *crypt_stat,
			       size_t *written)
{
	u32 header_extent_size;
	u16 num_header_extents_at_front;

	header_extent_size = (u32)crypt_stat->extent_size;
	num_header_extents_at_front =
		(u16)crypt_stat->num_header_extents_at_front;
	header_extent_size = cpu_to_be32(header_extent_size);
	memcpy(virt, &header_extent_size, 4);
	virt += 4;
	num_header_extents_at_front = cpu_to_be16(num_header_extents_at_front);
	memcpy(virt, &num_header_extents_at_front, 2);
	(*written) = 6;
}

struct kmem_cache *ecryptfs_header_cache_0;
struct kmem_cache *ecryptfs_header_cache_1;
struct kmem_cache *ecryptfs_header_cache_2;

/**
 * ecryptfs_write_headers_virt
 * @page_virt: The virtual address to write the headers to
 * @size: Set to the number of bytes written by this function
 * @crypt_stat: The cryptographic context
 * @ecryptfs_dentry: The eCryptfs dentry
 *
 * Format version: 1
 *
 *   Header Extent:
 *     Octets 0-7:        Unencrypted file size (big-endian)
 *     Octets 8-15:       eCryptfs special marker
 *     Octets 16-19:      Flags
 *      Octet 16:         File format version number (between 0 and 255)
 *      Octets 17-18:     Reserved
 *      Octet 19:         Bit 1 (lsb): Reserved
 *                        Bit 2: Encrypted?
 *                        Bits 3-8: Reserved
 *     Octets 20-23:      Header extent size (big-endian)
 *     Octets 24-25:      Number of header extents at front of file
 *                        (big-endian)
 *     Octet  26:         Begin RFC 2440 authentication token packet set
 *   Data Extent 0:
 *     Lower data (CBC encrypted)
 *   Data Extent 1:
 *     Lower data (CBC encrypted)
 *   ...
 *
 * Returns zero on success
 */
static int ecryptfs_write_headers_virt(char *page_virt, size_t *size,
				       struct ecryptfs_crypt_stat *crypt_stat,
				       struct dentry *ecryptfs_dentry)
{
	int rc;
	size_t written;
	size_t offset;

	offset = ECRYPTFS_FILE_SIZE_BYTES;
	write_ecryptfs_marker((page_virt + offset), &written);
	offset += written;
	write_ecryptfs_flags((page_virt + offset), crypt_stat, &written);
	offset += written;
	ecryptfs_write_header_metadata((page_virt + offset), crypt_stat,
				       &written);
	offset += written;
	rc = ecryptfs_generate_key_packet_set((page_virt + offset), crypt_stat,
					      ecryptfs_dentry, &written,
					      PAGE_CACHE_SIZE - offset);
	if (rc)
		ecryptfs_printk(KERN_WARNING, "Error generating key packet "
				"set; rc = [%d]\n", rc);
	if (size) {
		offset += written;
		*size = offset;
	}
	return rc;
}

static int
ecryptfs_write_metadata_to_contents(struct ecryptfs_crypt_stat *crypt_stat,
				    struct dentry *ecryptfs_dentry,
				    char *page_virt)
{
	int current_header_page;
	int header_pages;
	int rc;

	rc = ecryptfs_write_lower(ecryptfs_dentry->d_inode, page_virt,
				  0, PAGE_CACHE_SIZE);
	if (rc) {
		printk(KERN_ERR "%s: Error attempting to write header "
		       "information to lower file; rc = [%d]\n", __FUNCTION__,
		       rc);
		goto out;
	}
	header_pages = ((crypt_stat->extent_size
			 * crypt_stat->num_header_extents_at_front)
			/ PAGE_CACHE_SIZE);
	memset(page_virt, 0, PAGE_CACHE_SIZE);
	current_header_page = 1;
	while (current_header_page < header_pages) {
		loff_t offset;

		offset = (current_header_page << PAGE_CACHE_SHIFT);
		if ((rc = ecryptfs_write_lower(ecryptfs_dentry->d_inode,
					       page_virt, offset,
					       PAGE_CACHE_SIZE))) {
			printk(KERN_ERR "%s: Error attempting to write header "
			       "information to lower file; rc = [%d]\n",
			       __FUNCTION__, rc);
			goto out;
		}
		current_header_page++;
	}
out:
	return rc;
}

static int
ecryptfs_write_metadata_to_xattr(struct dentry *ecryptfs_dentry,
				 struct ecryptfs_crypt_stat *crypt_stat,
				 char *page_virt, size_t size)
{
	int rc;

	rc = ecryptfs_setxattr(ecryptfs_dentry, ECRYPTFS_XATTR_NAME, page_virt,
			       size, 0);
	return rc;
}

/**
 * ecryptfs_write_metadata
 * @ecryptfs_dentry: The eCryptfs dentry
 *
 * Write the file headers out.  This will likely involve a userspace
 * callout, in which the session key is encrypted with one or more
 * public keys and/or the passphrase necessary to do the encryption is
 * retrieved via a prompt.  Exactly what happens at this point should
 * be policy-dependent.
 *
 * TODO: Support header information spanning multiple pages
 *
 * Returns zero on success; non-zero on error
 */
int ecryptfs_write_metadata(struct dentry *ecryptfs_dentry)
{
	struct ecryptfs_crypt_stat *crypt_stat =
		&ecryptfs_inode_to_private(ecryptfs_dentry->d_inode)->crypt_stat;
	char *page_virt;
	size_t size = 0;
	int rc = 0;

	if (likely(crypt_stat->flags & ECRYPTFS_ENCRYPTED)) {
		if (!(crypt_stat->flags & ECRYPTFS_KEY_VALID)) {
			printk(KERN_ERR "Key is invalid; bailing out\n");
			rc = -EINVAL;
			goto out;
		}
	} else {
		rc = -EINVAL;
		ecryptfs_printk(KERN_WARNING,
				"Called with crypt_stat->encrypted == 0\n");
		goto out;
	}
	/* Released in this function */
	page_virt = kmem_cache_zalloc(ecryptfs_header_cache_0, GFP_USER);
	if (!page_virt) {
		ecryptfs_printk(KERN_ERR, "Out of memory\n");
		rc = -ENOMEM;
		goto out;
	}
	rc = ecryptfs_write_headers_virt(page_virt, &size, crypt_stat,
  					 ecryptfs_dentry);
	if (unlikely(rc)) {
		ecryptfs_printk(KERN_ERR, "Error whilst writing headers\n");
		memset(page_virt, 0, PAGE_CACHE_SIZE);
		goto out_free;
	}
	if (crypt_stat->flags & ECRYPTFS_METADATA_IN_XATTR)
		rc = ecryptfs_write_metadata_to_xattr(ecryptfs_dentry,
						      crypt_stat, page_virt,
						      size);
	else
		rc = ecryptfs_write_metadata_to_contents(crypt_stat,
							 ecryptfs_dentry,
							 page_virt);
	if (rc) {
		printk(KERN_ERR "Error writing metadata out to lower file; "
		       "rc = [%d]\n", rc);
		goto out_free;
	}
out_free:
	kmem_cache_free(ecryptfs_header_cache_0, page_virt);
out:
	return rc;
}

#define ECRYPTFS_DONT_VALIDATE_HEADER_SIZE 0
#define ECRYPTFS_VALIDATE_HEADER_SIZE 1
static int parse_header_metadata(struct ecryptfs_crypt_stat *crypt_stat,
				 char *virt, int *bytes_read,
				 int validate_header_size)
{
	int rc = 0;
	u32 header_extent_size;
	u16 num_header_extents_at_front;

	memcpy(&header_extent_size, virt, 4);
	header_extent_size = be32_to_cpu(header_extent_size);
	virt += 4;
	memcpy(&num_header_extents_at_front, virt, 2);
	num_header_extents_at_front = be16_to_cpu(num_header_extents_at_front);
	crypt_stat->num_header_extents_at_front =
		(int)num_header_extents_at_front;
	(*bytes_read) = (sizeof(u32) + sizeof(u16));
	if ((validate_header_size == ECRYPTFS_VALIDATE_HEADER_SIZE)
	    && ((crypt_stat->extent_size
		 * crypt_stat->num_header_extents_at_front)
		< ECRYPTFS_MINIMUM_HEADER_EXTENT_SIZE)) {
		rc = -EINVAL;
		printk(KERN_WARNING "Invalid number of header extents: [%zd]\n",
		       crypt_stat->num_header_extents_at_front);
	}
	return rc;
}

/**
 * set_default_header_data
 * @crypt_stat: The cryptographic context
 *
 * For version 0 file format; this function is only for backwards
 * compatibility for files created with the prior versions of
 * eCryptfs.
 */
static void set_default_header_data(struct ecryptfs_crypt_stat *crypt_stat)
{
	crypt_stat->num_header_extents_at_front = 2;
}

/**
 * ecryptfs_read_headers_virt
 * @page_virt: The virtual address into which to read the headers
 * @crypt_stat: The cryptographic context
 * @ecryptfs_dentry: The eCryptfs dentry
 * @validate_header_size: Whether to validate the header size while reading
 *
 * Read/parse the header data. The header format is detailed in the
 * comment block for the ecryptfs_write_headers_virt() function.
 *
 * Returns zero on success
 */
static int ecryptfs_read_headers_virt(char *page_virt,
				      struct ecryptfs_crypt_stat *crypt_stat,
				      struct dentry *ecryptfs_dentry,
				      int validate_header_size)
{
	int rc = 0;
	int offset;
	int bytes_read;

	ecryptfs_set_default_sizes(crypt_stat);
	crypt_stat->mount_crypt_stat = &ecryptfs_superblock_to_private(
		ecryptfs_dentry->d_sb)->mount_crypt_stat;
	offset = ECRYPTFS_FILE_SIZE_BYTES;
	rc = contains_ecryptfs_marker(page_virt + offset);
	if (rc == 0) {
		rc = -EINVAL;
		goto out;
	}
	offset += MAGIC_ECRYPTFS_MARKER_SIZE_BYTES;
	rc = ecryptfs_process_flags(crypt_stat, (page_virt + offset),
				    &bytes_read);
	if (rc) {
		ecryptfs_printk(KERN_WARNING, "Error processing flags\n");
		goto out;
	}
	if (crypt_stat->file_version > ECRYPTFS_SUPPORTED_FILE_VERSION) {
		ecryptfs_printk(KERN_WARNING, "File version is [%d]; only "
				"file version [%d] is supported by this "
				"version of eCryptfs\n",
				crypt_stat->file_version,
				ECRYPTFS_SUPPORTED_FILE_VERSION);
		rc = -EINVAL;
		goto out;
	}
	offset += bytes_read;
	if (crypt_stat->file_version >= 1) {
		rc = parse_header_metadata(crypt_stat, (page_virt + offset),
					   &bytes_read, validate_header_size);
		if (rc) {
			ecryptfs_printk(KERN_WARNING, "Error reading header "
					"metadata; rc = [%d]\n", rc);
		}
		offset += bytes_read;
	} else
		set_default_header_data(crypt_stat);
	rc = ecryptfs_parse_packet_set(crypt_stat, (page_virt + offset),
				       ecryptfs_dentry);
out:
	return rc;
}

/**
 * ecryptfs_read_xattr_region
 * @page_virt: The vitual address into which to read the xattr data
 * @ecryptfs_dentry: The eCryptfs dentry
 *
 * Attempts to read the crypto metadata from the extended attribute
 * region of the lower file.
 *
 * Returns zero on success; non-zero on error
 */
int ecryptfs_read_xattr_region(char *page_virt, struct inode *ecryptfs_inode)
{
	struct dentry *lower_dentry =
		ecryptfs_inode_to_private(ecryptfs_inode)->lower_file->f_dentry;
	ssize_t size;
	int rc = 0;

	size = ecryptfs_getxattr_lower(lower_dentry, ECRYPTFS_XATTR_NAME,
				       page_virt, ECRYPTFS_DEFAULT_EXTENT_SIZE);
	if (size < 0) {
		printk(KERN_ERR "Error attempting to read the [%s] "
		       "xattr from the lower file; return value = [%zd]\n",
		       ECRYPTFS_XATTR_NAME, size);
		rc = -EINVAL;
		goto out;
	}
out:
	return rc;
}

int ecryptfs_read_and_validate_xattr_region(char *page_virt,
					    struct dentry *ecryptfs_dentry)
{
	int rc;

	rc = ecryptfs_read_xattr_region(page_virt, ecryptfs_dentry->d_inode);
	if (rc)
		goto out;
	if (!contains_ecryptfs_marker(page_virt	+ ECRYPTFS_FILE_SIZE_BYTES)) {
		printk(KERN_WARNING "Valid data found in [%s] xattr, but "
			"the marker is invalid\n", ECRYPTFS_XATTR_NAME);
		rc = -EINVAL;
	}
out:
	return rc;
}

/**
 * ecryptfs_read_metadata
 *
 * Common entry point for reading file metadata. From here, we could
 * retrieve the header information from the header region of the file,
 * the xattr region of the file, or some other repostory that is
 * stored separately from the file itself. The current implementation
 * supports retrieving the metadata information from the file contents
 * and from the xattr region.
 *
 * Returns zero if valid headers found and parsed; non-zero otherwise
 */
int ecryptfs_read_metadata(struct dentry *ecryptfs_dentry)
{
	int rc = 0;
	char *page_virt = NULL;
	struct inode *ecryptfs_inode = ecryptfs_dentry->d_inode;
	struct ecryptfs_crypt_stat *crypt_stat =
	    &ecryptfs_inode_to_private(ecryptfs_inode)->crypt_stat;
	struct ecryptfs_mount_crypt_stat *mount_crypt_stat =
		&ecryptfs_superblock_to_private(
			ecryptfs_dentry->d_sb)->mount_crypt_stat;

	ecryptfs_copy_mount_wide_flags_to_inode_flags(crypt_stat,
						      mount_crypt_stat);
	/* Read the first page from the underlying file */
	page_virt = kmem_cache_alloc(ecryptfs_header_cache_1, GFP_USER);
	if (!page_virt) {
		rc = -ENOMEM;
		printk(KERN_ERR "%s: Unable to allocate page_virt\n",
		       __FUNCTION__);
		goto out;
	}
	rc = ecryptfs_read_lower(page_virt, 0, crypt_stat->extent_size,
				 ecryptfs_inode);
	if (!rc)
		rc = ecryptfs_read_headers_virt(page_virt, crypt_stat,
						ecryptfs_dentry,
						ECRYPTFS_VALIDATE_HEADER_SIZE);
	if (rc) {
		rc = ecryptfs_read_xattr_region(page_virt, ecryptfs_inode);
		if (rc) {
			printk(KERN_DEBUG "Valid eCryptfs headers not found in "
			       "file header region or xattr region\n");
			rc = -EINVAL;
			goto out;
		}
		rc = ecryptfs_read_headers_virt(page_virt, crypt_stat,
						ecryptfs_dentry,
						ECRYPTFS_DONT_VALIDATE_HEADER_SIZE);
		if (rc) {
			printk(KERN_DEBUG "Valid eCryptfs headers not found in "
			       "file xattr region either\n");
			rc = -EINVAL;
		}
		if (crypt_stat->mount_crypt_stat->flags
		    & ECRYPTFS_XATTR_METADATA_ENABLED) {
			crypt_stat->flags |= ECRYPTFS_METADATA_IN_XATTR;
		} else {
			printk(KERN_WARNING "Attempt to access file with "
			       "crypto metadata only in the extended attribute "
			       "region, but eCryptfs was mounted without "
			       "xattr support enabled. eCryptfs will not treat "
			       "this like an encrypted file.\n");
			rc = -EINVAL;
		}
	}
out:
	if (page_virt) {
		memset(page_virt, 0, PAGE_CACHE_SIZE);
		kmem_cache_free(ecryptfs_header_cache_1, page_virt);
	}
	return rc;
}

/**
 * ecryptfs_encode_filename - converts a plaintext file name to cipher text
 * @crypt_stat: The crypt_stat struct associated with the file anem to encode
 * @name: The plaintext name
 * @length: The length of the plaintext
 * @encoded_name: The encypted name
 *
 * Encrypts and encodes a filename into something that constitutes a
 * valid filename for a filesystem, with printable characters.
 *
 * We assume that we have a properly initialized crypto context,
 * pointed to by crypt_stat->tfm.
 *
 * TODO: Implement filename decoding and decryption here, in place of
 * memcpy. We are keeping the framework around for now to (1)
 * facilitate testing of the components needed to implement filename
 * encryption and (2) to provide a code base from which other
 * developers in the community can easily implement this feature.
 *
 * Returns the length of encoded filename; negative if error
 */
int
ecryptfs_encode_filename(struct ecryptfs_crypt_stat *crypt_stat,
			 const char *name, int length, char **encoded_name)
{
	int error = 0;

	(*encoded_name) = kmalloc(length + 2, GFP_KERNEL);
	if (!(*encoded_name)) {
		error = -ENOMEM;
		goto out;
	}
	/* TODO: Filename encryption is a scheduled feature for a
	 * future version of eCryptfs. This function is here only for
	 * the purpose of providing a framework for other developers
	 * to easily implement filename encryption. Hint: Replace this
	 * memcpy() with a call to encrypt and encode the
	 * filename, the set the length accordingly. */
	memcpy((void *)(*encoded_name), (void *)name, length);
	(*encoded_name)[length] = '\0';
	error = length + 1;
out:
	return error;
}

/**
 * ecryptfs_decode_filename - converts the cipher text name to plaintext
 * @crypt_stat: The crypt_stat struct associated with the file
 * @name: The filename in cipher text
 * @length: The length of the cipher text name
 * @decrypted_name: The plaintext name
 *
 * Decodes and decrypts the filename.
 *
 * We assume that we have a properly initialized crypto context,
 * pointed to by crypt_stat->tfm.
 *
 * TODO: Implement filename decoding and decryption here, in place of
 * memcpy. We are keeping the framework around for now to (1)
 * facilitate testing of the components needed to implement filename
 * encryption and (2) to provide a code base from which other
 * developers in the community can easily implement this feature.
 *
 * Returns the length of decoded filename; negative if error
 */
int
ecryptfs_decode_filename(struct ecryptfs_crypt_stat *crypt_stat,
			 const char *name, int length, char **decrypted_name)
{
	int error = 0;

	(*decrypted_name) = kmalloc(length + 2, GFP_KERNEL);
	if (!(*decrypted_name)) {
		error = -ENOMEM;
		goto out;
	}
	/* TODO: Filename encryption is a scheduled feature for a
	 * future version of eCryptfs. This function is here only for
	 * the purpose of providing a framework for other developers
	 * to easily implement filename encryption. Hint: Replace this
	 * memcpy() with a call to decode and decrypt the
	 * filename, the set the length accordingly. */
	memcpy((void *)(*decrypted_name), (void *)name, length);
	(*decrypted_name)[length + 1] = '\0';	/* Only for convenience
						 * in printing out the
						 * string in debug
						 * messages */
	error = length;
out:
	return error;
}

/**
 * ecryptfs_process_key_cipher - Perform key cipher initialization.
 * @key_tfm: Crypto context for key material, set by this function
 * @cipher_name: Name of the cipher
 * @key_size: Size of the key in bytes
 *
 * Returns zero on success. Any crypto_tfm structs allocated here
 * should be released by other functions, such as on a superblock put
 * event, regardless of whether this function succeeds for fails.
 */
static int
ecryptfs_process_key_cipher(struct crypto_blkcipher **key_tfm,
			    char *cipher_name, size_t *key_size)
{
	char dummy_key[ECRYPTFS_MAX_KEY_BYTES];
	char *full_alg_name;
	int rc;

	*key_tfm = NULL;
	if (*key_size > ECRYPTFS_MAX_KEY_BYTES) {
		rc = -EINVAL;
		printk(KERN_ERR "Requested key size is [%Zd] bytes; maximum "
		      "allowable is [%d]\n", *key_size, ECRYPTFS_MAX_KEY_BYTES);
		goto out;
	}
	rc = ecryptfs_crypto_api_algify_cipher_name(&full_alg_name, cipher_name,
						    "ecb");
	if (rc)
		goto out;
	*key_tfm = crypto_alloc_blkcipher(full_alg_name, 0, CRYPTO_ALG_ASYNC);
	kfree(full_alg_name);
	if (IS_ERR(*key_tfm)) {
		rc = PTR_ERR(*key_tfm);
		printk(KERN_ERR "Unable to allocate crypto cipher with name "
		       "[%s]; rc = [%d]\n", cipher_name, rc);
		goto out;
	}
	crypto_blkcipher_set_flags(*key_tfm, CRYPTO_TFM_REQ_WEAK_KEY);
	if (*key_size == 0) {
		struct blkcipher_alg *alg = crypto_blkcipher_alg(*key_tfm);

		*key_size = alg->max_keysize;
	}
	get_random_bytes(dummy_key, *key_size);
	rc = crypto_blkcipher_setkey(*key_tfm, dummy_key, *key_size);
	if (rc) {
		printk(KERN_ERR "Error attempting to set key of size [%Zd] for "
		       "cipher [%s]; rc = [%d]\n", *key_size, cipher_name, rc);
		rc = -EINVAL;
		goto out;
	}
out:
	return rc;
}

struct kmem_cache *ecryptfs_key_tfm_cache;
struct list_head key_tfm_list;
struct mutex key_tfm_list_mutex;

int ecryptfs_init_crypto(void)
{
	mutex_init(&key_tfm_list_mutex);
	INIT_LIST_HEAD(&key_tfm_list);
	return 0;
}

int ecryptfs_destroy_crypto(void)
{
	struct ecryptfs_key_tfm *key_tfm, *key_tfm_tmp;

	mutex_lock(&key_tfm_list_mutex);
	list_for_each_entry_safe(key_tfm, key_tfm_tmp, &key_tfm_list,
				 key_tfm_list) {
		list_del(&key_tfm->key_tfm_list);
		if (key_tfm->key_tfm)
			crypto_free_blkcipher(key_tfm->key_tfm);
		kmem_cache_free(ecryptfs_key_tfm_cache, key_tfm);
	}
	mutex_unlock(&key_tfm_list_mutex);
	return 0;
}

int
ecryptfs_add_new_key_tfm(struct ecryptfs_key_tfm **key_tfm, char *cipher_name,
			 size_t key_size)
{
	struct ecryptfs_key_tfm *tmp_tfm;
	int rc = 0;

	tmp_tfm = kmem_cache_alloc(ecryptfs_key_tfm_cache, GFP_KERNEL);
	if (key_tfm != NULL)
		(*key_tfm) = tmp_tfm;
	if (!tmp_tfm) {
		rc = -ENOMEM;
		printk(KERN_ERR "Error attempting to allocate from "
		       "ecryptfs_key_tfm_cache\n");
		goto out;
	}
	mutex_init(&tmp_tfm->key_tfm_mutex);
	strncpy(tmp_tfm->cipher_name, cipher_name,
		ECRYPTFS_MAX_CIPHER_NAME_SIZE);
	tmp_tfm->key_size = key_size;
	rc = ecryptfs_process_key_cipher(&tmp_tfm->key_tfm,
					 tmp_tfm->cipher_name,
					 &tmp_tfm->key_size);
	if (rc) {
		printk(KERN_ERR "Error attempting to initialize key TFM "
		       "cipher with name = [%s]; rc = [%d]\n",
		       tmp_tfm->cipher_name, rc);
		kmem_cache_free(ecryptfs_key_tfm_cache, tmp_tfm);
		if (key_tfm != NULL)
			(*key_tfm) = NULL;
		goto out;
	}
	mutex_lock(&key_tfm_list_mutex);
	list_add(&tmp_tfm->key_tfm_list, &key_tfm_list);
	mutex_unlock(&key_tfm_list_mutex);
out:
	return rc;
}

int ecryptfs_get_tfm_and_mutex_for_cipher_name(struct crypto_blkcipher **tfm,
					       struct mutex **tfm_mutex,
					       char *cipher_name)
{
	struct ecryptfs_key_tfm *key_tfm;
	int rc = 0;

	(*tfm) = NULL;
	(*tfm_mutex) = NULL;
	mutex_lock(&key_tfm_list_mutex);
	list_for_each_entry(key_tfm, &key_tfm_list, key_tfm_list) {
		if (strcmp(key_tfm->cipher_name, cipher_name) == 0) {
			(*tfm) = key_tfm->key_tfm;
			(*tfm_mutex) = &key_tfm->key_tfm_mutex;
			mutex_unlock(&key_tfm_list_mutex);
			goto out;
		}
	}
	mutex_unlock(&key_tfm_list_mutex);
	rc = ecryptfs_add_new_key_tfm(&key_tfm, cipher_name, 0);
	if (rc) {
		printk(KERN_ERR "Error adding new key_tfm to list; rc = [%d]\n",
		       rc);
		goto out;
	}
	(*tfm) = key_tfm->key_tfm;
	(*tfm_mutex) = &key_tfm->key_tfm_mutex;
out:
	return rc;
}
