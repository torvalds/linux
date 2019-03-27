/*-
 * Copyright (c) 2013,2014 Juniper Networks, Inc.
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

#include <sys/param.h>
#include <sys/stat.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <getopt.h>
#include <libutil.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "image.h"
#include "format.h"
#include "mkimg.h"
#include "scheme.h"

#define	LONGOPT_FORMATS		0x01000001
#define	LONGOPT_SCHEMES		0x01000002
#define	LONGOPT_VERSION		0x01000003
#define	LONGOPT_CAPACITY	0x01000004

static struct option longopts[] = {
	{ "formats", no_argument, NULL, LONGOPT_FORMATS },
	{ "schemes", no_argument, NULL, LONGOPT_SCHEMES },
	{ "version", no_argument, NULL, LONGOPT_VERSION },
	{ "capacity", required_argument, NULL, LONGOPT_CAPACITY },
	{ NULL, 0, NULL, 0 }
};

static uint64_t min_capacity = 0;
static uint64_t max_capacity = 0;

struct partlisthead partlist = TAILQ_HEAD_INITIALIZER(partlist);
u_int nparts = 0;

u_int unit_testing;
u_int verbose;

u_int ncyls = 0;
u_int nheads = 1;
u_int nsecs = 1;
u_int secsz = 512;
u_int blksz = 0;
uint32_t active_partition = 0;

static void
print_formats(int usage)
{
	struct mkimg_format *f;
	const char *sep;

	if (usage) {
		fprintf(stderr, "    formats:\n");
		f = NULL;
		while ((f = format_iterate(f)) != NULL) {
			fprintf(stderr, "\t%s\t-  %s\n", f->name,
			    f->description);
		}
	} else {
		sep = "";
		f = NULL;
		while ((f = format_iterate(f)) != NULL) {
			printf("%s%s", sep, f->name);
			sep = " ";
		}
		putchar('\n');
	}
}

static void
print_schemes(int usage)
{
	struct mkimg_scheme *s;
	const char *sep;

	if (usage) {
		fprintf(stderr, "    schemes:\n");
		s = NULL;
		while ((s = scheme_iterate(s)) != NULL) {
			fprintf(stderr, "\t%s\t-  %s\n", s->name,
			    s->description);
		}
	} else {
		sep = "";
		s = NULL;
		while ((s = scheme_iterate(s)) != NULL) {
			printf("%s%s", sep, s->name);
			sep = " ";
		}
		putchar('\n');
	}
}

static void
print_version(void)
{
	u_int width;

#ifdef __LP64__
	width = 64;
#else
	width = 32;
#endif
	printf("mkimg %u (%u-bit)\n", MKIMG_VERSION, width);
}

static void
usage(const char *why)
{

	warnx("error: %s", why);
	fputc('\n', stderr);
	fprintf(stderr, "usage: %s <options>\n", getprogname());

	fprintf(stderr, "    options:\n");
	fprintf(stderr, "\t--formats\t-  list image formats\n");
	fprintf(stderr, "\t--schemes\t-  list partition schemes\n");
	fprintf(stderr, "\t--version\t-  show version information\n");
	fputc('\n', stderr);
	fprintf(stderr, "\t-a <num>\t-  mark num'th partion as active\n");
	fprintf(stderr, "\t-b <file>\t-  file containing boot code\n");
	fprintf(stderr, "\t-c <num>\t-  minimum capacity (in bytes) of the disk\n");
	fprintf(stderr, "\t-C <num>\t-  maximum capacity (in bytes) of the disk\n");
	fprintf(stderr, "\t-f <format>\n");
	fprintf(stderr, "\t-o <file>\t-  file to write image into\n");
	fprintf(stderr, "\t-p <partition>\n");
	fprintf(stderr, "\t-s <scheme>\n");
	fprintf(stderr, "\t-v\t\t-  increase verbosity\n");
	fprintf(stderr, "\t-y\t\t-  [developers] enable unit test\n");
	fprintf(stderr, "\t-H <num>\t-  number of heads to simulate\n");
	fprintf(stderr, "\t-P <num>\t-  physical sector size\n");
	fprintf(stderr, "\t-S <num>\t-  logical sector size\n");
	fprintf(stderr, "\t-T <num>\t-  number of tracks to simulate\n");
	fputc('\n', stderr);
	print_formats(1);
	fputc('\n', stderr);
	print_schemes(1);
	fputc('\n', stderr);
	fprintf(stderr, "    partition specification:\n");
	fprintf(stderr, "\t<t>[/<l>]::<size>[:[+]<offset>]\t-  "
	    "empty partition of given size and\n\t\t\t\t\t"
	    "   optional relative or absolute offset\n");
	fprintf(stderr, "\t<t>[/<l>]:=<file>\t\t-  partition content and size "
	    "are\n\t\t\t\t\t   determined by the named file\n");
	fprintf(stderr, "\t<t>[/<l>]:-<cmd>\t\t-  partition content and size "
	    "are taken\n\t\t\t\t\t   from the output of the command to run\n");
	fprintf(stderr, "\t-\t\t\t\t-  unused partition entry\n");
	fprintf(stderr, "\t    where:\n");
	fprintf(stderr, "\t\t<t>\t-  scheme neutral partition type\n");
	fprintf(stderr, "\t\t<l>\t-  optional scheme-dependent partition "
	    "label\n");

	exit(EX_USAGE);
}

static int
parse_uint32(uint32_t *valp, uint32_t min, uint32_t max, const char *arg)
{
	uint64_t val;

	if (expand_number(arg, &val) == -1)
		return (errno);
	if (val > UINT_MAX || val < (uint64_t)min || val > (uint64_t)max)
		return (EINVAL);
	*valp = (uint32_t)val;
	return (0);
}

static int
parse_uint64(uint64_t *valp, uint64_t min, uint64_t max, const char *arg)
{
	uint64_t val;

	if (expand_number(arg, &val) == -1)
		return (errno);
	if (val < min || val > max)
		return (EINVAL);
	*valp = val;
	return (0);
}

static int
pwr_of_two(u_int nr)
{

	return (((nr & (nr - 1)) == 0) ? 1 : 0);
}

/*
 * A partition specification has the following format:
 *	<type> ':' <kind> <contents>
 * where:
 *	type	  the partition type alias
 *	kind	  the interpretation of the contents specification
 *		  ':'   contents holds the size of an empty partition
 *		  '='   contents holds the name of a file to read
 *		  '-'   contents holds a command to run; the output of
 *			which is the contents of the partition.
 *	contents  the specification of a partition's contents
 *
 * A specification that is a single dash indicates an unused partition
 * entry.
 */
