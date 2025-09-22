/* $OpenBSD: icdb.c,v 1.8 2016/09/04 16:56:02 nicm Exp $ */
/*
 * Copyright (c) 2015 Ted Unangst <tedu@openbsd.org>
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

#include <errno.h>
#include <fcntl.h>
#include <icdb.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>

#include <siphash.h>

/*
 * Creating a new icdb: icdb_new
 * Opening existing icdb: icdb_open
 *
 * Adding new entries: icdb_add
 * Adding entries does not update the disk or indices.
 *
 * Save to disk: icdb_save
 * Update indices: icdb_rehash
 * icdb_save will call rehash.
 *
 * Change an existing entry: icdb_update
 * Changing entries does write to disk.
 *
 * Find an entry: icdb_lookup
 * Looking up an entry is only defined when the indices are synced.
 *
 * Close and free resources: icdb_close
 */

/*
 * There are two major modes of operation.
 *
 * Existing databases use the mmap codepath. The entire database is mapped
 * into the address space for quick access. Individual entries may be updated,
 * but no new entries added.
 *
 * New databases use malloc backed memory instead. The database may be saved
 * with icdb_save. It should be saved to a new file to avoid corrupting any
 * open databases in other processes.
 */

/*
 * An icdb has the following format:
 *   struct icbinfo header
 *   indexes [ uint32_t * indexsize * nkeys ]
 *   entries [ entrysize * nentries ]
 *
 * To find an entry in the file, the user specifies which key to use.
 * The key is hashed and looked up in the index. The index contains the
 * position of the entry in the entries array. -1 identifies not found.
 * Chaining is done by rehashing the hash. All keys are fixed size byte arrays.
 */

/*
 * Header info for icdb. This struct is stored on disk.
 */
struct icdbinfo {
	uint32_t magic;		/* magic */
	uint32_t version;	/* user specified version */
	uint32_t nentries;	/* number of entries stored */
	uint32_t entrysize;	/* size of each entry */
	uint32_t indexsize;	/* number of entries in hash index */
	uint32_t nkeys;		/* number of keys defined */
	uint32_t keysize[8];	/* size of each key */
	uint32_t keyoffset[8];	/* offset of each key in entry */
	SIPHASH_KEY siphashkey;	/* random hash key */
};

/*
 * In memory representation with auxiliary data.
 * idxdata and entries will be written to disk after info.
 */
struct icdb {
	struct icdbinfo *info;
	void *idxdata[8];
	void *entries;
	size_t maplen;
	uint32_t allocated;
	int fd;
};

static const uint32_t magic = 0x1ca9d0b7;

static uint32_t
roundup(uint32_t num)
{
	uint32_t r = 2;

	while (r < num * 3 / 2)
		r *= 2;
	return r;
}

struct icdb *
icdb_new(uint32_t version, uint32_t nentries, uint32_t entrysize,
    uint32_t nkeys, const uint32_t *keysizes, const uint32_t *keyoffsets)
{
	struct icdb *db;
	struct icdbinfo *info;
	int i;

	if (entrysize == 0 || entrysize > 1048576 || nkeys > 8) {
		errno = EINVAL;
		return NULL;
	}

	if (!(db = calloc(1, sizeof(*db))))
		return NULL;
	if (!(info = calloc(1, sizeof(*info)))) {
		free(db);
		return NULL;
	}
	db->info = info;
	db->fd = -1;
	info->magic = magic;
	info->version = version;
	if (nentries)
		if ((db->entries = reallocarray(NULL, nentries, entrysize)))
			db->allocated = nentries;
	info->entrysize = entrysize;
	info->nkeys = nkeys;
	for (i = 0; i < nkeys; i++) {
		info->keysize[i] = keysizes[i];
		info->keyoffset[i] = keyoffsets[i];
	}
	return db;
}
DEF_WEAK(icdb_new);

struct icdb *
icdb_open(const char *name, int flags, uint32_t version)
{
	struct icdb *db = NULL;
	struct icdbinfo *info;
	struct stat sb;
	uint8_t *ptr = MAP_FAILED;
	uint32_t baseoff, indexsize, idxmask, idxlen;
	int fd, i, saved_errno;

	if ((fd = open(name, flags | O_CLOEXEC)) == -1)
		return NULL;
	if (fstat(fd, &sb) != 0)
		goto fail;
	if (sb.st_size < sizeof(struct icdbinfo))
		goto fail;
	ptr = mmap(NULL, sb.st_size, PROT_READ |
	    ((flags & O_RDWR) ? PROT_WRITE : 0), MAP_SHARED, fd, 0);
	if (ptr == MAP_FAILED)
		goto fail;
	info = (struct icdbinfo *)ptr;
	if (info->magic != magic || info->version != version) {
		errno = ENOENT;
		goto fail;
	}

	if (!(db = calloc(1, sizeof(*db))))
		goto fail;
	db->info = info;

	indexsize = info->indexsize;
	idxmask = indexsize - 1;
	idxlen = indexsize * sizeof(uint32_t);
	baseoff = sizeof(*info) + idxlen * info->nkeys;

	for (i = 0; i < info->nkeys; i++)
		db->idxdata[i] = ptr + sizeof(*info) + i * idxlen;
	db->entries = ptr + baseoff;
	db->maplen = sb.st_size;
	db->fd = fd;
	return db;

fail:
	saved_errno = errno;
	if (ptr != MAP_FAILED)
		munmap(ptr, sb.st_size);
	if (fd != -1)
		close(fd);
	free(db);
	errno = saved_errno;
	return NULL;
}
DEF_WEAK(icdb_open);

