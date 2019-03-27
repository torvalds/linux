/*-
 * Copyright (c) 1998 Robert Nordier
 * All rights reserved.
 * Copyright (c) 2001 Robert Drehmel
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are freely
 * permitted provided that the above copyright notice and this
 * paragraph and the following disclaimer are duplicated in all
 * such forms.
 *
 * This software is provided "AS IS" and without any express or
 * implied warranties, including, without limitation, the implied
 * warranties of merchantability and fitness for a particular
 * purpose.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/dirent.h>
#include <machine/elf.h>
#include <machine/stdarg.h>
#include <machine/md_var.h>

#include "paths.h"

#define BSIZEMAX	16384

typedef int putc_func_t(char c, void *arg);
typedef int32_t ofwh_t;

struct sp_data {
	char	*sp_buf;
	u_int	sp_len;
	u_int	sp_size;
};

static const char digits[] = "0123456789abcdef";

static char bootpath[128];
static char bootargs[128];

static ofwh_t bootdev;

static struct fs fs;
static char blkbuf[BSIZEMAX];
static unsigned int fsblks;

static uint32_t fs_off;

int main(int ac, char **av);

static void exit(int) __dead2;
static void load(const char *);
static int dskread(void *, uint64_t, int);

static void usage(void);

static void bcopy(const void *src, void *dst, size_t len);
static void bzero(void *b, size_t len);

static int domount(const char *device, int quiet);

static void panic(const char *fmt, ...) __dead2;
static int printf(const char *fmt, ...);
static int putchar(char c, void *arg);
static int vprintf(const char *fmt, va_list ap);
static int vsnprintf(char *str, size_t sz, const char *fmt, va_list ap);

static int __printf(const char *fmt, putc_func_t *putc, void *arg, va_list ap);
static int __putc(char c, void *arg);
static int __puts(const char *s, putc_func_t *putc, void *arg);
static int __sputc(char c, void *arg);
static char *__uitoa(char *buf, u_int val, int base);
static char *__ultoa(char *buf, u_long val, int base);

/*
 * Open Firmware interface functions
 */
typedef uint32_t	ofwcell_t;
typedef uint32_t	u_ofwh_t;
typedef int (*ofwfp_t)(void *);
ofwfp_t ofw;			/* the prom Open Firmware entry */
ofwh_t chosenh;

void ofw_init(void *, int, int (*)(void *), char *, int);
static ofwh_t ofw_finddevice(const char *);
static ofwh_t ofw_open(const char *);
static int ofw_close(ofwh_t);
static int ofw_getprop(ofwh_t, const char *, void *, size_t);
static int ofw_setprop(ofwh_t, const char *, void *, size_t);
static int ofw_read(ofwh_t, void *, size_t);
static int ofw_write(ofwh_t, const void *, size_t);
static int ofw_claim(void *virt, size_t len, u_int align);
static int ofw_seek(ofwh_t, uint64_t);
static void ofw_exit(void) __dead2;

ofwh_t bootdevh;
ofwh_t stdinh, stdouth;

__asm("                         \n\
        .data                   \n\
	.align 4		\n\
stack:                          \n\
        .space  16384           \n\
                                \n\
        .text                   \n\
        .globl  _start          \n\
_start:                         \n\
        lis     %r1,stack@ha    \n\
        addi    %r1,%r1,stack@l \n\
        addi    %r1,%r1,8192    \n\
                                \n\
        b       ofw_init        \n\
");

void
ofw_init(void *vpd, int res, int (*openfirm)(void *), char *arg, int argl)
{
	char *av[16];
	char *p;
	int ac;

	ofw = openfirm;

	chosenh = ofw_finddevice("/chosen");
	ofw_getprop(chosenh, "stdin", &stdinh, sizeof(stdinh));
	ofw_getprop(chosenh, "stdout", &stdouth, sizeof(stdouth));
	ofw_getprop(chosenh, "bootargs", bootargs, sizeof(bootargs));
	ofw_getprop(chosenh, "bootpath", bootpath, sizeof(bootpath));

	bootargs[sizeof(bootargs) - 1] = '\0';
	bootpath[sizeof(bootpath) - 1] = '\0';

	p = bootpath;
	while (*p != '\0') {
		/* Truncate partition ID */
		if (*p == ':') {
			ofw_close(bootdev);
			*(++p) = '\0';
			break;
		}
		p++;
	}

	ac = 0;
	p = bootargs;
	for (;;) {
		while (*p == ' ' && *p != '\0')
			p++;
		if (*p == '\0' || ac >= 16)
			break;
		av[ac++] = p;
		while (*p != ' ' && *p != '\0')
			p++;
		if (*p != '\0')
			*p++ = '\0';
	}

	exit(main(ac, av));
}

