/* $OpenBSD: softraid_crypto.c,v 1.146 2025/09/15 14:15:54 krw Exp $ */
/*
 * Copyright (c) 2007 Marco Peereboom <marco@peereboom.us>
 * Copyright (c) 2008 Hans-Joerg Hoexer <hshoexer@openbsd.org>
 * Copyright (c) 2008 Damien Miller <djm@mindrot.org>
 * Copyright (c) 2009 Joel Sing <jsing@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "bio.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/kernel.h>
#include <sys/disk.h>
#include <sys/rwlock.h>
#include <sys/queue.h>
#include <sys/fcntl.h>
#include <sys/disklabel.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/sensors.h>
#include <sys/stat.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/dkio.h>

#include <crypto/cryptodev.h>
#include <crypto/rijndael.h>
#include <crypto/md5.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <crypto/hmac.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>
#include <scsi/scsi_disk.h>

#include <dev/softraidvar.h>

struct sr_crypto_wu *sr_crypto_prepare(struct sr_workunit *,
		    struct sr_crypto *, int);
int		sr_crypto_decrypt(u_char *, u_char *, u_char *, size_t, int);
int		sr_crypto_encrypt(u_char *, u_char *, u_char *, size_t, int);
int		sr_crypto_decrypt_key(struct sr_discipline *,
		    struct sr_crypto *);
int		sr_crypto_change_maskkey(struct sr_discipline *,
		    struct sr_crypto *, struct sr_crypto_kdfinfo *,
		    struct sr_crypto_kdfinfo *);
int		sr_crypto_create(struct sr_discipline *,
		    struct bioc_createraid *, int, int64_t);
int		sr_crypto_meta_create(struct sr_discipline *,
		    struct sr_crypto *, struct bioc_createraid *);
int		sr_crypto_set_key(struct sr_discipline *, struct sr_crypto *,
		    struct bioc_createraid *, int, void *);
int		sr_crypto_assemble(struct sr_discipline *,
		    struct bioc_createraid *, int, void *);
void		sr_crypto_free_sessions(struct sr_discipline *,
		    struct sr_crypto *);
int		sr_crypto_alloc_resources_internal(struct sr_discipline *,
		    struct sr_crypto *);
int		sr_crypto_alloc_resources(struct sr_discipline *);
void		sr_crypto_free_resources_internal(struct sr_discipline *,
		    struct sr_crypto *);
void		sr_crypto_free_resources(struct sr_discipline *);
int		sr_crypto_ioctl_internal(struct sr_discipline *,
		    struct sr_crypto *, struct bioc_discipline *);
int		sr_crypto_ioctl(struct sr_discipline *,
		    struct bioc_discipline *);
int		sr_crypto_meta_opt_handler_internal(struct sr_discipline *,
		    struct sr_crypto *, struct sr_meta_opt_hdr *);
int		sr_crypto_meta_opt_handler(struct sr_discipline *,
		    struct sr_meta_opt_hdr *);
int		sr_crypto_rw(struct sr_workunit *);
int		sr_crypto_dev_rw(struct sr_workunit *, struct sr_crypto_wu *);
void		sr_crypto_done_internal(struct sr_workunit *,
		    struct sr_crypto *);
void		sr_crypto_done(struct sr_workunit *);
void		sr_crypto_calculate_check_hmac_sha1(u_int8_t *, int,
		   u_int8_t *, int, u_char *);
void		sr_crypto_hotplug(struct sr_discipline *, struct disk *, int);

#ifdef SR_DEBUG0
void		 sr_crypto_dumpkeys(struct sr_crypto *);
#endif

/* Discipline initialisation. */
void
sr_crypto_discipline_init(struct sr_discipline *sd)
{
	int i;

	/* Fill out discipline members. */
	sd->sd_wu_size = sizeof(struct sr_crypto_wu);
	sd->sd_type = SR_MD_CRYPTO;
	strlcpy(sd->sd_name, "CRYPTO", sizeof(sd->sd_name));
	sd->sd_capabilities = SR_CAP_SYSTEM_DISK | SR_CAP_AUTO_ASSEMBLE;
	sd->sd_max_wu = SR_CRYPTO_NOWU;

	for (i = 0; i < SR_CRYPTO_MAXKEYS; i++)
		sd->mds.mdd_crypto.scr_sid[i] = (u_int64_t)-1;

	/* Setup discipline specific function pointers. */
	sd->sd_alloc_resources = sr_crypto_alloc_resources;
	sd->sd_assemble = sr_crypto_assemble;
	sd->sd_create = sr_crypto_create;
	sd->sd_free_resources = sr_crypto_free_resources;
	sd->sd_ioctl_handler = sr_crypto_ioctl;
	sd->sd_meta_opt_handler = sr_crypto_meta_opt_handler;
	sd->sd_scsi_rw = sr_crypto_rw;
	sd->sd_scsi_done = sr_crypto_done;
}

int
sr_crypto_create(struct sr_discipline *sd, struct bioc_createraid *bc,
    int no_chunk, int64_t coerced_size)
{
	int rv = EINVAL;

	if (no_chunk != 1) {
		sr_error(sd->sd_sc, "%s requires exactly one chunk",
		    sd->sd_name);
		return (rv);
	}

	sd->sd_meta->ssdi.ssd_size = coerced_size;

	rv = sr_crypto_meta_create(sd, &sd->mds.mdd_crypto, bc);
	if (rv)
		return (rv);

	sd->sd_max_ccb_per_wu = no_chunk;
	return (0);
}

int
sr_crypto_meta_create(struct sr_discipline *sd, struct sr_crypto *mdd_crypto,
    struct bioc_createraid *bc)
{
	struct sr_meta_opt_item	*omi;
	int			rv = EINVAL;

	if (sd->sd_meta->ssdi.ssd_size > SR_CRYPTO_MAXSIZE) {
		sr_error(sd->sd_sc, "%s exceeds maximum size (%lli > %llu)",
		    sd->sd_name, sd->sd_meta->ssdi.ssd_size,
		    SR_CRYPTO_MAXSIZE);
		goto done;
	}

	/* Create crypto optional metadata. */
	omi = malloc(sizeof(struct sr_meta_opt_item), M_DEVBUF,
	    M_WAITOK | M_ZERO);
	omi->omi_som = malloc(sizeof(struct sr_meta_crypto), M_DEVBUF,
	    M_WAITOK | M_ZERO);
	omi->omi_som->som_type = SR_OPT_CRYPTO;
	omi->omi_som->som_length = sizeof(struct sr_meta_crypto);
	SLIST_INSERT_HEAD(&sd->sd_meta_opt, omi, omi_link);
	mdd_crypto->scr_meta = (struct sr_meta_crypto *)omi->omi_som;
	sd->sd_meta->ssdi.ssd_opt_no++;

