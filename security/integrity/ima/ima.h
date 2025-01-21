/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2005,2006,2007,2008 IBM Corporation
 *
 * Authors:
 * Reiner Sailer <sailer@watson.ibm.com>
 * Mimi Zohar <zohar@us.ibm.com>
 *
 * File: ima.h
 *	internal Integrity Measurement Architecture (IMA) definitions
 */

#ifndef __LINUX_IMA_H
#define __LINUX_IMA_H

#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/fs.h>
#include <linux/security.h>
#include <linux/hash.h>
#include <linux/tpm.h>
#include <linux/audit.h>
#include <crypto/hash_info.h>

#include "../integrity.h"

enum ima_show_type { IMA_SHOW_BINARY, IMA_SHOW_BINARY_NO_FIELD_LEN,
		     IMA_SHOW_BINARY_OLD_STRING_FMT, IMA_SHOW_ASCII };
enum tpm_pcrs { TPM_PCR0 = 0, TPM_PCR8 = 8, TPM_PCR10 = 10 };

/* digest size for IMA, fits SHA1 or MD5 */
#define IMA_DIGEST_SIZE		SHA1_DIGEST_SIZE
#define IMA_EVENT_NAME_LEN_MAX	255

#define IMA_HASH_BITS 10
#define IMA_MEASURE_HTABLE_SIZE (1 << IMA_HASH_BITS)

#define IMA_TEMPLATE_FIELD_ID_MAX_LEN	16
#define IMA_TEMPLATE_NUM_FIELDS_MAX	15

#define IMA_TEMPLATE_IMA_NAME "ima"
#define IMA_TEMPLATE_IMA_FMT "d|n"

#define NR_BANKS(chip) ((chip != NULL) ? chip->nr_allocated_banks : 0)

/* current content of the policy */
extern int ima_policy_flag;

/* bitset of digests algorithms allowed in the setxattr hook */
extern atomic_t ima_setxattr_allowed_hash_algorithms;

/* IMA hash algorithm description */
struct ima_algo_desc {
	struct crypto_shash *tfm;
	enum hash_algo algo;
};

/* set during initialization */
extern int ima_hash_algo __ro_after_init;
extern int ima_sha1_idx __ro_after_init;
extern int ima_hash_algo_idx __ro_after_init;
extern int ima_extra_slots __ro_after_init;
extern struct ima_algo_desc *ima_algo_array __ro_after_init;

extern int ima_appraise;
extern struct tpm_chip *ima_tpm_chip;
extern const char boot_aggregate_name[];

/* IMA event related data */
struct ima_event_data {
	struct ima_iint_cache *iint;
	struct file *file;
	const unsigned char *filename;
	struct evm_ima_xattr_data *xattr_value;
	int xattr_len;
	const struct modsig *modsig;
	const char *violation;
	const void *buf;
	int buf_len;
};

/* IMA template field data definition */
struct ima_field_data {
	u8 *data;
	u32 len;
};

/* IMA template field definition */
struct ima_template_field {
	const char field_id[IMA_TEMPLATE_FIELD_ID_MAX_LEN];
	int (*field_init)(struct ima_event_data *event_data,
			  struct ima_field_data *field_data);
	void (*field_show)(struct seq_file *m, enum ima_show_type show,
			   struct ima_field_data *field_data);
};

/* IMA template descriptor definition */
struct ima_template_desc {
	struct list_head list;
	char *name;
	char *fmt;
	int num_fields;
	const struct ima_template_field **fields;
};

struct ima_template_entry {
	int pcr;
	struct tpm_digest *digests;
	struct ima_template_desc *template_desc; /* template descriptor */
	u32 template_data_len;
	struct ima_field_data template_data[];	/* template related data */
};

struct ima_queue_entry {
	struct hlist_node hnext;	/* place in hash collision list */
	struct list_head later;		/* place in ima_measurements list */
	struct ima_template_entry *entry;
};
extern struct list_head ima_measurements;	/* list of all measurements */

/* Some details preceding the binary serialized measurement list */
struct ima_kexec_hdr {
	u16 version;
	u16 _reserved0;
	u32 _reserved1;
	u64 buffer_size;
	u64 count;
};

