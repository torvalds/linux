/*-
 * Copyright (c) 2015 Allan Jude <allanjude@FreeBSD.org>
 * Copyright (c) 2005-2011 Pawel Jakub Dawidek <pawel@dawidek.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <stand.h>
#include <stdarg.h>
#include "geliboot.h"
#include "geliboot_internal.h"

struct known_dev {
	char			name[GELIDEV_NAMELEN];
	struct geli_dev 	*gdev;
	SLIST_ENTRY(known_dev)	entries;
};

SLIST_HEAD(known_dev_list, known_dev) known_devs_head = 
    SLIST_HEAD_INITIALIZER(known_devs_head);

static geli_ukey saved_keys[GELI_MAX_KEYS];
static unsigned int nsaved_keys = 0;

/*
 * Copy keys from local storage to the keybuf struct.
 * Destroy the local storage when finished.
 */
void
geli_export_key_buffer(struct keybuf *fkeybuf)
{
	unsigned int i;

	for (i = 0; i < nsaved_keys; i++) {
		fkeybuf->kb_ents[i].ke_type = KEYBUF_TYPE_GELI;
		memcpy(fkeybuf->kb_ents[i].ke_data, saved_keys[i],
		    G_ELI_USERKEYLEN);
	}
	fkeybuf->kb_nents = nsaved_keys;
	explicit_bzero(saved_keys, sizeof(saved_keys));
}

/*
 * Copy keys from a keybuf struct into local storage.
 * Zero out the keybuf.
 */
void
geli_import_key_buffer(struct keybuf *skeybuf)
{
	unsigned int i;

	for (i = 0; i < skeybuf->kb_nents && i < GELI_MAX_KEYS; i++) {
		memcpy(saved_keys[i], skeybuf->kb_ents[i].ke_data,
		    G_ELI_USERKEYLEN);
		explicit_bzero(skeybuf->kb_ents[i].ke_data,
		    G_ELI_USERKEYLEN);
		skeybuf->kb_ents[i].ke_type = KEYBUF_TYPE_NONE;
	}
	nsaved_keys = skeybuf->kb_nents;
	skeybuf->kb_nents = 0;
}

void
geli_add_key(geli_ukey key)
{

	/*
	 * If we run out of key space, the worst that will happen is
	 * it will ask the user for the password again.
	 */
	if (nsaved_keys < GELI_MAX_KEYS) {
		memcpy(saved_keys[nsaved_keys], key, G_ELI_USERKEYLEN);
		nsaved_keys++;
	}
}

static int
geli_findkey(struct geli_dev *gdev, u_char *mkey)
{
	u_int keynum;
	int i;

	if (gdev->keybuf_slot >= 0) {
		if (g_eli_mkey_decrypt_any(&gdev->md, saved_keys[gdev->keybuf_slot],
		    mkey, &keynum) == 0) {
			return (0);
		}
	}

	for (i = 0; i < nsaved_keys; i++) {
		if (g_eli_mkey_decrypt_any(&gdev->md, saved_keys[i], mkey,
		    &keynum) == 0) {
			gdev->keybuf_slot = i;
			return (0);
		}
	}

	return (1);
}

/*
 * Read the last sector of a drive or partition and see if it is GELI encrypted.
 */
