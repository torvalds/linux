// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/*
 * Routines for dealing with .zip archives.
 *
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "libbpf_internal.h"
#include "zip.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpacked"
#pragma GCC diagnostic ignored "-Wattributes"

/* Specification of ZIP file format can be found here:
 * https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT
 * For a high level overview of the structure of a ZIP file see
 * sections 4.3.1 - 4.3.6.
 *
 * Data structures appearing in ZIP files do not contain any
 * padding and they might be misaligned. To allow us to safely
 * operate on pointers to such structures and their members, we
 * declare the types as packed.
 */

#define END_OF_CD_RECORD_MAGIC 0x06054b50

/* See section 4.3.16 of the spec. */
struct end_of_cd_record {
	/* Magic value equal to END_OF_CD_RECORD_MAGIC */
	__u32 magic;

	/* Number of the file containing this structure or 0xFFFF if ZIP64 archive.
	 * Zip archive might span multiple files (disks).
	 */
	__u16 this_disk;

	/* Number of the file containing the beginning of the central directory or
	 * 0xFFFF if ZIP64 archive.
	 */
	__u16 cd_disk;

	/* Number of central directory records on this disk or 0xFFFF if ZIP64
	 * archive.
	 */
	__u16 cd_records;

	/* Number of central directory records on all disks or 0xFFFF if ZIP64
	 * archive.
	 */
	__u16 cd_records_total;

	/* Size of the central directory record or 0xFFFFFFFF if ZIP64 archive. */
	__u32 cd_size;

	/* Offset of the central directory from the beginning of the archive or
	 * 0xFFFFFFFF if ZIP64 archive.
	 */
	__u32 cd_offset;

	/* Length of comment data following end of central directory record. */
	__u16 comment_length;

	/* Up to 64k of arbitrary bytes. */
	/* uint8_t comment[comment_length] */
} __attribute__((packed));

#define CD_FILE_HEADER_MAGIC 0x02014b50
#define FLAG_ENCRYPTED (1 << 0)
#define FLAG_HAS_DATA_DESCRIPTOR (1 << 3)

/* See section 4.3.12 of the spec. */
struct cd_file_header {
	/* Magic value equal to CD_FILE_HEADER_MAGIC. */
	__u32 magic;
	__u16 version;
	/* Minimum zip version needed to extract the file. */
	__u16 min_version;
	__u16 flags;
	__u16 compression;
	__u16 last_modified_time;
	__u16 last_modified_date;
	__u32 crc;
	__u32 compressed_size;
	__u32 uncompressed_size;
	__u16 file_name_length;
	__u16 extra_field_length;
	__u16 file_comment_length;
	/* Number of the disk where the file starts or 0xFFFF if ZIP64 archive. */
	__u16 disk;
	__u16 internal_attributes;
	__u32 external_attributes;
	/* Offset from the start of the disk containing the local file header to the
	 * start of the local file header.
	 */
	__u32 offset;
} __attribute__((packed));

#define LOCAL_FILE_HEADER_MAGIC 0x04034b50

/* See section 4.3.7 of the spec. */
struct local_file_header {
	/* Magic value equal to LOCAL_FILE_HEADER_MAGIC. */
	__u32 magic;
	/* Minimum zip version needed to extract the file. */
	__u16 min_version;
	__u16 flags;
	__u16 compression;
	__u16 last_modified_time;
	__u16 last_modified_date;
	__u32 crc;
	__u32 compressed_size;
	__u32 uncompressed_size;
	__u16 file_name_length;
	__u16 extra_field_length;
} __attribute__((packed));

#pragma GCC diagnostic pop

struct zip_archive {
	void *data;
	__u32 size;
	__u32 cd_offset;
	__u32 cd_records;
};

static void *check_access(struct zip_archive *archive, __u32 offset, __u32 size)
{
	if (offset + size > archive->size || offset > offset + size)
		return NULL;

	return archive->data + offset;
}

/* Returns 0 on success, -EINVAL on error and -ENOTSUP if the eocd indicates the
 * archive uses features which are not supported.
 */
static int try_parse_end_of_cd(struct zip_archive *archive, __u32 offset)
{
	__u16 comment_length, cd_records;
	struct end_of_cd_record *eocd;
	__u32 cd_offset, cd_size;

	eocd = check_access(archive, offset, sizeof(*eocd));
	if (!eocd || eocd->magic != END_OF_CD_RECORD_MAGIC)
		return -EINVAL;

	comment_length = eocd->comment_length;
	if (offset + sizeof(*eocd) + comment_length != archive->size)
		return -EINVAL;

	cd_records = eocd->cd_records;
	if (eocd->this_disk != 0 || eocd->cd_disk != 0 || eocd->cd_records_total != cd_records)
		/* This is a valid eocd, but we only support single-file non-ZIP64 archives. */
		return -ENOTSUP;

	cd_offset = eocd->cd_offset;
	cd_size = eocd->cd_size;
	if (!check_access(archive, cd_offset, cd_size))
		return -EINVAL;

	archive->cd_offset = cd_offset;
	archive->cd_records = cd_records;
	return 0;
}