/* IMA iint action cache flags */
#define IMA_MEASURE		0x00000001
#define IMA_MEASURED		0x00000002
#define IMA_APPRAISE		0x00000004
#define IMA_APPRAISED		0x00000008
/*#define IMA_COLLECT		0x00000010  do not use this flag */
#define IMA_COLLECTED		0x00000020
#define IMA_AUDIT		0x00000040
#define IMA_AUDITED		0x00000080
#define IMA_HASH		0x00000100
#define IMA_HASHED		0x00000200

/* IMA iint policy rule cache flags */
#define IMA_NONACTION_FLAGS	0xff000000
#define IMA_DIGSIG_REQUIRED	0x01000000
#define IMA_PERMIT_DIRECTIO	0x02000000
#define IMA_NEW_FILE		0x04000000
#define IMA_FAIL_UNVERIFIABLE_SIGS	0x10000000
#define IMA_MODSIG_ALLOWED	0x20000000
#define IMA_CHECK_BLACKLIST	0x40000000
#define IMA_VERITY_REQUIRED	0x80000000

#define IMA_DO_MASK		(IMA_MEASURE | IMA_APPRAISE | IMA_AUDIT | \
				 IMA_HASH | IMA_APPRAISE_SUBMASK)
#define IMA_DONE_MASK		(IMA_MEASURED | IMA_APPRAISED | IMA_AUDITED | \
				 IMA_HASHED | IMA_COLLECTED | \
				 IMA_APPRAISED_SUBMASK)

/* IMA iint subaction appraise cache flags */
#define IMA_FILE_APPRAISE	0x00001000
#define IMA_FILE_APPRAISED	0x00002000
#define IMA_MMAP_APPRAISE	0x00004000
#define IMA_MMAP_APPRAISED	0x00008000
#define IMA_BPRM_APPRAISE	0x00010000
#define IMA_BPRM_APPRAISED	0x00020000
#define IMA_READ_APPRAISE	0x00040000
#define IMA_READ_APPRAISED	0x00080000
#define IMA_CREDS_APPRAISE	0x00100000
#define IMA_CREDS_APPRAISED	0x00200000
#define IMA_APPRAISE_SUBMASK	(IMA_FILE_APPRAISE | IMA_MMAP_APPRAISE | \
				 IMA_BPRM_APPRAISE | IMA_READ_APPRAISE | \
				 IMA_CREDS_APPRAISE)
#define IMA_APPRAISED_SUBMASK	(IMA_FILE_APPRAISED | IMA_MMAP_APPRAISED | \
				 IMA_BPRM_APPRAISED | IMA_READ_APPRAISED | \
				 IMA_CREDS_APPRAISED)

/* IMA iint cache atomic_flags */
#define IMA_CHANGE_XATTR	0
#define IMA_UPDATE_XATTR	1
#define IMA_CHANGE_ATTR		2
#define IMA_DIGSIG		3
#define IMA_MUST_MEASURE	4

/* IMA integrity metadata associated with an inode */
struct ima_iint_cache {
	struct mutex mutex;	/* protects: version, flags, digest */
	struct integrity_inode_attributes real_inode;
	unsigned long flags;
	unsigned long measured_pcrs;
	unsigned long atomic_flags;
	enum integrity_status ima_file_status:4;
	enum integrity_status ima_mmap_status:4;
	enum integrity_status ima_bprm_status:4;
	enum integrity_status ima_read_status:4;
	enum integrity_status ima_creds_status:4;
	struct ima_digest_data *ima_hash;
};

extern struct lsm_blob_sizes ima_blob_sizes;

static inline struct ima_iint_cache *
ima_inode_get_iint(const struct inode *inode)
{
	struct ima_iint_cache **iint_sec;

	if (unlikely(!inode->i_security))
		return NULL;

	iint_sec = inode->i_security + ima_blob_sizes.lbs_inode;
	return *iint_sec;
}

static inline void ima_inode_set_iint(const struct inode *inode,
				      struct ima_iint_cache *iint)
{
	struct ima_iint_cache **iint_sec;

	if (unlikely(!inode->i_security))
		return;

	iint_sec = inode->i_security + ima_blob_sizes.lbs_inode;
	*iint_sec = iint;
}

struct ima_iint_cache *ima_iint_find(struct inode *inode);
struct ima_iint_cache *ima_inode_get(struct inode *inode);
void ima_inode_free_rcu(void *inode_security);
void __init ima_iintcache_init(void);

extern const int read_idmap[];

#ifdef CONFIG_HAVE_IMA_KEXEC
void ima_load_kexec_buffer(void);
#else
static inline void ima_load_kexec_buffer(void) {}
#endif /* CONFIG_HAVE_IMA_KEXEC */

