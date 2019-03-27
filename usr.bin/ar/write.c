/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Kai Wang
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/endian.h>
#include <sys/mman.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <fcntl.h>
#include <gelf.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "ar.h"

#define _ARMAG_LEN 8		/* length of ar magic string */
#define _ARHDR_LEN 60		/* length of ar header */
#define _INIT_AS_CAP 128	/* initial archive string table size */
#define _INIT_SYMOFF_CAP (256*(sizeof(uint32_t))) /* initial so table size */
#define _INIT_SYMNAME_CAP 1024			  /* initial sn table size */
#define _MAXNAMELEN_SVR4 15	/* max member name length in svr4 variant */
#define _TRUNCATE_LEN 15	/* number of bytes to keep for member name */

static void	add_to_ar_str_table(struct bsdar *bsdar, const char *name);
static void	add_to_ar_sym_table(struct bsdar *bsdar, const char *name);
static struct ar_obj	*create_obj_from_file(struct bsdar *bsdar,
		    const char *name, time_t mtime);
static void	create_symtab_entry(struct bsdar *bsdar, void *maddr,
		    size_t size);
static void	free_obj(struct bsdar *bsdar, struct ar_obj *obj);
static void	insert_obj(struct bsdar *bsdar, struct ar_obj *obj,
		    struct ar_obj *pos);
static void	prefault_buffer(const char *buf, size_t s);
static void	read_objs(struct bsdar *bsdar, const char *archive,
		    int checkargv);
static void	write_archive(struct bsdar *bsdar, char mode);
static void	write_cleanup(struct bsdar *bsdar);
static void	write_data(struct bsdar *bsdar, struct archive *a,
		    const void *buf, size_t s);
static void	write_objs(struct bsdar *bsdar);

void
ar_mode_d(struct bsdar *bsdar)
{

	write_archive(bsdar, 'd');
}

void
ar_mode_m(struct bsdar *bsdar)
{

	write_archive(bsdar, 'm');
}

void
ar_mode_q(struct bsdar *bsdar)
{

	write_archive(bsdar, 'q');
}

void
ar_mode_r(struct bsdar *bsdar)
{

	write_archive(bsdar, 'r');
}

void
ar_mode_s(struct bsdar *bsdar)
{

	write_archive(bsdar, 's');
}

void
ar_mode_A(struct bsdar *bsdar)
{

	write_archive(bsdar, 'A');
}

/*
 * Create object from file, return created obj upon success, or NULL
 * when an error occurs or the member is not newer than existing
 * one while -u is specified.
 */
