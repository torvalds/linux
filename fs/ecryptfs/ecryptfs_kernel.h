/**
 * eCryptfs: Linux filesystem encryption layer
 * Kernel declarations.
 *
 * Copyright (C) 1997-2003 Erez Zadok
 * Copyright (C) 2001-2003 Stony Brook University
 * Copyright (C) 2004-2006 International Business Machines Corp.
 *   Author(s): Michael A. Halcrow <mahalcro@us.ibm.com>
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

#ifndef ECRYPTFS_KERNEL_H
#define ECRYPTFS_KERNEL_H

#include <keys/user-type.h>
#include <linux/fs.h>
#include <linux/fs_stack.h>
#include <linux/scatterlist.h>

/* Version verification for shared data structures w/ userspace */
#define ECRYPTFS_VERSION_MAJOR 0x00
#define ECRYPTFS_VERSION_MINOR 0x04
#define ECRYPTFS_SUPPORTED_FILE_VERSION 0x01
/* These flags indicate which features are supported by the kernel
 * module; userspace tools such as the mount helper read
 * ECRYPTFS_VERSIONING_MASK from a sysfs handle in order to determine
 * how to behave. */
#define ECRYPTFS_VERSIONING_PASSPHRASE 0x00000001
#define ECRYPTFS_VERSIONING_PUBKEY 0x00000002
#define ECRYPTFS_VERSIONING_PLAINTEXT_PASSTHROUGH 0x00000004
#define ECRYPTFS_VERSIONING_POLICY 0x00000008
#define ECRYPTFS_VERSIONING_MASK (ECRYPTFS_VERSIONING_PASSPHRASE \
                                  | ECRYPTFS_VERSIONING_PLAINTEXT_PASSTHROUGH)

#define ECRYPTFS_MAX_PASSWORD_LENGTH 64
#define ECRYPTFS_MAX_PASSPHRASE_BYTES ECRYPTFS_MAX_PASSWORD_LENGTH
#define ECRYPTFS_SALT_SIZE 8
#define ECRYPTFS_SALT_SIZE_HEX (ECRYPTFS_SALT_SIZE*2)
/* The original signature size is only for what is stored on disk; all
 * in-memory representations are expanded hex, so it better adapted to
 * be passed around or referenced on the command line */
#define ECRYPTFS_SIG_SIZE 8
#define ECRYPTFS_SIG_SIZE_HEX (ECRYPTFS_SIG_SIZE*2)
#define ECRYPTFS_PASSWORD_SIG_SIZE ECRYPTFS_SIG_SIZE_HEX
#define ECRYPTFS_MAX_KEY_BYTES 64
#define ECRYPTFS_MAX_ENCRYPTED_KEY_BYTES 512
#define ECRYPTFS_DEFAULT_IV_BYTES 16
#define ECRYPTFS_FILE_VERSION 0x01
#define ECRYPTFS_DEFAULT_HEADER_EXTENT_SIZE 8192
#define ECRYPTFS_DEFAULT_EXTENT_SIZE 4096
#define ECRYPTFS_MINIMUM_HEADER_EXTENT_SIZE 8192

#define RFC2440_CIPHER_DES3_EDE 0x02
#define RFC2440_CIPHER_CAST_5 0x03
#define RFC2440_CIPHER_BLOWFISH 0x04
#define RFC2440_CIPHER_AES_128 0x07
#define RFC2440_CIPHER_AES_192 0x08
#define RFC2440_CIPHER_AES_256 0x09
#define RFC2440_CIPHER_TWOFISH 0x0a
#define RFC2440_CIPHER_CAST_6 0x0b

#define ECRYPTFS_SET_FLAG(flag_bit_vector, flag) (flag_bit_vector |= (flag))
#define ECRYPTFS_CLEAR_FLAG(flag_bit_vector, flag) (flag_bit_vector &= ~(flag))
#define ECRYPTFS_CHECK_FLAG(flag_bit_vector, flag) (flag_bit_vector & (flag))

/**
 * For convenience, we may need to pass around the encrypted session
 * key between kernel and userspace because the authentication token
 * may not be extractable.  For example, the TPM may not release the
 * private key, instead requiring the encrypted data and returning the
 * decrypted data.
 */
