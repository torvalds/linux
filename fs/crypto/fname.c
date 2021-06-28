// SPDX-License-Identifier: GPL-2.0
/*
 * This contains functions for filename crypto management
 *
 * Copyright (C) 2015, Google, Inc.
 * Copyright (C) 2015, Motorola Mobility
 *
 * Written by Uday Savagaonkar, 2014.
 * Modified by Jaegeuk Kim, 2015.
 *
 * This has not yet undergone a rigorous security audit.
 */

#include <linux/namei.h>
#include <linux/scatterlist.h>
#include <crypto/hash.h>
#include <crypto/sha2.h>
#include <crypto/skcipher.h>
#include "fscrypt_private.h"

/*
 * struct fscrypt_nokey_name - identifier for directory entry when key is absent
 *
 * When userspace lists an encrypted directory without access to the key, the
 * filesystem must present a unique "no-key name" for each filename that allows
 * it to find the directory entry again if requested.  Naively, that would just
 * mean using the ciphertext filenames.  However, since the ciphertext filenames
 * can contain illegal characters ('\0' and '/'), they must be encoded in some
 * way.  We use base64.  But that can cause names to exceed NAME_MAX (255
 * bytes), so we also need to use a strong hash to abbreviate long names.
 *
 * The filesystem may also need another kind of hash, the "dirhash", to quickly
 * find the directory entry.  Since filesystems normally compute the dirhash
 * over the on-disk filename (i.e. the ciphertext), it's not computable from
 * no-key names that abbreviate the ciphertext using the strong hash to fit in
 * NAME_MAX.  It's also not computable if it's a keyed hash taken over the
 * plaintext (but it may still be available in the on-disk directory entry);
 * casefolded directories use this type of dirhash.  At least in these cases,
 * each no-key name must include the name's dirhash too.
 *
 * To meet all these requirements, we base64-encode the following
 * variable-length structure.  It contains the dirhash, or 0's if the filesystem
 * didn't provide one; up to 149 bytes of the ciphertext name; and for
 * ciphertexts longer than 149 bytes, also the SHA-256 of the remaining bytes.
 *
 * This ensures that each no-key name contains everything needed to find the
 * directory entry again, contains only legal characters, doesn't exceed
 * NAME_MAX, is unambiguous unless there's a SHA-256 collision, and that we only
 * take the performance hit of SHA-256 on very long filenames (which are rare).
 */
struct fscrypt_nokey_name {
	u32 dirhash[2];
	u8 bytes[149];
	u8 sha256[SHA256_DIGEST_SIZE];
}; /* 189 bytes => 252 bytes base64-encoded, which is <= NAME_MAX (255) */

/*
 * Decoded size of max-size nokey name, i.e. a name that was abbreviated using
 * the strong hash and thus includes the 'sha256' field.  This isn't simply
 * sizeof(struct fscrypt_nokey_name), as the padding at the end isn't included.
 */
#define FSCRYPT_NOKEY_NAME_MAX	offsetofend(struct fscrypt_nokey_name, sha256)

static inline bool fscrypt_is_dot_dotdot(const struct qstr *str)
{
	if (str->len == 1 && str->name[0] == '.')
		return true;

	if (str->len == 2 && str->name[0] == '.' && str->name[1] == '.')
		return true;

	return false;
}

/**
 * fscrypt_fname_encrypt() - encrypt a filename
 * @inode: inode of the parent directory (for regular filenames)
 *	   or of the symlink (for symlink targets)
 * @iname: the filename to encrypt
 * @out: (output) the encrypted filename
 * @olen: size of the encrypted filename.  It must be at least @iname->len.
 *	  Any extra space is filled with NUL padding before encryption.
 *
 * Return: 0 on success, -errno on failure
 */