	mdd_crypto->key_disk = NULL;

	if (bc->bc_key_disk != NODEV) {

		/* Create a key disk. */
		if (sr_crypto_get_kdf(bc, sd, mdd_crypto))
			goto done;
		mdd_crypto->key_disk =
		    sr_crypto_create_key_disk(sd, mdd_crypto, bc->bc_key_disk);
		if (mdd_crypto->key_disk == NULL)
			goto done;
		sd->sd_capabilities |= SR_CAP_AUTO_ASSEMBLE;

	} else if (bc->bc_opaque_flags & BIOC_SOOUT) {

		/* No hint available yet. */
		bc->bc_opaque_status = BIOC_SOINOUT_FAILED;
		rv = EAGAIN;
		goto done;

	} else if (sr_crypto_get_kdf(bc, sd, mdd_crypto))
		goto done;

	/* Passphrase volumes cannot be automatically assembled. */
	if (!(bc->bc_flags & BIOC_SCNOAUTOASSEMBLE) && bc->bc_key_disk == NODEV)
		goto done;

	sr_crypto_create_keys(sd, mdd_crypto);

	rv = 0;
done:
	return (rv);
}

int
sr_crypto_set_key(struct sr_discipline *sd, struct sr_crypto *mdd_crypto,
    struct bioc_createraid *bc, int no_chunk, void *data)
{
	int	rv = EINVAL;

	mdd_crypto->key_disk = NULL;

	/* Crypto optional metadata must already exist... */
	if (mdd_crypto->scr_meta == NULL)
		goto done;

	if (data != NULL) {
		/* Kernel already has mask key. */
		memcpy(mdd_crypto->scr_maskkey, data,
		    sizeof(mdd_crypto->scr_maskkey));
	} else if (bc->bc_key_disk != NODEV) {
		/* Read the mask key from the key disk. */
		mdd_crypto->key_disk =
		    sr_crypto_read_key_disk(sd, mdd_crypto, bc->bc_key_disk);
		if (mdd_crypto->key_disk == NULL)
			goto done;
	} else if (bc->bc_opaque_flags & BIOC_SOOUT) {
		/* provide userland with kdf hint */
		if (bc->bc_opaque == NULL)
			goto done;

		if (sizeof(mdd_crypto->scr_meta->scm_kdfhint) <
		    bc->bc_opaque_size)
			goto done;

		if (copyout(mdd_crypto->scr_meta->scm_kdfhint,
		    bc->bc_opaque, bc->bc_opaque_size))
			goto done;

		/* we're done */
		bc->bc_opaque_status = BIOC_SOINOUT_OK;
		rv = EAGAIN;
		goto done;
	} else if (bc->bc_opaque_flags & BIOC_SOIN) {
		/* get kdf with maskkey from userland */
		if (sr_crypto_get_kdf(bc, sd, mdd_crypto))
			goto done;
	} else
		goto done;


	rv = 0;
done:
	return (rv);
}

int
sr_crypto_assemble(struct sr_discipline *sd,
    struct bioc_createraid *bc, int no_chunk, void *data)
{
	int rv;

	rv = sr_crypto_set_key(sd, &sd->mds.mdd_crypto, bc, no_chunk, data);
	if (rv)
		return (rv);

	sd->sd_max_ccb_per_wu = sd->sd_meta->ssdi.ssd_chunk_no;
	return (0);
}

struct sr_crypto_wu *
sr_crypto_prepare(struct sr_workunit *wu, struct sr_crypto *mdd_crypto,
    int encrypt)
{
	struct scsi_xfer	*xs = wu->swu_xs;
	struct sr_crypto_wu	*crwu;
	struct cryptodesc	*crd;
	int			flags, i, n;
	daddr_t			blkno;
	u_int			keyndx;

	DNPRINTF(SR_D_DIS, "%s: sr_crypto_prepare wu %p encrypt %d\n",
	    DEVNAME(wu->swu_dis->sd_sc), wu, encrypt);

	crwu = (struct sr_crypto_wu *)wu;
	crwu->cr_uio.uio_iovcnt = 1;
	crwu->cr_uio.uio_iov->iov_len = xs->datalen;
	if (xs->flags & SCSI_DATA_OUT) {
		crwu->cr_uio.uio_iov->iov_base = crwu->cr_dmabuf;
		memcpy(crwu->cr_uio.uio_iov->iov_base, xs->data, xs->datalen);
	} else
		crwu->cr_uio.uio_iov->iov_base = xs->data;

	blkno = wu->swu_blk_start;
	n = xs->datalen >> DEV_BSHIFT;

	/*
	 * We preallocated enough crypto descs for up to MAXPHYS of I/O.
	 * Since there may be less than that we need to tweak the amount
	 * of crypto desc structures to be just long enough for our needs.
	 */
	KASSERT(crwu->cr_crp->crp_ndescalloc >= n);
	crwu->cr_crp->crp_ndesc = n;
	flags = (encrypt ? CRD_F_ENCRYPT : 0) |
	    CRD_F_IV_PRESENT | CRD_F_IV_EXPLICIT;

	/*
	 * Select crypto session based on block number.
	 *
	 * XXX - this does not handle the case where the read/write spans
	 * across a different key blocks (e.g. 0.5TB boundary). Currently
	 * this is already broken by the use of scr_key[0] below.
	 */
	keyndx = blkno >> SR_CRYPTO_KEY_BLKSHIFT;
	crwu->cr_crp->crp_sid = mdd_crypto->scr_sid[keyndx];

	crwu->cr_crp->crp_ilen = xs->datalen;
	crwu->cr_crp->crp_alloctype = M_DEVBUF;
	crwu->cr_crp->crp_flags = CRYPTO_F_IOV;
	crwu->cr_crp->crp_buf = &crwu->cr_uio;
	for (i = 0; i < crwu->cr_crp->crp_ndesc; i++, blkno++) {
		crd = &crwu->cr_crp->crp_desc[i];
		crd->crd_skip = i << DEV_BSHIFT;
		crd->crd_len = DEV_BSIZE;
		crd->crd_inject = 0;
		crd->crd_flags = flags;
		crd->crd_alg = mdd_crypto->scr_alg;
		crd->crd_klen = mdd_crypto->scr_klen;
		crd->crd_key = mdd_crypto->scr_key[0];
		memcpy(crd->crd_iv, &blkno, sizeof(blkno));
	}

	return (crwu);
}

