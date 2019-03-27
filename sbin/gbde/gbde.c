/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Poul-Henning Kamp
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Poul-Henning Kamp
 * and NAI Labs, the Security Research Division of Network Associates, Inc.
 * under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the
 * DARPA CHATS research program.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 * XXX: Future stuff
 *
 * Replace the template file options (-i & -f) with command-line variables
 * "-v property=foo"
 *
 * Introduce -e, extra entropy source (XOR with /dev/random)
 *
 * Introduce -E, alternate entropy source (instead of /dev/random)
 *
 * Introduce -i take IV from keyboard or
 *
 * Introduce -I take IV from file/cmd
 *
 * Introduce -m/-M store encrypted+encoded masterkey in file
 *
 * Introduce -k/-K get pass-phrase part from file/cmd
 *
 * Introduce -d add more dest-devices to worklist.
 *
 * Add key-option: selfdestruct bit.
 *
 * New/changed verbs:
 *	"onetime"	attach with onetime nonstored locksector
 *	"key"/"unkey" to blast memory copy of key without orphaning
 *	"nuke" blow away everything attached, crash/halt/power-off if possible.
 *	"blast" destroy all copies of the masterkey
 *	"destroy" destroy one copy of the masterkey
 *	"backup"/"restore" of masterkey sectors.
 *
 * Make all verbs work on both attached/detached devices.
 *
 */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/mutex.h>
#include <md5.h>
#include <readpassphrase.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <paths.h>
#include <strings.h>
#include <stdlib.h>
#include <err.h>
#include <stdio.h>
#include <libutil.h>
#include <libgeom.h>
#include <sys/errno.h>
#include <sys/disk.h>
#include <sys/stat.h>
#include <crypto/rijndael/rijndael-api-fst.h>
#include <crypto/sha2/sha512.h>
#include <sys/param.h>
#include <sys/linker.h>

#define GBDEMOD "geom_bde"
#define KASSERT(foo, bar) do { if(!(foo)) { warn bar ; exit (1); } } while (0)

#include <geom/geom.h>
#include <geom/bde/g_bde.h>

extern const char template[];


#if 0
static void
g_hexdump(void *ptr, int length)
{
	int i, j, k;
	unsigned char *cp;

	cp = ptr;
	for (i = 0; i < length; i+= 16) {
		printf("%04x  ", i);
		for (j = 0; j < 16; j++) {
			k = i + j;
			if (k < length)
				printf(" %02x", cp[k]);
			else
				printf("   ");
		}
		printf("  |");
		for (j = 0; j < 16; j++) {
			k = i + j;
			if (k >= length)
				printf(" ");
			else if (cp[k] >= ' ' && cp[k] <= '~')
				printf("%c", cp[k]);
			else
				printf(".");
		}
		printf("|\n");
	}
}
#endif

static void __dead2
usage(void)
{

	(void)fprintf(stderr,
"usage: gbde attach destination [-k keyfile] [-l lockfile] [-p pass-phrase]\n"
"       gbde detach destination\n"
"       gbde init destination [-i] [-f filename] [-K new-keyfile]\n"
"            [-L new-lockfile] [-P new-pass-phrase]\n"
"       gbde setkey destination [-n key]\n"
"            [-k keyfile] [-l lockfile] [-p pass-phrase]\n"
"            [-K new-keyfile] [-L new-lockfile] [-P new-pass-phrase]\n"
"       gbde nuke destination [-n key]\n"
"            [-k keyfile] [-l lockfile] [-p pass-phrase]\n"
"       gbde destroy destination [-k keyfile] [-l lockfile] [-p pass-phrase]\n");
	exit(1);
}

void *
g_read_data(struct g_consumer *cp, off_t offset, off_t length, int *error)
{
	void *p;
	int fd, i;
	off_t o2;

	p = malloc(length);
	if (p == NULL)
		err(1, "malloc");
	fd = *(int *)cp;
	o2 = lseek(fd, offset, SEEK_SET);
	if (o2 != offset)
		err(1, "lseek");
	i = read(fd, p, length);
	if (i != length)
		err(1, "read");
	if (error != NULL)
		error = 0;
	return (p);
}

