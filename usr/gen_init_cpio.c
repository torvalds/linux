// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>

/*
 * Original work by Jeff Garzik
 *
 * External file lists, symlink, pipe and fifo support by Thayne Harbaugh
 * Hard link support by Luciano Rocha
 */

#define xstr(s) #s
#define str(s) xstr(s)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CPIO_HDR_LEN 110
#define CPIO_TRAILER "TRAILER!!!"
#define padlen(_off, _align) (((_align) - ((_off) & ((_align) - 1))) % (_align))

/* zero-padding the filename field for data alignment is limited by PATH_MAX */
static char padding[PATH_MAX];
static unsigned int offset;
static unsigned int ino = 721;
static time_t default_mtime;
static bool do_file_mtime;
static bool do_csum = false;
static int outfd = STDOUT_FILENO;
static unsigned int dalign;

struct file_handler {
	const char *type;
	int (*handler)(const char *line);
};

static int push_buf(const char *name, size_t name_len)
{
	ssize_t len;

	len = write(outfd, name, name_len);
	if (len != name_len)
		return -1;

	offset += name_len;
	return 0;
}

static int push_pad(size_t padlen)
{
	ssize_t len = 0;

	if (!padlen)
		return 0;

	if (padlen < sizeof(padding))
		len = write(outfd, padding, padlen);
	if (len != padlen)
		return -1;

	offset += padlen;
	return 0;
}

static int push_rest(const char *name, size_t name_len)
{
	ssize_t len;

	len = write(outfd, name, name_len);
	if (len != name_len)
		return -1;

	offset += name_len;

	return push_pad(padlen(name_len + CPIO_HDR_LEN, 4));
}

static int cpio_trailer(void)
{
	int len;
	unsigned int namesize = sizeof(CPIO_TRAILER);

	len = dprintf(outfd, "%s%08X%08X%08lX%08lX%08X%08lX"
	       "%08X%08X%08X%08X%08X%08X%08X",
		do_csum ? "070702" : "070701", /* magic */
		0,			/* ino */
		0,			/* mode */
		(long) 0,		/* uid */
		(long) 0,		/* gid */
		1,			/* nlink */
		(long) 0,		/* mtime */
		0,			/* filesize */
		0,			/* major */
		0,			/* minor */
		0,			/* rmajor */
		0,			/* rminor */
		namesize,		/* namesize */
		0);			/* chksum */
	offset += len;

	if (len != CPIO_HDR_LEN ||
	    push_rest(CPIO_TRAILER, namesize) < 0 ||
	    push_pad(padlen(offset, 512)) < 0)
		return -1;

	if (fsync(outfd) < 0 && errno != EINVAL)
		return -1;

	return 0;
}

static int cpio_mkslink(const char *name, const char *target,
			 unsigned int mode, uid_t uid, gid_t gid)
{
	int len;
	unsigned int namesize, targetsize = strlen(target) + 1;

	if (name[0] == '/')
		name++;
	namesize = strlen(name) + 1;

	len = dprintf(outfd, "%s%08X%08X%08lX%08lX%08X%08lX"
	       "%08X%08X%08X%08X%08X%08X%08X",
		do_csum ? "070702" : "070701", /* magic */
		ino++,			/* ino */
		S_IFLNK | mode,		/* mode */
		(long) uid,		/* uid */
		(long) gid,		/* gid */
		1,			/* nlink */
		(long) default_mtime,	/* mtime */
		targetsize,		/* filesize */
		3,			/* major */
		1,			/* minor */
		0,			/* rmajor */
		0,			/* rminor */
		namesize,		/* namesize */
		0);			/* chksum */
	offset += len;

	if (len != CPIO_HDR_LEN ||
	    push_buf(name, namesize) < 0 ||
	    push_pad(padlen(offset, 4)) < 0 ||
	    push_buf(target, targetsize) < 0 ||
	    push_pad(padlen(offset, 4)) < 0)
		return -1;

	return 0;

}

static int cpio_mkslink_line(const char *line)
{
	char name[PATH_MAX + 1];
	char target[PATH_MAX + 1];
	unsigned int mode;
	int uid;
	int gid;
	int rc = -1;

	if (5 != sscanf(line, "%" str(PATH_MAX) "s %" str(PATH_MAX) "s %o %d %d", name, target, &mode, &uid, &gid)) {
		fprintf(stderr, "Unrecognized dir format '%s'", line);
		goto fail;
	}
	rc = cpio_mkslink(name, target, mode, uid, gid);
 fail:
	return rc;
}