static struct ar_obj *
create_obj_from_file(struct bsdar *bsdar, const char *name, time_t mtime)
{
	struct ar_obj		*obj;
	struct stat		 sb;
	const char		*bname;
	char			*tmpname;

	if (name == NULL)
		return (NULL);

	obj = malloc(sizeof(struct ar_obj));
	if (obj == NULL)
		bsdar_errc(bsdar, EX_SOFTWARE, errno, "malloc failed");
	if ((obj->fd = open(name, O_RDONLY, 0)) < 0) {
		bsdar_warnc(bsdar, errno, "can't open file: %s", name);
		free(obj);
		return (NULL);
	}

	tmpname = strdup(name);
	if (tmpname == NULL)
		bsdar_errc(bsdar, EX_SOFTWARE, errno, "strdup failed");
	if ((bname = basename(tmpname)) == NULL)
		bsdar_errc(bsdar, EX_SOFTWARE, errno, "basename failed");
	if (bsdar->options & AR_TR && strlen(bname) > _TRUNCATE_LEN) {
		if ((obj->name = malloc(_TRUNCATE_LEN + 1)) == NULL)
			bsdar_errc(bsdar, EX_SOFTWARE, errno, "malloc failed");
		(void)strncpy(obj->name, bname, _TRUNCATE_LEN);
		obj->name[_TRUNCATE_LEN] = '\0';
	} else
		if ((obj->name = strdup(bname)) == NULL)
		    bsdar_errc(bsdar, EX_SOFTWARE, errno, "strdup failed");
	free(tmpname);

	if (fstat(obj->fd, &sb) < 0) {
		bsdar_warnc(bsdar, errno, "can't fstat file: %s", obj->name);
		goto giveup;
	}
	if (!S_ISREG(sb.st_mode)) {
		bsdar_warnc(bsdar, 0, "%s is not an ordinary file", obj->name);
		goto giveup;
	}

	/*
	 * When option '-u' is specified and member is not newer than the
	 * existing one, the replace will not happen. While if mtime == 0,
	 * which indicates that this is to "replace a none exist member",
	 * the replace will proceed regardless of '-u'.
	 */
	if (mtime != 0 && bsdar->options & AR_U && sb.st_mtime <= mtime)
		goto giveup;

	/*
	 * When option '-D' is specified, mtime and UID / GID from the file
	 * will be replaced with 0, and file mode with 644. This ensures that 
	 * checksums will match for two archives containing the exact same
	 * files.
	 */
	if (bsdar->options & AR_D) {
		obj->uid = 0;
		obj->gid = 0;
		obj->mtime = 0;
		obj->md = S_IFREG | 0644;
	} else {
		obj->uid = sb.st_uid;
		obj->gid = sb.st_gid;
		obj->mtime = sb.st_mtime;
		obj->md = sb.st_mode;
	}
	obj->size = sb.st_size;
	obj->dev = sb.st_dev;
	obj->ino = sb.st_ino;

	if (obj->size == 0) {
		obj->maddr = NULL;
		return (obj);
	}

	if ((obj->maddr = mmap(NULL, obj->size, PROT_READ,
	    MAP_PRIVATE, obj->fd, (off_t)0)) == MAP_FAILED) {
		bsdar_warnc(bsdar, errno, "can't mmap file: %s", obj->name);
		goto giveup;
	}
	if (close(obj->fd) < 0)
		bsdar_errc(bsdar, EX_SOFTWARE, errno, "close failed: %s",
		    obj->name);

	return (obj);

giveup:
	if (close(obj->fd) < 0)
		bsdar_errc(bsdar, EX_SOFTWARE, errno, "close failed: %s",
		    obj->name);
	free(obj->name);
	free(obj);
	return (NULL);
}

/*
 * Free object itself and its associated allocations.
 */
static void
free_obj(struct bsdar *bsdar, struct ar_obj *obj)
{
	if (obj->fd == -1)
		free(obj->maddr);
	else
		if (obj->maddr != NULL && munmap(obj->maddr, obj->size))
			bsdar_warnc(bsdar, errno,
			    "can't munmap file: %s", obj->name);
	free(obj->name);
	free(obj);
}

/*
 * Insert obj to the tail, or before/after the pos obj.
 */
static void
insert_obj(struct bsdar *bsdar, struct ar_obj *obj, struct ar_obj *pos)
{
	if (obj == NULL)
		bsdar_errc(bsdar, EX_SOFTWARE, 0, "try to insert a null obj");

	if (pos == NULL || obj == pos)
		/*
		 * If the object to move happens to be the position obj,
		 * or if there is not a pos obj, move it to tail.
		 */
		goto tail;

	if (bsdar->options & AR_B) {
		TAILQ_INSERT_BEFORE(pos, obj, objs);
		return;
	}
	if (bsdar->options & AR_A) {
		TAILQ_INSERT_AFTER(&bsdar->v_obj, pos, obj, objs);
		return;
	}

tail:
	TAILQ_INSERT_TAIL(&bsdar->v_obj, obj, objs);

}

/*
 * Read objects from archive into v_obj list. Note that checkargv is
 * set when read_objs is used to read objects from the target of
 * ADDLIB command (ar script mode), in this case argv array possibly
 * specifies the members ADDLIB want.
 */