int
sr_crypto_get_kdf(struct bioc_createraid *bc, struct sr_discipline *sd,
    struct sr_crypto *mdd_crypto)
{
	int			rv = EINVAL;
	struct sr_crypto_kdfinfo *kdfinfo;

	if (!(bc->bc_opaque_flags & BIOC_SOIN))
		return (rv);
	if (bc->bc_opaque == NULL)
		return (rv);
	if (bc->bc_opaque_size != sizeof(*kdfinfo))
		return (rv);

	kdfinfo = malloc(bc->bc_opaque_size, M_DEVBUF, M_WAITOK | M_ZERO);
	if (copyin(bc->bc_opaque, kdfinfo, bc->bc_opaque_size))
		goto out;

	if (kdfinfo->len != bc->bc_opaque_size)
		goto out;

	/* copy KDF hint to disk meta data */
	if (kdfinfo->flags & SR_CRYPTOKDF_HINT) {
		if (sizeof(mdd_crypto->scr_meta->scm_kdfhint) <
		    kdfinfo->genkdf.len)
			goto out;
		memcpy(mdd_crypto->scr_meta->scm_kdfhint,
		    &kdfinfo->genkdf, kdfinfo->genkdf.len);
	}

	/* copy mask key to run-time meta data */
	if ((kdfinfo->flags & SR_CRYPTOKDF_KEY)) {
		if (sizeof(mdd_crypto->scr_maskkey) < sizeof(kdfinfo->maskkey))
			goto out;
		memcpy(mdd_crypto->scr_maskkey, &kdfinfo->maskkey,
		    sizeof(kdfinfo->maskkey));
	}

	bc->bc_opaque_status = BIOC_SOINOUT_OK;
	rv = 0;
out:
	explicit_bzero(kdfinfo, bc->bc_opaque_size);
	free(kdfinfo, M_DEVBUF, bc->bc_opaque_size);

	return (rv);
}

int
sr_crypto_encrypt(u_char *p, u_char *c, u_char *key, size_t size, int alg)
{
	rijndael_ctx		ctx;
	int			i, rv = 1;

	switch (alg) {
	case SR_CRYPTOM_AES_ECB_256:
		if (rijndael_set_key_enc_only(&ctx, key, 256) != 0)
			goto out;
		for (i = 0; i < size; i += RIJNDAEL128_BLOCK_LEN)
			rijndael_encrypt(&ctx, &p[i], &c[i]);
		rv = 0;
		break;
	default:
		DNPRINTF(SR_D_DIS, "%s: unsupported encryption algorithm %d\n",
		    "softraid", alg);
		rv = -1;
		goto out;
	}

out:
	explicit_bzero(&ctx, sizeof(ctx));
	return (rv);
}

int
sr_crypto_decrypt(u_char *c, u_char *p, u_char *key, size_t size, int alg)
{
	rijndael_ctx		ctx;
	int			i, rv = 1;

	switch (alg) {
	case SR_CRYPTOM_AES_ECB_256:
		if (rijndael_set_key(&ctx, key, 256) != 0)
			goto out;
		for (i = 0; i < size; i += RIJNDAEL128_BLOCK_LEN)
			rijndael_decrypt(&ctx, &c[i], &p[i]);
		rv = 0;
		break;
	default:
		DNPRINTF(SR_D_DIS, "%s: unsupported encryption algorithm %d\n",
		    "softraid", alg);
		rv = -1;
		goto out;
	}

out:
	explicit_bzero(&ctx, sizeof(ctx));
	return (rv);
}

void
sr_crypto_calculate_check_hmac_sha1(u_int8_t *maskkey, int maskkey_size,
    u_int8_t *key, int key_size, u_char *check_digest)
{
	u_char			check_key[SHA1_DIGEST_LENGTH];
	HMAC_SHA1_CTX		hmacctx;
	SHA1_CTX		shactx;

	bzero(check_key, sizeof(check_key));
	bzero(&hmacctx, sizeof(hmacctx));
	bzero(&shactx, sizeof(shactx));

	/* k = SHA1(mask_key) */
	SHA1Init(&shactx);
	SHA1Update(&shactx, maskkey, maskkey_size);
	SHA1Final(check_key, &shactx);

	/* mac = HMAC_SHA1_k(unencrypted key) */
	HMAC_SHA1_Init(&hmacctx, check_key, sizeof(check_key));
	HMAC_SHA1_Update(&hmacctx, key, key_size);
	HMAC_SHA1_Final(check_digest, &hmacctx);

	explicit_bzero(check_key, sizeof(check_key));
	explicit_bzero(&hmacctx, sizeof(hmacctx));
	explicit_bzero(&shactx, sizeof(shactx));
}

int
sr_crypto_decrypt_key(struct sr_discipline *sd, struct sr_crypto *mdd_crypto)
{
	u_char			check_digest[SHA1_DIGEST_LENGTH];
	int			rv = 1;

	DNPRINTF(SR_D_DIS, "%s: sr_crypto_decrypt_key\n", DEVNAME(sd->sd_sc));

	if (mdd_crypto->scr_meta->scm_check_alg != SR_CRYPTOC_HMAC_SHA1)
		goto out;

	if (sr_crypto_decrypt((u_char *)mdd_crypto->scr_meta->scm_key,
	    (u_char *)mdd_crypto->scr_key,
	    mdd_crypto->scr_maskkey, sizeof(mdd_crypto->scr_key),
	    mdd_crypto->scr_meta->scm_mask_alg) == -1)
		goto out;

#ifdef SR_DEBUG0
	sr_crypto_dumpkeys(mdd_crypto);
#endif

	/* Check that the key decrypted properly. */
	sr_crypto_calculate_check_hmac_sha1(mdd_crypto->scr_maskkey,
	    sizeof(mdd_crypto->scr_maskkey), (u_int8_t *)mdd_crypto->scr_key,
	    sizeof(mdd_crypto->scr_key), check_digest);
	if (memcmp(mdd_crypto->scr_meta->chk_hmac_sha1.sch_mac,
	    check_digest, sizeof(check_digest)) != 0) {
		explicit_bzero(mdd_crypto->scr_key,
		    sizeof(mdd_crypto->scr_key));
		goto out;
	}

	rv = 0; /* Success */
out:
	/* we don't need the mask key anymore */
	explicit_bzero(&mdd_crypto->scr_maskkey,
	    sizeof(mdd_crypto->scr_maskkey));

	explicit_bzero(check_digest, sizeof(check_digest));

	return rv;
}