int fscrypt_fname_encrypt(const struct inode *inode, const struct qstr *iname,
			  u8 *out, unsigned int olen)
{
	struct skcipher_request *req = NULL;
	DECLARE_CRYPTO_WAIT(wait);
	const struct fscrypt_info *ci = inode->i_crypt_info;
	struct crypto_skcipher *tfm = ci->ci_enc_key.tfm;
	union fscrypt_iv iv;
	struct scatterlist sg;
	int res;

	/*
	 * Copy the filename to the output buffer for encrypting in-place and
	 * pad it with the needed number of NUL bytes.
	 */
	if (WARN_ON(olen < iname->len))
		return -ENOBUFS;
	memcpy(out, iname->name, iname->len);
	memset(out + iname->len, 0, olen - iname->len);

	/* Initialize the IV */
	fscrypt_generate_iv(&iv, 0, ci);

	/* Set up the encryption request */
	req = skcipher_request_alloc(tfm, GFP_NOFS);
	if (!req)
		return -ENOMEM;
	skcipher_request_set_callback(req,
			CRYPTO_TFM_REQ_MAY_BACKLOG | CRYPTO_TFM_REQ_MAY_SLEEP,
			crypto_req_done, &wait);
	sg_init_one(&sg, out, olen);
	skcipher_request_set_crypt(req, &sg, &sg, olen, &iv);

	/* Do the encryption */
	res = crypto_wait_req(crypto_skcipher_encrypt(req), &wait);
	skcipher_request_free(req);
	if (res < 0) {
		fscrypt_err(inode, "Filename encryption failed: %d", res);
		return res;
	}

	return 0;
}

/**
 * fname_decrypt() - decrypt a filename
 * @inode: inode of the parent directory (for regular filenames)
 *	   or of the symlink (for symlink targets)
 * @iname: the encrypted filename to decrypt
 * @oname: (output) the decrypted filename.  The caller must have allocated
 *	   enough space for this, e.g. using fscrypt_fname_alloc_buffer().
 *
 * Return: 0 on success, -errno on failure
 */
static int fname_decrypt(const struct inode *inode,
			 const struct fscrypt_str *iname,
			 struct fscrypt_str *oname)
{
	struct skcipher_request *req = NULL;
	DECLARE_CRYPTO_WAIT(wait);
	struct scatterlist src_sg, dst_sg;
	const struct fscrypt_info *ci = inode->i_crypt_info;
	struct crypto_skcipher *tfm = ci->ci_enc_key.tfm;
	union fscrypt_iv iv;
	int res;

	/* Allocate request */
	req = skcipher_request_alloc(tfm, GFP_NOFS);
	if (!req)
		return -ENOMEM;
	skcipher_request_set_callback(req,
		CRYPTO_TFM_REQ_MAY_BACKLOG | CRYPTO_TFM_REQ_MAY_SLEEP,
		crypto_req_done, &wait);

	/* Initialize IV */
	fscrypt_generate_iv(&iv, 0, ci);

	/* Create decryption request */
	sg_init_one(&src_sg, iname->name, iname->len);
	sg_init_one(&dst_sg, oname->name, oname->len);
	skcipher_request_set_crypt(req, &src_sg, &dst_sg, iname->len, &iv);
	res = crypto_wait_req(crypto_skcipher_decrypt(req), &wait);
	skcipher_request_free(req);
	if (res < 0) {
		fscrypt_err(inode, "Filename decryption failed: %d", res);
		return res;
	}

	oname->len = strnlen(oname->name, iname->len);
	return 0;
}

static const char lookup_table[65] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+,";

#define BASE64_CHARS(nbytes)	DIV_ROUND_UP((nbytes) * 4, 3)

/**
 * base64_encode() - base64-encode some bytes
 * @src: the bytes to encode
 * @len: number of bytes to encode
 * @dst: (output) the base64-encoded string.  Not NUL-terminated.
 *
 * Encodes the input string using characters from the set [A-Za-z0-9+,].
 * The encoded string is roughly 4/3 times the size of the input string.
 *
 * Return: length of the encoded string
 */
static int base64_encode(const u8 *src, int len, char *dst)
{
	int i, bits = 0, ac = 0;
	char *cp = dst;

	for (i = 0; i < len; i++) {
		ac += src[i] << bits;
		bits += 8;
		do {
			*cp++ = lookup_table[ac & 0x3f];
			ac >>= 6;
			bits -= 6;
		} while (bits >= 6);
	}
	if (bits)
		*cp++ = lookup_table[ac & 0x3f];
	return cp - dst;
}

static int base64_decode(const char *src, int len, u8 *dst)
{
	int i, bits = 0, ac = 0;
	const char *p;
	u8 *cp = dst;

	for (i = 0; i < len; i++) {
		p = strchr(lookup_table, src[i]);
		if (p == NULL || src[i] == 0)
			return -2;
		ac += (p - lookup_table) << bits;
		bits += 6;
		if (bits >= 8) {
			*cp++ = ac & 0xff;
			ac >>= 8;
			bits -= 8;
		}
	}
	if (ac)
		return -1;
	return cp - dst;
}