static int
parse_part(const char *spec)
{
	struct part *part;
	char *sep;
	size_t len;
	int error;

	if (strcmp(spec, "-") == 0) {
		nparts++;
		return (0);
	}

	part = calloc(1, sizeof(struct part));
	if (part == NULL)
		return (ENOMEM);

	sep = strchr(spec, ':');
	if (sep == NULL) {
		error = EINVAL;
		goto errout;
	}
	len = sep - spec + 1;
	if (len < 2) {
		error = EINVAL;
		goto errout;
	}
	part->alias = malloc(len);
	if (part->alias == NULL) {
		error = ENOMEM;
		goto errout;
	}
	strlcpy(part->alias, spec, len);
	spec = sep + 1;

	switch (*spec) {
	case ':':
		part->kind = PART_KIND_SIZE;
		break;
	case '=':
		part->kind = PART_KIND_FILE;
		break;
	case '-':
		part->kind = PART_KIND_PIPE;
		break;
	default:
		error = EINVAL;
		goto errout;
	}
	spec++;

	part->contents = strdup(spec);
	if (part->contents == NULL) {
		error = ENOMEM;
		goto errout;
	}

	spec = part->alias;
	sep = strchr(spec, '/');
	if (sep != NULL) {
		*sep++ = '\0';
		if (strlen(part->alias) == 0 || strlen(sep) == 0) {
			error = EINVAL;
			goto errout;
		}
		part->label = strdup(sep);
		if (part->label == NULL) {
			error = ENOMEM;
			goto errout;
		}
	}

	part->index = nparts;
	TAILQ_INSERT_TAIL(&partlist, part, link);
	nparts++;
	return (0);

 errout:
	if (part->alias != NULL)
		free(part->alias);
	free(part);
	return (error);
}