static void
read_objs(struct bsdar *bsdar, const char *archive, int checkargv)
{
	struct archive		 *a;
	struct archive_entry	 *entry;
	struct ar_obj		 *obj;
	const char		 *name;
	const char		 *bname;
	char			 *buff;
	char			**av;
	size_t			  size;
	int			  i, r, find;

	if ((a = archive_read_new()) == NULL)
		bsdar_errc(bsdar, EX_SOFTWARE, 0, "archive_read_new failed");
	archive_read_support_format_ar(a);
	AC(archive_read_open_filename(a, archive, DEF_BLKSZ));
	for (;;) {
		r = archive_read_next_header(a, &entry);
		if (r == ARCHIVE_FATAL)
			bsdar_errc(bsdar, EX_DATAERR, archive_errno(a), "%s",
			    archive_error_string(a));
		if (r == ARCHIVE_EOF)
			break;
		if (r == ARCHIVE_WARN || r == ARCHIVE_RETRY)
			bsdar_warnc(bsdar, archive_errno(a), "%s",
			    archive_error_string(a));
		if (r == ARCHIVE_RETRY) {
			bsdar_warnc(bsdar, 0, "Retrying...");
			continue;
		}

		name = archive_entry_pathname(entry);

		/*
		 * skip pseudo members.
		 */
		if (strcmp(name, "/") == 0 || strcmp(name, "//") == 0)
			continue;

		/*
		 * If checkargv is set, only read those members specified
		 * in argv.
		 */
		if (checkargv && bsdar->argc > 0) {
			find = 0;
			for(i = 0; i < bsdar->argc; i++) {
				av = &bsdar->argv[i];
				if (*av == NULL)
					continue;
				if ((bname = basename(*av)) == NULL)
					bsdar_errc(bsdar, EX_SOFTWARE, errno,
					    "basename failed");
				if (strcmp(bname, name) != 0)
					continue;

				*av = NULL;
				find = 1;
				break;
			}
			if (!find)
				continue;
		}

		size = archive_entry_size(entry);

		if (size > 0) {
			if ((buff = malloc(size)) == NULL)
				bsdar_errc(bsdar, EX_SOFTWARE, errno,
				    "malloc failed");
			if (archive_read_data(a, buff, size) != (ssize_t)size) {
				bsdar_warnc(bsdar, archive_errno(a), "%s",
				    archive_error_string(a));
				free(buff);
				continue;
			}
		} else
			buff = NULL;

		obj = malloc(sizeof(struct ar_obj));
		if (obj == NULL)
			bsdar_errc(bsdar, EX_SOFTWARE, errno, "malloc failed");
		obj->maddr = buff;
		if ((obj->name = strdup(name)) == NULL)
			bsdar_errc(bsdar, EX_SOFTWARE, errno, "strdup failed");
		obj->size = size;
		obj->uid = archive_entry_uid(entry);
		obj->gid = archive_entry_gid(entry);
		obj->md = archive_entry_mode(entry);
		obj->mtime = archive_entry_mtime(entry);
		obj->dev = 0;
		obj->ino = 0;

		/*
		 * Objects from archive have obj->fd set to -1,
		 * for the ease of cleaning up.
		 */
		obj->fd = -1;
		TAILQ_INSERT_TAIL(&bsdar->v_obj, obj, objs);
	}
	AC(archive_read_close(a));
	AC(archive_read_free(a));
}

/*
 * Determine the constitution of resulting archive.
 */