int
sr_crypto_create_keys(struct sr_discipline *sd, struct sr_crypto *mdd_crypto)
{

	DNPRINTF(SR_D_DIS, "%s: sr_crypto_create_keys\n",
	    DEVNAME(sd->sd_sc));

	if (AES_MAXKEYBYTES < sizeof(mdd_crypto->scr_maskkey))
		return (1);

	/* XXX allow user to specify */
	mdd_crypto->scr_meta->scm_alg = SR_CRYPTOA_AES_XTS_256;

	/* generate crypto keys */
	arc4random_buf(mdd_crypto->scr_key, sizeof(mdd_crypto->scr_key));

	/* Mask the disk keys. */
	mdd_crypto->scr_meta->scm_mask_alg = SR_CRYPTOM_AES_ECB_256;
	sr_crypto_encrypt((u_char *)mdd_crypto->scr_key,
	    (u_char *)mdd_crypto->scr_meta->scm_key,
	    mdd_crypto->scr_maskkey, sizeof(mdd_crypto->scr_key),
	    mdd_crypto->scr_meta->scm_mask_alg);

	/* Prepare key decryption check code. */
	mdd_crypto->scr_meta->scm_check_alg = SR_CRYPTOC_HMAC_SHA1;
	sr_crypto_calculate_check_hmac_sha1(mdd_crypto->scr_maskkey,
	    sizeof(mdd_crypto->scr_maskkey),
	    (u_int8_t *)mdd_crypto->scr_key, sizeof(mdd_crypto->scr_key),
	    mdd_crypto->scr_meta->chk_hmac_sha1.sch_mac);

	/* Erase the plaintext disk keys */
	explicit_bzero(mdd_crypto->scr_key, sizeof(mdd_crypto->scr_key));

#ifdef SR_DEBUG0
	sr_crypto_dumpkeys(mdd_crypto);
#endif

	mdd_crypto->scr_meta->scm_flags = SR_CRYPTOF_KEY | SR_CRYPTOF_KDFHINT;

	return (0);
}

int
sr_crypto_change_maskkey(struct sr_discipline *sd, struct sr_crypto *mdd_crypto,
  struct sr_crypto_kdfinfo *kdfinfo1, struct sr_crypto_kdfinfo *kdfinfo2)
{
	u_char			check_digest[SHA1_DIGEST_LENGTH];
	u_char			*c, *p = NULL;
	size_t			ksz;
	int			rv = 1;

	DNPRINTF(SR_D_DIS, "%s: sr_crypto_change_maskkey\n",
	    DEVNAME(sd->sd_sc));

	if (mdd_crypto->scr_meta->scm_check_alg != SR_CRYPTOC_HMAC_SHA1)
		goto out;

	c = (u_char *)mdd_crypto->scr_meta->scm_key;
	ksz = sizeof(mdd_crypto->scr_key);
	p = malloc(ksz, M_DEVBUF, M_WAITOK | M_CANFAIL | M_ZERO);
	if (p == NULL)
		goto out;

	if (sr_crypto_decrypt(c, p, kdfinfo1->maskkey, ksz,
	    mdd_crypto->scr_meta->scm_mask_alg) == -1)
		goto out;

#ifdef SR_DEBUG0
	sr_crypto_dumpkeys(mdd_crypto);
#endif

	sr_crypto_calculate_check_hmac_sha1(kdfinfo1->maskkey,
	    sizeof(kdfinfo1->maskkey), p, ksz, check_digest);
	if (memcmp(mdd_crypto->scr_meta->chk_hmac_sha1.sch_mac,
	    check_digest, sizeof(check_digest)) != 0) {
		sr_error(sd->sd_sc, "incorrect key or passphrase");
		rv = EPERM;
		goto out;
	}

	/* Copy new KDF hint to metadata, if supplied. */
	if (kdfinfo2->flags & SR_CRYPTOKDF_HINT) {
		if (kdfinfo2->genkdf.len >
		    sizeof(mdd_crypto->scr_meta->scm_kdfhint))
			goto out;
		explicit_bzero(mdd_crypto->scr_meta->scm_kdfhint,
		    sizeof(mdd_crypto->scr_meta->scm_kdfhint));
		memcpy(mdd_crypto->scr_meta->scm_kdfhint,
		    &kdfinfo2->genkdf, kdfinfo2->genkdf.len);
	}

	/* Mask the disk keys. */
	c = (u_char *)mdd_crypto->scr_meta->scm_key;
	if (sr_crypto_encrypt(p, c, kdfinfo2->maskkey, ksz,
	    mdd_crypto->scr_meta->scm_mask_alg) == -1)
		goto out;

	/* Prepare key decryption check code. */
	mdd_crypto->scr_meta->scm_check_alg = SR_CRYPTOC_HMAC_SHA1;
	sr_crypto_calculate_check_hmac_sha1(kdfinfo2->maskkey,
	    sizeof(kdfinfo2->maskkey), (u_int8_t *)mdd_crypto->scr_key,
	    sizeof(mdd_crypto->scr_key), check_digest);

	/* Copy new encrypted key and HMAC to metadata. */
	memcpy(mdd_crypto->scr_meta->chk_hmac_sha1.sch_mac, check_digest,
	    sizeof(mdd_crypto->scr_meta->chk_hmac_sha1.sch_mac));

	rv = 0; /* Success */

out:
	if (p) {
		explicit_bzero(p, ksz);
		free(p, M_DEVBUF, ksz);
	}

	explicit_bzero(check_digest, sizeof(check_digest));
	explicit_bzero(&kdfinfo1->maskkey, sizeof(kdfinfo1->maskkey));
	explicit_bzero(&kdfinfo2->maskkey, sizeof(kdfinfo2->maskkey));

	return (rv);
}

struct sr_chunk *
sr_crypto_create_key_disk(struct sr_discipline *sd,
    struct sr_crypto *mdd_crypto, dev_t dev)
{
	struct sr_softc		*sc = sd->sd_sc;
	struct sr_discipline	*fakesd = NULL;
	struct sr_metadata	*sm = NULL;
	struct sr_meta_chunk    *km;
	struct sr_meta_opt_item *omi = NULL;
	struct sr_meta_keydisk	*skm;
	struct sr_chunk		*key_disk = NULL;
	struct disklabel	*label = NULL;
	struct vnode		*vn;
	char			devname[32];
	int			c, part, open = 0;