struct geli_dev *
geli_taste(geli_readfunc readfunc, void *readpriv, daddr_t lastsector,
    const char *namefmt, ...)
{
	va_list args;
	struct g_eli_metadata md;
	struct known_dev *kdev;
	struct geli_dev *gdev;
	u_char *buf;
	char devname[GELIDEV_NAMELEN];
	int error;
	off_t alignsector;

	/*
	 * Format the name into a temp buffer and use that to search for an
	 * existing known_dev instance.  If not found, this has the side effect
	 * of initializing kdev to NULL.
	 */
	va_start(args, namefmt);
	vsnprintf(devname, sizeof(devname), namefmt, args);
	va_end(args);
	SLIST_FOREACH(kdev, &known_devs_head, entries) {
		if (strcmp(kdev->name, devname) == 0)
			return (kdev->gdev);
	}

        /* Determine whether the new device is geli-encrypted... */
	if ((buf = malloc(DEV_GELIBOOT_BSIZE)) == NULL)
		goto out;
	alignsector = rounddown2(lastsector * DEV_BSIZE, DEV_GELIBOOT_BSIZE);
	if (alignsector + DEV_GELIBOOT_BSIZE > ((lastsector + 1) * DEV_BSIZE)) {
		/* Don't read past the end of the disk */
		alignsector = (lastsector * DEV_BSIZE) + DEV_BSIZE -
		    DEV_GELIBOOT_BSIZE;
	}
	error = readfunc(NULL, readpriv, alignsector, buf, DEV_GELIBOOT_BSIZE);
	if (error != 0) {
		goto out;
	}

	/*
	 * We have a new known_device.  Whether it's geli-encrypted or not,
	 * record its existance so we can avoid doing IO to probe it next time.
	 */
	if ((kdev = malloc(sizeof(*kdev))) == NULL)
		goto out;
	strlcpy(kdev->name, devname, sizeof(kdev->name));
	kdev->gdev = NULL;
	SLIST_INSERT_HEAD(&known_devs_head, kdev, entries);

	/* Extract the last 4k sector of the disk. */
	error = eli_metadata_decode(buf, &md);
	if (error != 0) {
		/* Try the last 512 byte sector instead. */
		error = eli_metadata_decode(buf +
		    (DEV_GELIBOOT_BSIZE - DEV_BSIZE), &md);
		if (error != 0) {
			goto out;
		}
	}

	if (!(md.md_flags & G_ELI_FLAG_GELIBOOT)) {
		/* The GELIBOOT feature is not activated */
		goto out;
	}
	if ((md.md_flags & G_ELI_FLAG_ONETIME)) {
		/* Swap device, skip it. */
		goto out;
	}

	/*
	 * It's geli-encrypted, create a geli_dev for it and link it into the
	 * known_dev instance.
	 */
	gdev = malloc(sizeof(struct geli_dev));
	if (gdev == NULL)
		goto out;
	gdev->part_end = lastsector;
	gdev->keybuf_slot = -1;
	gdev->md = md;
	gdev->name = kdev->name;
	eli_metadata_softc(&gdev->sc, &md, DEV_BSIZE,
	    (lastsector + DEV_BSIZE) * DEV_BSIZE);
	kdev->gdev = gdev;
out:
	free(buf);
	if (kdev == NULL)
		return (NULL);
	return (kdev->gdev);
}

/*
 * Attempt to decrypt the device.  This will try existing keys first, then will
 * prompt for a passphrase if there are no existing keys that work.
 */
static int
geli_probe(struct geli_dev *gdev, const char *passphrase, u_char *mkeyp)
{
	u_char key[G_ELI_USERKEYLEN], mkey[G_ELI_DATAIVKEYLEN], *mkp;
	u_int keynum;
	struct hmac_ctx ctx;
	int error;

	if (mkeyp != NULL) {
		memcpy(&mkey, mkeyp, G_ELI_DATAIVKEYLEN);
		explicit_bzero(mkeyp, G_ELI_DATAIVKEYLEN);
		goto found_key;
	}

	if (geli_findkey(gdev, mkey) == 0) {
		goto found_key;
	}

	g_eli_crypto_hmac_init(&ctx, NULL, 0);
	/*
	 * Prepare Derived-Key from the user passphrase.
	 */
	if (gdev->md.md_iterations < 0) {
		/* XXX TODO: Support loading key files. */
		return (1);
	} else if (gdev->md.md_iterations == 0) {
		g_eli_crypto_hmac_update(&ctx, gdev->md.md_salt,
		    sizeof(gdev->md.md_salt));
		g_eli_crypto_hmac_update(&ctx, (const uint8_t *)passphrase,
		    strlen(passphrase));
	} else if (gdev->md.md_iterations > 0) {
		printf("Calculating GELI Decryption Key for %s %d"
		    " iterations...\n", gdev->name, gdev->md.md_iterations);
		u_char dkey[G_ELI_USERKEYLEN];

		pkcs5v2_genkey(dkey, sizeof(dkey), gdev->md.md_salt,
		    sizeof(gdev->md.md_salt), passphrase,
		    gdev->md.md_iterations);
		g_eli_crypto_hmac_update(&ctx, dkey, sizeof(dkey));
		explicit_bzero(dkey, sizeof(dkey));
	}

	g_eli_crypto_hmac_final(&ctx, key, 0);

	error = g_eli_mkey_decrypt_any(&gdev->md, key, mkey, &keynum);
	if (error == -1) {
		explicit_bzero(mkey, sizeof(mkey));
		explicit_bzero(key, sizeof(key));
		printf("Bad GELI key: bad password?\n");
		return (error);
	} else if (error != 0) {
		explicit_bzero(mkey, sizeof(mkey));
		explicit_bzero(key, sizeof(key));
		printf("Failed to decrypt GELI master key: %d\n", error);
		return (error);
	} else {
		/* Add key to keychain */
		geli_add_key(key);
		explicit_bzero(&key, sizeof(key));
	}

found_key:
	/* Store the keys */
	bcopy(mkey, gdev->sc.sc_mkey, sizeof(gdev->sc.sc_mkey));
	bcopy(mkey, gdev->sc.sc_ivkey, sizeof(gdev->sc.sc_ivkey));
	mkp = mkey + sizeof(gdev->sc.sc_ivkey);
	if ((gdev->sc.sc_flags & G_ELI_FLAG_AUTH) == 0) {
		bcopy(mkp, gdev->sc.sc_ekey, G_ELI_DATAKEYLEN);
	} else {
		/*
		 * The encryption key is: ekey = HMAC_SHA512(Data-Key, 0x10)
		 */
		g_eli_crypto_hmac(mkp, G_ELI_MAXKEYLEN, (const uint8_t *)"\x10", 1,
		    gdev->sc.sc_ekey, 0);
	}
	explicit_bzero(mkey, sizeof(mkey));

	/* Initialize the per-sector IV. */
	switch (gdev->sc.sc_ealgo) {
	case CRYPTO_AES_XTS:
		break;
	default:
		SHA256_Init(&gdev->sc.sc_ivctx);
		SHA256_Update(&gdev->sc.sc_ivctx, gdev->sc.sc_ivkey,
		    sizeof(gdev->sc.sc_ivkey));
		break;
	}

	return (0);
}