static void
write_archive(struct bsdar *bsdar, char mode)
{
	struct ar_obj		 *nobj, *obj, *obj_temp, *pos;
	struct stat		  sb;
	const char		 *bname;
	char			**av;
	int			  i;

	TAILQ_INIT(&bsdar->v_obj);
	nobj = NULL;
	pos = NULL;
	memset(&sb, 0, sizeof(sb));

	/*
	 * Test if the specified archive exists, to figure out
	 * whether we are creating one here.
	 */
	if (stat(bsdar->filename, &sb) != 0) {
		if (errno != ENOENT) {
			bsdar_warnc(bsdar, 0, "stat %s failed",
			    bsdar->filename);
			return;
		}

		/* We do not create archive in mode 'd', 'm' and 's'.  */
		if (mode != 'r' && mode != 'q') {
			bsdar_warnc(bsdar, 0, "%s: no such file",
			    bsdar->filename);
			return;
		}

		/* Issue a warning if -c is not specified when creating. */
		if (!(bsdar->options & AR_C))
			bsdar_warnc(bsdar, 0, "creating %s", bsdar->filename);
		goto new_archive;
	}

	/*
	 * First read members from existing archive.
	 */
	read_objs(bsdar, bsdar->filename, 0);

	/*
	 * For mode 's', no member will be moved, deleted or replaced.
	 */
	if (mode == 's')
		goto write_objs;

	/*
	 * For mode 'q', we don't need to adjust existing members either.
	 * Also, -a, -b and -i are ignored in this mode. New members are
	 * always inserted at tail.
	 */
	if (mode == 'q')
		goto new_archive;

	/*
	 * Mode 'A' adds the contents of another archive to the tail of
	 * current archive. Note that mode 'A' is a special mode for the
	 * ADDLIB command of the ar script mode. Currently there is no
	 * access to this function from the ar command line mode.
	 */
	if (mode == 'A') {
		/*
		 * Read objects from the target archive of ADDLIB command.
		 * If there are members specified in argv, read those members
		 * only, otherwise the entire archive will be read.
		 */
		read_objs(bsdar, bsdar->addlib, 1);
		goto write_objs;
	}

	/*
	 * Try to find the position member specified by user.
	 */
	if (bsdar->options & AR_A || bsdar->options & AR_B) {
		TAILQ_FOREACH(obj, &bsdar->v_obj, objs) {
			if (strcmp(obj->name, bsdar->posarg) == 0) {
				pos = obj;
				break;
			}
		}

		/*
		 * If can't find `pos' specified by user,
		 * silently insert objects at tail.
		 */
		if (pos == NULL)
			bsdar->options &= ~(AR_A | AR_B);
	}

	for (i = 0; i < bsdar->argc; i++) {
		av = &bsdar->argv[i];

		TAILQ_FOREACH_SAFE(obj, &bsdar->v_obj, objs, obj_temp) {
			if ((bname = basename(*av)) == NULL)
				bsdar_errc(bsdar, EX_SOFTWARE, errno,
				    "basename failed");
			if (bsdar->options & AR_TR) {
				if (strncmp(bname, obj->name, _TRUNCATE_LEN))
					continue;
			} else
				if (strcmp(bname, obj->name) != 0)
					continue;

			if (mode == 'r') {
				/*
				 * if the new member is not qualified
				 * to replace the old one, skip it.
				 */
				nobj = create_obj_from_file(bsdar, *av,
				    obj->mtime);
				if (nobj == NULL)
					goto skip_obj;
			}

			if (bsdar->options & AR_V)
				(void)fprintf(stdout, "%c - %s\n", mode,
				    *av);

			TAILQ_REMOVE(&bsdar->v_obj, obj, objs);
			if (mode == 'd' || mode == 'r')
				free_obj(bsdar, obj);

			if (mode == 'm')
				insert_obj(bsdar, obj, pos);
			if (mode == 'r')
				insert_obj(bsdar, nobj, pos);

		skip_obj:
			*av = NULL;
			break;
		}

	}

new_archive:
	/*
	 * When operating in mode 'r', directly add those user specified
	 * objects which do not exist in current archive. When operating
	 * in mode 'q', all objects specified in command line args are
	 * appended to the archive, without comparing with existing ones.
	 */
	for (i = 0; i < bsdar->argc; i++) {
		av = &bsdar->argv[i];
		if (*av != NULL && (mode == 'r' || mode == 'q')) {
			nobj = create_obj_from_file(bsdar, *av, 0);
			if (nobj != NULL)
				insert_obj(bsdar, nobj, pos);
			if (bsdar->options & AR_V && nobj != NULL)
				(void)fprintf(stdout, "a - %s\n", *av);
			*av = NULL;
		}
	}

write_objs:
	write_objs(bsdar);
	write_cleanup(bsdar);
}