	/*
	 * Create a metadata structure on the key disk and store
	 * keying material in the optional metadata.
	 */

	sr_meta_getdevname(sc, dev, devname, sizeof(devname));

	/* Make sure chunk is not already in use. */
	c = sr_chunk_in_use(sc, dev);
	if (c != BIOC_SDINVALID && c != BIOC_SDOFFLINE) {
		sr_error(sc, "%s is already in use", devname);
		goto done;
	}

	/* Open device. */
	if (bdevvp(dev, &vn)) {
		sr_error(sc, "cannot open key disk %s", devname);
		goto done;
	}
	if (VOP_OPEN(vn, FREAD | FWRITE, NOCRED, curproc)) {
		DNPRINTF(SR_D_META,"%s: sr_crypto_create_key_disk cannot "
		    "open %s\n", DEVNAME(sc), devname);
		vput(vn);
		goto done;
	}
	open = 1; /* close dev on error */

	/* Get partition details. */
	label = malloc(sizeof(*label), M_DEVBUF, M_WAITOK);
	part = DISKPART(dev);
	if (VOP_IOCTL(vn, DIOCGDINFO, (caddr_t)label,
	    FREAD, NOCRED, curproc)) {
		DNPRINTF(SR_D_META, "%s: sr_crypto_create_key_disk ioctl "
		    "failed\n", DEVNAME(sc));
		goto done;
	}
	if (label->d_partitions[part].p_fstype != FS_RAID) {
		sr_error(sc, "%s partition not of type RAID (%d)",
		    devname, label->d_partitions[part].p_fstype);
		goto done;
	}

	/*
	 * Create and populate chunk metadata.
	 */

	key_disk = malloc(sizeof(struct sr_chunk), M_DEVBUF, M_WAITOK | M_ZERO);
	km = &key_disk->src_meta;

	key_disk->src_dev_mm = dev;
	key_disk->src_vn = vn;
	strlcpy(key_disk->src_devname, devname, sizeof(km->scmi.scm_devname));
	key_disk->src_size = 0;

	km->scmi.scm_volid = sd->sd_meta->ssdi.ssd_level;
	km->scmi.scm_chunk_id = 0;
	km->scmi.scm_size = 0;
	km->scmi.scm_coerced_size = 0;
	strlcpy(km->scmi.scm_devname, devname, sizeof(km->scmi.scm_devname));
	memcpy(&km->scmi.scm_uuid, &sd->sd_meta->ssdi.ssd_uuid,
	    sizeof(struct sr_uuid));

	sr_checksum(sc, km, &km->scm_checksum,
	    sizeof(struct sr_meta_chunk_invariant));

	km->scm_status = BIOC_SDONLINE;

	/*
	 * Create and populate our own discipline and metadata.
	 */

	sm = malloc(sizeof(struct sr_metadata), M_DEVBUF, M_WAITOK | M_ZERO);
	sm->ssdi.ssd_magic = SR_MAGIC;
	sm->ssdi.ssd_version = SR_META_VERSION;
	sm->ssd_ondisk = 0;
	sm->ssdi.ssd_vol_flags = 0;
	memcpy(&sm->ssdi.ssd_uuid, &sd->sd_meta->ssdi.ssd_uuid,
	    sizeof(struct sr_uuid));
	sm->ssdi.ssd_chunk_no = 1;
	sm->ssdi.ssd_volid = SR_KEYDISK_VOLID;
	sm->ssdi.ssd_level = SR_KEYDISK_LEVEL;
	sm->ssdi.ssd_size = 0;
	strlcpy(sm->ssdi.ssd_vendor, "OPENBSD", sizeof(sm->ssdi.ssd_vendor));
	snprintf(sm->ssdi.ssd_product, sizeof(sm->ssdi.ssd_product),
	    "SR %s", "KEYDISK");
	snprintf(sm->ssdi.ssd_revision, sizeof(sm->ssdi.ssd_revision),
	    "%03d", SR_META_VERSION);

	fakesd = malloc(sizeof(struct sr_discipline), M_DEVBUF,
	    M_WAITOK | M_ZERO);
	fakesd->sd_sc = sd->sd_sc;
	fakesd->sd_meta = sm;
	fakesd->sd_meta_type = SR_META_F_NATIVE;
	fakesd->sd_vol_status = BIOC_SVONLINE;
	strlcpy(fakesd->sd_name, "KEYDISK", sizeof(fakesd->sd_name));
	SLIST_INIT(&fakesd->sd_meta_opt);

	/* Add chunk to volume. */
	fakesd->sd_vol.sv_chunks = malloc(sizeof(struct sr_chunk *), M_DEVBUF,
	    M_WAITOK | M_ZERO);
	fakesd->sd_vol.sv_chunks[0] = key_disk;
	SLIST_INIT(&fakesd->sd_vol.sv_chunk_list);
	SLIST_INSERT_HEAD(&fakesd->sd_vol.sv_chunk_list, key_disk, src_link);

	/* Generate mask key. */
	arc4random_buf(mdd_crypto->scr_maskkey,
	    sizeof(mdd_crypto->scr_maskkey));

	/* Copy mask key to optional metadata area. */
	omi = malloc(sizeof(struct sr_meta_opt_item), M_DEVBUF,
	    M_WAITOK | M_ZERO);
	omi->omi_som = malloc(sizeof(struct sr_meta_keydisk), M_DEVBUF,
	    M_WAITOK | M_ZERO);
	omi->omi_som->som_type = SR_OPT_KEYDISK;
	omi->omi_som->som_length = sizeof(struct sr_meta_keydisk);
	skm = (struct sr_meta_keydisk *)omi->omi_som;
	memcpy(&skm->skm_maskkey, mdd_crypto->scr_maskkey,
	    sizeof(skm->skm_maskkey));
	SLIST_INSERT_HEAD(&fakesd->sd_meta_opt, omi, omi_link);
	fakesd->sd_meta->ssdi.ssd_opt_no++;

	/* Save metadata. */
	if (sr_meta_save(fakesd, SR_META_DIRTY)) {
		sr_error(sc, "could not save metadata to %s", devname);
		goto fail;
	}

