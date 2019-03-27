/*	$NetBSD: cd9660_write.c,v 1.14 2011/01/04 09:48:21 wiz Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2005 Daniel Watt, Walter Deignan, Ryan Gabrys, Alan
 * Perez-Rathke and Ram Vedam.  All rights reserved.
 *
 * This code was written by Daniel Watt, Walter Deignan, Ryan Gabrys,
 * Alan Perez-Rathke and Ram Vedam.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY DANIEL WATT, WALTER DEIGNAN, RYAN
 * GABRYS, ALAN PEREZ-RATHKE AND RAM VEDAM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL DANIEL WATT, WALTER DEIGNAN, RYAN
 * GABRYS, ALAN PEREZ-RATHKE AND RAM VEDAM BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE,DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#include "cd9660.h"
#include "iso9660_rrip.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <util.h>

static int cd9660_write_volume_descriptors(iso9660_disk *, FILE *);
static int cd9660_write_path_table(iso9660_disk *, FILE *, off_t, int);
static int cd9660_write_path_tables(iso9660_disk *, FILE *);
static int cd9660_write_file(iso9660_disk *, FILE *, cd9660node *);
static int cd9660_write_filedata(iso9660_disk *, FILE *, off_t,
    const unsigned char *, int);
#if 0
static int cd9660_write_buffered(FILE *, off_t, int, const unsigned char *);
#endif
static void cd9660_write_rr(iso9660_disk *, FILE *, cd9660node *, off_t, off_t);

/*
 * Write the image
 * Writes the entire image
 * @param const char* The filename for the image
 * @returns int 1 on success, 0 on failure
 */
int
cd9660_write_image(iso9660_disk *diskStructure, const char* image)
{
	FILE *fd;
	int status;
	char buf[CD9660_SECTOR_SIZE];

	if ((fd = fopen(image, "w+")) == NULL) {
		err(EXIT_FAILURE, "%s: Can't open `%s' for writing", __func__,
		    image);
	}

	if (diskStructure->verbose_level > 0)
		printf("Writing image\n");

	if (diskStructure->has_generic_bootimage) {
		status = cd9660_copy_file(diskStructure, fd, 0,
		    diskStructure->generic_bootimage);
		if (status == 0) {
			warnx("%s: Error writing generic boot image",
			    __func__);
			goto cleanup_bad_image;
		}
	}

	/* Write the volume descriptors */
	status = cd9660_write_volume_descriptors(diskStructure, fd);
	if (status == 0) {
		warnx("%s: Error writing volume descriptors to image",
		    __func__);
		goto cleanup_bad_image;
	}

	if (diskStructure->verbose_level > 0)
		printf("Volume descriptors written\n");

	/*
	 * Write the path tables: there are actually four, but right
	 * now we are only concearned with two.
	 */
	status = cd9660_write_path_tables(diskStructure, fd);
	if (status == 0) {
		warnx("%s: Error writing path tables to image", __func__);
		goto cleanup_bad_image;
	}

	if (diskStructure->verbose_level > 0)
		printf("Path tables written\n");

	/* Write the directories and files */
	status = cd9660_write_file(diskStructure, fd, diskStructure->rootNode);
	if (status == 0) {
		warnx("%s: Error writing files to image", __func__);
		goto cleanup_bad_image;
	}

	if (diskStructure->is_bootable) {
		cd9660_write_boot(diskStructure, fd);
	}

	/* Write padding bits. This is temporary */
	memset(buf, 0, CD9660_SECTOR_SIZE);
	cd9660_write_filedata(diskStructure, fd,
	    diskStructure->totalSectors - 1, buf, 1);

	if (diskStructure->verbose_level > 0)
		printf("Files written\n");
	fclose(fd);

	if (diskStructure->verbose_level > 0)
		printf("Image closed\n");
	return 1;

cleanup_bad_image:
	fclose(fd);
	if (!diskStructure->keep_bad_images)
		unlink(image);
	if (diskStructure->verbose_level > 0)
		printf("Bad image cleaned up\n");
	return 0;
}