/*
 * Memory cleaning up.
 */
static void
write_cleanup(struct bsdar *bsdar)
{
	struct ar_obj		*obj, *obj_temp;

	TAILQ_FOREACH_SAFE(obj, &bsdar->v_obj, objs, obj_temp) {
		TAILQ_REMOVE(&bsdar->v_obj, obj, objs);
		free_obj(bsdar, obj);
	}

	free(bsdar->as);
	free(bsdar->s_so);
	free(bsdar->s_sn);
	bsdar->as = NULL;
	bsdar->s_so = NULL;
	bsdar->s_sn = NULL;
}

/*
 * Fault in the buffer prior to writing as a workaround for poor performance
 * due to interaction with kernel fs deadlock avoidance code. See the comment
 * above vn_io_fault_doio() in sys/kern/vfs_vnops.c for details of the issue.
 */
static void
prefault_buffer(const char *buf, size_t s)
{
	volatile const char *p;
	size_t page_size;

	if (s == 0)
		return;
	page_size = sysconf(_SC_PAGESIZE);
	for (p = buf; p < buf + s; p += page_size)
		*p;
	/*
	 * Ensure we touch the last page as well, in case the buffer is not
	 * page-aligned.
	 */
	*(volatile const char *)(buf + s - 1);
}

/*
 * Wrapper for archive_write_data().
 */
static void
write_data(struct bsdar *bsdar, struct archive *a, const void *buf, size_t s)
{
	ssize_t written;

	prefault_buffer(buf, s);
	while (s > 0) {
		written = archive_write_data(a, buf, s);
		if (written < 0)
			bsdar_errc(bsdar, EX_SOFTWARE, archive_errno(a), "%s",
			    archive_error_string(a));
		buf = (const char *)buf + written;
		s -= written;
	}
}

/*
 * Write the resulting archive members.
 */