struct ecryptfs_session_key {
#define ECRYPTFS_USERSPACE_SHOULD_TRY_TO_DECRYPT 0x00000001
#define ECRYPTFS_USERSPACE_SHOULD_TRY_TO_ENCRYPT 0x00000002
#define ECRYPTFS_CONTAINS_DECRYPTED_KEY 0x00000004
#define ECRYPTFS_CONTAINS_ENCRYPTED_KEY 0x00000008
	u32 flags;
	u32 encrypted_key_size;
	u32 decrypted_key_size;
	u8 encrypted_key[ECRYPTFS_MAX_ENCRYPTED_KEY_BYTES];
	u8 decrypted_key[ECRYPTFS_MAX_KEY_BYTES];
};

struct ecryptfs_password {
	u32 password_bytes;
	s32 hash_algo;
	u32 hash_iterations;
	u32 session_key_encryption_key_bytes;
#define ECRYPTFS_PERSISTENT_PASSWORD 0x01
#define ECRYPTFS_SESSION_KEY_ENCRYPTION_KEY_SET 0x02
	u32 flags;
	/* Iterated-hash concatenation of salt and passphrase */
	u8 session_key_encryption_key[ECRYPTFS_MAX_KEY_BYTES];
	u8 signature[ECRYPTFS_PASSWORD_SIG_SIZE + 1];
	/* Always in expanded hex */
	u8 salt[ECRYPTFS_SALT_SIZE];
};

enum ecryptfs_token_types {ECRYPTFS_PASSWORD, ECRYPTFS_PRIVATE_KEY};

/* May be a password or a private key */
struct ecryptfs_auth_tok {
	u16 version; /* 8-bit major and 8-bit minor */
	u16 token_type;
	u32 flags;
	struct ecryptfs_session_key session_key;
	u8 reserved[32];
	union {
		struct ecryptfs_password password;
		/* Private key is in future eCryptfs releases */
	} token;
} __attribute__ ((packed));

void ecryptfs_dump_auth_tok(struct ecryptfs_auth_tok *auth_tok);
extern void ecryptfs_to_hex(char *dst, char *src, size_t src_size);
extern void ecryptfs_from_hex(char *dst, char *src, int dst_size);

struct ecryptfs_key_record {
	unsigned char type;
	size_t enc_key_size;
	unsigned char sig[ECRYPTFS_SIG_SIZE];
	unsigned char enc_key[ECRYPTFS_MAX_ENCRYPTED_KEY_BYTES];
};

struct ecryptfs_auth_tok_list {
	struct ecryptfs_auth_tok *auth_tok;
	struct list_head list;
};

struct ecryptfs_crypt_stat;
struct ecryptfs_mount_crypt_stat;

struct ecryptfs_page_crypt_context {
	struct page *page;
#define ECRYPTFS_PREPARE_COMMIT_MODE 0
#define ECRYPTFS_WRITEPAGE_MODE      1
	unsigned int mode;
	union {
		struct file *lower_file;
		struct writeback_control *wbc;
	} param;
};

static inline struct ecryptfs_auth_tok *
ecryptfs_get_key_payload_data(struct key *key)
{
	return (struct ecryptfs_auth_tok *)
		(((struct user_key_payload*)key->payload.data)->data);
}

#define ECRYPTFS_SUPER_MAGIC 0xf15f
#define ECRYPTFS_MAX_KEYSET_SIZE 1024
#define ECRYPTFS_MAX_CIPHER_NAME_SIZE 32
#define ECRYPTFS_MAX_NUM_ENC_KEYS 64
#define ECRYPTFS_MAX_NUM_KEYSIGS 2 /* TODO: Make this a linked list */
#define ECRYPTFS_MAX_IV_BYTES 16	/* 128 bits */
#define ECRYPTFS_SALT_BYTES 2
#define MAGIC_ECRYPTFS_MARKER 0x3c81b7f5
#define MAGIC_ECRYPTFS_MARKER_SIZE_BYTES 8	/* 4*2 */
#define ECRYPTFS_FILE_SIZE_BYTES 8
#define ECRYPTFS_DEFAULT_CIPHER "aes"
#define ECRYPTFS_DEFAULT_KEY_BYTES 16
#define ECRYPTFS_DEFAULT_CHAINING_MODE CRYPTO_TFM_MODE_CBC
#define ECRYPTFS_DEFAULT_HASH "md5"
#define ECRYPTFS_TAG_3_PACKET_TYPE 0x8C
#define ECRYPTFS_TAG_11_PACKET_TYPE 0xED
#define MD5_DIGEST_SIZE 16