static void
random_bits(void *p, u_int len)
{
	arc4random_buf(p, len);
}

/* XXX: not nice */
static u_char sha2[SHA512_DIGEST_LENGTH];

static void
reset_passphrase(struct g_bde_softc *sc)
{

	memcpy(sc->sha2, sha2, SHA512_DIGEST_LENGTH);
}

static void
setup_passphrase(struct g_bde_softc *sc, int sure, const char *input,
    const char *keyfile)
{
	char buf1[BUFSIZ + SHA512_DIGEST_LENGTH];
	char buf2[BUFSIZ + SHA512_DIGEST_LENGTH];
	char *p;
	int kfd, klen, bpos = 0;

	if (keyfile != NULL) {
		/* Read up to BUFSIZ bytes from keyfile */
		kfd = open(keyfile, O_RDONLY, 0);
		if (kfd < 0)
			err(1, "%s", keyfile);
		klen = read(kfd, buf1, BUFSIZ);
		if (klen == -1)
			err(1, "%s", keyfile);
		close(kfd);

		/* Prepend the passphrase with the hash of the key read */
		g_bde_hash_pass(sc, buf1, klen);
		memcpy(buf1, sc->sha2, SHA512_DIGEST_LENGTH);
		memcpy(buf2, sc->sha2, SHA512_DIGEST_LENGTH);
		bpos = SHA512_DIGEST_LENGTH;
	}

	if (input != NULL) {
		if (strlen(input) >= BUFSIZ)
			errx(1, "Passphrase too long");
		strcpy(buf1 + bpos, input);

		g_bde_hash_pass(sc, buf1, strlen(buf1 + bpos) + bpos);
		memcpy(sha2, sc->sha2, SHA512_DIGEST_LENGTH);
		return;
	}
	for (;;) {
		p = readpassphrase(
		    sure ? "Enter new passphrase:" : "Enter passphrase: ",
		    buf1 + bpos, sizeof buf1 - bpos,
		    RPP_ECHO_OFF | RPP_REQUIRE_TTY);
		if (p == NULL)
			err(1, "readpassphrase");

		if (sure) {
			p = readpassphrase("Reenter new passphrase: ",
			    buf2 + bpos, sizeof buf2 - bpos,
			    RPP_ECHO_OFF | RPP_REQUIRE_TTY);
			if (p == NULL)
				err(1, "readpassphrase");

			if (strcmp(buf1 + bpos, buf2 + bpos)) {
				printf("They didn't match.\n");
				continue;
			}
		}
		if (strlen(buf1 + bpos) < 3) {
			printf("Too short passphrase.\n");
			continue;
		}
		break;
	}
	g_bde_hash_pass(sc, buf1, strlen(buf1 + bpos) + bpos);
	memcpy(sha2, sc->sha2, SHA512_DIGEST_LENGTH);
}

static void
encrypt_sector(void *d, int len, int klen, void *key)
{
	keyInstance ki;
	cipherInstance ci;
	int error;

	error = rijndael_cipherInit(&ci, MODE_CBC, NULL);
	if (error <= 0)
		errx(1, "rijndael_cipherInit=%d", error);
	error = rijndael_makeKey(&ki, DIR_ENCRYPT, klen, key);
	if (error <= 0)
		errx(1, "rijndael_makeKeY=%d", error);
	error = rijndael_blockEncrypt(&ci, &ki, d, len * 8, d);
	if (error <= 0)
		errx(1, "rijndael_blockEncrypt=%d", error);
}