static int
cd9660_write_volume_descriptors(iso9660_disk *diskStructure, FILE *fd)
{
	volume_descriptor *vd_temp = diskStructure->firstVolumeDescriptor;

	while (vd_temp != NULL) {
		cd9660_write_filedata(diskStructure, fd, vd_temp->sector,
		    vd_temp->volumeDescriptorData, 1);
		vd_temp = vd_temp->next;
	}
	return 1;
}

/*
 * Write out an individual path table
 * Used just to keep redundant code to a minimum
 * @param FILE *fd Valid file pointer
 * @param int Sector to start writing path table to
 * @param int Endian mode : BIG_ENDIAN or LITTLE_ENDIAN
 * @returns int 1 on success, 0 on failure
 */
static int
cd9660_write_path_table(iso9660_disk *diskStructure, FILE *fd, off_t sector,
    int mode)
{
	int path_table_sectors = CD9660_BLOCKS(diskStructure->sectorSize,
	    diskStructure->pathTableLength);
	unsigned char *buffer;
	unsigned char *buffer_head;
	int len, ret;
	path_table_entry temp_entry;
	cd9660node *ptcur;

	buffer = ecalloc(path_table_sectors, diskStructure->sectorSize);
	buffer_head = buffer;

	ptcur = diskStructure->rootNode;

	while (ptcur != NULL) {
		memset(&temp_entry, 0, sizeof(path_table_entry));
		temp_entry.length[0] = ptcur->isoDirRecord->name_len[0];
		temp_entry.extended_attribute_length[0] =
		    ptcur->isoDirRecord->ext_attr_length[0];
		memcpy(temp_entry.name, ptcur->isoDirRecord->name,
		    temp_entry.length[0] + 1);

		/* round up */
		len = temp_entry.length[0] + 8 + (temp_entry.length[0] & 0x01);

                /* todo: function pointers instead */
		if (mode == LITTLE_ENDIAN) {
			cd9660_731(ptcur->fileDataSector,
			    temp_entry.first_sector);
			cd9660_721((ptcur->parent == NULL ?
				1 : ptcur->parent->ptnumber),
			    temp_entry.parent_number);
		} else {
			cd9660_732(ptcur->fileDataSector,
			    temp_entry.first_sector);
			cd9660_722((ptcur->parent == NULL ?
				1 : ptcur->parent->ptnumber),
			    temp_entry.parent_number);
		}


		memcpy(buffer, &temp_entry, len);
		buffer += len;

		ptcur = ptcur->ptnext;
	}

	ret = cd9660_write_filedata(diskStructure, fd, sector, buffer_head,
	    path_table_sectors);
	free(buffer_head);
	return ret;
}


/*
 * Write out the path tables to disk
 * Each file descriptor should be pointed to by the PVD, so we know which
 * sector to copy them to. One thing to watch out for: the only path tables
 * stored are in the endian mode that the application is compiled for. So,
 * the first thing to do is write out that path table, then to write the one
 * in the other endian mode requires to convert the endianness of each entry
 * in the table. The best way to do this would be to create a temporary
 * path_table_entry structure, then for each path table entry, copy it to
 * the temporary entry, translate, then copy that to disk.
 *
 * @param FILE* Valid file descriptor
 * @returns int 0 on failure, 1 on success
 */
static int
cd9660_write_path_tables(iso9660_disk *diskStructure, FILE *fd)
{
	if (cd9660_write_path_table(diskStructure, fd,
	    diskStructure->primaryLittleEndianTableSector, LITTLE_ENDIAN) == 0)
		return 0;

	if (cd9660_write_path_table(diskStructure, fd,
	    diskStructure->primaryBigEndianTableSector, BIG_ENDIAN) == 0)
		return 0;

	/* @TODO: handle remaining two path tables */
	return 1;
}

/*
 * Write a file to disk
 * Writes a file, its directory record, and its data to disk
 * This file is designed to be called RECURSIVELY, so initially call it
 * with the root node. All of the records should store what sector the
 * file goes in, so no computation should be  necessary.
 *
 * @param int fd Valid file descriptor
 * @param struct cd9660node* writenode Pointer to the file to be written
 * @returns int 0 on failure, 1 on success
 */