static void
write_objs(struct bsdar *bsdar)
{
	struct ar_obj		*obj;
	struct archive		*a;
	struct archive_entry	*entry;
	size_t s_sz;		/* size of archive symbol table. */
	size_t pm_sz;		/* size of pseudo members */
	int			 i, nr;

	if (elf_version(EV_CURRENT) == EV_NONE)
		bsdar_errc(bsdar, EX_SOFTWARE, 0,
		    "ELF library initialization failed: %s", elf_errmsg(-1));

	bsdar->rela_off = 0;

	/* Create archive symbol table and archive string table, if need. */
	TAILQ_FOREACH(obj, &bsdar->v_obj, objs) {
		if (!(bsdar->options & AR_SS) && obj->maddr != NULL)
			create_symtab_entry(bsdar, obj->maddr, obj->size);
		if (strlen(obj->name) > _MAXNAMELEN_SVR4)
			add_to_ar_str_table(bsdar, obj->name);
		bsdar->rela_off += _ARHDR_LEN + obj->size + obj->size % 2;
		if (bsdar->rela_off > UINT32_MAX)
			bsdar_errc(bsdar, EX_SOFTWARE, 0,
			    "Symbol table offset overflow");
	}

	/*
	 * Pad the symbol name string table. It is treated specially because
	 * symbol name table should be padded by a '\0', not the common '\n'
	 * for other members. The size of sn table includes the pad bit.
	 */
	if (bsdar->s_cnt != 0 && bsdar->s_sn_sz % 2 != 0)
		bsdar->s_sn[bsdar->s_sn_sz++] = '\0';

	/*
	 * Archive string table is padded by a "\n" as the normal members.
	 * The difference is that the size of archive string table counts
	 * in the pad bit, while normal members' size fileds do not.
	 */
	if (bsdar->as != NULL && bsdar->as_sz % 2 != 0)
		bsdar->as[bsdar->as_sz++] = '\n';

	/*
	 * If there is a symbol table, calculate the size of pseudo members,
	 * convert previously stored relative offsets to absolute ones, and
	 * then make them Big Endian.
	 *
	 * absolute_offset = htobe32(relative_offset + size_of_pseudo_members)
	 */

	if (bsdar->s_cnt != 0) {
		s_sz = (bsdar->s_cnt + 1) * sizeof(uint32_t) + bsdar->s_sn_sz;
		pm_sz = _ARMAG_LEN + (_ARHDR_LEN + s_sz);
		if (bsdar->as != NULL)
			pm_sz += _ARHDR_LEN + bsdar->as_sz;
		for (i = 0; (size_t)i < bsdar->s_cnt; i++) {
			if (*(bsdar->s_so + i) > UINT32_MAX - pm_sz)
				bsdar_errc(bsdar, EX_SOFTWARE, 0,
				    "Symbol table offset overflow");
			*(bsdar->s_so + i) = htobe32(*(bsdar->s_so + i) +
			    pm_sz);
		}
	}

	if ((a = archive_write_new()) == NULL)
		bsdar_errc(bsdar, EX_SOFTWARE, 0, "archive_write_new failed");

	archive_write_set_format_ar_svr4(a);

	AC(archive_write_open_filename(a, bsdar->filename));

	/*
	 * write the archive symbol table, if there is one.
	 * If options -s is explicitly specified or we are invoked
	 * as ranlib, write the symbol table even if it is empty.
	 */
	if ((bsdar->s_cnt != 0 && !(bsdar->options & AR_SS)) ||
	    bsdar->options & AR_S) {
		entry = archive_entry_new();
		if (entry == NULL)
			bsdar_errc(bsdar, EX_SOFTWARE, 0,
			    "archive_entry_new failed");
		archive_entry_copy_pathname(entry, "/");
		if ((bsdar->options & AR_D) == 0)
			archive_entry_set_mtime(entry, time(NULL), 0);
		archive_entry_set_size(entry, (bsdar->s_cnt + 1) *
		    sizeof(uint32_t) + bsdar->s_sn_sz);
		AC(archive_write_header(a, entry));
		nr = htobe32(bsdar->s_cnt);
		write_data(bsdar, a, &nr, sizeof(uint32_t));
		write_data(bsdar, a, bsdar->s_so, sizeof(uint32_t) *
		    bsdar->s_cnt);
		write_data(bsdar, a, bsdar->s_sn, bsdar->s_sn_sz);
		archive_entry_free(entry);
	}

	/* write the archive string table, if any. */
	if (bsdar->as != NULL) {
		entry = archive_entry_new();
		if (entry == NULL)
			bsdar_errc(bsdar, EX_SOFTWARE, 0,
			    "archive_entry_new failed");
		archive_entry_copy_pathname(entry, "//");
		archive_entry_set_size(entry, bsdar->as_sz);
		AC(archive_write_header(a, entry));
		write_data(bsdar, a, bsdar->as, bsdar->as_sz);
		archive_entry_free(entry);
	}

	/* write normal members. */
	TAILQ_FOREACH(obj, &bsdar->v_obj, objs) {
		entry = archive_entry_new();
		if (entry == NULL)
			bsdar_errc(bsdar, EX_SOFTWARE, 0,
			    "archive_entry_new failed");
		archive_entry_copy_pathname(entry, obj->name);
		archive_entry_set_uid(entry, obj->uid);
		archive_entry_set_gid(entry, obj->gid);
		archive_entry_set_mode(entry, obj->md);
		archive_entry_set_size(entry, obj->size);
		archive_entry_set_mtime(entry, obj->mtime, 0);
		archive_entry_set_dev(entry, obj->dev);
		archive_entry_set_ino(entry, obj->ino);
		archive_entry_set_filetype(entry, AE_IFREG);
		AC(archive_write_header(a, entry));
		write_data(bsdar, a, obj->maddr, obj->size);
		archive_entry_free(entry);
	}

	AC(archive_write_close(a));
	AC(archive_write_free(a));
}