int
icdb_get(struct icdb *db, void *entry, uint32_t idx)
{
	uint32_t entrysize = db->info->entrysize;

	memcpy(entry, (uint8_t *)db->entries + idx * entrysize, entrysize);
	return 0;
}
DEF_WEAK(icdb_get);

int
icdb_lookup(struct icdb *db, int keynum, const void *key, void *entry,
    uint32_t *idxp)
{
	struct icdbinfo *info = db->info;
	uint32_t offset;
	uint64_t hash;
	uint32_t indexsize, idxmask, idxlen;
	uint32_t *idxdata;

	indexsize = info->indexsize;
	idxmask = indexsize - 1;
	idxlen = indexsize * sizeof(uint32_t);

	idxdata = db->idxdata[keynum];

	hash = SipHash24(&info->siphashkey, key, info->keysize[keynum]);
	while ((offset = idxdata[hash & idxmask]) != -1) {
		if (icdb_get(db, entry, offset) != 0) {
			errno = ENOENT;
			return -1;
		}
		if (memcmp((uint8_t *)entry + info->keyoffset[keynum], key,
		    info->keysize[keynum]) == 0) {
			if (idxp)
				*idxp = offset;
			return 0;
		}
		hash = SipHash24(&info->siphashkey, &hash, sizeof(hash));
	}
	return 1;
}
DEF_WEAK(icdb_lookup);

int
icdb_nentries(struct icdb *db)
{
	return db->info->nentries;
}
DEF_WEAK(icdb_nentries);

const void *
icdb_entries(struct icdb *db)
{
	return db->entries;
}
DEF_WEAK(icdb_entries);

int
icdb_update(struct icdb *db, const void *entry, int offset)
{
	struct icdbinfo *info = db->info;
	uint32_t entrysize = info->entrysize;
	uint32_t baseoff;
	uint32_t indexsize, idxmask, idxlen;

	indexsize = info->indexsize;
	idxmask = indexsize - 1;
	idxlen = indexsize * sizeof(uint32_t);
	baseoff = sizeof(*info) + idxlen * info->nkeys;

	memcpy((uint8_t *)db->entries + offset * entrysize, entry, entrysize);
	if (db->fd != -1) {
		msync((uint8_t *)db->entries + offset * entrysize, entrysize,
		    MS_SYNC);
	}
	return 0;
}
DEF_WEAK(icdb_update);

int
icdb_add(struct icdb *db, const void *entry)
{
	struct icdbinfo *info = db->info;
	size_t entrysize = info->entrysize;

	if (db->allocated == info->nentries) {
		void *p;
		size_t amt = db->allocated ? db->allocated * 2 : 63;
		if (!(p = reallocarray(db->entries, amt, entrysize)))
			return -1;
		db->allocated = amt;
		db->entries = p;
	}
	memcpy((uint8_t *)db->entries + info->nentries * entrysize,
	    entry, entrysize);
	info->nentries++;
	return 0;
}
DEF_WEAK(icdb_add);

int
icdb_rehash(struct icdb *db)
{
	struct icdbinfo *info = db->info;
	uint32_t entrysize = info->entrysize;
	uint32_t indexsize, idxmask, idxlen;
	int i, j;

	indexsize = info->indexsize = roundup(info->nentries);
	idxmask = indexsize - 1;
	idxlen = sizeof(uint32_t) * indexsize;

	arc4random_buf(&info->siphashkey, sizeof(info->siphashkey));

	for (i = 0; i < info->nkeys; i++) {
		uint32_t *idxdata = reallocarray(db->idxdata[i],
		    indexsize, sizeof(uint32_t));
		if (!idxdata)
			return -1;
		memset(idxdata, 0xff, idxlen);
		db->idxdata[i] = idxdata;
	}
	for (j = 0; j < info->nentries; j++) {
		for (i = 0; i < info->nkeys; i++) {
			uint32_t *idxdata = db->idxdata[i];
			uint64_t hash = SipHash24(&info->siphashkey,
			    (uint8_t *)db->entries + j * entrysize +
			    info->keyoffset[i], info->keysize[i]);
			while (idxdata[hash & idxmask] != -1)
				hash = SipHash24(&info->siphashkey, &hash, sizeof(hash));
			idxdata[hash & idxmask] = j;
		}
	}
	return 0;
}
DEF_WEAK(icdb_rehash);

int
icdb_save(struct icdb *db, int fd)
{
	struct icdbinfo *info = db->info;
	uint32_t entrysize = info->entrysize;
	uint32_t indexsize, idxlen;
	int i;

	if (icdb_rehash(db) != 0)
		return -1;

	indexsize = info->indexsize;
	idxlen = sizeof(uint32_t) * indexsize;

	if (ftruncate(fd, 0) != 0)
		return -1;
	if (write(fd, info, sizeof(*info)) != sizeof(*info))
		return -1;
	for (i = 0; i < info->nkeys; i++) {
		if (write(fd, db->idxdata[i], idxlen) != idxlen)
			return -1;
	}
	if (write(fd, db->entries, info->nentries * entrysize) !=
	    info->nentries * entrysize)
		return -1;
	return 0;
}
DEF_WEAK(icdb_save);

int
icdb_close(struct icdb *db)
{
	int i;

	if (db->fd == -1) {
		for (i = 0; i < db->info->nkeys; i++)
			free(db->idxdata[i]);
		free(db->entries);
		free(db->info);
	} else {
		munmap(db->info, db->maplen);
		close(db->fd);
	}
	free(db);
	return 0;
}
DEF_WEAK(icdb_close);
