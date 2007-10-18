
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/minix_fs.h>
#include <linux/ext2_fs.h>
#include <linux/romfs_fs.h>
#include <linux/cramfs_fs.h>
#include <linux/initrd.h>
#include <linux/string.h>

#include "do_mounts.h"

#define BUILD_CRAMDISK

int __initdata rd_prompt = 1;/* 1 = prompt for RAM disk, 0 = don't prompt */

static int __init prompt_ramdisk(char *str)
{
	rd_prompt = simple_strtol(str,NULL,0) & 1;
	return 1;
}
__setup("prompt_ramdisk=", prompt_ramdisk);

int __initdata rd_image_start;		/* starting block # of image */

static int __init ramdisk_start_setup(char *str)
{
	rd_image_start = simple_strtol(str,NULL,0);
	return 1;
}
__setup("ramdisk_start=", ramdisk_start_setup);

static int __init crd_load(int in_fd, int out_fd);

/*
 * This routine tries to find a RAM disk image to load, and returns the
 * number of blocks to read for a non-compressed image, 0 if the image
 * is a compressed image, and -1 if an image with the right magic
 * numbers could not be found.
 *
 * We currently check for the following magic numbers:
 * 	minix
 * 	ext2
 *	romfs
 *	cramfs
 * 	gzip
 */
static int __init 
identify_ramdisk_image(int fd, int start_block)
{
	const int size = 512;
	struct minix_super_block *minixsb;
	struct ext2_super_block *ext2sb;
	struct romfs_super_block *romfsb;
	struct cramfs_super *cramfsb;
	int nblocks = -1;
	unsigned char *buf;

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf)
		return -1;

	minixsb = (struct minix_super_block *) buf;
	ext2sb = (struct ext2_super_block *) buf;
	romfsb = (struct romfs_super_block *) buf;
	cramfsb = (struct cramfs_super *) buf;
	memset(buf, 0xe5, size);

	/*
	 * Read block 0 to test for gzipped kernel
	 */
	sys_lseek(fd, start_block * BLOCK_SIZE, 0);
	sys_read(fd, buf, size);

	/*
	 * If it matches the gzip magic numbers, return -1
	 */
	if (buf[0] == 037 && ((buf[1] == 0213) || (buf[1] == 0236))) {
		printk(KERN_NOTICE
		       "RAMDISK: Compressed image found at block %d\n",
		       start_block);
		nblocks = 0;
		goto done;
	}

	/* romfs is at block zero too */
	if (romfsb->word0 == ROMSB_WORD0 &&
	    romfsb->word1 == ROMSB_WORD1) {
		printk(KERN_NOTICE
		       "RAMDISK: romfs filesystem found at block %d\n",
		       start_block);
		nblocks = (ntohl(romfsb->size)+BLOCK_SIZE-1)>>BLOCK_SIZE_BITS;
		goto done;
	}

	if (cramfsb->magic == CRAMFS_MAGIC) {
		printk(KERN_NOTICE
		       "RAMDISK: cramfs filesystem found at block %d\n",
		       start_block);
		nblocks = (cramfsb->size + BLOCK_SIZE - 1) >> BLOCK_SIZE_BITS;
		goto done;
	}

	/*
	 * Read block 1 to test for minix and ext2 superblock
	 */
	sys_lseek(fd, (start_block+1) * BLOCK_SIZE, 0);
	sys_read(fd, buf, size);

	/* Try minix */
	if (minixsb->s_magic == MINIX_SUPER_MAGIC ||
	    minixsb->s_magic == MINIX_SUPER_MAGIC2) {
		printk(KERN_NOTICE
		       "RAMDISK: Minix filesystem found at block %d\n",
		       start_block);
		nblocks = minixsb->s_nzones << minixsb->s_log_zone_size;
		goto done;
	}

	/* Try ext2 */
	if (ext2sb->s_magic == cpu_to_le16(EXT2_SUPER_MAGIC)) {
		printk(KERN_NOTICE
		       "RAMDISK: ext2 filesystem found at block %d\n",
		       start_block);
		nblocks = le32_to_cpu(ext2sb->s_blocks_count) <<
			le32_to_cpu(ext2sb->s_log_block_size);
		goto done;
	}

	printk(KERN_NOTICE
	       "RAMDISK: Couldn't find valid RAM disk image starting at %d.\n",
	       start_block);
	