static int cpio_mkgeneric(const char *name, unsigned int mode,
		       uid_t uid, gid_t gid)
{
	int len;
	unsigned int namesize;

	if (name[0] == '/')
		name++;
	namesize = strlen(name) + 1;

	len = dprintf(outfd, "%s%08X%08X%08lX%08lX%08X%08lX"
	       "%08X%08X%08X%08X%08X%08X%08X",
		do_csum ? "070702" : "070701", /* magic */
		ino++,			/* ino */
		mode,			/* mode */
		(long) uid,		/* uid */
		(long) gid,		/* gid */
		2,			/* nlink */
		(long) default_mtime,	/* mtime */
		0,			/* filesize */
		3,			/* major */
		1,			/* minor */
		0,			/* rmajor */
		0,			/* rminor */
		namesize,		/* namesize */
		0);			/* chksum */
	offset += len;

	if (len != CPIO_HDR_LEN ||
	    push_rest(name, namesize) < 0)
		return -1;

	return 0;
}

enum generic_types {
	GT_DIR,
	GT_PIPE,
	GT_SOCK
};

struct generic_type {
	const char *type;
	mode_t mode;
};

static const struct generic_type generic_type_table[] = {
	[GT_DIR] = {
		.type = "dir",
		.mode = S_IFDIR
	},
	[GT_PIPE] = {
		.type = "pipe",
		.mode = S_IFIFO
	},
	[GT_SOCK] = {
		.type = "sock",
		.mode = S_IFSOCK
	}
};

static int cpio_mkgeneric_line(const char *line, enum generic_types gt)
{
	char name[PATH_MAX + 1];
	unsigned int mode;
	int uid;
	int gid;
	int rc = -1;

	if (4 != sscanf(line, "%" str(PATH_MAX) "s %o %d %d", name, &mode, &uid, &gid)) {
		fprintf(stderr, "Unrecognized %s format '%s'",
			line, generic_type_table[gt].type);
		goto fail;
	}
	mode |= generic_type_table[gt].mode;
	rc = cpio_mkgeneric(name, mode, uid, gid);
 fail:
	return rc;
}

static int cpio_mkdir_line(const char *line)
{
	return cpio_mkgeneric_line(line, GT_DIR);
}

static int cpio_mkpipe_line(const char *line)
{
	return cpio_mkgeneric_line(line, GT_PIPE);
}

static int cpio_mksock_line(const char *line)
{
	return cpio_mkgeneric_line(line, GT_SOCK);
}

static int cpio_mknod(const char *name, unsigned int mode,
		       uid_t uid, gid_t gid, char dev_type,
		       unsigned int maj, unsigned int min)
{
	int len;
	unsigned int namesize;

	if (dev_type == 'b')
		mode |= S_IFBLK;
	else
		mode |= S_IFCHR;

	if (name[0] == '/')
		name++;
	namesize = strlen(name) + 1;

	len = dprintf(outfd, "%s%08X%08X%08lX%08lX%08X%08lX"
	       "%08X%08X%08X%08X%08X%08X%08X",
		do_csum ? "070702" : "070701", /* magic */
		ino++,			/* ino */
		mode,			/* mode */
		(long) uid,		/* uid */
		(long) gid,		/* gid */
		1,			/* nlink */
		(long) default_mtime,	/* mtime */
		0,			/* filesize */
		3,			/* major */
		1,			/* minor */
		maj,			/* rmajor */
		min,			/* rminor */
		namesize,		/* namesize */
		0);			/* chksum */
	offset += len;

	if (len != CPIO_HDR_LEN ||
	    push_rest(name, namesize) < 0)
		return -1;

	return 0;
}

static int cpio_mknod_line(const char *line)
{
	char name[PATH_MAX + 1];
	unsigned int mode;
	int uid;
	int gid;
	char dev_type;
	unsigned int maj;
	unsigned int min;
	int rc = -1;

	if (7 != sscanf(line, "%" str(PATH_MAX) "s %o %d %d %c %u %u",
			 name, &mode, &uid, &gid, &dev_type, &maj, &min)) {
		fprintf(stderr, "Unrecognized nod format '%s'", line);
		goto fail;
	}
	rc = cpio_mknod(name, mode, uid, gid, dev_type, maj, min);
 fail:
	return rc;
}

