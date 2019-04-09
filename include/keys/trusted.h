/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TRUSTED_KEY_H
#define __TRUSTED_KEY_H

/* implementation specific TPM constants */
#define MAX_BUF_SIZE			1024
#define TPM_GETRANDOM_SIZE		14
#define TPM_OSAP_SIZE			36
#define TPM_OIAP_SIZE			10
#define TPM_SEAL_SIZE			87
#define TPM_UNSEAL_SIZE			104
#define TPM_SIZE_OFFSET			2
#define TPM_RETURN_OFFSET		6
#define TPM_DATA_OFFSET			10

#define LOAD32(buffer, offset)	(ntohl(*(uint32_t *)&buffer[offset]))
#define LOAD32N(buffer, offset)	(*(uint32_t *)&buffer[offset])
#define LOAD16(buffer, offset)	(ntohs(*(uint16_t *)&buffer[offset]))

struct tpm_buf {
	int len;
	unsigned char data[MAX_BUF_SIZE];
};

#define INIT_BUF(tb) (tb->len = 0)

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

#define TPM_DEBUG 0

#if TPM_DEBUG
static inline void dump_options(struct trusted_key_options *o)
{
	pr_info("trusted_key: sealing key type %d\n", o->keytype);
	pr_info("trusted_key: sealing key handle %0X\n", o->keyhandle);
	pr_info("trusted_key: pcrlock %d\n", o->pcrlock);
	pr_info("trusted_key: pcrinfo %d\n", o->pcrinfo_len);
	print_hex_dump(KERN_INFO, "pcrinfo ", DUMP_PREFIX_NONE,
		       16, 1, o->pcrinfo, o->pcrinfo_len, 0);
}

static inline void dump_payload(struct trusted_key_payload *p)
{
	pr_info("trusted_key: key_len %d\n", p->key_len);
	print_hex_dump(KERN_INFO, "key ", DUMP_PREFIX_NONE,
		       16, 1, p->key, p->key_len, 0);
	pr_info("trusted_key: bloblen %d\n", p->blob_len);
	print_hex_dump(KERN_INFO, "blob ", DUMP_PREFIX_NONE,
		       16, 1, p->blob, p->blob_len, 0);
	pr_info("trusted_key: migratable %d\n", p->migratable);
}

static inline void dump_sess(struct osapsess *s)
{
	print_hex_dump(KERN_INFO, "trusted-key: handle ", DUMP_PREFIX_NONE,
		       16, 1, &s->handle, 4, 0);
	pr_info("trusted-key: secret:\n");
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_NONE,
		       16, 1, &s->secret, SHA1_DIGEST_SIZE, 0);
	pr_info("trusted-key: enonce:\n");
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_NONE,
		       16, 1, &s->enonce, SHA1_DIGEST_SIZE, 0);
}

static inline void dump_tpm_buf(unsigned char *buf)
{
	int len;

	pr_info("\ntrusted-key: tpm buffer\n");
	len = LOAD32(buf, TPM_SIZE_OFFSET);
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_NONE, 16, 1, buf, len, 0);
}
#else
static inline void dump_options(struct trusted_key_options *o)
{
}

static inline void dump_payload(struct trusted_key_payload *p)
{
}

static inline void dump_sess(struct osapsess *s)
{
}

static inline void dump_tpm_buf(unsigned char *buf)
{
}
#endif

static inline void store8(struct tpm_buf *buf, const unsigned char value)
{
	buf->data[buf->len++] = value;
}

static inline void store16(struct tpm_buf *buf, const uint16_t value)
{
	*(uint16_t *) & buf->data[buf->len] = htons(value);
	buf->len += sizeof value;
}

static inline void store32(struct tpm_buf *buf, const uint32_t value)
{
	*(uint32_t *) & buf->data[buf->len] = htonl(value);
	buf->len += sizeof value;
}

static inline void storebytes(struct tpm_buf *buf, const unsigned char *in,
			      const int len)
{
	memcpy(buf->data + buf->len, in, len);
	buf->len += len;
}
#endif