done:
	sys_lseek(fd, start_block * BLOCK_SIZE, 0);
	kfree(buf);
	return nblocks;
}

int __init rd_load_image(char *from)
{
	int res = 0;
	int in_fd, out_fd;
	unsigned long rd_blocks, devblocks;
	int nblocks, i, disk;
	char *buf = NULL;
	unsigned short rotate = 0;
#if !defined(CONFIG_S390) && !defined(CONFIG_PPC_ISERIES)
	char rotator[4] = { '|' , '/' , '-' , '\\' };
#endif

	out_fd = sys_open("/dev/ram", O_RDWR, 0);
	if (out_fd < 0)
		goto out;

	in_fd = sys_open(from, O_RDONLY, 0);
	if (in_fd < 0)
		goto noclose_input;

	nblocks = identify_ramdisk_image(in_fd, rd_image_start);
	if (nblocks < 0)
		goto done;

	if (nblocks == 0) {
#ifdef BUILD_CRAMDISK
		if (crd_load(in_fd, out_fd) == 0)
			goto successful_load;
#else
		printk(KERN_NOTICE
		       "RAMDISK: Kernel does not support compressed "
		       "RAM disk images\n");
#endif
		goto done;
	}

	/*
	 * NOTE NOTE: nblocks is not actually blocks but
	 * the number of kibibytes of data to load into a ramdisk.
	 * So any ramdisk block size that is a multiple of 1KiB should
	 * work when the appropriate ramdisk_blocksize is specified
	 * on the command line.
	 *
	 * The default ramdisk_blocksize is 1KiB and it is generally
	 * silly to use anything else, so make sure to use 1KiB
	 * blocksize while generating ext2fs ramdisk-images.
	 */
	if (sys_ioctl(out_fd, BLKGETSIZE, (unsigned long)&rd_blocks) < 0)
		rd_blocks = 0;
	else
		rd_blocks >>= 1;

	if (nblocks > rd_blocks) {
		printk("RAMDISK: image too big! (%dKiB/%ldKiB)\n",
		       nblocks, rd_blocks);
		goto done;
	}
		
	/*
	 * OK, time to copy in the data
	 */
	if (sys_ioctl(in_fd, BLKGETSIZE, (unsigned long)&devblocks) < 0)
		devblocks = 0;
	else
		devblocks >>= 1;

	if (strcmp(from, "/initrd.image") == 0)
		devblocks = nblocks;

	if (devblocks == 0) {
		printk(KERN_ERR "RAMDISK: could not determine device size\n");
		goto done;
	}

	buf = kmalloc(BLOCK_SIZE, GFP_KERNEL);
	if (buf == 0) {
		printk(KERN_ERR "RAMDISK: could not allocate buffer\n");
		goto done;
	}

	printk(KERN_NOTICE "RAMDISK: Loading %dKiB [%ld disk%s] into ram disk... ",
		nblocks, ((nblocks-1)/devblocks)+1, nblocks>devblocks ? "s" : "");
	for (i = 0, disk = 1; i < nblocks; i++) {
		if (i && (i % devblocks == 0)) {
			printk("done disk #%d.\n", disk++);
			rotate = 0;
			if (sys_close(in_fd)) {
				printk("Error closing the disk.\n");
				goto noclose_input;
			}
			change_floppy("disk #%d", disk);
			in_fd = sys_open(from, O_RDONLY, 0);
			if (in_fd < 0)  {
				printk("Error opening disk.\n");
				goto noclose_input;
			}
			printk("Loading disk #%d... ", disk);
		}
		sys_read(in_fd, buf, BLOCK_SIZE);
		sys_write(out_fd, buf, BLOCK_SIZE);
#if !defined(CONFIG_S390) && !defined(CONFIG_PPC_ISERIES)
		if (!(i % 16)) {
			printk("%c\b", rotator[rotate & 0x3]);
			rotate++;
		}
#endif
	}
	printk("done.\n");

successful_load:
	res = 1;
done:
	sys_close(in_fd);
noclose_input:
	sys_close(out_fd);
out:
	kfree(buf);
	sys_unlink("/dev/ram");
	return res;
}

int __init rd_load_disk(int n)
{
	if (rd_prompt)
		change_floppy("root floppy disk to be loaded into RAM disk");
	create_dev("/dev/root", ROOT_DEV);
	create_dev("/dev/ram", MKDEV(RAMDISK_MAJOR, n));
	return rd_load_image("/dev/root");
}