#if defined(SPARSE_WRITE)
ssize_t
sparse_write(int fd, const void *ptr, size_t sz)
{
	const char *buf, *p;
	off_t ofs;
	size_t len;
	ssize_t wr, wrsz;

	buf = ptr;
	wrsz = 0;
	p = memchr(buf, 0, sz);
	while (sz > 0) {
		len = (p != NULL) ? (size_t)(p - buf) : sz;
		if (len > 0) {
			len = (len + secsz - 1) & ~(secsz - 1);
			if (len > sz)
				len = sz;
			wr = write(fd, buf, len);
			if (wr < 0)
				return (-1);
		} else {
			while (len < sz && *p++ == '\0')
				len++;
			if (len < sz)
				len &= ~(secsz - 1);
			if (len == 0)
				continue;
			ofs = lseek(fd, len, SEEK_CUR);
			if (ofs < 0)
				return (-1);
			wr = len;
		}
		buf += wr;
		sz -= wr;
		wrsz += wr;
		p = memchr(buf, 0, sz);
	}
	return (wrsz);
}
#endif /* SPARSE_WRITE */

void
mkimg_chs(lba_t lba, u_int maxcyl, u_int *cylp, u_int *hdp, u_int *secp)
{
	u_int hd, sec;

	*cylp = *hdp = *secp = ~0U;
	if (nsecs == 1 || nheads == 1)
		return;

	sec = lba % nsecs + 1;
	lba /= nsecs;
	hd = lba % nheads;
	lba /= nheads;
	if (lba > maxcyl)
		return;

	*cylp = lba;
	*hdp = hd;
	*secp = sec;
}

static int
capacity_resize(lba_t end)
{
	lba_t min_capsz, max_capsz;

	min_capsz = (min_capacity + secsz - 1) / secsz;
	max_capsz = (max_capacity + secsz - 1) / secsz;

	if (max_capsz != 0 && end > max_capsz)
		return (ENOSPC);
	if (end >= min_capsz)
		return (0);

	return (image_set_size(min_capsz));
}

static void
mkimg_validate(void)
{
	struct part *part, *part2;
	lba_t start, end, start2, end2;
	int i, j;

	i = 0;

	TAILQ_FOREACH(part, &partlist, link) {
		start = part->block;
		end = part->block + part->size;
		j = i + 1;
		part2 = TAILQ_NEXT(part, link);
		if (part2 == NULL)
			break;

		TAILQ_FOREACH_FROM(part2, &partlist, link) {
			start2 = part2->block;
			end2 = part2->block + part2->size;

			if ((start >= start2 && start < end2) ||
			    (end > start2 && end <= end2)) {
				errx(1, "partition %d overlaps partition %d",
				    i, j);
			}

			j++;
		}

		i++;
	}
}