static int cpio_mkfile_csum(int fd, unsigned long size, uint32_t *csum)
{
	while (size) {
		unsigned char filebuf[65536];
		ssize_t this_read;
		size_t i, this_size = MIN(size, sizeof(filebuf));

		this_read = read(fd, filebuf, this_size);
		if (this_read <= 0 || this_read > this_size)
			return -1;

		for (i = 0; i < this_read; i++)
			*csum += filebuf[i];

		size -= this_read;
	}
	/* seek back to the start for data segment I/O */
	if (lseek(fd, 0, SEEK_SET) < 0)
		return -1;

	return 0;
}

static int cpio_mkfile(const char *name, const char *location,
			unsigned int mode, uid_t uid, gid_t gid,
			unsigned int nlinks)
{
	struct stat buf;
	unsigned long size;
	int file, retval, len;
	int rc = -1;
	time_t mtime;
	int namesize, namepadlen;
	unsigned int i;
	uint32_t csum = 0;
	ssize_t this_read;

	mode |= S_IFREG;

	file = open (location, O_RDONLY);
	if (file < 0) {
		fprintf (stderr, "File %s could not be opened for reading\n", location);
		goto error;
	}

	retval = fstat(file, &buf);
	if (retval) {
		fprintf(stderr, "File %s could not be stat()'ed\n", location);
		goto error;
	}

	if (do_file_mtime) {
		mtime = default_mtime;
	} else {
		mtime = buf.st_mtime;
		if (mtime > 0xffffffff) {
			fprintf(stderr, "%s: Timestamp exceeds maximum cpio timestamp, clipping.\n",
					location);
			mtime = 0xffffffff;
		}

		if (mtime < 0) {
			fprintf(stderr, "%s: Timestamp negative, clipping.\n",
					location);
			mtime = 0;
		}
	}

	if (buf.st_size > 0xffffffff) {
		fprintf(stderr, "%s: Size exceeds maximum cpio file size\n",
			location);
		goto error;
	}

	if (do_csum && cpio_mkfile_csum(file, buf.st_size, &csum) < 0) {
		fprintf(stderr, "Failed to checksum file %s\n", location);
		goto error;
	}

	size = 0;
	namepadlen = 0;
	for (i = 1; i <= nlinks; i++) {
		if (name[0] == '/')
			name++;
		namesize = strlen(name) + 1;

		/* data goes on last link, after any alignment padding */
		if (i == nlinks)
			size = buf.st_size;

		if (dalign && size > dalign) {
			namepadlen = padlen(offset + CPIO_HDR_LEN + namesize,
					    dalign);
			if (namesize + namepadlen > PATH_MAX) {
				fprintf(stderr,
					"%s: best-effort alignment %u missed\n",
					name, dalign);
				namepadlen = 0;
			}
		}

		len = dprintf(outfd, "%s%08X%08X%08lX%08lX%08X%08lX"
		       "%08lX%08X%08X%08X%08X%08X%08X",
			do_csum ? "070702" : "070701", /* magic */
			ino,			/* ino */
			mode,			/* mode */
			(long) uid,		/* uid */
			(long) gid,		/* gid */
			nlinks,			/* nlink */
			(long) mtime,		/* mtime */
			size,			/* filesize */
			3,			/* major */
			1,			/* minor */
			0,			/* rmajor */
			0,			/* rminor */
			namesize + namepadlen,	/* namesize */
			size ? csum : 0);	/* chksum */
		offset += len;

		if (len != CPIO_HDR_LEN ||
		    push_buf(name, namesize) < 0 ||
		    push_pad(namepadlen ? namepadlen : padlen(offset, 4)) < 0)
			goto error;

		if (size) {
			this_read = copy_file_range(file, NULL, outfd, NULL, size, 0);
			if (this_read > 0) {
				if (this_read > size)
					goto error;
				offset += this_read;
				size -= this_read;
			}
			/* short or failed copy falls back to read/write... */
		}

		while (size) {
			unsigned char filebuf[65536];
			size_t this_size = MIN(size, sizeof(filebuf));

			this_read = read(file, filebuf, this_size);
			if (this_read <= 0 || this_read > this_size) {
				fprintf(stderr, "Can not read %s file\n", location);
				goto error;
			}

			if (write(outfd, filebuf, this_read) != this_read) {
				fprintf(stderr, "writing filebuf failed\n");
				goto error;
			}
			offset += this_read;
			size -= this_read;
		}
		if (push_pad(padlen(offset, 4)) < 0)
			goto error;

		name += namesize;
	}
	ino++;
	rc = 0;

error:
	if (file >= 0)
		close(file);
	return rc;
}