/**
 * This is the primary struct associated with each encrypted file.
 *
 * TODO: cache align/pack?
 */
struct ecryptfs_crypt_stat {
#define ECRYPTFS_STRUCT_INITIALIZED 0x00000001
#define ECRYPTFS_POLICY_APPLIED     0x00000002
#define ECRYPTFS_NEW_FILE           0x00000004
#define ECRYPTFS_ENCRYPTED          0x00000008
#define ECRYPTFS_SECURITY_WARNING   0x00000010
#define ECRYPTFS_ENABLE_HMAC        0x00000020
#define ECRYPTFS_ENCRYPT_IV_PAGES   0x00000040
#define ECRYPTFS_KEY_VALID          0x00000080
	u32 flags;
	unsigned int file_version;
	size_t iv_bytes;
	size_t num_keysigs;
	size_t header_extent_size;
	size_t num_header_extents_at_front;
	size_t extent_size; /* Data extent size; default is 4096 */
	size_t key_size;
	size_t extent_shift;
	unsigned int extent_mask;
	struct ecryptfs_mount_crypt_stat *mount_crypt_stat;
	struct crypto_blkcipher *tfm;
	struct crypto_hash *hash_tfm; /* Crypto context for generating
				       * the initialization vectors */
	unsigned char cipher[ECRYPTFS_MAX_CIPHER_NAME_SIZE];
	unsigned char key[ECRYPTFS_MAX_KEY_BYTES];
	unsigned char root_iv[ECRYPTFS_MAX_IV_BYTES];
	unsigned char keysigs[ECRYPTFS_MAX_NUM_KEYSIGS][ECRYPTFS_SIG_SIZE_HEX];
	struct mutex cs_tfm_mutex;
	struct mutex cs_hash_tfm_mutex;
	struct mutex cs_mutex;
};

/* inode private data. */
struct ecryptfs_inode_info {
	struct inode vfs_inode;
	struct inode *wii_inode;
	struct ecryptfs_crypt_stat crypt_stat;
};

/* dentry private data. Each dentry must keep track of a lower
 * vfsmount too. */
struct ecryptfs_dentry_info {
	struct dentry *wdi_dentry;
	struct vfsmount *lower_mnt;
	struct ecryptfs_crypt_stat *crypt_stat;
};

/**
 * This struct is to enable a mount-wide passphrase/salt combo. This
 * is more or less a stopgap to provide similar functionality to other
 * crypto filesystems like EncFS or CFS until full policy support is
 * implemented in eCryptfs.
 */
struct ecryptfs_mount_crypt_stat {
	/* Pointers to memory we do not own, do not free these */
#define ECRYPTFS_PLAINTEXT_PASSTHROUGH_ENABLED 0x00000001
	u32 flags;
	struct ecryptfs_auth_tok *global_auth_tok;
	struct key *global_auth_tok_key;
	size_t global_default_cipher_key_size;
	struct crypto_blkcipher *global_key_tfm;
	struct mutex global_key_tfm_mutex;
	unsigned char global_default_cipher_name[ECRYPTFS_MAX_CIPHER_NAME_SIZE
						 + 1];
	unsigned char global_auth_tok_sig[ECRYPTFS_SIG_SIZE_HEX + 1];
};

/* superblock private data. */
struct ecryptfs_sb_info {
	struct super_block *wsi_sb;
	struct ecryptfs_mount_crypt_stat mount_crypt_stat;
};

/* file private data. */
struct ecryptfs_file_info {
	struct file *wfi_file;
	struct ecryptfs_crypt_stat *crypt_stat;
};

/* auth_tok <=> encrypted_session_key mappings */
struct ecryptfs_auth_tok_list_item {
	unsigned char encrypted_session_key[ECRYPTFS_MAX_KEY_BYTES];
	struct list_head list;
	struct ecryptfs_auth_tok auth_tok;
};

static inline struct ecryptfs_file_info *
ecryptfs_file_to_private(struct file *file)
{
	return (struct ecryptfs_file_info *)file->private_data;
}

static inline void
ecryptfs_set_file_private(struct file *file,
			  struct ecryptfs_file_info *file_info)
{
	file->private_data = file_info;
}

static inline struct file *ecryptfs_file_to_lower(struct file *file)
{
	return ((struct ecryptfs_file_info *)file->private_data)->wfi_file;
}

static inline void
ecryptfs_set_file_lower(struct file *file, struct file *lower_file)
{
	((struct ecryptfs_file_info *)file->private_data)->wfi_file =
		lower_file;
}