static int
cd9660_write_file(iso9660_disk *diskStructure, FILE *fd, cd9660node *writenode)
{
	char *buf;
	char *temp_file_name;
	int ret;
	off_t working_sector;
	int cur_sector_offset;
	iso_directory_record_cd9660 temp_record;
	cd9660node *temp;
	int rv = 0;

	/* Todo : clean up variables */

	temp_file_name = ecalloc(CD9660MAXPATH + 1, 1);
	buf = emalloc(diskStructure->sectorSize);
	if ((writenode->level != 0) &&
	    !(writenode->node->type & S_IFDIR)) {
		fsinode *inode = writenode->node->inode;
		/* Only attempt to write unwritten files that have length. */
		if ((inode->flags & FI_WRITTEN) != 0) {
			INODE_WARNX(("%s: skipping written inode %d", __func__,
			    (int)inode->st.st_ino));
		} else if (writenode->fileDataLength > 0) {
			INODE_WARNX(("%s: writing inode %d blocks at %" PRIu32,
			    __func__, (int)inode->st.st_ino, inode->ino));
			inode->flags |= FI_WRITTEN;
			if (writenode->node->contents == NULL)
				cd9660_compute_full_filename(writenode,
				    temp_file_name);
			ret = cd9660_copy_file(diskStructure, fd,
			    writenode->fileDataSector,
			    (writenode->node->contents != NULL) ?
			    writenode->node->contents : temp_file_name);
			if (ret == 0)
				goto out;
		}
	} else {
		/*
		 * Here is a new revelation that ECMA didn't explain
		 * (at least not well).
		 * ALL . and .. records store the name "\0" and "\1"
		 * respectively. So, for each directory, we have to
		 * make a new node.
		 *
		 * This is where it gets kinda messy, since we have to
		 * be careful of sector boundaries
		 */
		cur_sector_offset = 0;
		working_sector = writenode->fileDataSector;
		if (fseeko(fd, working_sector * diskStructure->sectorSize,
		    SEEK_SET) == -1)
			err(1, "fseeko");

		/*
		 * Now loop over children, writing out their directory
		 * records - beware of sector boundaries
	 	 */
		TAILQ_FOREACH(temp, &writenode->cn_children, cn_next_child) {
			/*
			 * Copy the temporary record and adjust its size
			 * if necessary
			 */
			memcpy(&temp_record, temp->isoDirRecord,
			    sizeof(iso_directory_record_cd9660));

			temp_record.length[0] =
			    cd9660_compute_record_size(diskStructure, temp);

			if (temp_record.length[0] + cur_sector_offset >=
			    diskStructure->sectorSize) {
				cur_sector_offset = 0;
				working_sector++;

				/* Seek to the next sector. */
				if (fseeko(fd, working_sector *
				    diskStructure->sectorSize, SEEK_SET) == -1)
					err(1, "fseeko");
			}
			/* Write out the basic ISO directory record */
			(void)fwrite(&temp_record, 1,
			    temp->isoDirRecord->length[0], fd);
			if (diskStructure->rock_ridge_enabled) {
				cd9660_write_rr(diskStructure, fd, temp,
				    cur_sector_offset, working_sector);
			}
			if (fseeko(fd, working_sector *
			    diskStructure->sectorSize + cur_sector_offset +
			    temp_record.length[0] - temp->su_tail_size,
			    SEEK_SET) == -1)
				err(1, "fseeko");
			if (temp->su_tail_size > 0)
				fwrite(temp->su_tail_data, 1,
				    temp->su_tail_size, fd);
			if (ferror(fd)) {
				warnx("%s: write error", __func__);
				goto out;
			}
			cur_sector_offset += temp_record.length[0];

		}

		/*
		 * Recurse on children.
		 */
		TAILQ_FOREACH(temp, &writenode->cn_children, cn_next_child) {
			if ((ret = cd9660_write_file(diskStructure, fd, temp)) == 0)
				goto out;
		}
	}
	rv = 1;
out:
	free(temp_file_name);
	free(buf);
	return rv;
}