static char *cpio_replace_env(char *new_location)
{
	char expanded[PATH_MAX + 1];
	char *start, *end, *var;

	while ((start = strstr(new_location, "${")) &&
	       (end = strchr(start + 2, '}'))) {
		*start = *end = 0;
		var = getenv(start + 2);
		snprintf(expanded, sizeof expanded, "%s%s%s",
			 new_location, var ? var : "", end + 1);
		strcpy(new_location, expanded);
	}

	return new_location;
}

static int cpio_mkfile_line(const char *line)
{
	char name[PATH_MAX + 1];
	char *dname = NULL; /* malloc'ed buffer for hard links */
	char location[PATH_MAX + 1];
	unsigned int mode;
	int uid;
	int gid;
	int nlinks = 1;
	int end = 0, dname_len = 0;
	int rc = -1;

	if (5 > sscanf(line, "%" str(PATH_MAX) "s %" str(PATH_MAX)
				"s %o %d %d %n",
				name, location, &mode, &uid, &gid, &end)) {
		fprintf(stderr, "Unrecognized file format '%s'", line);
		goto fail;
	}
	if (end && isgraph(line[end])) {
		int len;
		int nend;

		dname = malloc(strlen(line));
		if (!dname) {
			fprintf (stderr, "out of memory (%d)\n", dname_len);
			goto fail;
		}

		dname_len = strlen(name) + 1;
		memcpy(dname, name, dname_len);

		do {
			nend = 0;
			if (sscanf(line + end, "%" str(PATH_MAX) "s %n",
					name, &nend) < 1)
				break;
			len = strlen(name) + 1;
			memcpy(dname + dname_len, name, len);
			dname_len += len;
			nlinks++;
			end += nend;
		} while (isgraph(line[end]));
	} else {
		dname = name;
	}
	rc = cpio_mkfile(dname, cpio_replace_env(location),
	                 mode, uid, gid, nlinks);
 fail:
	if (dname_len) free(dname);
	return rc;
}

static void usage(const char *prog)
{
	fprintf(stderr, "Usage:\n"
		"\t%s [-t <timestamp>] [-c] [-o <output_file>] [-a <data_align>] <cpio_list>\n"
		"\n"
		"<cpio_list> is a file containing newline separated entries that\n"
		"describe the files to be included in the initramfs archive:\n"
		"\n"
		"# a comment\n"
		"file <name> <location> <mode> <uid> <gid> [<hard links>]\n"
		"dir <name> <mode> <uid> <gid>\n"
		"nod <name> <mode> <uid> <gid> <dev_type> <maj> <min>\n"
		"slink <name> <target> <mode> <uid> <gid>\n"
		"pipe <name> <mode> <uid> <gid>\n"
		"sock <name> <mode> <uid> <gid>\n"
		"\n"
		"<name>       name of the file/dir/nod/etc in the archive\n"
		"<location>   location of the file in the current filesystem\n"
		"             expands shell variables quoted with ${}\n"
		"<target>     link target\n"
		"<mode>       mode/permissions of the file\n"
		"<uid>        user id (0=root)\n"
		"<gid>        group id (0=root)\n"
		"<dev_type>   device type (b=block, c=character)\n"
		"<maj>        major number of nod\n"
		"<min>        minor number of nod\n"
		"<hard links> space separated list of other links to file\n"
		"\n"
		"example:\n"
		"# A simple initramfs\n"
		"dir /dev 0755 0 0\n"
		"nod /dev/console 0600 0 0 c 5 1\n"
		"dir /root 0700 0 0\n"
		"dir /sbin 0755 0 0\n"
		"file /sbin/kinit /usr/src/klibc/kinit/kinit 0755 0 0\n"
		"\n"
		"<timestamp> is time in seconds since Epoch that will be used\n"
		"as mtime for symlinks, directories, regular and special files.\n"
		"The default is to use the current time for all files, but\n"
		"preserve modification time for regular files.\n"
		"-c: calculate and store 32-bit checksums for file data.\n"
		"<output_file>: write cpio to this file instead of stdout\n"
		"<data_align>: attempt to align file data by zero-padding the\n"
		"filename field up to data_align. Must be a multiple of 4.\n"
		"Alignment is best-effort; PATH_MAX limits filename padding.\n",
		prog);
}