	goto done;

fail:
	free(key_disk, M_DEVBUF, sizeof(struct sr_chunk));
	key_disk = NULL;

done:
	free(label, M_DEVBUF, sizeof(*label));
	free(omi, M_DEVBUF, sizeof(struct sr_meta_opt_item));
	if (fakesd && fakesd->sd_vol.sv_chunks)
		free(fakesd->sd_vol.sv_chunks, M_DEVBUF,
		    sizeof(struct sr_chunk *));
	free(fakesd, M_DEVBUF, sizeof(struct sr_discipline));
	free(sm, M_DEVBUF, sizeof(struct sr_metadata));
	if (open) {
		VOP_CLOSE(vn, FREAD | FWRITE, NOCRED, curproc);
		vput(vn);
	}

	return key_disk;
}

struct sr_chunk *
sr_crypto_read_key_disk(struct sr_discipline *sd, struct sr_crypto *mdd_crypto,
    dev_t dev)
{
	struct sr_softc		*sc = sd->sd_sc;
	struct sr_metadata	*sm = NULL;
	struct sr_meta_opt_item *omi, *omi_next;
	struct sr_meta_opt_hdr	*omh;
	struct sr_meta_keydisk	*skm;
	struct sr_meta_opt_head som;
	struct sr_chunk		*key_disk = NULL;
	struct disklabel	*label = NULL;
	struct vnode		*vn = NULL;
	char			devname[32];
	int			c, part, open = 0;

	/*
	 * Load a key disk and load keying material into memory.
	 */

	SLIST_INIT(&som);

	sr_meta_getdevname(sc, dev, devname, sizeof(devname));

	/* Make sure chunk is not already in use. */
	c = sr_chunk_in_use(sc, dev);
	if (c != BIOC_SDINVALID && c != BIOC_SDOFFLINE) {
		sr_error(sc, "%s is already in use", devname);
		goto done;
	}

	/* Open device. */
	if (bdevvp(dev, &vn)) {
		sr_error(sc, "cannot open key disk %s", devname);
		goto done;
	}
	if (VOP_OPEN(vn, FREAD, NOCRED, curproc)) {
		DNPRINTF(SR_D_META,"%s: sr_crypto_read_key_disk cannot "
		    "open %s\n", DEVNAME(sc), devname);
		vput(vn);
		goto done;
	}
	open = 1; /* close dev on error */

	/* Get partition details. */
	label = malloc(sizeof(*label), M_DEVBUF, M_WAITOK);
	part = DISKPART(dev);
	if (VOP_IOCTL(vn, DIOCGDINFO, (caddr_t)label, FREAD,
	    NOCRED, curproc)) {
		DNPRINTF(SR_D_META, "%s: sr_crypto_read_key_disk ioctl "
		    "failed\n", DEVNAME(sc));
		goto done;
	}
	if (label->d_partitions[part].p_fstype != FS_RAID) {
		sr_error(sc, "%s partition not of type RAID (%d)",
		    devname, label->d_partitions[part].p_fstype);
		goto done;
	}

	/*
	 * Read and validate key disk metadata.
	 */
	sm = malloc(SR_META_SIZE * DEV_BSIZE, M_DEVBUF, M_WAITOK | M_ZERO);
	if (sr_meta_native_read(sd, dev, sm, NULL)) {
		sr_error(sc, "native bootprobe could not read native metadata");
		goto done;
	}

	if (sr_meta_validate(sd, dev, sm, NULL)) {
		DNPRINTF(SR_D_META, "%s: invalid metadata\n",
		    DEVNAME(sc));
		goto done;
	}

	/* Make sure this is a key disk. */
	if (sm->ssdi.ssd_level != SR_KEYDISK_LEVEL) {
		sr_error(sc, "%s is not a key disk", devname);
		goto done;
	}

	/* Construct key disk chunk. */
	key_disk = malloc(sizeof(struct sr_chunk), M_DEVBUF, M_WAITOK | M_ZERO);
	key_disk->src_dev_mm = dev;
	key_disk->src_vn = vn;
	key_disk->src_size = 0;

	memcpy(&key_disk->src_meta, (struct sr_meta_chunk *)(sm + 1),
	    sizeof(key_disk->src_meta));

	/* Read mask key from optional metadata. */
	sr_meta_opt_load(sc, sm, &som);
	SLIST_FOREACH(omi, &som, omi_link) {
		omh = omi->omi_som;
		if (omh->som_type == SR_OPT_KEYDISK) {
			skm = (struct sr_meta_keydisk *)omh;
			memcpy(mdd_crypto->scr_maskkey, &skm->skm_maskkey,
			    sizeof(mdd_crypto->scr_maskkey));
		} else if (omh->som_type == SR_OPT_CRYPTO) {
			/* Original keydisk format with key in crypto area. */
			memcpy(mdd_crypto->scr_maskkey,
			    omh + sizeof(struct sr_meta_opt_hdr),
			    sizeof(mdd_crypto->scr_maskkey));
		}
	}

	open = 0;

done:
	for (omi = SLIST_FIRST(&som); omi != NULL; omi = omi_next) {
		omi_next = SLIST_NEXT(omi, omi_link);
		free(omi->omi_som, M_DEVBUF, 0);
		free(omi, M_DEVBUF, sizeof(struct sr_meta_opt_item));
	}

	free(label, M_DEVBUF, sizeof(*label));
	free(sm, M_DEVBUF, SR_META_SIZE * DEV_BSIZE);

	if (vn && open) {
		VOP_CLOSE(vn, FREAD, NOCRED, curproc);
		vput(vn);
	}

	return key_disk;
}

void
sr_crypto_free_sessions(struct sr_discipline *sd, struct sr_crypto *mdd_crypto)
{
	u_int			i;

	for (i = 0; i < SR_CRYPTO_MAXKEYS; i++) {
		if (mdd_crypto->scr_sid[i] != (u_int64_t)-1) {
			crypto_freesession(mdd_crypto->scr_sid[i]);
			mdd_crypto->scr_sid[i] = (u_int64_t)-1;
		}
	}
}

int
sr_crypto_alloc_resources_internal(struct sr_discipline *sd,
    struct sr_crypto *mdd_crypto)
{
	struct sr_workunit	*wu;
	struct sr_crypto_wu	*crwu;
	struct cryptoini	cri;
	u_int			num_keys, i;

	DNPRINTF(SR_D_DIS, "%s: sr_crypto_alloc_resources\n",
	    DEVNAME(sd->sd_sc));