#ifdef BUILD_CRAMDISK

/*
 * gzip declarations
 */

#define OF(args)  args

#ifndef memzero
#define memzero(s, n)     memset ((s), 0, (n))
#endif

typedef unsigned char  uch;
typedef unsigned short ush;
typedef unsigned long  ulg;

#define INBUFSIZ 4096
#define WSIZE 0x8000    /* window size--must be a power of two, and */
			/*  at least 32K for zip's deflate method */

static uch *inbuf;
static uch *window;

static unsigned insize;  /* valid bytes in inbuf */
static unsigned inptr;   /* index of next byte to be processed in inbuf */
static unsigned outcnt;  /* bytes in output buffer */
static int exit_code;
static int unzip_error;
static long bytes_out;
static int crd_infd, crd_outfd;

#define get_byte()  (inptr < insize ? inbuf[inptr++] : fill_inbuf())
		
/* Diagnostic functions (stubbed out) */
#define Assert(cond,msg)
#define Trace(x)
#define Tracev(x)
#define Tracevv(x)
#define Tracec(c,x)
#define Tracecv(c,x)

#define STATIC static
#define INIT __init

static int  __init fill_inbuf(void);
static void __init flush_window(void);
static void __init *malloc(size_t size);
static void __init free(void *where);
static void __init error(char *m);
static void __init gzip_mark(void **);
static void __init gzip_release(void **);

#include "../lib/inflate.c"

static void __init *malloc(size_t size)
{
	return kmalloc(size, GFP_KERNEL);
}

static void __init free(void *where)
{
	kfree(where);
}

static void __init gzip_mark(void **ptr)
{
}

static void __init gzip_release(void **ptr)
{
}


/* ===========================================================================
 * Fill the input buffer. This is called only when the buffer is empty
 * and at least one byte is really needed.
 * Returning -1 does not guarantee that gunzip() will ever return.
 */
static int __init fill_inbuf(void)
{
	if (exit_code) return -1;
	
	insize = sys_read(crd_infd, inbuf, INBUFSIZ);
	if (insize == 0) {
		error("RAMDISK: ran out of compressed data");
		return -1;
	}

	inptr = 1;

	return inbuf[0];
}

/* ===========================================================================
 * Write the output window window[0..outcnt-1] and update crc and bytes_out.
 * (Used for the decompressed data only.)
 */
static void __init flush_window(void)
{
    ulg c = crc;         /* temporary variable */
    unsigned n, written;
    uch *in, ch;
    
    written = sys_write(crd_outfd, window, outcnt);
    if (written != outcnt && unzip_error == 0) {
	printk(KERN_ERR "RAMDISK: incomplete write (%d != %d) %ld\n",
	       written, outcnt, bytes_out);
	unzip_error = 1;
    }
    in = window;
    for (n = 0; n < outcnt; n++) {
	    ch = *in++;
	    c = crc_32_tab[((int)c ^ ch) & 0xff] ^ (c >> 8);
    }
    crc = c;
    bytes_out += (ulg)outcnt;
    outcnt = 0;
}

static void __init error(char *x)
{
	printk(KERN_ERR "%s\n", x);
	exit_code = 1;
	unzip_error = 1;
}

static int __init crd_load(int in_fd, int out_fd)
{
	int result;

	insize = 0;		/* valid bytes in inbuf */
	inptr = 0;		/* index of next byte to be processed in inbuf */
	outcnt = 0;		/* bytes in output buffer */
	exit_code = 0;
	bytes_out = 0;
	crc = (ulg)0xffffffffL; /* shift register contents */

	crd_infd = in_fd;
	crd_outfd = out_fd;
	inbuf = kmalloc(INBUFSIZ, GFP_KERNEL);
	if (!inbuf) {
		printk(KERN_ERR "RAMDISK: Couldn't allocate gzip buffer\n");
		return -1;
	}
	window = kmalloc(WSIZE, GFP_KERNEL);
	if (!window) {
		printk(KERN_ERR "RAMDISK: Couldn't allocate gzip window\n");
		kfree(inbuf);
		return -1;
	}
	makecrc();
	result = gunzip();
	if (unzip_error)
		result = 1;
	kfree(inbuf);
	kfree(window);
	return result;
}

#endif  /* BUILD_CRAMDISK */