/*
 * Wrapper function to write a buffer (one sector) to disk.
 * Seeks and writes the buffer.
 * NOTE: You dont NEED to use this function, but it might make your
 * life easier if you have to write things that align to a sector
 * (such as volume descriptors).
 *
 * @param int fd Valid file descriptor
 * @param int sector Sector number to write to
 * @param const unsigned char* Buffer to write. This should be the
 *                             size of a sector, and if only a portion
 *                             is written, the rest should be set to 0.
 */
static int
cd9660_write_filedata(iso9660_disk *diskStructure, FILE *fd, off_t sector,
    const unsigned char *buf, int numsecs)
{
	off_t curpos;
	size_t success;

	curpos = ftello(fd);

	if (fseeko(fd, sector * diskStructure->sectorSize, SEEK_SET) == -1)
		err(1, "fseeko");

	success = fwrite(buf, diskStructure->sectorSize * numsecs, 1, fd);

	if (fseeko(fd, curpos, SEEK_SET) == -1)
		err(1, "fseeko");

	if (success == 1)
		success = diskStructure->sectorSize * numsecs;
	return success;
}

#if 0
static int
cd9660_write_buffered(FILE *fd, off_t offset, int buff_len,
		      const unsigned char* buffer)
{
	static int working_sector = -1;
	static char buf[CD9660_SECTOR_SIZE];

	return 0;
}
#endif

int
cd9660_copy_file(iso9660_disk *diskStructure, FILE *fd, off_t start_sector,
    const char *filename)
{
	FILE *rf;
	int bytes_read;
	off_t sector = start_sector;
	int buf_size = diskStructure->sectorSize;
	char *buf;

	buf = emalloc(buf_size);
	if ((rf = fopen(filename, "rb")) == NULL) {
		warn("%s: cannot open %s", __func__, filename);
		free(buf);
		return 0;
	}

	if (diskStructure->verbose_level > 1)
		printf("Writing file: %s\n",filename);

	if (fseeko(fd, start_sector * diskStructure->sectorSize, SEEK_SET) == -1)
		err(1, "fseeko");

	while (!feof(rf)) {
		bytes_read = fread(buf,1,buf_size,rf);
		if (ferror(rf)) {
			warn("%s: fread", __func__);
			free(buf);
			(void)fclose(rf);
			return 0;
		}

		fwrite(buf,1,bytes_read,fd);
		if (ferror(fd)) {
			warn("%s: fwrite", __func__);
			free(buf);
			(void)fclose(rf);
			return 0;
		}
		sector++;
	}

	fclose(rf);
	free(buf);
	return 1;
}

static void
cd9660_write_rr(iso9660_disk *diskStructure, FILE *fd, cd9660node *writenode,
    off_t offset, off_t sector)
{
	int in_ca = 0;
	struct ISO_SUSP_ATTRIBUTES *myattr;

	offset += writenode->isoDirRecord->length[0];
	if (fseeko(fd, sector * diskStructure->sectorSize + offset, SEEK_SET) ==
	    -1)
		err(1, "fseeko");
	/* Offset now points at the end of the record */
	TAILQ_FOREACH(myattr, &writenode->head, rr_ll) {
		fwrite(&(myattr->attr), CD9660_SUSP_ENTRY_SIZE(myattr), 1, fd);

		if (!in_ca) {
			offset += CD9660_SUSP_ENTRY_SIZE(myattr);
			if (myattr->last_in_suf) {
				/*
				 * Point the offset to the start of this
				 * record's CE area
				 */
				if (fseeko(fd, ((off_t)diskStructure->
				    susp_continuation_area_start_sector *
				    diskStructure->sectorSize)
				    + writenode->susp_entry_ce_start,
				    SEEK_SET) == -1)
					err(1, "fseeko");
				in_ca = 1;
			}
		}
	}

	/*
	 * If we had to go to the continuation area, head back to
	 * where we should be.
	 */
	if (in_ca)
		if (fseeko(fd, sector * diskStructure->sectorSize + offset,
		    SEEK_SET) == -1)
			err(1, "fseeko");
}