	mdd_crypto->scr_alg = CRYPTO_AES_XTS;
	switch (mdd_crypto->scr_meta->scm_alg) {
	case SR_CRYPTOA_AES_XTS_128:
		mdd_crypto->scr_klen = 256;
		break;
	case SR_CRYPTOA_AES_XTS_256:
		mdd_crypto->scr_klen = 512;
		break;
	default:
		sr_error(sd->sd_sc, "unknown crypto algorithm");
		return (EINVAL);
	}

	for (i = 0; i < SR_CRYPTO_MAXKEYS; i++)
		mdd_crypto->scr_sid[i] = (u_int64_t)-1;

	if (sr_wu_alloc(sd)) {
		sr_error(sd->sd_sc, "unable to allocate work units");
		return (ENOMEM);
	}
	if (sr_ccb_alloc(sd)) {
		sr_error(sd->sd_sc, "unable to allocate CCBs");
		return (ENOMEM);
	}
	if (sr_crypto_decrypt_key(sd, mdd_crypto)) {
		sr_error(sd->sd_sc, "incorrect key or passphrase");
		return (EPERM);
	}

	/*
	 * For each work unit allocate the uio, iovec and crypto structures.
	 * These have to be allocated now because during runtime we cannot
	 * fail an allocation without failing the I/O (which can cause real
	 * problems).
	 */
	TAILQ_FOREACH(wu, &sd->sd_wu, swu_next) {
		crwu = (struct sr_crypto_wu *)wu;
		crwu->cr_uio.uio_iov = &crwu->cr_iov;
		crwu->cr_dmabuf = dma_alloc(MAXPHYS, PR_WAITOK);
		crwu->cr_crp = crypto_getreq(MAXPHYS >> DEV_BSHIFT);
		if (crwu->cr_crp == NULL)
			return (ENOMEM);
	}

	memset(&cri, 0, sizeof(cri));
	cri.cri_alg = mdd_crypto->scr_alg;
	cri.cri_klen = mdd_crypto->scr_klen;

	/* Allocate a session for every 2^SR_CRYPTO_KEY_BLKSHIFT blocks. */
	num_keys = ((sd->sd_meta->ssdi.ssd_size - 1) >>
	    SR_CRYPTO_KEY_BLKSHIFT) + 1;
	if (num_keys > SR_CRYPTO_MAXKEYS)
		return (EFBIG);
	for (i = 0; i < num_keys; i++) {
		cri.cri_key = mdd_crypto->scr_key[i];
		if (crypto_newsession(&mdd_crypto->scr_sid[i],
		    &cri, 0) != 0) {
			sr_crypto_free_sessions(sd, mdd_crypto);
			return (EINVAL);
		}
	}

	sr_hotplug_register(sd, sr_crypto_hotplug);

	return (0);
}

int
sr_crypto_alloc_resources(struct sr_discipline *sd)
{
	return sr_crypto_alloc_resources_internal(sd, &sd->mds.mdd_crypto);
}

void
sr_crypto_free_resources_internal(struct sr_discipline *sd,
    struct sr_crypto *mdd_crypto)
{
	struct sr_workunit	*wu;
	struct sr_crypto_wu	*crwu;

	DNPRINTF(SR_D_DIS, "%s: sr_crypto_free_resources\n",
	    DEVNAME(sd->sd_sc));

	if (mdd_crypto->key_disk != NULL) {
		explicit_bzero(mdd_crypto->key_disk,
		    sizeof(*mdd_crypto->key_disk));
		free(mdd_crypto->key_disk, M_DEVBUF,
		    sizeof(*mdd_crypto->key_disk));
	}

	sr_hotplug_unregister(sd, sr_crypto_hotplug);

	sr_crypto_free_sessions(sd, mdd_crypto);

	TAILQ_FOREACH(wu, &sd->sd_wu, swu_next) {
		crwu = (struct sr_crypto_wu *)wu;
		if (crwu->cr_dmabuf)
			dma_free(crwu->cr_dmabuf, MAXPHYS);
		if (crwu->cr_crp)
			crypto_freereq(crwu->cr_crp);
	}

	sr_wu_free(sd);
	sr_ccb_free(sd);
}

void
sr_crypto_free_resources(struct sr_discipline *sd)
{
	struct sr_crypto *mdd_crypto = &sd->mds.mdd_crypto;
	sr_crypto_free_resources_internal(sd, mdd_crypto);
}

int
sr_crypto_ioctl_internal(struct sr_discipline *sd,
    struct sr_crypto *mdd_crypto, struct bioc_discipline *bd)
{
	struct sr_crypto_kdfpair kdfpair;
	struct sr_crypto_kdfinfo kdfinfo1, kdfinfo2;
	int			size, rv = 1;

	DNPRINTF(SR_D_IOCTL, "%s: sr_crypto_ioctl %u\n",
	    DEVNAME(sd->sd_sc), bd->bd_cmd);

	switch (bd->bd_cmd) {
	case SR_IOCTL_GET_KDFHINT:

		/* Get KDF hint for userland. */
		size = sizeof(mdd_crypto->scr_meta->scm_kdfhint);
		if (bd->bd_data == NULL || bd->bd_size > size)
			goto bad;
		if (copyout(mdd_crypto->scr_meta->scm_kdfhint,
		    bd->bd_data, bd->bd_size))
			goto bad;

		rv = 0;

		break;

	case SR_IOCTL_CHANGE_PASSPHRASE:

		/* Attempt to change passphrase. */

		size = sizeof(kdfpair);
		if (bd->bd_data == NULL || bd->bd_size > size)
			goto bad;
		if (copyin(bd->bd_data, &kdfpair, size))
			goto bad;

		size = sizeof(kdfinfo1);
		if (kdfpair.kdfinfo1 == NULL || kdfpair.kdfsize1 > size)
			goto bad;
		if (copyin(kdfpair.kdfinfo1, &kdfinfo1, size))
			goto bad;

		size = sizeof(kdfinfo2);
		if (kdfpair.kdfinfo2 == NULL || kdfpair.kdfsize2 > size)
			goto bad;
		if (copyin(kdfpair.kdfinfo2, &kdfinfo2, size))
			goto bad;

		if (sr_crypto_change_maskkey(sd, mdd_crypto, &kdfinfo1,
		    &kdfinfo2))
			goto bad;

		/* Save metadata to disk. */
		rv = sr_meta_save(sd, SR_META_DIRTY);

		break;
	}

bad:
	explicit_bzero(&kdfpair, sizeof(kdfpair));
	explicit_bzero(&kdfinfo1, sizeof(kdfinfo1));
	explicit_bzero(&kdfinfo2, sizeof(kdfinfo2));