static ofwh_t
ofw_finddevice(const char *name)
{
	ofwcell_t args[] = {
		(ofwcell_t)"finddevice",
		1,
		1,
		(ofwcell_t)name,
		0
	};

	if ((*ofw)(args)) {
		printf("ofw_finddevice: name=\"%s\"\n", name);
		return (1);
	}
	return (args[4]);
}

static int
ofw_getprop(ofwh_t ofwh, const char *name, void *buf, size_t len)
{
	ofwcell_t args[] = {
		(ofwcell_t)"getprop",
		4,
		1,
		(u_ofwh_t)ofwh,
		(ofwcell_t)name,
		(ofwcell_t)buf,
		len,
	0
	};

	if ((*ofw)(args)) {
		printf("ofw_getprop: ofwh=0x%x buf=%p len=%u\n",
			ofwh, buf, len);
		return (1);
	}
	return (0);
}

static int
ofw_setprop(ofwh_t ofwh, const char *name, void *buf, size_t len)
{
	ofwcell_t args[] = {
		(ofwcell_t)"setprop",
		4,
		1,
		(u_ofwh_t)ofwh,
		(ofwcell_t)name,
		(ofwcell_t)buf,
		len,
	0
	};

	if ((*ofw)(args)) {
		printf("ofw_setprop: ofwh=0x%x buf=%p len=%u\n",
			ofwh, buf, len);
		return (1);
	}
	return (0);
}

static ofwh_t
ofw_open(const char *path)
{
	ofwcell_t args[] = {
		(ofwcell_t)"open",
		1,
		1,
		(ofwcell_t)path,
		0
	};

	if ((*ofw)(args)) {
		printf("ofw_open: path=\"%s\"\n", path);
		return (-1);
	}
	return (args[4]);
}

static int
ofw_close(ofwh_t devh)
{
	ofwcell_t args[] = {
		(ofwcell_t)"close",
		1,
		0,
		(u_ofwh_t)devh
	};

	if ((*ofw)(args)) {
		printf("ofw_close: devh=0x%x\n", devh);
		return (1);
	}
	return (0);
}

static int
ofw_claim(void *virt, size_t len, u_int align)
{
	ofwcell_t args[] = {
		(ofwcell_t)"claim",
		3,
		1,
		(ofwcell_t)virt,
		len,
		align,
		0,
		0
	};

	if ((*ofw)(args)) {
		printf("ofw_claim: virt=%p len=%u\n", virt, len);
		return (1);
	}

	return (0);
}

static int
ofw_read(ofwh_t devh, void *buf, size_t len)
{
	ofwcell_t args[] = {
		(ofwcell_t)"read",
		3,
		1,
		(u_ofwh_t)devh,
		(ofwcell_t)buf,
		len,
		0
	};

	if ((*ofw)(args)) {
		printf("ofw_read: devh=0x%x buf=%p len=%u\n", devh, buf, len);
		return (1);
	}
	return (0);
}

static int
ofw_write(ofwh_t devh, const void *buf, size_t len)
{
	ofwcell_t args[] = {
		(ofwcell_t)"write",
		3,
		1,
		(u_ofwh_t)devh,
		(ofwcell_t)buf,
		len,
		0
	};

	if ((*ofw)(args)) {
		printf("ofw_write: devh=0x%x buf=%p len=%u\n", devh, buf, len);
		return (1);
	}
	return (0);
}

static int
ofw_seek(ofwh_t devh, uint64_t off)
{
	ofwcell_t args[] = {
		(ofwcell_t)"seek",
		3,
		1,
		(u_ofwh_t)devh,
		off >> 32,
		off,
		0
	};

	if ((*ofw)(args)) {
		printf("ofw_seek: devh=0x%x off=0x%lx\n", devh, off);
		return (1);
	}
	return (0);
}

static void
ofw_exit(void)
{
	ofwcell_t args[3];

	args[0] = (ofwcell_t)"exit";
	args[1] = 0;
	args[2] = 0;

	for (;;)
		(*ofw)(args);
}

static void
bcopy(const void *src, void *dst, size_t len)
{
	const char *s = src;
	char *d = dst;

	while (len-- != 0)
		*d++ = *s++;
}

static void
memcpy(void *dst, const void *src, size_t len)
{
	bcopy(src, dst, len);
}

static void
bzero(void *b, size_t len)
{
	char *p = b;

	while (len-- != 0)
		*p++ = 0;
}

static int
strcmp(const char *s1, const char *s2)
{
	for (; *s1 == *s2 && *s1; s1++, s2++)
		;
	return ((u_char)*s1 - (u_char)*s2);
}