/*
 * Extract global symbols from ELF binary members.
 */
static void
create_symtab_entry(struct bsdar *bsdar, void *maddr, size_t size)
{
	Elf		*e;
	Elf_Scn		*scn;
	GElf_Shdr	 shdr;
	GElf_Sym	 sym;
	Elf_Data	*data;
	char		*name;
	size_t		 n, shstrndx;
	int		 elferr, tabndx, len, i;

	if ((e = elf_memory(maddr, size)) == NULL) {
		bsdar_warnc(bsdar, 0, "elf_memory() failed: %s",
		     elf_errmsg(-1));
		return;
	}
	if (elf_kind(e) != ELF_K_ELF) {
		/* Silently ignore non-elf member. */
		elf_end(e);
		return;
	}
	if (elf_getshstrndx(e, &shstrndx) == 0) {
		bsdar_warnc(bsdar, EX_SOFTWARE, 0, "elf_getshstrndx failed: %s",
		     elf_errmsg(-1));
		elf_end(e);
		return;
	}

	tabndx = -1;
	scn = NULL;
	while ((scn = elf_nextscn(e, scn)) != NULL) {
		if (gelf_getshdr(scn, &shdr) != &shdr) {
			bsdar_warnc(bsdar, 0,
			    "elf_getshdr failed: %s", elf_errmsg(-1));
			continue;
		}
		if ((name = elf_strptr(e, shstrndx, shdr.sh_name)) == NULL) {
			bsdar_warnc(bsdar, 0,
			    "elf_strptr failed: %s", elf_errmsg(-1));
			continue;
		}
		if (strcmp(name, ".strtab") == 0) {
			tabndx = elf_ndxscn(scn);
			break;
		}
	}
	elferr = elf_errno();
	if (elferr != 0)
		bsdar_warnc(bsdar, 0, "elf_nextscn failed: %s",
		     elf_errmsg(elferr));
	if (tabndx == -1) {
		bsdar_warnc(bsdar, 0, "can't find .strtab section");
		elf_end(e);
		return;
	}

	scn = NULL;
	while ((scn = elf_nextscn(e, scn)) != NULL) {
		if (gelf_getshdr(scn, &shdr) != &shdr) {
			bsdar_warnc(bsdar, EX_SOFTWARE, 0,
			    "elf_getshdr failed: %s", elf_errmsg(-1));
			continue;
		}
		if (shdr.sh_type != SHT_SYMTAB)
			continue;

		data = NULL;
		n = 0;
		while (n < shdr.sh_size &&
		    (data = elf_getdata(scn, data)) != NULL) {
			len = data->d_size / shdr.sh_entsize;
			for (i = 0; i < len; i++) {
				if (gelf_getsym(data, i, &sym) != &sym) {
					bsdar_warnc(bsdar, EX_SOFTWARE, 0,
					    "gelf_getsym failed: %s",
					     elf_errmsg(-1));
					continue;
				}

				/* keep only global or weak symbols */
				if (GELF_ST_BIND(sym.st_info) != STB_GLOBAL &&
				    GELF_ST_BIND(sym.st_info) != STB_WEAK)
					continue;

				/* keep only defined symbols */
				if (sym.st_shndx == SHN_UNDEF)
					continue;

				if ((name = elf_strptr(e, tabndx,
				    sym.st_name)) == NULL) {
					bsdar_warnc(bsdar, EX_SOFTWARE, 0,
					    "elf_strptr failed: %s",
					     elf_errmsg(-1));
					continue;
				}

				add_to_ar_sym_table(bsdar, name);
			}
		}
	}
	elferr = elf_errno();
	if (elferr != 0)
		bsdar_warnc(bsdar, EX_SOFTWARE, 0, "elf_nextscn failed: %s",
		     elf_errmsg(elferr));

	elf_end(e);
}