	return (rv);
}

int
sr_crypto_ioctl(struct sr_discipline *sd, struct bioc_discipline *bd)
{
	struct sr_crypto *mdd_crypto = &sd->mds.mdd_crypto;
	return sr_crypto_ioctl_internal(sd, mdd_crypto, bd);
}

int
sr_crypto_meta_opt_handler_internal(struct sr_discipline *sd,
    struct sr_crypto *mdd_crypto, struct sr_meta_opt_hdr *om)
{
	int rv = EINVAL;

	if (om->som_type == SR_OPT_CRYPTO) {
		mdd_crypto->scr_meta = (struct sr_meta_crypto *)om;
		rv = 0;
	}

	return (rv);
}

int
sr_crypto_meta_opt_handler(struct sr_discipline *sd, struct sr_meta_opt_hdr *om)
{
	struct sr_crypto *mdd_crypto = &sd->mds.mdd_crypto;
	return sr_crypto_meta_opt_handler_internal(sd, mdd_crypto, om);
}

int
sr_crypto_rw(struct sr_workunit *wu)
{
	struct sr_crypto_wu	*crwu;
	struct sr_crypto	*mdd_crypto;
	daddr_t			blkno;
	int			rv, err;
	int			s;

	DNPRINTF(SR_D_DIS, "%s: sr_crypto_rw wu %p\n",
	    DEVNAME(wu->swu_dis->sd_sc), wu);

	if (sr_validate_io(wu, &blkno, "sr_crypto_rw"))
		return (1);

	if (wu->swu_xs->flags & SCSI_DATA_OUT) {
		mdd_crypto = &wu->swu_dis->mds.mdd_crypto;
		crwu = sr_crypto_prepare(wu, mdd_crypto, 1);
		rv = crypto_invoke(crwu->cr_crp);

		DNPRINTF(SR_D_INTR, "%s: sr_crypto_rw: wu %p xs: %p\n",
		    DEVNAME(wu->swu_dis->sd_sc), wu, wu->swu_xs);

		if (rv) {
			/* fail io */
			wu->swu_xs->error = XS_DRIVER_STUFFUP;
			s = splbio();
			sr_scsi_done(wu->swu_dis, wu->swu_xs);
			splx(s);
		}

		if ((err = sr_crypto_dev_rw(wu, crwu)) != 0)
			return err;
	} else
		rv = sr_crypto_dev_rw(wu, NULL);

	return (rv);
}

int
sr_crypto_dev_rw(struct sr_workunit *wu, struct sr_crypto_wu *crwu)
{
	struct sr_discipline	*sd = wu->swu_dis;
	struct scsi_xfer	*xs = wu->swu_xs;
	struct sr_ccb		*ccb;
	struct uio		*uio;
	daddr_t			blkno;

	blkno = wu->swu_blk_start;

	ccb = sr_ccb_rw(sd, 0, blkno, xs->datalen, xs->data, xs->flags, 0);
	if (!ccb) {
		/* should never happen but handle more gracefully */
		printf("%s: %s: too many ccbs queued\n",
		    DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname);
		goto bad;
	}
	if (!ISSET(xs->flags, SCSI_DATA_IN)) {
		uio = crwu->cr_crp->crp_buf;
		ccb->ccb_buf.b_data = uio->uio_iov->iov_base;
		ccb->ccb_opaque = crwu;
	}
	sr_wu_enqueue_ccb(wu, ccb);
	sr_schedule_wu(wu);

	return (0);

bad:
	return (EINVAL);
}

void
sr_crypto_done_internal(struct sr_workunit *wu, struct sr_crypto *mdd_crypto)
{
	struct scsi_xfer	*xs = wu->swu_xs;
	struct sr_crypto_wu	*crwu;
	int			rv;
	int			s;

	if (ISSET(wu->swu_flags, SR_WUF_REBUILD)) /* RAID 1C */
		return;

	/* If this was a successful read, initiate decryption of the data. */
	if (ISSET(xs->flags, SCSI_DATA_IN) && xs->error == XS_NOERROR) {
		crwu = sr_crypto_prepare(wu, mdd_crypto, 0);
		DNPRINTF(SR_D_INTR, "%s: sr_crypto_done: crypto_invoke %p\n",
		    DEVNAME(wu->swu_dis->sd_sc), crwu->cr_crp);
		rv = crypto_invoke(crwu->cr_crp);

		DNPRINTF(SR_D_INTR, "%s: sr_crypto_done: wu %p xs: %p\n",
		    DEVNAME(wu->swu_dis->sd_sc), wu, wu->swu_xs);

		if (rv)
			wu->swu_xs->error = XS_DRIVER_STUFFUP;

		s = splbio();
		sr_scsi_done(wu->swu_dis, wu->swu_xs);
		splx(s);
		return;
	}

	s = splbio();
	sr_scsi_done(wu->swu_dis, wu->swu_xs);
	splx(s);
}

void
sr_crypto_done(struct sr_workunit *wu)
{
	struct sr_crypto *mdd_crypto = &wu->swu_dis->mds.mdd_crypto;
	sr_crypto_done_internal(wu, mdd_crypto);
}

void
sr_crypto_hotplug(struct sr_discipline *sd, struct disk *diskp, int action)
{
	DNPRINTF(SR_D_MISC, "%s: sr_crypto_hotplug: %s %d\n",
	    DEVNAME(sd->sd_sc), diskp->dk_name, action);
}

#ifdef SR_DEBUG0
void
sr_crypto_dumpkeys(struct sr_crypto *mdd_crypto)
{
	int			i, j;

	printf("sr_crypto_dumpkeys:\n");
	for (i = 0; i < SR_CRYPTO_MAXKEYS; i++) {
		printf("\tscm_key[%d]: 0x", i);
		for (j = 0; j < SR_CRYPTO_KEYBYTES; j++) {
			printf("%02x", mdd_crypto->scr_meta->scm_key[i][j]);
		}
		printf("\n");
	}
	printf("sr_crypto_dumpkeys: runtime data keys:\n");
	for (i = 0; i < SR_CRYPTO_MAXKEYS; i++) {
		printf("\tscr_key[%d]: 0x", i);
		for (j = 0; j < SR_CRYPTO_KEYBYTES; j++) {
			printf("%02x", mdd_crypto->scr_key[i][j]);
		}
		printf("\n");
	}
}
#endif	/* SR_DEBUG */