static int find_cd(struct zip_archive *archive)
{
	int64_t limit, offset;
	int rc = -EINVAL;

	if (archive->size <= sizeof(struct end_of_cd_record))
		return -EINVAL;

	/* Because the end of central directory ends with a variable length array of
	 * up to 0xFFFF bytes we can't know exactly where it starts and need to
	 * search for it at the end of the file, scanning the (limit, offset] range.
	 */
	offset = archive->size - sizeof(struct end_of_cd_record);
	limit = (int64_t)offset - (1 << 16);

	for (; offset >= 0 && offset > limit && rc != 0; offset--) {
		rc = try_parse_end_of_cd(archive, offset);
		if (rc == -ENOTSUP)
			break;
	}
	return rc;
}

struct zip_archive *zip_archive_open(const char *path)
{
	struct zip_archive *archive;
	int err, fd;
	off_t size;
	void *data;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return ERR_PTR(-errno);

	size = lseek(fd, 0, SEEK_END);
	if (size == (off_t)-1 || size > UINT32_MAX) {
		close(fd);
		return ERR_PTR(-EINVAL);
	}

	data = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	err = -errno;
	close(fd);

	if (data == MAP_FAILED)
		return ERR_PTR(err);

	archive = malloc(sizeof(*archive));
	if (!archive) {
		munmap(data, size);
		return ERR_PTR(-ENOMEM);
	};

	archive->data = data;
	archive->size = size;

	err = find_cd(archive);
	if (err) {
		munmap(data, size);
		free(archive);
		return ERR_PTR(err);
	}

	return archive;
}

void zip_archive_close(struct zip_archive *archive)
{
	munmap(archive->data, archive->size);
	free(archive);
}

static struct local_file_header *local_file_header_at_offset(struct zip_archive *archive,
							     __u32 offset)
{
	struct local_file_header *lfh;

	lfh = check_access(archive, offset, sizeof(*lfh));
	if (!lfh || lfh->magic != LOCAL_FILE_HEADER_MAGIC)
		return NULL;

	return lfh;
}

static int get_entry_at_offset(struct zip_archive *archive, __u32 offset, struct zip_entry *out)
{
	struct local_file_header *lfh;
	__u32 compressed_size;
	const char *name;
	void *data;

	lfh = local_file_header_at_offset(archive, offset);
	if (!lfh)
		return -EINVAL;

	offset += sizeof(*lfh);
	if ((lfh->flags & FLAG_ENCRYPTED) || (lfh->flags & FLAG_HAS_DATA_DESCRIPTOR))
		return -EINVAL;

	name = check_access(archive, offset, lfh->file_name_length);
	if (!name)
		return -EINVAL;

	offset += lfh->file_name_length;
	if (!check_access(archive, offset, lfh->extra_field_length))
		return -EINVAL;

	offset += lfh->extra_field_length;
	compressed_size = lfh->compressed_size;
	data = check_access(archive, offset, compressed_size);
	if (!data)
		return -EINVAL;

	out->compression = lfh->compression;
	out->name_length = lfh->file_name_length;
	out->name = name;
	out->data = data;
	out->data_length = compressed_size;
	out->data_offset = offset;

	return 0;
}

int zip_archive_find_entry(struct zip_archive *archive, const char *file_name,
			   struct zip_entry *out)
{
	size_t file_name_length = strlen(file_name);
	__u32 i, offset = archive->cd_offset;

	for (i = 0; i < archive->cd_records; ++i) {
		__u16 cdfh_name_length, cdfh_flags;
		struct cd_file_header *cdfh;
		const char *cdfh_name;

		cdfh = check_access(archive, offset, sizeof(*cdfh));
		if (!cdfh || cdfh->magic != CD_FILE_HEADER_MAGIC)
			return -EINVAL;

		offset += sizeof(*cdfh);
		cdfh_name_length = cdfh->file_name_length;
		cdfh_name = check_access(archive, offset, cdfh_name_length);
		if (!cdfh_name)
			return -EINVAL;

		cdfh_flags = cdfh->flags;
		if ((cdfh_flags & FLAG_ENCRYPTED) == 0 &&
		    (cdfh_flags & FLAG_HAS_DATA_DESCRIPTOR) == 0 &&
		    file_name_length == cdfh_name_length &&
		    memcmp(file_name, archive->data + offset, file_name_length) == 0) {
			return get_entry_at_offset(archive, cdfh->offset, out);
		}

		offset += cdfh_name_length;
		offset += cdfh->extra_field_length;
		offset += cdfh->file_comment_length;
	}

	return -ENOENT;
}
