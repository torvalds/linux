/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2003 Poul-Henning Kamp
 * Copyright (c) 2015 Spectra Logic Corporation
 * Copyright (c) 2017 Alexander Motin <mav@FreeBSD.org>
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
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <libutil.h>
#include <paths.h>
#include <err.h>
#include <geom/geom_disk.h>
#include <sysexits.h>
#include <sys/aio.h>
#include <sys/disk.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>

#define	NAIO	128
#define	MAXTX	(8*1024*1024)
#define	MEGATX	(1024*1024)

static void
usage(void)
{
	fprintf(stderr, "usage: diskinfo [-cipsStvw] disk ...\n");
	exit (1);
}

static int opt_c, opt_i, opt_p, opt_s, opt_S, opt_t, opt_v, opt_w;

static bool candelete(int fd);
static void speeddisk(int fd, off_t mediasize, u_int sectorsize);
static void commandtime(int fd, off_t mediasize, u_int sectorsize);
static void iopsbench(int fd, off_t mediasize, u_int sectorsize);
static void rotationrate(int fd, char *buf, size_t buflen);
static void slogbench(int fd, int isreg, off_t mediasize, u_int sectorsize);
static int zonecheck(int fd, uint32_t *zone_mode, char *zone_str,
		     size_t zone_str_len);

static uint8_t *buf;

