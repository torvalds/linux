/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TRUSTED_TPM_H
#define __TRUSTED_TPM_H

#include <keys/trusted-type.h>
#include <linux/tpm_command.h>

/* implementation specific TPM constants */
#define TPM_SIZE_OFFSET			2
#define TPM_RETURN_OFFSET		6
#define TPM_DATA_OFFSET			10

#define LOAD32(buffer, offset)	(ntohl(*(uint32_t *)&buffer[offset]))
#define LOAD32N(buffer, offset)	(*(uint32_t *)&buffer[offset])
#define LOAD16(buffer, offset)	(ntohs(*(uint16_t *)&buffer[offset]))

extern struct trusted_key_ops trusted_key_tpm_ops;

struct osapsess {
	uint32_t handle;
	unsigned char secret[SHA1_DIGEST_SIZE];
	unsigned char enonce[TPM_NONCE_SIZE];
};

/* discrete values, but have to store in uint16_t for TPM use */
enum {
	SEAL_keytype = 1,
	SRK_keytype = 4
};

int TSS_authhmac(unsigned char *digest, const unsigned char *key,
			unsigned int keylen, unsigned char *h1,
			unsigned char *h2, unsigned int h3, ...);
int TSS_checkhmac1(unsigned char *buffer,
			  const uint32_t command,
			  const unsigned char *ononce,
			  const unsigned char *key,
			  unsigned int keylen, ...);

int trusted_tpm_send(unsigned char *cmd, size_t buflen);
int oiap(struct tpm_buf *tb, uint32_t *handle, unsigned char *nonce);

int tpm2_seal_trusted(struct tpm_chip *chip,
		      struct trusted_key_payload *payload,
		      struct trusted_key_options *options);
int tpm2_unseal_trusted(struct tpm_chip *chip,
			struct trusted_key_payload *payload,
			struct trusted_key_options *options);

#define TPM_DEBUG 0

#if TPM_DEBUG
static inline void dump_options(struct trusted_key_options *o)
{
	pr_info("sealing key type %d\n", o->keytype);
	pr_info("sealing key handle %0X\n", o->keyhandle);
	pr_info("pcrlock %d\n", o->pcrlock);
	pr_info("pcrinfo %d\n", o->pcrinfo_len);
	print_hex_dump(KERN_INFO, "pcrinfo ", DUMP_PREFIX_NONE,
		       16, 1, o->pcrinfo, o->pcrinfo_len, 0);
}

static inline void dump_sess(struct osapsess *s)
{
	print_hex_dump(KERN_INFO, "trusted-key: handle ", DUMP_PREFIX_NONE,
		       16, 1, &s->handle, 4, 0);
	pr_info("secret:\n");
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_NONE,
		       16, 1, &s->secret, SHA1_DIGEST_SIZE, 0);
	pr_info("trusted-key: enonce:\n");
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_NONE,
		       16, 1, &s->enonce, SHA1_DIGEST_SIZE, 0);
}

static inline void dump_tpm_buf(unsigned char *buf)
{
	int len;

	pr_info("\ntpm buffer\n");
	len = LOAD32(buf, TPM_SIZE_OFFSET);
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_NONE, 16, 1, buf, len, 0);
}
#else
static inline void dump_options(struct trusted_key_options *o)
{
}

static inline void dump_sess(struct osapsess *s)
{
}

static inline void dump_tpm_buf(unsigned char *buf)
{
}
#endif
#endif