static inline struct ecryptfs_inode_info *
ecryptfs_inode_to_private(struct inode *inode)
{
	return container_of(inode, struct ecryptfs_inode_info, vfs_inode);
}

static inline struct inode *ecryptfs_inode_to_lower(struct inode *inode)
{
	return ecryptfs_inode_to_private(inode)->wii_inode;
}

static inline void
ecryptfs_set_inode_lower(struct inode *inode, struct inode *lower_inode)
{
	ecryptfs_inode_to_private(inode)->wii_inode = lower_inode;
}

static inline struct ecryptfs_sb_info *
ecryptfs_superblock_to_private(struct super_block *sb)
{
	return (struct ecryptfs_sb_info *)sb->s_fs_info;
}

static inline void
ecryptfs_set_superblock_private(struct super_block *sb,
				struct ecryptfs_sb_info *sb_info)
{
	sb->s_fs_info = sb_info;
}

static inline struct super_block *
ecryptfs_superblock_to_lower(struct super_block *sb)
{
	return ((struct ecryptfs_sb_info *)sb->s_fs_info)->wsi_sb;
}

static inline void
ecryptfs_set_superblock_lower(struct super_block *sb,
			      struct super_block *lower_sb)
{
	((struct ecryptfs_sb_info *)sb->s_fs_info)->wsi_sb = lower_sb;
}

static inline struct ecryptfs_dentry_info *
ecryptfs_dentry_to_private(struct dentry *dentry)
{
	return (struct ecryptfs_dentry_info *)dentry->d_fsdata;
}

static inline void
ecryptfs_set_dentry_private(struct dentry *dentry,
			    struct ecryptfs_dentry_info *dentry_info)
{
	dentry->d_fsdata = dentry_info;
}

static inline struct dentry *
ecryptfs_dentry_to_lower(struct dentry *dentry)
{
	return ((struct ecryptfs_dentry_info *)dentry->d_fsdata)->wdi_dentry;
}

static inline void
ecryptfs_set_dentry_lower(struct dentry *dentry, struct dentry *lower_dentry)
{
	((struct ecryptfs_dentry_info *)dentry->d_fsdata)->wdi_dentry =
		lower_dentry;
}

static inline struct vfsmount *
ecryptfs_dentry_to_lower_mnt(struct dentry *dentry)
{
	return ((struct ecryptfs_dentry_info *)dentry->d_fsdata)->lower_mnt;
}

static inline void
ecryptfs_set_dentry_lower_mnt(struct dentry *dentry, struct vfsmount *lower_mnt)
{
	((struct ecryptfs_dentry_info *)dentry->d_fsdata)->lower_mnt =
		lower_mnt;
}