static const struct file_handler file_handler_table[] = {
	{
		.type    = "file",
		.handler = cpio_mkfile_line,
	}, {
		.type    = "nod",
		.handler = cpio_mknod_line,
	}, {
		.type    = "dir",
		.handler = cpio_mkdir_line,
	}, {
		.type    = "slink",
		.handler = cpio_mkslink_line,
	}, {
		.type    = "pipe",
		.handler = cpio_mkpipe_line,
	}, {
		.type    = "sock",
		.handler = cpio_mksock_line,
	}, {
		.type    = NULL,
		.handler = NULL,
	}
};

#define LINE_SIZE (2 * PATH_MAX + 50)

int main (int argc, char *argv[])
{
	FILE *cpio_list;
	char line[LINE_SIZE];
	char *args, *type;
	int ec = 0;
	int line_nr = 0;
	const char *filename;

	default_mtime = time(NULL);
	while (1) {
		int opt = getopt(argc, argv, "t:cho:a:");
		char *invalid;

		if (opt == -1)
			break;
		switch (opt) {
		case 't':
			default_mtime = strtol(optarg, &invalid, 10);
			if (!*optarg || *invalid) {
				fprintf(stderr, "Invalid timestamp: %s\n",
						optarg);
				usage(argv[0]);
				exit(1);
			}
			do_file_mtime = true;
			break;
		case 'c':
			do_csum = true;
			break;
		case 'o':
			outfd = open(optarg,
				     O_WRONLY | O_CREAT | O_LARGEFILE | O_TRUNC,
				     0600);
			if (outfd < 0) {
				fprintf(stderr, "failed to open %s\n", optarg);
				usage(argv[0]);
				exit(1);
			}
			break;
		case 'a':
			dalign = strtoul(optarg, &invalid, 10);
			if (!*optarg || *invalid || (dalign & 3)) {
				fprintf(stderr, "Invalid data_align: %s\n",
						optarg);
				usage(argv[0]);
				exit(1);
			}
			break;
		case 'h':
		case '?':
			usage(argv[0]);
			exit(opt == 'h' ? 0 : 1);
		}
	}

	/*
	 * Timestamps after 2106-02-07 06:28:15 UTC have an ascii hex time_t
	 * representation that exceeds 8 chars and breaks the cpio header
	 * specification. Negative timestamps similarly exceed 8 chars.
	 */
	if (default_mtime > 0xffffffff || default_mtime < 0) {
		fprintf(stderr, "ERROR: Timestamp out of range for cpio format\n");
		exit(1);
	}

	if (argc - optind != 1) {
		usage(argv[0]);
		exit(1);
	}
	filename = argv[optind];
	if (!strcmp(filename, "-"))
		cpio_list = stdin;
	else if (!(cpio_list = fopen(filename, "r"))) {
		fprintf(stderr, "ERROR: unable to open '%s': %s\n\n",
			filename, strerror(errno));
		usage(argv[0]);
		exit(1);
	}

	while (fgets(line, LINE_SIZE, cpio_list)) {
		int type_idx;
		size_t slen = strlen(line);

		line_nr++;

		if ('#' == *line) {
			/* comment - skip to next line */
			continue;
		}

		if (! (type = strtok(line, " \t"))) {
			fprintf(stderr,
				"ERROR: incorrect format, could not locate file type line %d: '%s'\n",
				line_nr, line);
			ec = -1;
			break;
		}

		if ('\n' == *type) {
			/* a blank line */
			continue;
		}

		if (slen == strlen(type)) {
			/* must be an empty line */
			continue;
		}

		if (! (args = strtok(NULL, "\n"))) {
			fprintf(stderr,
				"ERROR: incorrect format, newline required line %d: '%s'\n",
				line_nr, line);
			ec = -1;
		}

		for (type_idx = 0; file_handler_table[type_idx].type; type_idx++) {
			int rc;
			if (! strcmp(line, file_handler_table[type_idx].type)) {
				if ((rc = file_handler_table[type_idx].handler(args))) {
					ec = rc;
					fprintf(stderr, " line %d\n", line_nr);
				}
				break;
			}
		}

		if (NULL == file_handler_table[type_idx].type) {
			fprintf(stderr, "unknown file type line %d: '%s'\n",
				line_nr, line);
		}
	}
	if (ec == 0)
		ec = cpio_trailer();

	exit(ec);
}