bool fscrypt_fname_encrypted_size(const union fscrypt_policy *policy,
				  u32 orig_len, u32 max_len,
				  u32 *encrypted_len_ret)
{
	int padding = 4 << (fscrypt_policy_flags(policy) &
			    FSCRYPT_POLICY_FLAGS_PAD_MASK);
	u32 encrypted_len;

	if (orig_len > max_len)
		return false;
	encrypted_len = max(orig_len, (u32)FS_CRYPTO_BLOCK_SIZE);
	encrypted_len = round_up(encrypted_len, padding);
	*encrypted_len_ret = min(encrypted_len, max_len);
	return true;
}

/**
 * fscrypt_fname_alloc_buffer() - allocate a buffer for presented filenames
 * @max_encrypted_len: maximum length of encrypted filenames the buffer will be
 *		       used to present
 * @crypto_str: (output) buffer to allocate
 *
 * Allocate a buffer that is large enough to hold any decrypted or encoded
 * filename (null-terminated), for the given maximum encrypted filename length.
 *
 * Return: 0 on success, -errno on failure
 */
int fscrypt_fname_alloc_buffer(u32 max_encrypted_len,
			       struct fscrypt_str *crypto_str)
{
	const u32 max_encoded_len = BASE64_CHARS(FSCRYPT_NOKEY_NAME_MAX);
	u32 max_presented_len;

	max_presented_len = max(max_encoded_len, max_encrypted_len);

	crypto_str->name = kmalloc(max_presented_len + 1, GFP_NOFS);
	if (!crypto_str->name)
		return -ENOMEM;
	crypto_str->len = max_presented_len;
	return 0;
}
EXPORT_SYMBOL(fscrypt_fname_alloc_buffer);

/**
 * fscrypt_fname_free_buffer() - free a buffer for presented filenames
 * @crypto_str: the buffer to free
 *
 * Free a buffer that was allocated by fscrypt_fname_alloc_buffer().
 */
void fscrypt_fname_free_buffer(struct fscrypt_str *crypto_str)
{
	if (!crypto_str)
		return;
	kfree(crypto_str->name);
	crypto_str->name = NULL;
}
EXPORT_SYMBOL(fscrypt_fname_free_buffer);

/**
 * fscrypt_fname_disk_to_usr() - convert an encrypted filename to
 *				 user-presentable form
 * @inode: inode of the parent directory (for regular filenames)
 *	   or of the symlink (for symlink targets)
 * @hash: first part of the name's dirhash, if applicable.  This only needs to
 *	  be provided if the filename is located in an indexed directory whose
 *	  encryption key may be unavailable.  Not needed for symlink targets.
 * @minor_hash: second part of the name's dirhash, if applicable
 * @iname: encrypted filename to convert.  May also be "." or "..", which
 *	   aren't actually encrypted.
 * @oname: output buffer for the user-presentable filename.  The caller must
 *	   have allocated enough space for this, e.g. using
 *	   fscrypt_fname_alloc_buffer().
 *
 * If the key is available, we'll decrypt the disk name.  Otherwise, we'll
 * encode it for presentation in fscrypt_nokey_name format.
 * See struct fscrypt_nokey_name for details.
 *
 * Return: 0 on success, -errno on failure
 */
int fscrypt_fname_disk_to_usr(const struct inode *inode,
			      u32 hash, u32 minor_hash,
			      const struct fscrypt_str *iname,
			      struct fscrypt_str *oname)
{
	const struct qstr qname = FSTR_TO_QSTR(iname);
	struct fscrypt_nokey_name nokey_name;
	u32 size; /* size of the unencoded no-key name */

	if (fscrypt_is_dot_dotdot(&qname)) {
		oname->name[0] = '.';
		oname->name[iname->len - 1] = '.';
		oname->len = iname->len;
		return 0;
	}

	if (iname->len < FS_CRYPTO_BLOCK_SIZE)
		return -EUCLEAN;

	if (fscrypt_has_encryption_key(inode))
		return fname_decrypt(inode, iname, oname);

	/*
	 * Sanity check that struct fscrypt_nokey_name doesn't have padding
	 * between fields and that its encoded size never exceeds NAME_MAX.
	 */
	BUILD_BUG_ON(offsetofend(struct fscrypt_nokey_name, dirhash) !=
		     offsetof(struct fscrypt_nokey_name, bytes));
	BUILD_BUG_ON(offsetofend(struct fscrypt_nokey_name, bytes) !=
		     offsetof(struct fscrypt_nokey_name, sha256));
	BUILD_BUG_ON(BASE64_CHARS(FSCRYPT_NOKEY_NAME_MAX) > NAME_MAX);