/*
 * Append to the archive string table buffer.
 */
static void
add_to_ar_str_table(struct bsdar *bsdar, const char *name)
{

	if (bsdar->as == NULL) {
		bsdar->as_cap = _INIT_AS_CAP;
		bsdar->as_sz = 0;
		if ((bsdar->as = malloc(bsdar->as_cap)) == NULL)
			bsdar_errc(bsdar, EX_SOFTWARE, errno, "malloc failed");
	}

	/*
	 * The space required for holding one member name in as table includes:
	 * strlen(name) + (1 for '/') + (1 for '\n') + (possibly 1 for padding).
	 */
	while (bsdar->as_sz + strlen(name) + 3 > bsdar->as_cap) {
		bsdar->as_cap *= 2;
		bsdar->as = realloc(bsdar->as, bsdar->as_cap);
		if (bsdar->as == NULL)
			bsdar_errc(bsdar, EX_SOFTWARE, errno, "realloc failed");
	}
	strncpy(&bsdar->as[bsdar->as_sz], name, strlen(name));
	bsdar->as_sz += strlen(name);
	bsdar->as[bsdar->as_sz++] = '/';
	bsdar->as[bsdar->as_sz++] = '\n';
}

/*
 * Append to the archive symbol table buffer.
 */
static void
add_to_ar_sym_table(struct bsdar *bsdar, const char *name)
{

	if (bsdar->s_so == NULL) {
		if ((bsdar->s_so = malloc(_INIT_SYMOFF_CAP)) ==
		    NULL)
			bsdar_errc(bsdar, EX_SOFTWARE, errno, "malloc failed");
		bsdar->s_so_cap = _INIT_SYMOFF_CAP;
		bsdar->s_cnt = 0;
	}

	if (bsdar->s_sn == NULL) {
		if ((bsdar->s_sn = malloc(_INIT_SYMNAME_CAP)) == NULL)
			bsdar_errc(bsdar, EX_SOFTWARE, errno, "malloc failed");
		bsdar->s_sn_cap = _INIT_SYMNAME_CAP;
		bsdar->s_sn_sz = 0;
	}

	if (bsdar->s_cnt * sizeof(uint32_t) >= bsdar->s_so_cap) {
		bsdar->s_so_cap *= 2;
		bsdar->s_so = realloc(bsdar->s_so, bsdar->s_so_cap);
		if (bsdar->s_so == NULL)
			bsdar_errc(bsdar, EX_SOFTWARE, errno, "realloc failed");
	}
	bsdar->s_so[bsdar->s_cnt] = bsdar->rela_off;
	bsdar->s_cnt++;

	/*
	 * The space required for holding one symbol name in sn table includes:
	 * strlen(name) + (1 for '\n') + (possibly 1 for padding).
	 */
	while (bsdar->s_sn_sz + strlen(name) + 2 > bsdar->s_sn_cap) {
		bsdar->s_sn_cap *= 2;
		bsdar->s_sn = realloc(bsdar->s_sn, bsdar->s_sn_cap);
		if (bsdar->s_sn == NULL)
			bsdar_errc(bsdar, EX_SOFTWARE, errno, "realloc failed");
	}
	strncpy(&bsdar->s_sn[bsdar->s_sn_sz], name, strlen(name));
	bsdar->s_sn_sz += strlen(name);
	bsdar->s_sn[bsdar->s_sn_sz++] = '\0';
}