int
geli_read(struct geli_dev *gdev, off_t offset, u_char *buf, size_t bytes)
{
	u_char iv[G_ELI_IVKEYLEN];
	u_char *pbuf;
	int error;
	off_t dstoff;
	uint64_t keyno;
	size_t n, nsec, secsize;
	struct g_eli_key gkey;

	pbuf = buf;

	secsize = gdev->sc.sc_sectorsize;
	nsec = bytes / secsize;
	if (nsec == 0) {
		/*
		 * A read of less than the GELI sector size has been
		 * requested. The caller provided destination buffer may
		 * not be big enough to boost the read to a full sector,
		 * so just attempt to decrypt the truncated sector.
		 */
		secsize = bytes;
		nsec = 1;
	}

	for (n = 0, dstoff = offset; n < nsec; n++, dstoff += secsize) {

		g_eli_crypto_ivgen(&gdev->sc, dstoff, iv, G_ELI_IVKEYLEN);

		/* Get the key that corresponds to this offset. */
		keyno = (dstoff >> G_ELI_KEY_SHIFT) / secsize;
		g_eli_key_fill(&gdev->sc, &gkey, keyno);

		error = geliboot_crypt(gdev->sc.sc_ealgo, 0, pbuf, secsize,
		    gkey.gek_key, gdev->sc.sc_ekeylen, iv);

		if (error != 0) {
			explicit_bzero(&gkey, sizeof(gkey));
			printf("Failed to decrypt in geli_read()!");
			return (error);
		}
		pbuf += secsize;
	}
	explicit_bzero(&gkey, sizeof(gkey));
	return (0);
}

int
geli_havekey(struct geli_dev *gdev)
{
	u_char mkey[G_ELI_DATAIVKEYLEN];
	int err;

	err = ENOENT;
	if (geli_findkey(gdev, mkey) == 0) {
		if (geli_probe(gdev, NULL, mkey) == 0)
			err = 0;
		explicit_bzero(mkey, sizeof(mkey));
	}
	return (err);
}

int
geli_passphrase(struct geli_dev *gdev, char *pw)
{
	int i;

	/* TODO: Implement GELI keyfile(s) support */
	for (i = 0; i < 3; i++) {
		/* Try cached passphrase */
		if (i == 0 && pw[0] != '\0') {
			if (geli_probe(gdev, pw, NULL) == 0) {
				return (0);
			}
		}
		printf("GELI Passphrase for %s ", gdev->name);
		pwgets(pw, GELI_PW_MAXLEN,
		    (gdev->md.md_flags & G_ELI_FLAG_GELIDISPLAYPASS) == 0);
		printf("\n");
		if (geli_probe(gdev, pw, NULL) == 0) {
			return (0);
		}
	}

	return (1);
}