#ifdef CONFIG_IMA_MEASURE_ASYMMETRIC_KEYS
void ima_post_key_create_or_update(struct key *keyring, struct key *key,
				   const void *payload, size_t plen,
				   unsigned long flags, bool create);
#endif

/*
 * The default binary_runtime_measurements list format is defined as the
 * platform native format.  The canonical format is defined as little-endian.
 */
extern bool ima_canonical_fmt;

/* Internal IMA function definitions */
int ima_init(void);
int ima_fs_init(void);
int ima_add_template_entry(struct ima_template_entry *entry, int violation,
			   const char *op, struct inode *inode,
			   const unsigned char *filename);
int ima_calc_file_hash(struct file *file, struct ima_digest_data *hash);
int ima_calc_buffer_hash(const void *buf, loff_t len,
			 struct ima_digest_data *hash);
int ima_calc_field_array_hash(struct ima_field_data *field_data,
			      struct ima_template_entry *entry);
int ima_calc_boot_aggregate(struct ima_digest_data *hash);
void ima_add_violation(struct file *file, const unsigned char *filename,
		       struct ima_iint_cache *iint, const char *op,
		       const char *cause);
int ima_init_crypto(void);
void ima_putc(struct seq_file *m, void *data, int datalen);
void ima_print_digest(struct seq_file *m, u8 *digest, u32 size);
int template_desc_init_fields(const char *template_fmt,
			      const struct ima_template_field ***fields,
			      int *num_fields);
struct ima_template_desc *ima_template_desc_current(void);
struct ima_template_desc *ima_template_desc_buf(void);
struct ima_template_desc *lookup_template_desc(const char *name);
bool ima_template_has_modsig(const struct ima_template_desc *ima_template);
int ima_restore_measurement_entry(struct ima_template_entry *entry);
int ima_restore_measurement_list(loff_t bufsize, void *buf);
int ima_measurements_show(struct seq_file *m, void *v);
unsigned long ima_get_binary_runtime_size(void);
int ima_init_template(void);
void ima_init_template_list(void);
int __init ima_init_digests(void);
int ima_lsm_policy_change(struct notifier_block *nb, unsigned long event,
			  void *lsm_data);

/*
 * used to protect h_table and sha_table
 */
extern spinlock_t ima_queue_lock;

struct ima_h_table {
	atomic_long_t len;	/* number of stored measurements in the list */
	atomic_long_t violations;
	struct hlist_head queue[IMA_MEASURE_HTABLE_SIZE];
};
extern struct ima_h_table ima_htable;

static inline unsigned int ima_hash_key(u8 *digest)
{
	/* there is no point in taking a hash of part of a digest */
	return (digest[0] | digest[1] << 8) % IMA_MEASURE_HTABLE_SIZE;
}

#define __ima_hooks(hook)				\
	hook(NONE, none)				\
	hook(FILE_CHECK, file)				\
	hook(MMAP_CHECK, mmap)				\
	hook(MMAP_CHECK_REQPROT, mmap_reqprot)		\
	hook(BPRM_CHECK, bprm)				\
	hook(CREDS_CHECK, creds)			\
	hook(POST_SETATTR, post_setattr)		\
	hook(MODULE_CHECK, module)			\
	hook(FIRMWARE_CHECK, firmware)			\
	hook(KEXEC_KERNEL_CHECK, kexec_kernel)		\
	hook(KEXEC_INITRAMFS_CHECK, kexec_initramfs)	\
	hook(POLICY_CHECK, policy)			\
	hook(KEXEC_CMDLINE, kexec_cmdline)		\
	hook(KEY_CHECK, key)				\
	hook(CRITICAL_DATA, critical_data)		\
	hook(SETXATTR_CHECK, setxattr_check)		\
	hook(MAX_CHECK, none)