#include "ufsread.c"

int
main(int ac, char **av)
{
	const char *path;
	char bootpath_full[255];
	int i, len;

	path = PATH_LOADER;
	for (i = 0; i < ac; i++) {
		switch (av[i][0]) {
		case '-':
			switch (av[i][1]) {
			default:
				usage();
			}
			break;
		default:
			path = av[i];
			break;
		}
	}

	printf(" \n>> FreeBSD/powerpc Open Firmware boot block\n"
	"   Boot path:   %s\n"
	"   Boot loader: %s\n", bootpath, path);

	len = 0;
	while (bootpath[len] != '\0') len++;

	memcpy(bootpath_full,bootpath,len+1);

	if (bootpath_full[len-1] != ':') {
		/* First try full volume */
		if (domount(bootpath_full,1) == 0)
			goto out;

		/* Add a : so that we try partitions if that fails */
		if (bootdev > 0)
			ofw_close(bootdev);
		bootpath_full[len] = ':';
		len += 1;
	}

	/* Loop through first 16 partitions to find a UFS one */
	for (i = 0; i < 16; i++) {
		if (i < 10) {
			bootpath_full[len] = i + '0';
			bootpath_full[len+1] = '\0';
		} else {
			bootpath_full[len] = '1';
			bootpath_full[len+1] = i - 10 + '0';
			bootpath_full[len+2] = '\0';
		}
			
		if (domount(bootpath_full,1) >= 0)
			break;

		if (bootdev > 0)
			ofw_close(bootdev);
	}

	if (i >= 16)
		panic("domount");

out:
	printf("   Boot volume:   %s\n",bootpath_full);
	ofw_setprop(chosenh, "bootargs", bootpath_full, len+2);
	load(path);
	return (1);
}

static void
usage(void)
{

	printf("usage: boot device [/path/to/loader]\n");
	exit(1);
}

static void
exit(int code)
{

	ofw_exit();
}

static struct dmadat __dmadat;

static int
domount(const char *device, int quiet)
{

	dmadat = &__dmadat;
	if ((bootdev = ofw_open(device)) == -1) {
		printf("domount: can't open device\n");
		return (-1);
	}
	if (fsread(0, NULL, 0)) {
		if (!quiet)
			printf("domount: can't read superblock\n");
		return (-1);
	}
	return (0);
}

static void
load(const char *fname)
{
	Elf32_Ehdr eh;
	Elf32_Phdr ph;
	caddr_t p;
	ufs_ino_t ino;
	int i;

	if ((ino = lookup(fname)) == 0) {
		printf("File %s not found\n", fname);
		return;
	}
	if (fsread(ino, &eh, sizeof(eh)) != sizeof(eh)) {
		printf("Can't read elf header\n");
		return;
	}
	if (!IS_ELF(eh)) {
		printf("Not an ELF file\n");
		return;
	}
	for (i = 0; i < eh.e_phnum; i++) {
		fs_off = eh.e_phoff + i * eh.e_phentsize;
		if (fsread(ino, &ph, sizeof(ph)) != sizeof(ph)) {
			printf("Can't read program header %d\n", i);
			return;
		}
		if (ph.p_type != PT_LOAD)
			continue;
		fs_off = ph.p_offset;
		p = (caddr_t)ph.p_vaddr;
		ofw_claim(p,(ph.p_filesz > ph.p_memsz) ? 
		    ph.p_filesz : ph.p_memsz,0);
		if (fsread(ino, p, ph.p_filesz) != ph.p_filesz) {
			printf("Can't read content of section %d\n", i);
			return;
		}
		if (ph.p_filesz != ph.p_memsz)
			bzero(p + ph.p_filesz, ph.p_memsz - ph.p_filesz);
		__syncicache(p, ph.p_memsz);
	}
	ofw_close(bootdev);
	(*(void (*)(void *, int, ofwfp_t, char *, int))eh.e_entry)(NULL, 0, 
	    ofw,NULL,0);
}

static int
dskread(void *buf, uint64_t lba, int nblk)
{
	/*
	 * The Open Firmware should open the correct partition for us.
	 * That means, if we read from offset zero on an open instance handle,
	 * we should read from offset zero of that partition.
	 */
	ofw_seek(bootdev, lba * DEV_BSIZE);
	ofw_read(bootdev, buf, nblk * DEV_BSIZE);
	return (0);
}

static void
panic(const char *fmt, ...)
{
	char buf[128];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	printf("panic: %s\n", buf);
	va_end(ap);

	exit(1);
}

static int
printf(const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = vprintf(fmt, ap);
	va_end(ap);
	return (ret);
}