int
main(int argc, char **argv)
{
	struct stat sb;
	int i, ch, fd, error, exitval = 0;
	char tstr[BUFSIZ], ident[DISK_IDENT_SIZE], physpath[MAXPATHLEN];
	char zone_desc[64];
	char rrate[64];
	struct diocgattr_arg arg;
	off_t	mediasize, stripesize, stripeoffset;
	u_int	sectorsize, fwsectors, fwheads, zoned = 0, isreg;
	uint32_t zone_mode;

	while ((ch = getopt(argc, argv, "cipsStvw")) != -1) {
		switch (ch) {
		case 'c':
			opt_c = 1;
			opt_v = 1;
			break;
		case 'i':
			opt_i = 1;
			opt_v = 1;
			break;
		case 'p':
			opt_p = 1;
			break;
		case 's':
			opt_s = 1;
			break;
		case 'S':
			opt_S = 1;
			opt_v = 1;
			break;
		case 't':
			opt_t = 1;
			opt_v = 1;
			break;
		case 'v':
			opt_v = 1;
			break;
		case 'w':
			opt_w = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1)
		usage();

	if ((opt_p && opt_s) || ((opt_p || opt_s) && (opt_c || opt_i || opt_t || opt_v))) {
		warnx("-p or -s cannot be used with other options");
		usage();
	}

	if (opt_S && !opt_w) {
		warnx("-S require also -w");
		usage();
	}

	if (posix_memalign((void **)&buf, PAGE_SIZE, MAXTX))
		errx(1, "Can't allocate memory buffer");
	for (i = 0; i < argc; i++) {
		fd = open(argv[i], (opt_w ? O_RDWR : O_RDONLY) | O_DIRECT);
		if (fd < 0 && errno == ENOENT && *argv[i] != '/') {
			snprintf(tstr, sizeof(tstr), "%s%s", _PATH_DEV, argv[i]);
			fd = open(tstr, O_RDONLY);
		}
		if (fd < 0) {
			warn("%s", argv[i]);
			exit(1);
		}
		error = fstat(fd, &sb);
		if (error != 0) {
			warn("cannot stat %s", argv[i]);
			exitval = 1;
			goto out;
		}
		isreg = S_ISREG(sb.st_mode);
		if (isreg) {
			mediasize = sb.st_size;
			sectorsize = S_BLKSIZE;
			fwsectors = 0;
			fwheads = 0;
			stripesize = sb.st_blksize;
			stripeoffset = 0;
			if (opt_p || opt_s) {
				warnx("-p and -s only operate on physical devices: %s", argv[i]);
				goto out;
			}
		} else {
			if (opt_p) {
				if (ioctl(fd, DIOCGPHYSPATH, physpath) == 0) {
					printf("%s\n", physpath);
				} else {
					warnx("Failed to determine physpath for: %s", argv[i]);
				}
				goto out;
			}
			if (opt_s) {
				if (ioctl(fd, DIOCGIDENT, ident) == 0) {
					printf("%s\n", ident);
				} else {
					warnx("Failed to determine serial number for: %s", argv[i]);
				}
				goto out;
			}
			error = ioctl(fd, DIOCGMEDIASIZE, &mediasize);
			if (error) {
				warnx("%s: ioctl(DIOCGMEDIASIZE) failed, probably not a disk.", argv[i]);
				exitval = 1;
				goto out;
			}
			error = ioctl(fd, DIOCGSECTORSIZE, &sectorsize);
			if (error) {
				warnx("%s: ioctl(DIOCGSECTORSIZE) failed, probably not a disk.", argv[i]);
				exitval = 1;
				goto out;
			}
			error = ioctl(fd, DIOCGFWSECTORS, &fwsectors);
			if (error)
				fwsectors = 0;
			error = ioctl(fd, DIOCGFWHEADS, &fwheads);
			if (error)
				fwheads = 0;
			error = ioctl(fd, DIOCGSTRIPESIZE, &stripesize);
			if (error)
				stripesize = 0;
			error = ioctl(fd, DIOCGSTRIPEOFFSET, &stripeoffset);
			if (error)
				stripeoffset = 0;
			error = zonecheck(fd, &zone_mode, zone_desc, sizeof(zone_desc));
			if (error == 0)
				zoned = 1;
		}
		if (!opt_v) {
			printf("%s", argv[i]);
			printf("\t%u", sectorsize);
			printf("\t%jd", (intmax_t)mediasize);
			printf("\t%jd", (intmax_t)mediasize/sectorsize);
			printf("\t%jd", (intmax_t)stripesize);
			printf("\t%jd", (intmax_t)stripeoffset);
			if (fwsectors != 0 && fwheads != 0) {
				printf("\t%jd", (intmax_t)mediasize /
				    (fwsectors * fwheads * sectorsize));
				printf("\t%u", fwheads);
				printf("\t%u", fwsectors);
			} 
		} else {
			humanize_number(tstr, 5, (int64_t)mediasize, "",
			    HN_AUTOSCALE, HN_B | HN_NOSPACE | HN_DECIMAL);
			printf("%s\n", argv[i]);
			printf("\t%-12u\t# sectorsize\n", sectorsize);
			printf("\t%-12jd\t# mediasize in bytes (%s)\n",
			    (intmax_t)mediasize, tstr);
			printf("\t%-12jd\t# mediasize in sectors\n",
			    (intmax_t)mediasize/sectorsize);
			printf("\t%-12jd\t# stripesize\n", stripesize);
			printf("\t%-12jd\t# stripeoffset\n", stripeoffset);
			if (fwsectors != 0 && fwheads != 0) {
				printf("\t%-12jd\t# Cylinders according to firmware.\n", (intmax_t)mediasize /
				    (fwsectors * fwheads * sectorsize));
				printf("\t%-12u\t# Heads according to firmware.\n", fwheads);
				printf("\t%-12u\t# Sectors according to firmware.\n", fwsectors);
			} 
			strlcpy(arg.name, "GEOM::descr", sizeof(arg.name));
			arg.len = sizeof(arg.value.str);
			if (ioctl(fd, DIOCGATTR, &arg) == 0)
				printf("\t%-12s\t# Disk descr.\n", arg.value.str);
			if (ioctl(fd, DIOCGIDENT, ident) == 0)
				printf("\t%-12s\t# Disk ident.\n", ident);
			if (ioctl(fd, DIOCGPHYSPATH, physpath) == 0)
				printf("\t%-12s\t# Physical path\n", physpath);
			printf("\t%-12s\t# TRIM/UNMAP support\n",
			    candelete(fd) ? "Yes" : "No");
			rotationrate(fd, rrate, sizeof(rrate));
			printf("\t%-12s\t# Rotation rate in RPM\n", rrate);
			if (zoned != 0)
				printf("\t%-12s\t# Zone Mode\n", zone_desc);
		}
		printf("\n");
		if (opt_c)
			commandtime(fd, mediasize, sectorsize);
		if (opt_t)
			speeddisk(fd, mediasize, sectorsize);
		if (opt_i)
			iopsbench(fd, mediasize, sectorsize);
		if (opt_S)
			slogbench(fd, isreg, mediasize, sectorsize);
out:
		close(fd);
	}
	free(buf);
	exit (exitval);
}

static bool
candelete(int fd)
{
	struct diocgattr_arg arg;

	strlcpy(arg.name, "GEOM::candelete", sizeof(arg.name));
	arg.len = sizeof(arg.value.i);
	if (ioctl(fd, DIOCGATTR, &arg) == 0)
		return (arg.value.i != 0);
	else
		return (false);
}

static void
rotationrate(int fd, char *rate, size_t buflen)
{
	struct diocgattr_arg arg;
	int ret;

	strlcpy(arg.name, "GEOM::rotation_rate", sizeof(arg.name));
	arg.len = sizeof(arg.value.u16);

	ret = ioctl(fd, DIOCGATTR, &arg);
	if (ret < 0 || arg.value.u16 == DISK_RR_UNKNOWN)
		snprintf(rate, buflen, "Unknown");
	else if (arg.value.u16 == DISK_RR_NON_ROTATING)
		snprintf(rate, buflen, "%d", 0);
	else if (arg.value.u16 >= DISK_RR_MIN && arg.value.u16 <= DISK_RR_MAX)
		snprintf(rate, buflen, "%d", arg.value.u16);
	else
		snprintf(rate, buflen, "Invalid");
}

static void
rdsect(int fd, off_t blockno, u_int sectorsize)
{
	int error;

	if (lseek(fd, (off_t)blockno * sectorsize, SEEK_SET) == -1)
		err(1, "lseek");
	error = read(fd, buf, sectorsize);
	if (error == -1)
		err(1, "read");
	if (error != (int)sectorsize)
		errx(1, "disk too small for test.");
}

static void
rdmega(int fd)
{
	int error;

	error = read(fd, buf, MEGATX);
	if (error == -1)
		err(1, "read");
	if (error != MEGATX)
		errx(1, "disk too small for test.");
}

static struct timeval tv1, tv2;

static void
T0(void)
{

	fflush(stdout);
	sync();
	sleep(1);
	sync();
	sync();
	gettimeofday(&tv1, NULL);
}

static double
delta_t(void)
{
	double dt;

	gettimeofday(&tv2, NULL);
	dt = (tv2.tv_usec - tv1.tv_usec) / 1e6;
	dt += (tv2.tv_sec - tv1.tv_sec);

	return (dt);
}

static void
TN(int count)
{
	double dt;

	dt = delta_t();
	printf("%5d iter in %10.6f sec = %8.3f msec\n",
		count, dt, dt * 1000.0 / count);
}

static void
TR(double count)
{
	double dt;

	dt = delta_t();
	printf("%8.0f kbytes in %10.6f sec = %8.0f kbytes/sec\n",
		count, dt, count / dt);
}

static void
TI(double count)
{
	double dt;

	dt = delta_t();
	printf("%8.0f ops in  %10.6f sec = %8.0f IOPS\n",
		count, dt, count / dt);
}

static void
TS(u_int size, int count)
{
	double dt;

	dt = delta_t();
	printf("%8.1f usec/IO = %8.1f Mbytes/s\n",
	    dt * 1000000.0 / count, (double)size * count / dt / (1024 * 1024));
}

static void
speeddisk(int fd, off_t mediasize, u_int sectorsize)
{
	int bulk, i;
	off_t b0, b1, sectorcount, step;

	/*
	 * Drives smaller than 1MB produce negative sector numbers,
	 * as do 2048 or fewer sectors.
	 */
	sectorcount = mediasize / sectorsize;
	if (mediasize < 1024 * 1024 || sectorcount < 2048)
		return;


	step = 1ULL << (flsll(sectorcount / (4 * 200)) - 1);
	if (step > 16384)
		step = 16384;
	bulk = mediasize / (1024 * 1024);
	if (bulk > 100)
		bulk = 100;

	printf("Seek times:\n");
	printf("\tFull stroke:\t");
	b0 = 0;
	b1 = sectorcount - step;
	T0();
	for (i = 0; i < 125; i++) {
		rdsect(fd, b0, sectorsize);
		b0 += step;
		rdsect(fd, b1, sectorsize);
		b1 -= step;
	}
	TN(250);

	printf("\tHalf stroke:\t");
	b0 = sectorcount / 4;
	b1 = b0 + sectorcount / 2;
	T0();
	for (i = 0; i < 125; i++) {
		rdsect(fd, b0, sectorsize);
		b0 += step;
		rdsect(fd, b1, sectorsize);
		b1 += step;
	}
	TN(250);
	printf("\tQuarter stroke:\t");
	b0 = sectorcount / 4;
	b1 = b0 + sectorcount / 4;
	T0();
	for (i = 0; i < 250; i++) {
		rdsect(fd, b0, sectorsize);
		b0 += step;
		rdsect(fd, b1, sectorsize);
		b1 += step;
	}
	TN(500);

	printf("\tShort forward:\t");
	b0 = sectorcount / 2;
	T0();
	for (i = 0; i < 400; i++) {
		rdsect(fd, b0, sectorsize);
		b0 += step;
	}
	TN(400);

	printf("\tShort backward:\t");
	b0 = sectorcount / 2;
	T0();
	for (i = 0; i < 400; i++) {
		rdsect(fd, b0, sectorsize);
		b0 -= step;
	}
	TN(400);

	printf("\tSeq outer:\t");
	b0 = 0;
	T0();
	for (i = 0; i < 2048; i++) {
		rdsect(fd, b0, sectorsize);
		b0++;
	}
	TN(2048);

	printf("\tSeq inner:\t");
	b0 = sectorcount - 2048;
	T0();
	for (i = 0; i < 2048; i++) {
		rdsect(fd, b0, sectorsize);
		b0++;
	}
	TN(2048);

	printf("\nTransfer rates:\n");
	printf("\toutside:     ");
	rdsect(fd, 0, sectorsize);
	T0();
	for (i = 0; i < bulk; i++) {
		rdmega(fd);
	}
	TR(bulk * 1024);

	printf("\tmiddle:      ");
	b0 = sectorcount / 2 - bulk * (1024*1024 / sectorsize) / 2 - 1;
	rdsect(fd, b0, sectorsize);
	T0();
	for (i = 0; i < bulk; i++) {
		rdmega(fd);
	}
	TR(bulk * 1024);

	printf("\tinside:      ");
	b0 = sectorcount - bulk * (1024*1024 / sectorsize) - 1;
	rdsect(fd, b0, sectorsize);
	T0();
	for (i = 0; i < bulk; i++) {
		rdmega(fd);
	}
	TR(bulk * 1024);

	printf("\n");
	return;
}

static void
commandtime(int fd, off_t mediasize, u_int sectorsize)
{	
	double dtmega, dtsector;
	int i;

	printf("I/O command overhead:\n");
	i = mediasize;
	rdsect(fd, 0, sectorsize);
	T0();
	for (i = 0; i < 10; i++)
		rdmega(fd);
	dtmega = delta_t();

	printf("\ttime to read 10MB block    %10.6f sec\t= %8.3f msec/sector\n",
		dtmega, dtmega*100/2048);

	rdsect(fd, 0, sectorsize);
	T0();
	for (i = 0; i < 20480; i++)
		rdsect(fd, 0, sectorsize);
	dtsector = delta_t();

	printf("\ttime to read 20480 sectors %10.6f sec\t= %8.3f msec/sector\n",
		dtsector, dtsector*100/2048);
	printf("\tcalculated command overhead\t\t\t= %8.3f msec/sector\n",
		(dtsector - dtmega)*100/2048);

	printf("\n");
	return;
}

static void
iops(int fd, off_t mediasize, u_int sectorsize)
{
	struct aiocb aios[NAIO], *aiop;
	ssize_t ret;
	off_t sectorcount;
	int error, i, queued, completed;

	sectorcount = mediasize / sectorsize;

	for (i = 0; i < NAIO; i++) {
		aiop = &(aios[i]);
		bzero(aiop, sizeof(*aiop));
		aiop->aio_buf = malloc(sectorsize);
		if (aiop->aio_buf == NULL)
			err(1, "malloc");
	}

	T0();
	for (i = 0; i < NAIO; i++) {
		aiop = &(aios[i]);

		aiop->aio_fildes = fd;
		aiop->aio_offset = (random() % (sectorcount)) * sectorsize;
		aiop->aio_nbytes = sectorsize;

		error = aio_read(aiop);
		if (error != 0)
			err(1, "aio_read");
	}

	queued = i;
	completed = 0;

	for (;;) {
		ret = aio_waitcomplete(&aiop, NULL);
		if (ret < 0)
			err(1, "aio_waitcomplete");
		if (ret != (ssize_t)sectorsize)
			errx(1, "short read");

		completed++;

		if (delta_t() < 3.0) {
			aiop->aio_fildes = fd;
			aiop->aio_offset = (random() % (sectorcount)) * sectorsize;
			aiop->aio_nbytes = sectorsize;

			error = aio_read(aiop);
			if (error != 0)
				err(1, "aio_read");

			queued++;
		} else if (completed == queued) {
			break;
		}
	}

	TI(completed);

	return;
}

static void
iopsbench(int fd, off_t mediasize, u_int sectorsize)
{
	printf("Asynchronous random reads:\n");

	printf("\tsectorsize:  ");
	iops(fd, mediasize, sectorsize);

	if (sectorsize != 4096) {
		printf("\t4 kbytes:    ");
		iops(fd, mediasize, 4096);
	}

	printf("\t32 kbytes:   ");
	iops(fd, mediasize, 32 * 1024);

	printf("\t128 kbytes:  ");
	iops(fd, mediasize, 128 * 1024);

	printf("\n");
}

#define MAXIO (128*1024)
#define MAXIOS (MAXTX / MAXIO)

static void
parwrite(int fd, size_t size, off_t off)
{
	struct aiocb aios[MAXIOS];
	off_t o;
	int n, error;
	struct aiocb *aiop;

	// if size > MAXIO, use AIO to write n - 1 pieces in parallel
	for (n = 0, o = 0; size > MAXIO; n++, size -= MAXIO, o += MAXIO) {
		aiop = &aios[n];
		bzero(aiop, sizeof(*aiop));
		aiop->aio_buf = &buf[o];
		aiop->aio_fildes = fd;
		aiop->aio_offset = off + o;
		aiop->aio_nbytes = MAXIO;
		error = aio_write(aiop);
		if (error != 0)
			err(EX_IOERR, "AIO write submit error");
	}
	// Use synchronous writes for the runt of size <= MAXIO
	error = pwrite(fd, &buf[o], size, off + o);
	if (error < 0)
		err(EX_IOERR, "Sync write error");
	for (; n > 0; n--) {
		error = aio_waitcomplete(&aiop, NULL);
		if (error < 0)
			err(EX_IOERR, "AIO write wait error");
	}
}

static void
slogbench(int fd, int isreg, off_t mediasize, u_int sectorsize)
{
	off_t off;
	u_int size;
	int error, n, N, nowritecache = 0;

	printf("Synchronous random writes:\n");
	for (size = sectorsize; size <= MAXTX; size *= 2) {
		printf("\t%4.4g kbytes: ", (double)size / 1024);
		N = 0;
		T0();
		do {
			for (n = 0; n < 250; n++) {
				off = random() % (mediasize / size);
				parwrite(fd, size, off * size);
				if (nowritecache)
					continue;
				if (isreg)
					error = fsync(fd);
				else
					error = ioctl(fd, DIOCGFLUSH);
				if (error < 0) {
					if (errno == ENOTSUP)
						nowritecache = 1;
					else
						err(EX_IOERR, "Flush error");
				}
			}
			N += 250;
		} while (delta_t() < 1.0);
		TS(size, N);
	}
}

static int
zonecheck(int fd, uint32_t *zone_mode, char *zone_str, size_t zone_str_len)
{
	struct disk_zone_args zone_args;
	int error;

	bzero(&zone_args, sizeof(zone_args));

	zone_args.zone_cmd = DISK_ZONE_GET_PARAMS;
	error = ioctl(fd, DIOCZONECMD, &zone_args);

	if (error == 0) {
		*zone_mode = zone_args.zone_params.disk_params.zone_mode;

		switch (*zone_mode) {
		case DISK_ZONE_MODE_NONE:
			snprintf(zone_str, zone_str_len, "Not_Zoned");
			break;
		case DISK_ZONE_MODE_HOST_AWARE:
			snprintf(zone_str, zone_str_len, "Host_Aware");
			break;
		case DISK_ZONE_MODE_DRIVE_MANAGED:
			snprintf(zone_str, zone_str_len, "Drive_Managed");
			break;
		case DISK_ZONE_MODE_HOST_MANAGED:
			snprintf(zone_str, zone_str_len, "Host_Managed");
			break;
		default:
			snprintf(zone_str, zone_str_len, "Unknown_zone_mode_%u",
			    *zone_mode);
			break;
		}
	}
	return (error);
}