	nokey_name.dirhash[0] = hash;
	nokey_name.dirhash[1] = minor_hash;

	if (iname->len <= sizeof(nokey_name.bytes)) {
		memcpy(nokey_name.bytes, iname->name, iname->len);
		size = offsetof(struct fscrypt_nokey_name, bytes[iname->len]);
	} else {
		memcpy(nokey_name.bytes, iname->name, sizeof(nokey_name.bytes));
		/* Compute strong hash of remaining part of name. */
		sha256(&iname->name[sizeof(nokey_name.bytes)],
		       iname->len - sizeof(nokey_name.bytes),
		       nokey_name.sha256);
		size = FSCRYPT_NOKEY_NAME_MAX;
	}
	oname->len = base64_encode((const u8 *)&nokey_name, size, oname->name);
	return 0;
}
EXPORT_SYMBOL(fscrypt_fname_disk_to_usr);

/**
 * fscrypt_setup_filename() - prepare to search a possibly encrypted directory
 * @dir: the directory that will be searched
 * @iname: the user-provided filename being searched for
 * @lookup: 1 if we're allowed to proceed without the key because it's
 *	->lookup() or we're finding the dir_entry for deletion; 0 if we cannot
 *	proceed without the key because we're going to create the dir_entry.
 * @fname: the filename information to be filled in
 *
 * Given a user-provided filename @iname, this function sets @fname->disk_name
 * to the name that would be stored in the on-disk directory entry, if possible.
 * If the directory is unencrypted this is simply @iname.  Else, if we have the
 * directory's encryption key, then @iname is the plaintext, so we encrypt it to
 * get the disk_name.
 *
 * Else, for keyless @lookup operations, @iname should be a no-key name, so we
 * decode it to get the struct fscrypt_nokey_name.  Non-@lookup operations will
 * be impossible in this case, so we fail them with ENOKEY.
 *
 * If successful, fscrypt_free_filename() must be called later to clean up.
 *
 * Return: 0 on success, -errno on failure
 */
int fscrypt_setup_filename(struct inode *dir, const struct qstr *iname,
			      int lookup, struct fscrypt_name *fname)
{
	struct fscrypt_nokey_name *nokey_name;
	int ret;

	memset(fname, 0, sizeof(struct fscrypt_name));
	fname->usr_fname = iname;

	if (!IS_ENCRYPTED(dir) || fscrypt_is_dot_dotdot(iname)) {
		fname->disk_name.name = (unsigned char *)iname->name;
		fname->disk_name.len = iname->len;
		return 0;
	}
	ret = fscrypt_get_encryption_info(dir, lookup);
	if (ret)
		return ret;

	if (fscrypt_has_encryption_key(dir)) {
		if (!fscrypt_fname_encrypted_size(&dir->i_crypt_info->ci_policy,
						  iname->len,
						  dir->i_sb->s_cop->max_namelen,
						  &fname->crypto_buf.len))
			return -ENAMETOOLONG;
		fname->crypto_buf.name = kmalloc(fname->crypto_buf.len,
						 GFP_NOFS);
		if (!fname->crypto_buf.name)
			return -ENOMEM;

		ret = fscrypt_fname_encrypt(dir, iname, fname->crypto_buf.name,
					    fname->crypto_buf.len);
		if (ret)
			goto errout;
		fname->disk_name.name = fname->crypto_buf.name;
		fname->disk_name.len = fname->crypto_buf.len;
		return 0;
	}
	if (!lookup)
		return -ENOKEY;
	fname->is_nokey_name = true;

	/*
	 * We don't have the key and we are doing a lookup; decode the
	 * user-supplied name
	 */

	if (iname->len > BASE64_CHARS(FSCRYPT_NOKEY_NAME_MAX))
		return -ENOENT;

	fname->crypto_buf.name = kmalloc(FSCRYPT_NOKEY_NAME_MAX, GFP_KERNEL);
	if (fname->crypto_buf.name == NULL)
		return -ENOMEM;

	ret = base64_decode(iname->name, iname->len, fname->crypto_buf.name);
	if (ret < (int)offsetof(struct fscrypt_nokey_name, bytes[1]) ||
	    (ret > offsetof(struct fscrypt_nokey_name, sha256) &&
	     ret != FSCRYPT_NOKEY_NAME_MAX)) {
		ret = -ENOENT;
		goto errout;
	}
	fname->crypto_buf.len = ret;

	nokey_name = (void *)fname->crypto_buf.name;
	fname->hash = nokey_name->dirhash[0];
	fname->minor_hash = nokey_name->dirhash[1];
	if (ret != FSCRYPT_NOKEY_NAME_MAX) {
		/* The full ciphertext filename is available. */
		fname->disk_name.name = nokey_name->bytes;
		fname->disk_name.len =
			ret - offsetof(struct fscrypt_nokey_name, bytes);
	}
	return 0;

errout:
	kfree(fname->crypto_buf.name);
	return ret;
}
EXPORT_SYMBOL(fscrypt_setup_filename);