static void
cmd_attach(const struct g_bde_softc *sc, const char *dest, const char *lfile)
{
	int ffd;
	u_char buf[16];
	struct gctl_req *r;
	const char *errstr;

	r = gctl_get_handle();
	gctl_ro_param(r, "verb", -1, "create geom");
	gctl_ro_param(r, "class", -1, "BDE");
	gctl_ro_param(r, "provider", -1, dest);
	gctl_ro_param(r, "pass", SHA512_DIGEST_LENGTH, sc->sha2);
	if (lfile != NULL) {
		ffd = open(lfile, O_RDONLY, 0);
		if (ffd < 0)
			err(1, "%s", lfile);
		read(ffd, buf, 16);
		gctl_ro_param(r, "key", 16, buf);
		close(ffd);
	}
	errstr = gctl_issue(r);
	if (errstr != NULL)
		errx(1, "Attach to %s failed: %s", dest, errstr);

	exit (0);
}

static void
cmd_detach(const char *dest)
{
	struct gctl_req *r;
	const char *errstr;
	char buf[BUFSIZ];

	r = gctl_get_handle();
	gctl_ro_param(r, "verb", -1, "destroy geom");
	gctl_ro_param(r, "class", -1, "BDE");
	sprintf(buf, "%s.bde", dest);
	gctl_ro_param(r, "geom", -1, buf);
	/* gctl_dump(r, stdout); */
	errstr = gctl_issue(r);
	if (errstr != NULL)
		errx(1, "Detach of %s failed: %s", dest, errstr);
	exit (0);
}

static void
cmd_open(struct g_bde_softc *sc, int dfd , const char *l_opt, u_int *nkey)
{
	int error;
	int ffd;
	u_char keyloc[16];
	u_int sectorsize;
	off_t mediasize;
	struct stat st;

	error = ioctl(dfd, DIOCGSECTORSIZE, &sectorsize);
	if (error)
		sectorsize = 512;
	error = ioctl(dfd, DIOCGMEDIASIZE, &mediasize);
	if (error) {
		error = fstat(dfd, &st);
		if (error == 0 && S_ISREG(st.st_mode))
			mediasize = st.st_size;
		else
			error = ENOENT;
	}
	if (error)
		mediasize = (off_t)-1;
	if (l_opt != NULL) {
		ffd = open(l_opt, O_RDONLY, 0);
		if (ffd < 0)
			err(1, "%s", l_opt);
		read(ffd, keyloc, sizeof keyloc);
		close(ffd);
	} else {
		memset(keyloc, 0, sizeof keyloc);
	}

	error = g_bde_decrypt_lock(sc, sc->sha2, keyloc, mediasize,
	    sectorsize, nkey);
	if (error == ENOENT)
		errx(1, "Lock was destroyed.");
	if (error == ESRCH)
		errx(1, "Lock was nuked.");
	if (error == ENOTDIR)
		errx(1, "Lock not found");
	if (error != 0)
		errx(1, "Error %d decrypting lock", error);
	if (nkey)
		printf("Opened with key %u\n", 1 + *nkey);
	return;
}

static void
cmd_nuke(struct g_bde_key *gl, int dfd , int key)
{
	int i;
	u_char *sbuf;
	off_t offset, offset2;

	sbuf = malloc(gl->sectorsize);
	memset(sbuf, 0, gl->sectorsize);
	offset = (gl->lsector[key] & ~(gl->sectorsize - 1));
	offset2 = lseek(dfd, offset, SEEK_SET);
	if (offset2 != offset)
		err(1, "lseek");
	i = write(dfd, sbuf, gl->sectorsize);
	free(sbuf);
	if (i != (int)gl->sectorsize)
		err(1, "write");
	printf("Nuked key %d\n", 1 + key);
}