static void
mkimg(void)
{
	FILE *fp;
	struct part *part;
	lba_t block, blkoffset;
	off_t bytesize, byteoffset;
	char *size, *offset;
	bool abs_offset;
	int error, fd;

	/* First check partition information */
	TAILQ_FOREACH(part, &partlist, link) {
		error = scheme_check_part(part);
		if (error)
			errc(EX_DATAERR, error, "partition %d", part->index+1);
	}

	block = scheme_metadata(SCHEME_META_IMG_START, 0);
	abs_offset = false;
	TAILQ_FOREACH(part, &partlist, link) {
		byteoffset = blkoffset = 0;
		abs_offset = false;

		/* Look for an offset. Set size too if we can. */
		switch (part->kind) {
		case PART_KIND_SIZE:
			offset = part->contents;
			size = strsep(&offset, ":");
			if (expand_number(size, &bytesize) == -1)
				error = errno;
			if (offset != NULL) {
				if (*offset != '+')
					abs_offset = true;
				else
					offset++;
				if (expand_number(offset, &byteoffset) == -1)
					error = errno;
			}
			break;
		}

		/* Work out exactly where the partition starts. */
		blkoffset = (byteoffset + secsz - 1) / secsz;
		if (abs_offset) {
			part->block = scheme_metadata(SCHEME_META_PART_ABSOLUTE,
			    blkoffset);
		} else {
			block = scheme_metadata(SCHEME_META_PART_BEFORE,
			    block + blkoffset);
			part->block = block;
		}

		if (verbose)
			fprintf(stderr, "partition %d: starting block %llu "
			    "... ", part->index + 1, (long long)part->block);

		/* Pull in partition contents, set size if we haven't yet. */
		switch (part->kind) {
		case PART_KIND_FILE:
			fd = open(part->contents, O_RDONLY, 0);
			if (fd != -1) {
				error = image_copyin(block, fd, &bytesize);
				close(fd);
			} else
				error = errno;
			break;
		case PART_KIND_PIPE:
			fp = popen(part->contents, "r");
			if (fp != NULL) {
				fd = fileno(fp);
				error = image_copyin(block, fd, &bytesize);
				pclose(fp);
			} else
				error = errno;
			break;
		}
		if (error)
			errc(EX_IOERR, error, "partition %d", part->index + 1);
		part->size = (bytesize + secsz - 1) / secsz;
		if (verbose) {
			bytesize = part->size * secsz;
			fprintf(stderr, "size %llu bytes (%llu blocks)\n",
			     (long long)bytesize, (long long)part->size);
			if (abs_offset) {
				fprintf(stderr,
				    "    location %llu bytes (%llu blocks)\n",
				    (long long)byteoffset,
				    (long long)blkoffset);
			} else if (blkoffset > 0) {
				fprintf(stderr,
				    "    offset %llu bytes (%llu blocks)\n",
				    (long long)byteoffset,
				    (long long)blkoffset);
			}
		}
		if (!abs_offset) {
			block = scheme_metadata(SCHEME_META_PART_AFTER,
			    part->block + part->size);
		}
	}

	mkimg_validate();

	block = scheme_metadata(SCHEME_META_IMG_END, block);
	error = image_set_size(block);
	if (!error) {
		error = capacity_resize(block);
		block = image_get_size();
	}
	if (!error) {
		error = format_resize(block);
		block = image_get_size();
	}
	if (error)
		errc(EX_IOERR, error, "image sizing");
	ncyls = block / (nsecs * nheads);
	error = scheme_write(block);
	if (error)
		errc(EX_IOERR, error, "writing metadata");
}