#define ecryptfs_printk(type, fmt, arg...) \
        __ecryptfs_printk(type "%s: " fmt, __FUNCTION__, ## arg);
void __ecryptfs_printk(const char *fmt, ...);

extern const struct file_operations ecryptfs_main_fops;
extern const struct file_operations ecryptfs_dir_fops;
extern struct inode_operations ecryptfs_main_iops;
extern struct inode_operations ecryptfs_dir_iops;
extern struct inode_operations ecryptfs_symlink_iops;
extern struct super_operations ecryptfs_sops;
extern struct dentry_operations ecryptfs_dops;
extern struct address_space_operations ecryptfs_aops;
extern int ecryptfs_verbosity;

extern struct kmem_cache *ecryptfs_auth_tok_list_item_cache;
extern struct kmem_cache *ecryptfs_file_info_cache;
extern struct kmem_cache *ecryptfs_dentry_info_cache;
extern struct kmem_cache *ecryptfs_inode_info_cache;
extern struct kmem_cache *ecryptfs_sb_info_cache;
extern struct kmem_cache *ecryptfs_header_cache_0;
extern struct kmem_cache *ecryptfs_header_cache_1;
extern struct kmem_cache *ecryptfs_header_cache_2;
extern struct kmem_cache *ecryptfs_lower_page_cache;

int ecryptfs_interpose(struct dentry *hidden_dentry,
		       struct dentry *this_dentry, struct super_block *sb,
		       int flag);
int ecryptfs_fill_zeros(struct file *file, loff_t new_length);
int ecryptfs_decode_filename(struct ecryptfs_crypt_stat *crypt_stat,
			     const char *name, int length,
			     char **decrypted_name);
int ecryptfs_encode_filename(struct ecryptfs_crypt_stat *crypt_stat,
			     const char *name, int length,
			     char **encoded_name);
struct dentry *ecryptfs_lower_dentry(struct dentry *this_dentry);
void ecryptfs_dump_hex(char *data, int bytes);
int virt_to_scatterlist(const void *addr, int size, struct scatterlist *sg,
			int sg_size);
int ecryptfs_compute_root_iv(struct ecryptfs_crypt_stat *crypt_stat);
void ecryptfs_rotate_iv(unsigned char *iv);
void ecryptfs_init_crypt_stat(struct ecryptfs_crypt_stat *crypt_stat);
void ecryptfs_destruct_crypt_stat(struct ecryptfs_crypt_stat *crypt_stat);
void ecryptfs_destruct_mount_crypt_stat(
	struct ecryptfs_mount_crypt_stat *mount_crypt_stat);
int ecryptfs_init_crypt_ctx(struct ecryptfs_crypt_stat *crypt_stat);
int ecryptfs_crypto_api_algify_cipher_name(char **algified_name,
					   char *cipher_name,
					   char *chaining_modifier);
int ecryptfs_write_inode_size_to_header(struct file *lower_file,
					struct inode *lower_inode,
					struct inode *inode);
int ecryptfs_get_lower_page(struct page **lower_page, struct inode *lower_inode,
			    struct file *lower_file,
			    unsigned long lower_page_index, int byte_offset,
			    int region_bytes);
int
ecryptfs_commit_lower_page(struct page *lower_page, struct inode *lower_inode,
			   struct file *lower_file, int byte_offset,
			   int region_size);
int ecryptfs_copy_page_to_lower(struct page *page, struct inode *lower_inode,
				struct file *lower_file);
int ecryptfs_do_readpage(struct file *file, struct page *page,
			 pgoff_t lower_page_index);
int ecryptfs_grab_and_map_lower_page(struct page **lower_page,
				     char **lower_virt,
				     struct inode *lower_inode,
				     unsigned long lower_page_index);
int ecryptfs_writepage_and_release_lower_page(struct page *lower_page,
					      struct inode *lower_inode,
					      struct writeback_control *wbc);
int ecryptfs_encrypt_page(struct ecryptfs_page_crypt_context *ctx);
int ecryptfs_decrypt_page(struct file *file, struct page *page);
int ecryptfs_write_headers(struct dentry *ecryptfs_dentry,
			   struct file *lower_file);
int ecryptfs_write_headers_virt(char *page_virt,
				struct ecryptfs_crypt_stat *crypt_stat,
				struct dentry *ecryptfs_dentry);
int ecryptfs_read_headers(struct dentry *ecryptfs_dentry,
			  struct file *lower_file);
int ecryptfs_new_file_context(struct dentry *ecryptfs_dentry);
int contains_ecryptfs_marker(char *data);
int ecryptfs_read_header_region(char *data, struct dentry *dentry,
				struct vfsmount *mnt);
u16 ecryptfs_code_for_cipher_string(struct ecryptfs_crypt_stat *crypt_stat);
int ecryptfs_cipher_code_to_string(char *str, u16 cipher_code);
void ecryptfs_set_default_sizes(struct ecryptfs_crypt_stat *crypt_stat);
int ecryptfs_generate_key_packet_set(char *dest_base,
				     struct ecryptfs_crypt_stat *crypt_stat,
				     struct dentry *ecryptfs_dentry,
				     size_t *len, size_t max);
int process_request_key_err(long err_code);
int
ecryptfs_parse_packet_set(struct ecryptfs_crypt_stat *crypt_stat,
			  unsigned char *src, struct dentry *ecryptfs_dentry);
int ecryptfs_truncate(struct dentry *dentry, loff_t new_length);
int
ecryptfs_process_cipher(struct crypto_blkcipher **key_tfm, char *cipher_name,
			size_t *key_size);
int ecryptfs_inode_test(struct inode *inode, void *candidate_lower_inode);
int ecryptfs_inode_set(struct inode *inode, void *lower_inode);
void ecryptfs_init_inode(struct inode *inode, struct inode *lower_inode);
int ecryptfs_open_lower_file(struct file **lower_file,
			     struct dentry *lower_dentry,
			     struct vfsmount *lower_mnt, int flags);
int ecryptfs_close_lower_file(struct file *lower_file);

#endif /* #ifndef ECRYPTFS_KERNEL_H */