static int
putchar(char c, void *arg)
{
	char buf;

	if (c == '\n') {
		buf = '\r';
		ofw_write(stdouth, &buf, 1);
	}
	buf = c;
	ofw_write(stdouth, &buf, 1);
	return (1);
}

static int
vprintf(const char *fmt, va_list ap)
{
	int ret;

	ret = __printf(fmt, putchar, 0, ap);
	return (ret);
}

static int
vsnprintf(char *str, size_t sz, const char *fmt, va_list ap)
{
	struct sp_data sp;
	int ret;

	sp.sp_buf = str;
	sp.sp_len = 0;
	sp.sp_size = sz;
	ret = __printf(fmt, __sputc, &sp, ap);
	return (ret);
}

static int
__printf(const char *fmt, putc_func_t *putc, void *arg, va_list ap)
{
	char buf[(sizeof(long) * 8) + 1];
	char *nbuf;
	u_long ul;
	u_int ui;
	int lflag;
	int sflag;
	char *s;
	int pad;
	int ret;
	int c;

	nbuf = &buf[sizeof buf - 1];
	ret = 0;
	while ((c = *fmt++) != 0) {
		if (c != '%') {
			ret += putc(c, arg);
			continue;
		}
		lflag = 0;
		sflag = 0;
		pad = 0;
reswitch:	c = *fmt++;
		switch (c) {
		case '#':
			sflag = 1;
			goto reswitch;
		case '%':
			ret += putc('%', arg);
			break;
		case 'c':
			c = va_arg(ap, int);
			ret += putc(c, arg);
			break;
		case 'd':
			if (lflag == 0) {
				ui = (u_int)va_arg(ap, int);
				if (ui < (int)ui) {
					ui = -ui;
					ret += putc('-', arg);
				}
				s = __uitoa(nbuf, ui, 10);
			} else {
				ul = (u_long)va_arg(ap, long);
				if (ul < (long)ul) {
					ul = -ul;
					ret += putc('-', arg);
				}
				s = __ultoa(nbuf, ul, 10);
			}
			ret += __puts(s, putc, arg);
			break;
		case 'l':
			lflag = 1;
			goto reswitch;
		case 'o':
			if (lflag == 0) {
				ui = (u_int)va_arg(ap, u_int);
				s = __uitoa(nbuf, ui, 8);
			} else {
				ul = (u_long)va_arg(ap, u_long);
				s = __ultoa(nbuf, ul, 8);
			}
			ret += __puts(s, putc, arg);
			break;
		case 'p':
			ul = (u_long)va_arg(ap, void *);
			s = __ultoa(nbuf, ul, 16);
			ret += __puts("0x", putc, arg);
			ret += __puts(s, putc, arg);
			break;
		case 's':
			s = va_arg(ap, char *);
			ret += __puts(s, putc, arg);
			break;
		case 'u':
			if (lflag == 0) {
				ui = va_arg(ap, u_int);
				s = __uitoa(nbuf, ui, 10);
			} else {
				ul = va_arg(ap, u_long);
				s = __ultoa(nbuf, ul, 10);
			}
			ret += __puts(s, putc, arg);
			break;
		case 'x':
			if (lflag == 0) {
				ui = va_arg(ap, u_int);
				s = __uitoa(nbuf, ui, 16);
			} else {
				ul = va_arg(ap, u_long);
				s = __ultoa(nbuf, ul, 16);
			}
			if (sflag)
				ret += __puts("0x", putc, arg);
			ret += __puts(s, putc, arg);
			break;
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			pad = pad * 10 + c - '0';
			goto reswitch;
		default:
			break;
		}
	}
	return (ret);
}

static int
__sputc(char c, void *arg)
{
	struct sp_data *sp;

	sp = arg;
	if (sp->sp_len < sp->sp_size)
		sp->sp_buf[sp->sp_len++] = c;
	sp->sp_buf[sp->sp_len] = '\0';
	return (1);
}

static int
__puts(const char *s, putc_func_t *putc, void *arg)
{
	const char *p;
	int ret;

	ret = 0;
	for (p = s; *p != '\0'; p++)
		ret += putc(*p, arg);
	return (ret);
}

static char *
__uitoa(char *buf, u_int ui, int base)
{
	char *p;

	p = buf;
	*p = '\0';
	do
		*--p = digits[ui % base];
	while ((ui /= base) != 0);
	return (p);
}

static char *
__ultoa(char *buf, u_long ul, int base)
{
	char *p;

	p = buf;
	*p = '\0';
	do
		*--p = digits[ul % base];
	while ((ul /= base) != 0);
	return (p);
}