#define __ima_hook_enumify(ENUM, str)	ENUM,
#define __ima_stringify(arg) (#arg)
#define __ima_hook_measuring_stringify(ENUM, str) \
		(__ima_stringify(measuring_ ##str)),

enum ima_hooks {
	__ima_hooks(__ima_hook_enumify)
};

static const char * const ima_hooks_measure_str[] = {
	__ima_hooks(__ima_hook_measuring_stringify)
};

static inline const char *func_measure_str(enum ima_hooks func)
{
	if (func >= MAX_CHECK)
		return ima_hooks_measure_str[NONE];

	return ima_hooks_measure_str[func];
}

extern const char *const func_tokens[];

struct modsig;

#ifdef CONFIG_IMA_QUEUE_EARLY_BOOT_KEYS
/*
 * To track keys that need to be measured.
 */
struct ima_key_entry {
	struct list_head list;
	void *payload;
	size_t payload_len;
	char *keyring_name;
};
void ima_init_key_queue(void);
bool ima_should_queue_key(void);
bool ima_queue_key(struct key *keyring, const void *payload,
		   size_t payload_len);
void ima_process_queued_keys(void);
#else
static inline void ima_init_key_queue(void) {}
static inline bool ima_should_queue_key(void) { return false; }
static inline bool ima_queue_key(struct key *keyring,
				 const void *payload,
				 size_t payload_len) { return false; }
static inline void ima_process_queued_keys(void) {}
#endif /* CONFIG_IMA_QUEUE_EARLY_BOOT_KEYS */

/* LIM API function definitions */
int ima_get_action(struct mnt_idmap *idmap, struct inode *inode,
		   const struct cred *cred, struct lsm_prop *prop, int mask,
		   enum ima_hooks func, int *pcr,
		   struct ima_template_desc **template_desc,
		   const char *func_data, unsigned int *allowed_algos);
int ima_must_measure(struct inode *inode, int mask, enum ima_hooks func);
int ima_collect_measurement(struct ima_iint_cache *iint, struct file *file,
			    void *buf, loff_t size, enum hash_algo algo,
			    struct modsig *modsig);
void ima_store_measurement(struct ima_iint_cache *iint, struct file *file,
			   const unsigned char *filename,
			   struct evm_ima_xattr_data *xattr_value,
			   int xattr_len, const struct modsig *modsig, int pcr,
			   struct ima_template_desc *template_desc);
int process_buffer_measurement(struct mnt_idmap *idmap,
			       struct inode *inode, const void *buf, int size,
			       const char *eventname, enum ima_hooks func,
			       int pcr, const char *func_data,
			       bool buf_hash, u8 *digest, size_t digest_len);
void ima_audit_measurement(struct ima_iint_cache *iint,
			   const unsigned char *filename);
int ima_alloc_init_template(struct ima_event_data *event_data,
			    struct ima_template_entry **entry,
			    struct ima_template_desc *template_desc);
int ima_store_template(struct ima_template_entry *entry, int violation,
		       struct inode *inode,
		       const unsigned char *filename, int pcr);
void ima_free_template_entry(struct ima_template_entry *entry);
const char *ima_d_path(const struct path *path, char **pathbuf, char *filename);

/* IMA policy related functions */
int ima_match_policy(struct mnt_idmap *idmap, struct inode *inode,
		     const struct cred *cred, struct lsm_prop *prop,
		     enum ima_hooks func, int mask, int flags, int *pcr,
		     struct ima_template_desc **template_desc,
		     const char *func_data, unsigned int *allowed_algos);
void ima_init_policy(void);
void ima_update_policy(void);
void ima_update_policy_flags(void);
ssize_t ima_parse_add_rule(char *);
void ima_delete_rules(void);
int ima_check_policy(void);
void *ima_policy_start(struct seq_file *m, loff_t *pos);
void *ima_policy_next(struct seq_file *m, void *v, loff_t *pos);
void ima_policy_stop(struct seq_file *m, void *v);
int ima_policy_show(struct seq_file *m, void *v);

/* Appraise integrity measurements */
#define IMA_APPRAISE_ENFORCE	0x01
#define IMA_APPRAISE_FIX	0x02
#define IMA_APPRAISE_LOG	0x04
#define IMA_APPRAISE_MODULES	0x08
#define IMA_APPRAISE_FIRMWARE	0x10
#define IMA_APPRAISE_POLICY	0x20
#define IMA_APPRAISE_KEXEC	0x40

#ifdef CONFIG_IMA_APPRAISE
int ima_check_blacklist(struct ima_iint_cache *iint,
			const struct modsig *modsig, int pcr);
int ima_appraise_measurement(enum ima_hooks func, struct ima_iint_cache *iint,
			     struct file *file, const unsigned char *filename,
			     struct evm_ima_xattr_data *xattr_value,
			     int xattr_len, const struct modsig *modsig);
int ima_must_appraise(struct mnt_idmap *idmap, struct inode *inode,
		      int mask, enum ima_hooks func);
void ima_update_xattr(struct ima_iint_cache *iint, struct file *file);
enum integrity_status ima_get_cache_status(struct ima_iint_cache *iint,
					   enum ima_hooks func);
enum hash_algo ima_get_hash_algo(const struct evm_ima_xattr_data *xattr_value,
				 int xattr_len);
int ima_read_xattr(struct dentry *dentry,
		   struct evm_ima_xattr_data **xattr_value, int xattr_len);
void __init init_ima_appraise_lsm(const struct lsm_id *lsmid);

#else
static inline int ima_check_blacklist(struct ima_iint_cache *iint,
				      const struct modsig *modsig, int pcr)
{
	return 0;
}

static inline int ima_appraise_measurement(enum ima_hooks func,
					   struct ima_iint_cache *iint,
					   struct file *file,
					   const unsigned char *filename,
					   struct evm_ima_xattr_data *xattr_value,
					   int xattr_len,
					   const struct modsig *modsig)
{
	return INTEGRITY_UNKNOWN;
}

static inline int ima_must_appraise(struct mnt_idmap *idmap,
				    struct inode *inode, int mask,
				    enum ima_hooks func)
{
	return 0;
}

static inline void ima_update_xattr(struct ima_iint_cache *iint,
				    struct file *file)
{
}

static inline enum integrity_status
ima_get_cache_status(struct ima_iint_cache *iint, enum ima_hooks func)
{
	return INTEGRITY_UNKNOWN;
}

static inline enum hash_algo
ima_get_hash_algo(struct evm_ima_xattr_data *xattr_value, int xattr_len)
{
	return ima_hash_algo;
}

static inline int ima_read_xattr(struct dentry *dentry,
				 struct evm_ima_xattr_data **xattr_value,
				 int xattr_len)
{
	return 0;
}

static inline void __init init_ima_appraise_lsm(const struct lsm_id *lsmid)
{
}

#endif /* CONFIG_IMA_APPRAISE */

#ifdef CONFIG_IMA_APPRAISE_MODSIG
int ima_read_modsig(enum ima_hooks func, const void *buf, loff_t buf_len,
		    struct modsig **modsig);
void ima_collect_modsig(struct modsig *modsig, const void *buf, loff_t size);
int ima_get_modsig_digest(const struct modsig *modsig, enum hash_algo *algo,
			  const u8 **digest, u32 *digest_size);
int ima_get_raw_modsig(const struct modsig *modsig, const void **data,
		       u32 *data_len);
void ima_free_modsig(struct modsig *modsig);
#else
static inline int ima_read_modsig(enum ima_hooks func, const void *buf,
				  loff_t buf_len, struct modsig **modsig)
{
	return -EOPNOTSUPP;
}

static inline void ima_collect_modsig(struct modsig *modsig, const void *buf,
				      loff_t size)
{
}

static inline int ima_get_modsig_digest(const struct modsig *modsig,
					enum hash_algo *algo, const u8 **digest,
					u32 *digest_size)
{
	return -EOPNOTSUPP;
}

static inline int ima_get_raw_modsig(const struct modsig *modsig,
				     const void **data, u32 *data_len)
{
	return -EOPNOTSUPP;
}

static inline void ima_free_modsig(struct modsig *modsig)
{
}
#endif /* CONFIG_IMA_APPRAISE_MODSIG */

/* LSM based policy rules require audit */
#ifdef CONFIG_IMA_LSM_RULES

#define ima_filter_rule_init security_audit_rule_init
#define ima_filter_rule_free security_audit_rule_free
#define ima_filter_rule_match security_audit_rule_match

#else

static inline int ima_filter_rule_init(u32 field, u32 op, char *rulestr,
				       void **lsmrule, gfp_t gfp)
{
	return -EINVAL;
}

static inline void ima_filter_rule_free(void *lsmrule)
{
}

static inline int ima_filter_rule_match(struct lsm_prop *prop, u32 field, u32 op,
					void *lsmrule)
{
	return -EINVAL;
}
#endif /* CONFIG_IMA_LSM_RULES */

#ifdef	CONFIG_IMA_READ_POLICY
#define	POLICY_FILE_FLAGS	(S_IWUSR | S_IRUSR)
#else
#define	POLICY_FILE_FLAGS	S_IWUSR
#endif /* CONFIG_IMA_READ_POLICY */

#endif /* __LINUX_IMA_H */