static void
cmd_write(struct g_bde_key *gl, struct g_bde_softc *sc, int dfd , int key, const char *l_opt)
{
	int i, ffd;
	uint64_t off[2];
	u_char keyloc[16];
	u_char *sbuf, *q;
	off_t offset, offset2;

	sbuf = malloc(gl->sectorsize);
	/*
	 * Find the byte-offset in the lock sector where we will put the lock
	 * data structure.  We can put it any random place as long as the
	 * structure fits.
	 */
	for(;;) {
		random_bits(off, sizeof off);
		off[0] &= (gl->sectorsize - 1);
		if (off[0] + G_BDE_LOCKSIZE > gl->sectorsize)
			continue;
		break;
	}

	/* Add the sector offset in bytes */
	off[0] += (gl->lsector[key] & ~(gl->sectorsize - 1));
	gl->lsector[key] = off[0];

	i = g_bde_keyloc_encrypt(sc->sha2, off[0], off[1], keyloc);
	if (i)
		errx(1, "g_bde_keyloc_encrypt()");
	if (l_opt != NULL) {
		ffd = open(l_opt, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (ffd < 0)
			err(1, "%s", l_opt);
		write(ffd, keyloc, sizeof keyloc);
		close(ffd);
	} else if (gl->flags & GBDE_F_SECT0) {
		offset2 = lseek(dfd, 0, SEEK_SET);
		if (offset2 != 0)
			err(1, "lseek");
		i = read(dfd, sbuf, gl->sectorsize);
		if (i != (int)gl->sectorsize)
			err(1, "read");
		memcpy(sbuf + key * 16, keyloc, sizeof keyloc);
		offset2 = lseek(dfd, 0, SEEK_SET);
		if (offset2 != 0)
			err(1, "lseek");
		i = write(dfd, sbuf, gl->sectorsize);
		if (i != (int)gl->sectorsize)
			err(1, "write");
	} else {
		errx(1, "No -L option and no space in sector 0 for lockfile");
	}

	/* Allocate a sectorbuffer and fill it with random junk */
	if (sbuf == NULL)
		err(1, "malloc");
	random_bits(sbuf, gl->sectorsize);

	/* Fill random bits in the spare field */
	random_bits(gl->spare, sizeof(gl->spare));

	/* Encode the structure where we want it */
	q = sbuf + (off[0] % gl->sectorsize);
	i = g_bde_encode_lock(sc->sha2, gl, q);
	if (i < 0)
		errx(1, "programming error encoding lock");

	encrypt_sector(q, G_BDE_LOCKSIZE, 256, sc->sha2 + 16);
	offset = gl->lsector[key] & ~(gl->sectorsize - 1);
	offset2 = lseek(dfd, offset, SEEK_SET);
	if (offset2 != offset)
		err(1, "lseek");
	i = write(dfd, sbuf, gl->sectorsize);
	if (i != (int)gl->sectorsize)
		err(1, "write");
	free(sbuf);
#if 0
	printf("Wrote key %d at %jd\n", key, (intmax_t)offset);
	printf("s0 = %jd\n", (intmax_t)gl->sector0);
	printf("sN = %jd\n", (intmax_t)gl->sectorN);
	printf("l[0] = %jd\n", (intmax_t)gl->lsector[0]);
	printf("l[1] = %jd\n", (intmax_t)gl->lsector[1]);
	printf("l[2] = %jd\n", (intmax_t)gl->lsector[2]);
	printf("l[3] = %jd\n", (intmax_t)gl->lsector[3]);
	printf("k = %jd\n", (intmax_t)gl->keyoffset);
	printf("ss = %jd\n", (intmax_t)gl->sectorsize);
#endif
}

static void
cmd_destroy(struct g_bde_key *gl, int nkey)
{
	int i;

	bzero(&gl->sector0, sizeof gl->sector0);
	bzero(&gl->sectorN, sizeof gl->sectorN);
	bzero(&gl->keyoffset, sizeof gl->keyoffset);
	gl->flags &= GBDE_F_SECT0;
	bzero(gl->mkey, sizeof gl->mkey);
	for (i = 0; i < G_BDE_MAXKEYS; i++)
		if (i != nkey)
			gl->lsector[i] = ~0;
}

static int
sorthelp(const void *a, const void *b)
{
	const uint64_t *oa, *ob;

	oa = a;
	ob = b;
	if (*oa > *ob)
		return 1;
	if (*oa < *ob)
		return -1;
	return 0;
}

static void
cmd_init(struct g_bde_key *gl, int dfd, const char *f_opt, int i_opt, const char *l_opt)
{
	int i;
	u_char *buf;
	unsigned sector_size;
	uint64_t	first_sector;
	uint64_t	last_sector;
	uint64_t	total_sectors;
	off_t	off, off2;
	unsigned nkeys;
	const char *p;
	char *q, cbuf[BUFSIZ];
	unsigned u, u2;
	uint64_t o;
	properties	params;

	bzero(gl, sizeof *gl);
	if (f_opt != NULL) {
		i = open(f_opt, O_RDONLY);
		if (i < 0)
			err(1, "%s", f_opt);
		params = properties_read(i);
		close (i);
	} else if (i_opt) {
		/* XXX: Polish */
		asprintf(&q, "%stemp.XXXXXXXXXX", _PATH_TMP);
		if (q == NULL)
			err(1, "asprintf");
		i = mkstemp(q);
		if (i < 0)
			err(1, "%s", q);
		write(i, template, strlen(template));
		close (i);
		p = getenv("EDITOR");
		if (p == NULL)
			p = "vi";
		if (snprintf(cbuf, sizeof(cbuf), "%s %s\n", p, q) >=
		    (ssize_t)sizeof(cbuf)) {
			unlink(q);
			errx(1, "EDITOR is too long");
		}
		system(cbuf);
		i = open(q, O_RDONLY);
		if (i < 0)
			err(1, "%s", f_opt);
		params = properties_read(i);
		close (i);
		unlink(q);
		free(q);
	} else {
		/* XXX: Hack */
		i = open(_PATH_DEVNULL, O_RDONLY);
		if (i < 0)
			err(1, "%s", _PATH_DEVNULL);
		params = properties_read(i);
		close (i);
	}

	/* <sector_size> */
	p = property_find(params, "sector_size");
	i = ioctl(dfd, DIOCGSECTORSIZE, &u);
	if (p != NULL) {
		sector_size = strtoul(p, &q, 0);
		if (!*p || *q)
			errx(1, "sector_size not a proper number");
	} else if (i == 0) {
		sector_size = u;
	} else {
		errx(1, "Missing sector_size property");
	}
	if (sector_size & (sector_size - 1))
		errx(1, "sector_size not a power of 2");
	if (sector_size < 512)
		errx(1, "sector_size is smaller than 512");
	buf = malloc(sector_size);
	if (buf == NULL)
		err(1, "Failed to malloc sector buffer");
	gl->sectorsize = sector_size;

	i = ioctl(dfd, DIOCGMEDIASIZE, &off);
	if (i == 0) {
		first_sector = 0;
		total_sectors = off / sector_size;
		last_sector = total_sectors - 1;
	} else {
		first_sector = 0;
		last_sector = 0;
		total_sectors = 0;
	}

	/* <first_sector> */
	p = property_find(params, "first_sector");
	if (p != NULL) {
		first_sector = strtoul(p, &q, 0);
		if (!*p || *q)
			errx(1, "first_sector not a proper number");
	}

	/* <last_sector> */
	p = property_find(params, "last_sector");
	if (p != NULL) {
		last_sector = strtoul(p, &q, 0);
		if (!*p || *q)
			errx(1, "last_sector not a proper number");
		if (last_sector <= first_sector)
			errx(1, "last_sector not larger than first_sector");
		total_sectors = last_sector + 1;
	}

	/* <total_sectors> */
	p = property_find(params, "total_sectors");
	if (p != NULL) {
		total_sectors = strtoul(p, &q, 0);
		if (!*p || *q)
			errx(1, "total_sectors not a proper number");
		if (last_sector == 0)
			last_sector = first_sector + total_sectors - 1;
	}

	if (l_opt == NULL && first_sector != 0)
		errx(1, "No -L new-lockfile argument and first_sector != 0");
	else if (l_opt == NULL) {
		first_sector++;
		total_sectors--;
		gl->flags |= GBDE_F_SECT0;
	}
	gl->sector0 = first_sector * gl->sectorsize;

	if (total_sectors != (last_sector - first_sector) + 1)
		errx(1, "total_sectors disagree with first_sector and last_sector");
	if (total_sectors == 0)
		errx(1, "missing last_sector or total_sectors");

	gl->sectorN = (last_sector + 1) * gl->sectorsize;

	/* Find a random keyoffset */
	random_bits(&o, sizeof o);
	o %= (gl->sectorN - gl->sector0);
	o &= ~(gl->sectorsize - 1);
	gl->keyoffset = o;

	/* <number_of_keys> */
	p = property_find(params, "number_of_keys");
	if (p != NULL) {
		nkeys = strtoul(p, &q, 0);
		if (!*p || *q)
			errx(1, "number_of_keys not a proper number");
		if (nkeys < 1 || nkeys > G_BDE_MAXKEYS)
			errx(1, "number_of_keys out of range");
	} else {
		nkeys = 4;
	}
	for (u = 0; u < nkeys; u++) {
		for(;;) {
			do {
				random_bits(&o, sizeof o);
				o %= gl->sectorN;
				o &= ~(gl->sectorsize - 1);
			} while(o < gl->sector0);
			for (u2 = 0; u2 < u; u2++)
				if (o == gl->lsector[u2])
					break;
			if (u2 < u)
				continue;
			break;
		}
		gl->lsector[u] = o;
	}
	for (; u < G_BDE_MAXKEYS; u++) {
		do
			random_bits(&o, sizeof o);
		while (o < gl->sectorN);
		gl->lsector[u] = o;
	}
	qsort(gl->lsector, G_BDE_MAXKEYS, sizeof gl->lsector[0], sorthelp);

	/* Flush sector zero if we use it for lockfile data */
	if (gl->flags & GBDE_F_SECT0) {
		off2 = lseek(dfd, 0, SEEK_SET);
		if (off2 != 0)
			err(1, "lseek(2) to sector 0");
		random_bits(buf, sector_size);
		i = write(dfd, buf, sector_size);
		if (i != (int)sector_size)
			err(1, "write sector 0");
	}

	/* <random_flush> */
	p = property_find(params, "random_flush");
	if (p != NULL) {
		off = first_sector * sector_size;
		off2 = lseek(dfd, off, SEEK_SET);
		if (off2 != off)
			err(1, "lseek(2) to first_sector");
		off2 = last_sector * sector_size;
		while (off <= off2) {
			random_bits(buf, sector_size);
			i = write(dfd, buf, sector_size);
			if (i != (int)sector_size)
				err(1, "write to $device_name");
			off += sector_size;
		}
	}

	random_bits(gl->mkey, sizeof gl->mkey);
	random_bits(gl->salt, sizeof gl->salt);

	return;
}

static enum action {
	ACT_HUH,
	ACT_ATTACH, ACT_DETACH,
	ACT_INIT, ACT_SETKEY, ACT_DESTROY, ACT_NUKE
} action;

int
main(int argc, char **argv)
{
	const char *opts;
	const char *k_opt, *K_opt;
	const char *l_opt, *L_opt;
	const char *p_opt, *P_opt;
	const char *f_opt;
	char *dest;
	int i_opt, n_opt, ch, dfd, doopen;
	u_int nkey;
	int i;
	char *q, buf[BUFSIZ];
	struct g_bde_key *gl;
	struct g_bde_softc sc;

	if (argc < 3)
		usage();

	if (modfind("g_bde") < 0) {
		/* need to load the gbde module */
		if (kldload(GBDEMOD) < 0 || modfind("g_bde") < 0)
			err(1, GBDEMOD ": Kernel module not available");
	}
	doopen = 0;
	if (!strcmp(argv[1], "attach")) {
		action = ACT_ATTACH;
		opts = "k:l:p:";
	} else if (!strcmp(argv[1], "detach")) {
		action = ACT_DETACH;
		opts = "";
	} else if (!strcmp(argv[1], "init")) {
		action = ACT_INIT;
		doopen = 1;
		opts = "f:iK:L:P:";
	} else if (!strcmp(argv[1], "setkey")) {
		action = ACT_SETKEY;
		doopen = 1;
		opts = "k:K:l:L:n:p:P:";
	} else if (!strcmp(argv[1], "destroy")) {
		action = ACT_DESTROY;
		doopen = 1;
		opts = "k:l:p:";
	} else if (!strcmp(argv[1], "nuke")) {
		action = ACT_NUKE;
		doopen = 1;
		opts = "k:l:n:p:";
	} else {
		usage();
	}
	argc--;
	argv++;

	dest = strdup(argv[1]);
	argc--;
	argv++;

	p_opt = NULL;
	P_opt = NULL;
	k_opt = NULL;
	K_opt = NULL;
	l_opt = NULL;
	L_opt = NULL;
	f_opt = NULL;
	n_opt = 0;
	i_opt = 0;

	while((ch = getopt(argc, argv, opts)) != -1)
		switch (ch) {
		case 'f':
			f_opt = optarg;
			break;
		case 'i':
			i_opt = !i_opt;
			break;
		case 'k':
			k_opt = optarg;
			break;
		case 'K':
			K_opt = optarg;
			break;
		case 'l':
			l_opt = optarg;
			break;
		case 'L':
			L_opt = optarg;
			break;
		case 'n':
			n_opt = strtoul(optarg, &q, 0);
			if (!*optarg || *q)
				errx(1, "-n argument not numeric");
			if (n_opt < -1 || n_opt > G_BDE_MAXKEYS)
				errx(1, "-n argument out of range");
			break;
		case 'p':
			p_opt = optarg;
			break;
		case 'P':
			P_opt = optarg;
			break;
		default:
			usage();
		}

	if (doopen) {
		dfd = open(dest, O_RDWR);
		if (dfd < 0 && dest[0] != '/') {
			if (snprintf(buf, sizeof(buf), "%s%s",
			    _PATH_DEV, dest) >= (ssize_t)sizeof(buf))
				errno = ENAMETOOLONG;
			else
				dfd = open(buf, O_RDWR);
		}
		if (dfd < 0)
			err(1, "%s", dest);
	} else {
		if (!memcmp(dest, _PATH_DEV, strlen(_PATH_DEV)))
			strcpy(dest, dest + strlen(_PATH_DEV));
	}

	memset(&sc, 0, sizeof sc);
	sc.consumer = (void *)&dfd;
	gl = &sc.key;
	switch(action) {
	case ACT_ATTACH:
		setup_passphrase(&sc, 0, p_opt, k_opt);
		cmd_attach(&sc, dest, l_opt);
		break;
	case ACT_DETACH:
		cmd_detach(dest);
		break;
	case ACT_INIT:
		cmd_init(gl, dfd, f_opt, i_opt, L_opt);
		setup_passphrase(&sc, 1, P_opt, K_opt);
		cmd_write(gl, &sc, dfd, 0, L_opt);
		break;
	case ACT_SETKEY:
		setup_passphrase(&sc, 0, p_opt, k_opt);
		cmd_open(&sc, dfd, l_opt, &nkey);
		if (n_opt == 0)
			n_opt = nkey + 1;
		setup_passphrase(&sc, 1, P_opt, K_opt);
		cmd_write(gl, &sc, dfd, n_opt - 1, L_opt);
		break;
	case ACT_DESTROY:
		setup_passphrase(&sc, 0, p_opt, k_opt);
		cmd_open(&sc, dfd, l_opt, &nkey);
		cmd_destroy(gl, nkey);
		reset_passphrase(&sc);
		cmd_write(gl, &sc, dfd, nkey, l_opt);
		break;
	case ACT_NUKE:
		setup_passphrase(&sc, 0, p_opt, k_opt);
		cmd_open(&sc, dfd, l_opt, &nkey);
		if (n_opt == 0)
			n_opt = nkey + 1;
		if (n_opt == -1) {
			for(i = 0; i < G_BDE_MAXKEYS; i++)
				cmd_nuke(gl, dfd, i);
		} else {
				cmd_nuke(gl, dfd, n_opt - 1);
		}
		break;
	default:
		errx(1, "internal error");
	}

	return(0);
}