int
main(int argc, char *argv[])
{
	int bcfd, outfd;
	int c, error;

	bcfd = -1;
	outfd = 1;	/* Write to stdout by default */
	while ((c = getopt_long(argc, argv, "a:b:c:C:f:o:p:s:vyH:P:S:T:",
	    longopts, NULL)) != -1) {
		switch (c) {
		case 'a':	/* ACTIVE PARTITION, if supported */
			error = parse_uint32(&active_partition, 1, 100, optarg);
			if (error)
				errc(EX_DATAERR, error, "Partition ordinal");
			break;
		case 'b':	/* BOOT CODE */
			if (bcfd != -1)
				usage("multiple bootcode given");
			bcfd = open(optarg, O_RDONLY, 0);
			if (bcfd == -1)
				err(EX_UNAVAILABLE, "%s", optarg);
			break;
		case 'c':	/* MINIMUM CAPACITY */
			error = parse_uint64(&min_capacity, 1, INT64_MAX, optarg);
			if (error)
				errc(EX_DATAERR, error, "minimum capacity in bytes");
			break;
		case 'C':	/* MAXIMUM CAPACITY */
			error = parse_uint64(&max_capacity, 1, INT64_MAX, optarg);
			if (error)
				errc(EX_DATAERR, error, "maximum capacity in bytes");
			break;
		case 'f':	/* OUTPUT FORMAT */
			if (format_selected() != NULL)
				usage("multiple formats given");
			error = format_select(optarg);
			if (error)
				errc(EX_DATAERR, error, "format");
			break;
		case 'o':	/* OUTPUT FILE */
			if (outfd != 1)
				usage("multiple output files given");
			outfd = open(optarg, O_WRONLY | O_CREAT | O_TRUNC,
			    S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
			if (outfd == -1)
				err(EX_CANTCREAT, "%s", optarg);
			break;
		case 'p':	/* PARTITION */
			error = parse_part(optarg);
			if (error)
				errc(EX_DATAERR, error, "partition");
			break;
		case 's':	/* SCHEME */
			if (scheme_selected() != NULL)
				usage("multiple schemes given");
			error = scheme_select(optarg);
			if (error)
				errc(EX_DATAERR, error, "scheme");
			break;
		case 'y':
			unit_testing++;
			break;
		case 'v':
			verbose++;
			break;
		case 'H':	/* GEOMETRY: HEADS */
			error = parse_uint32(&nheads, 1, 255, optarg);
			if (error)
				errc(EX_DATAERR, error, "number of heads");
			break;
		case 'P':	/* GEOMETRY: PHYSICAL SECTOR SIZE */
			error = parse_uint32(&blksz, 512, INT_MAX+1U, optarg);
			if (error == 0 && !pwr_of_two(blksz))
				error = EINVAL;
			if (error)
				errc(EX_DATAERR, error, "physical sector size");
			break;
		case 'S':	/* GEOMETRY: LOGICAL SECTOR SIZE */
			error = parse_uint32(&secsz, 512, INT_MAX+1U, optarg);
			if (error == 0 && !pwr_of_two(secsz))
				error = EINVAL;
			if (error)
				errc(EX_DATAERR, error, "logical sector size");
			break;
		case 'T':	/* GEOMETRY: TRACK SIZE */
			error = parse_uint32(&nsecs, 1, 63, optarg);
			if (error)
				errc(EX_DATAERR, error, "track size");
			break;
		case LONGOPT_FORMATS:
			print_formats(0);
			exit(EX_OK);
			/*NOTREACHED*/
		case LONGOPT_SCHEMES:
			print_schemes(0);
			exit(EX_OK);
			/*NOTREACHED*/
		case LONGOPT_VERSION:
			print_version();
			exit(EX_OK);
			/*NOTREACHED*/
		case LONGOPT_CAPACITY:
			error = parse_uint64(&min_capacity, 1, INT64_MAX, optarg);
			if (error)
				errc(EX_DATAERR, error, "capacity in bytes");
			max_capacity = min_capacity;
			break;
		default:
			usage("unknown option");
		}
	}

	if (argc > optind)
		usage("trailing arguments");
	if (scheme_selected() == NULL && nparts > 0)
		usage("no scheme");
	if (nparts == 0 && min_capacity == 0)
		usage("no partitions");
	if (max_capacity != 0 && min_capacity > max_capacity)
		usage("minimum capacity cannot be larger than the maximum one");

	if (secsz > blksz) {
		if (blksz != 0)
			errx(EX_DATAERR, "the physical block size cannot "
			    "be smaller than the sector size");
		blksz = secsz;
	}

	if (secsz > scheme_max_secsz())
		errx(EX_DATAERR, "maximum sector size supported is %u; "
		    "size specified is %u", scheme_max_secsz(), secsz);

	if (nparts > scheme_max_parts())
		errx(EX_DATAERR, "%d partitions supported; %d given",
		    scheme_max_parts(), nparts);

	if (format_selected() == NULL)
		format_select("raw");

	if (bcfd != -1) {
		error = scheme_bootcode(bcfd);
		close(bcfd);
		if (error)
			errc(EX_DATAERR, error, "boot code");
	}

	if (verbose) {
		fprintf(stderr, "Logical sector size: %u\n", secsz);
		fprintf(stderr, "Physical block size: %u\n", blksz);
		fprintf(stderr, "Sectors per track:   %u\n", nsecs);
		fprintf(stderr, "Number of heads:     %u\n", nheads);
		fputc('\n', stderr);
		if (scheme_selected())
			fprintf(stderr, "Partitioning scheme: %s\n",
			    scheme_selected()->name);
		fprintf(stderr, "Output file format:  %s\n",
		    format_selected()->name);
		fputc('\n', stderr);
	}

	error = image_init();
	if (error)
		errc(EX_OSERR, error, "cannot initialize");

	mkimg();

	if (verbose) {
		fputc('\n', stderr);
		fprintf(stderr, "Number of cylinders: %u\n", ncyls);
	}

	error = format_write(outfd);
	if (error)
		errc(EX_IOERR, error, "writing image");

	return (0);
}