/**
 * fscrypt_match_name() - test whether the given name matches a directory entry
 * @fname: the name being searched for
 * @de_name: the name from the directory entry
 * @de_name_len: the length of @de_name in bytes
 *
 * Normally @fname->disk_name will be set, and in that case we simply compare
 * that to the name stored in the directory entry.  The only exception is that
 * if we don't have the key for an encrypted directory and the name we're
 * looking for is very long, then we won't have the full disk_name and instead
 * we'll need to match against a fscrypt_nokey_name that includes a strong hash.
 *
 * Return: %true if the name matches, otherwise %false.
 */
bool fscrypt_match_name(const struct fscrypt_name *fname,
			const u8 *de_name, u32 de_name_len)
{
	const struct fscrypt_nokey_name *nokey_name =
		(const void *)fname->crypto_buf.name;
	u8 digest[SHA256_DIGEST_SIZE];

	if (likely(fname->disk_name.name)) {
		if (de_name_len != fname->disk_name.len)
			return false;
		return !memcmp(de_name, fname->disk_name.name, de_name_len);
	}
	if (de_name_len <= sizeof(nokey_name->bytes))
		return false;
	if (memcmp(de_name, nokey_name->bytes, sizeof(nokey_name->bytes)))
		return false;
	sha256(&de_name[sizeof(nokey_name->bytes)],
	       de_name_len - sizeof(nokey_name->bytes), digest);
	return !memcmp(digest, nokey_name->sha256, sizeof(digest));
}
EXPORT_SYMBOL_GPL(fscrypt_match_name);

/**
 * fscrypt_fname_siphash() - calculate the SipHash of a filename
 * @dir: the parent directory
 * @name: the filename to calculate the SipHash of
 *
 * Given a plaintext filename @name and a directory @dir which uses SipHash as
 * its dirhash method and has had its fscrypt key set up, this function
 * calculates the SipHash of that name using the directory's secret dirhash key.
 *
 * Return: the SipHash of @name using the hash key of @dir
 */
u64 fscrypt_fname_siphash(const struct inode *dir, const struct qstr *name)
{
	const struct fscrypt_info *ci = dir->i_crypt_info;

	WARN_ON(!ci->ci_dirhash_key_initialized);

	return siphash(name->name, name->len, &ci->ci_dirhash_key);
}
EXPORT_SYMBOL_GPL(fscrypt_fname_siphash);

/*
 * Validate dentries in encrypted directories to make sure we aren't potentially
 * caching stale dentries after a key has been added.
 */
int fscrypt_d_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct dentry *dir;
	int err;
	int valid;

	/*
	 * Plaintext names are always valid, since fscrypt doesn't support
	 * reverting to no-key names without evicting the directory's inode
	 * -- which implies eviction of the dentries in the directory.
	 */
	if (!(dentry->d_flags & DCACHE_NOKEY_NAME))
		return 1;

	/*
	 * No-key name; valid if the directory's key is still unavailable.
	 *
	 * Although fscrypt forbids rename() on no-key names, we still must use
	 * dget_parent() here rather than use ->d_parent directly.  That's
	 * because a corrupted fs image may contain directory hard links, which
	 * the VFS handles by moving the directory's dentry tree in the dcache
	 * each time ->lookup() finds the directory and it already has a dentry
	 * elsewhere.  Thus ->d_parent can be changing, and we must safely grab
	 * a reference to some ->d_parent to prevent it from being freed.
	 */

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	dir = dget_parent(dentry);
	/*
	 * Pass allow_unsupported=true, so that files with an unsupported
	 * encryption policy can be deleted.
	 */
	err = fscrypt_get_encryption_info(d_inode(dir), true);
	valid = !fscrypt_has_encryption_key(d_inode(dir));
	dput(dir);

	if (err < 0)
		return err;

	return valid;
}
EXPORT_SYMBOL_GPL(fscrypt_d_revalidate);
