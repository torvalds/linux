/****************************************************************************/
/*
 *  linux/fs/binfmt_flat.c
 *
 *	Copyright (C) 2000-2003 David McCullough <davidm@snapgear.com>
 *	Copyright (C) 2002 Greg Ungerer <gerg@snapgear.com>
 *	Copyright (C) 2002 SnapGear, by Paul Dale <pauli@snapgear.com>
 *	Copyright (C) 2000, 2001 Lineo, by David McCullough <davidm@lineo.com>
 *  based heavily on:
 *
 *  linux/fs/binfmt_aout.c:
 *      Copyright (C) 1991, 1992, 1996  Linus Torvalds
 *  linux/fs/binfmt_flat.c for 2.0 kernel
 *	    Copyright (C) 1998  Kenneth Albanowski <kjahds@kjahds.com>
 *	JAN/99 -- coded full program relocation (gerg@snapgear.com)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/slab.h>
#include <linux/binfmts.h>
#include <linux/personality.h>
#include <linux/init.h>
#include <linux/flat.h>
#include <linux/syscalls.h>

#include <asm/byteorder.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>
#include <asm/cacheflush.h>
#include <asm/page.h>

/****************************************************************************/

#if 0
#define DEBUG 1
#endif

#ifdef DEBUG
#define	DBG_FLT(a...)	printk(a)
#else
#define	DBG_FLT(a...)
#endif

/*
 * User data (data section and bss) needs to be aligned.
 * We pick 0x20 here because it is the max value elf2flt has always
 * used in producing FLAT files, and because it seems to be large
 * enough to make all the gcc alignment related tests happy.
 */
#define FLAT_DATA_ALIGN	(0x20)

/*
 * User data (stack) also needs to be aligned.
 * Here we can be a bit looser than the data sections since this
 * needs to only meet arch ABI requirements.
 */
#define FLAT_STACK_ALIGN	max_t(unsigned long, sizeof(void *), ARCH_SLAB_MINALIGN)

#define RELOC_FAILED 0xff00ff01		/* Relocation incorrect somewhere */
#define UNLOADED_LIB 0x7ff000ff		/* Placeholder for unused library */

struct lib_info {
	struct {
		unsigned long start_code;		/* Start of text segment */
		unsigned long start_data;		/* Start of data segment */
		unsigned long start_brk;		/* End of data segment */
		unsigned long text_len;			/* Length of text segment */
		unsigned long entry;			/* Start address for this module */
		unsigned long build_date;		/* When this one was compiled */
		short loaded;				/* Has this library been loaded? */
	} lib_list[MAX_SHARED_LIBS];
};

#ifdef CONFIG_BINFMT_SHARED_FLAT
static int load_flat_shared_library(int id, struct lib_info *p);
#endif

static int load_flat_binary(struct linux_binprm *, struct pt_regs * regs);
static int flat_core_dump(struct coredump_params *cprm);

static struct linux_binfmt flat_format = {
	.module		= THIS_MODULE,
	.load_binary	= load_flat_binary,
	.core_dump	= flat_core_dump,
	.min_coredump	= PAGE_SIZE
};

/****************************************************************************/
/*
 * Routine writes a core dump image in the current directory.
 * Currently only a stub-function.
 */

static int flat_core_dump(struct coredump_params *cprm)
{
	printk("Process %s:%d received signr %d and should have core dumped\n",
			current->comm, current->pid, (int) cprm->signr);
	return(1);
}

/****************************************************************************/
/*
 * create_flat_tables() parses the env- and arg-strings in new user
 * memory and creates the pointer tables from them, and puts their
 * addresses on the "stack", returning the new stack pointer value.
 */

static unsigned long create_flat_tables(
	unsigned long pp,
	struct linux_binprm * bprm)
{
	unsigned long *argv,*envp;
	unsigned long * sp;
	char * p = (char*)pp;
	int argc = bprm->argc;
	int envc = bprm->envc;
	char uninitialized_var(dummy);

	sp = (unsigned long *)p;
	sp -= (envc + argc + 2) + 1 + (flat_argvp_envp_on_stack() ? 2 : 0);
	sp = (unsigned long *) ((unsigned long)sp & -FLAT_STACK_ALIGN);
	argv = sp + 1 + (flat_argvp_envp_on_stack() ? 2 : 0);
	envp = argv + (argc + 1);

	if (flat_argvp_envp_on_stack()) {
		put_user((unsigned long) envp, sp + 2);
		put_user((unsigned long) argv, sp + 1);
	}

	put_user(argc, sp);
	current->mm->arg_start = (unsigned long) p;
	while (argc-->0) {
		put_user((unsigned long) p, argv++);
		do {
			get_user(dummy, p); p++;
		} while (dummy);
	}
	put_user((unsigned long) NULL, argv);
	current->mm->arg_end = current->mm->env_start = (unsigned long) p;
	while (envc-->0) {
		put_user((unsigned long)p, envp); envp++;
		do {
			get_user(dummy, p); p++;
		} while (dummy);
	}
	put_user((unsigned long) NULL, envp);
	current->mm->env_end = (unsigned long) p;
	return (unsigned long)sp;
}

/****************************************************************************/

#ifdef CONFIG_BINFMT_ZFLAT

#include <linux/zlib.h>

#define LBUFSIZE	4000

/* gzip flag byte */
#define ASCII_FLAG   0x01 /* bit 0 set: file probably ASCII text */
#define CONTINUATION 0x02 /* bit 1 set: continuation of multi-part gzip file */
#define EXTRA_FIELD  0x04 /* bit 2 set: extra field present */
#define ORIG_NAME    0x08 /* bit 3 set: original file name present */
#define COMMENT      0x10 /* bit 4 set: file comment present */
#define ENCRYPTED    0x20 /* bit 5 set: file is encrypted */
#define RESERVED     0xC0 /* bit 6,7:   reserved */

static int decompress_exec(
	struct linux_binprm *bprm,
	unsigned long offset,
	char *dst,
	long len,
	int fd)
{
	unsigned char *buf;
	z_stream strm;
	loff_t fpos;
	int ret, retval;

	DBG_FLT("decompress_exec(offset=%x,buf=%x,len=%x)\n",(int)offset, (int)dst, (int)len);

	memset(&strm, 0, sizeof(strm));
	strm.workspace = kmalloc(zlib_inflate_workspacesize(), GFP_KERNEL);
	if (strm.workspace == NULL) {
		DBG_FLT("binfmt_flat: no memory for decompress workspace\n");
		return -ENOMEM;
	}
	buf = kmalloc(LBUFSIZE, GFP_KERNEL);
	if (buf == NULL) {
		DBG_FLT("binfmt_flat: no memory for read buffer\n");
		retval = -ENOMEM;
		goto out_free;
	}

	/* Read in first chunk of data and parse gzip header. */
	fpos = offset;
	ret = bprm->file->f_op->read(bprm->file, buf, LBUFSIZE, &fpos);

	strm.next_in = buf;
	strm.avail_in = ret;
	strm.total_in = 0;

	retval = -ENOEXEC;

	/* Check minimum size -- gzip header */
	if (ret < 10) {
		DBG_FLT("binfmt_flat: file too small?\n");
		goto out_free_buf;
	}

	/* Check gzip magic number */
	if ((buf[0] != 037) || ((buf[1] != 0213) && (buf[1] != 0236))) {
		DBG_FLT("binfmt_flat: unknown compression magic?\n");
		goto out_free_buf;
	}

	/* Check gzip method */
	if (buf[2] != 8) {
		DBG_FLT("binfmt_flat: unknown compression method?\n");
		goto out_free_buf;
	}
	/* Check gzip flags */
	if ((buf[3] & ENCRYPTED) || (buf[3] & CONTINUATION) ||
	    (buf[3] & RESERVED)) {
		DBG_FLT("binfmt_flat: unknown flags?\n");
		goto out_free_buf;
	}

	ret = 10;
	if (buf[3] & EXTRA_FIELD) {
		ret += 2 + buf[10] + (buf[11] << 8);
		if (unlikely(LBUFSIZE <= ret)) {
			DBG_FLT("binfmt_flat: buffer overflow (EXTRA)?\n");
			goto out_free_buf;
		}
	}
	if (buf[3] & ORIG_NAME) {
		while (ret < LBUFSIZE && buf[ret++] != 0)
			;
		if (unlikely(LBUFSIZE == ret)) {
			DBG_FLT("binfmt_flat: buffer overflow (ORIG_NAME)?\n");
			goto out_free_buf;
		}
	}
	if (buf[3] & COMMENT) {
		while (ret < LBUFSIZE && buf[ret++] != 0)
			;
		if (unlikely(LBUFSIZE == ret)) {
			DBG_FLT("binfmt_flat: buffer overflow (COMMENT)?\n");
			goto out_free_buf;
		}
	}

	strm.next_in += ret;
	strm.avail_in -= ret;

	strm.next_out = dst;
	strm.avail_out = len;
	strm.total_out = 0;

	if (zlib_inflateInit2(&strm, -MAX_WBITS) != Z_OK) {
		DBG_FLT("binfmt_flat: zlib init failed?\n");
		goto out_free_buf;
	}

	while ((ret = zlib_inflate(&strm, Z_NO_FLUSH)) == Z_OK) {
		ret = bprm->file->f_op->read(bprm->file, buf, LBUFSIZE, &fpos);
		if (ret <= 0)
			break;
		len -= ret;

		strm.next_in = buf;
		strm.avail_in = ret;
		strm.total_in = 0;
	}

	if (ret < 0) {
		DBG_FLT("binfmt_flat: decompression failed (%d), %s\n",
			ret, strm.msg);
		goto out_zlib;
	}

	retval = 0;
out_zlib:
	zlib_inflateEnd(&strm);
out_free_buf:
	kfree(buf);
out_free:
	kfree(strm.workspace);
	return retval;
}

#endif /* CONFIG_BINFMT_ZFLAT */

/****************************************************************************/

static unsigned long
calc_reloc(unsigned long r, struct lib_info *p, int curid, int internalp)
{
	unsigned long addr;
	int id;
	unsigned long start_brk;
	unsigned long start_data;
	unsigned long text_len;
	unsigned long start_code;

#ifdef CONFIG_BINFMT_SHARED_FLAT
	if (r == 0)
		id = curid;	/* Relocs of 0 are always self referring */
	else {
		id = (r >> 24) & 0xff;	/* Find ID for this reloc */
		r &= 0x00ffffff;	/* Trim ID off here */
	}
	if (id >= MAX_SHARED_LIBS) {
		printk("BINFMT_FLAT: reference 0x%x to shared library %d",
				(unsigned) r, id);
		goto failed;
	}
	if (curid != id) {
		if (internalp) {
			printk("BINFMT_FLAT: reloc address 0x%x not in same module "
					"(%d != %d)", (unsigned) r, curid, id);
			goto failed;
		} else if ( ! p->lib_list[id].loaded &&
				IS_ERR_VALUE(load_flat_shared_library(id, p))) {
			printk("BINFMT_FLAT: failed to load library %d", id);
			goto failed;
		}
		/* Check versioning information (i.e. time stamps) */
		if (p->lib_list[id].build_date && p->lib_list[curid].build_date &&
				p->lib_list[curid].build_date < p->lib_list[id].build_date) {
			printk("BINFMT_FLAT: library %d is younger than %d", id, curid);
			goto failed;
		}
	}
#else
	id = 0;
#endif

	start_brk = p->lib_list[id].start_brk;
	start_data = p->lib_list[id].start_data;
	start_code = p->lib_list[id].start_code;
	text_len = p->lib_list[id].text_len;

	if (!flat_reloc_valid(r, start_brk - start_data + text_len)) {
		printk("BINFMT_FLAT: reloc outside program 0x%x (0 - 0x%x/0x%x)",
		       (int) r,(int)(start_brk-start_data+text_len),(int)text_len);
		goto failed;
	}

	if (r < text_len)			/* In text segment */
		addr = r + start_code;
	else					/* In data segment */
		addr = r - text_len + start_data;

	/* Range checked already above so doing the range tests is redundant...*/
	return(addr);

failed:
	printk(", killing %s!\n", current->comm);
	send_sig(SIGSEGV, current, 0);

	return RELOC_FAILED;
}

/****************************************************************************/

void old_reloc(unsigned long rl)
{
#ifdef DEBUG
	char *segment[] = { "TEXT", "DATA", "BSS", "*UNKNOWN*" };
#endif
	flat_v2_reloc_t	r;
	unsigned long *ptr;
	
	r.value = rl;
#if defined(CONFIG_COLDFIRE)
	ptr = (unsigned long *) (current->mm->start_code + r.reloc.offset);
#else
	ptr = (unsigned long *) (current->mm->start_data + r.reloc.offset);
#endif

#ifdef DEBUG
	printk("Relocation of variable at DATASEG+%x "
		"(address %p, currently %x) into segment %s\n",
		r.reloc.offset, ptr, (int)*ptr, segment[r.reloc.type]);
#endif
	
	switch (r.reloc.type) {
	case OLD_FLAT_RELOC_TYPE_TEXT:
		*ptr += current->mm->start_code;
		break;
	case OLD_FLAT_RELOC_TYPE_DATA:
		*ptr += current->mm->start_data;
		break;
	case OLD_FLAT_RELOC_TYPE_BSS:
		*ptr += current->mm->end_data;
		break;
	default:
		printk("BINFMT_FLAT: Unknown relocation type=%x\n", r.reloc.type);
		break;
	}

#ifdef DEBUG
	printk("Relocation became %x\n", (int)*ptr);
#endif
}		

/****************************************************************************/

static int load_flat_file(struct linux_binprm * bprm,
		struct lib_info *libinfo, int id, unsigned long *extra_stack)
{
	struct flat_hdr * hdr;
	unsigned long textpos = 0, datapos = 0, result;
	unsigned long realdatastart = 0;
	unsigned long text_len, data_len, bss_len, stack_len, flags;
	unsigned long len, memp = 0;
	unsigned long memp_size, extra, rlim;
	unsigned long *reloc = 0, *rp;
	struct inode *inode;
	int i, rev, relocs = 0;
	loff_t fpos;
	unsigned long start_code, end_code;
	int ret;

	hdr = ((struct flat_hdr *) bprm->buf);		/* exec-header */
	inode = bprm->file->f_path.dentry->d_inode;

	text_len  = ntohl(hdr->data_start);
	data_len  = ntohl(hdr->data_end) - ntohl(hdr->data_start);
	bss_len   = ntohl(hdr->bss_end) - ntohl(hdr->data_end);
	stack_len = ntohl(hdr->stack_size);
	if (extra_stack) {
		stack_len += *extra_stack;
		*extra_stack = stack_len;
	}
	relocs    = ntohl(hdr->reloc_count);
	flags     = ntohl(hdr->flags);
	rev       = ntohl(hdr->rev);

	if (strncmp(hdr->magic, "bFLT", 4)) {
		/*
		 * Previously, here was a printk to tell people
		 *   "BINFMT_FLAT: bad header magic".
		 * But for the kernel which also use ELF FD-PIC format, this
		 * error message is confusing.
		 * because a lot of people do not manage to produce good
		 */
		ret = -ENOEXEC;
		goto err;
	}

	if (flags & FLAT_FLAG_KTRACE)
		printk("BINFMT_FLAT: Loading file: %s\n", bprm->filename);

	if (rev != FLAT_VERSION && rev != OLD_FLAT_VERSION) {
		printk("BINFMT_FLAT: bad flat file version 0x%x (supported "
			"0x%lx and 0x%lx)\n",
			rev, FLAT_VERSION, OLD_FLAT_VERSION);
		ret = -ENOEXEC;
		goto err;
	}
	
	/* Don't allow old format executables to use shared libraries */
	if (rev == OLD_FLAT_VERSION && id != 0) {
		printk("BINFMT_FLAT: shared libraries are not available before rev 0x%x\n",
				(int) FLAT_VERSION);
		ret = -ENOEXEC;
		goto err;
	}

	/*
	 * fix up the flags for the older format,  there were all kinds
	 * of endian hacks,  this only works for the simple cases
	 */
	if (rev == OLD_FLAT_VERSION && flat_old_ram_flag(flags))
		flags = FLAT_FLAG_RAM;

#ifndef CONFIG_BINFMT_ZFLAT
	if (flags & (FLAT_FLAG_GZIP|FLAT_FLAG_GZDATA)) {
		printk("Support for ZFLAT executables is not enabled.\n");
		ret = -ENOEXEC;
		goto err;
	}
#endif

	/*
	 * Check initial limits. This avoids letting people circumvent
	 * size limits imposed on them by creating programs with large
	 * arrays in the data or bss.
	 */
	rlim = rlimit(RLIMIT_DATA);
	if (rlim >= RLIM_INFINITY)
		rlim = ~0;
	if (data_len + bss_len > rlim) {
		ret = -ENOMEM;
		goto err;
	}

	/* Flush all traces of the currently running executable */
	if (id == 0) {
		result = flush_old_exec(bprm);
		if (result) {
			ret = result;
			goto err;
		}

		/* OK, This is the point of no return */
		set_personality(PER_LINUX_32BIT);
		setup_new_exec(bprm);
	}

	/*
	 * calculate the extra space we need to map in
	 */
	extra = max_t(unsigned long, bss_len + stack_len,
			relocs * sizeof(unsigned long));

	/*
	 * there are a couple of cases here,  the separate code/data
	 * case,  and then the fully copied to RAM case which lumps
	 * it all together.
	 */
	if ((flags & (FLAT_FLAG_RAM|FLAT_FLAG_GZIP)) == 0) {
		/*
		 * this should give us a ROM ptr,  but if it doesn't we don't
		 * really care
		 */
		DBG_FLT("BINFMT_FLAT: ROM mapping of file (we hope)\n");

		down_write(&current->mm->mmap_sem);
		textpos = do_mmap(bprm->file, 0, text_len, PROT_READ|PROT_EXEC,
				  MAP_PRIVATE|MAP_EXECUTABLE, 0);
		up_write(&current->mm->mmap_sem);
		if (!textpos || IS_ERR_VALUE(textpos)) {
			if (!textpos)
				textpos = (unsigned long) -ENOMEM;
			printk("Unable to mmap process text, errno %d\n", (int)-textpos);
			ret = textpos;
			goto err;
		}

		len = data_len + extra + MAX_SHARED_LIBS * sizeof(unsigned long);
		len = PAGE_ALIGN(len);
		down_write(&current->mm->mmap_sem);
		realdatastart = do_mmap(0, 0, len,
			PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE, 0);
		up_write(&current->mm->mmap_sem);

		if (realdatastart == 0 || IS_ERR_VALUE(realdatastart)) {
			if (!realdatastart)
				realdatastart = (unsigned long) -ENOMEM;
			printk("Unable to allocate RAM for process data, errno %d\n",
					(int)-realdatastart);
			do_munmap(current->mm, textpos, text_len);
			ret = realdatastart;
			goto err;
		}
		datapos = ALIGN(realdatastart +
				MAX_SHARED_LIBS * sizeof(unsigned long),
				FLAT_DATA_ALIGN);

		DBG_FLT("BINFMT_FLAT: Allocated data+bss+stack (%d bytes): %x\n",
				(int)(data_len + bss_len + stack_len), (int)datapos);

		fpos = ntohl(hdr->data_start);
#ifdef CONFIG_BINFMT_ZFLAT
		if (flags & FLAT_FLAG_GZDATA) {
			result = decompress_exec(bprm, fpos, (char *) datapos, 
						 data_len + (relocs * sizeof(unsigned long)), 0);
		} else
#endif
		{
			result = bprm->file->f_op->read(bprm->file, (char *) datapos,
					data_len + (relocs * sizeof(unsigned long)), &fpos);
		}
		if (IS_ERR_VALUE(result)) {
			printk("Unable to read data+bss, errno %d\n", (int)-result);
			do_munmap(current->mm, textpos, text_len);
			do_munmap(current->mm, realdatastart, len);
			ret = result;
			goto err;
		}

		reloc = (unsigned long *) (datapos+(ntohl(hdr->reloc_start)-text_len));
		memp = realdatastart;
		memp_size = len;
	} else {

		len = text_len + data_len + extra + MAX_SHARED_LIBS * sizeof(unsigned long);
		len = PAGE_ALIGN(len);
		down_write(&current->mm->mmap_sem);
		textpos = do_mmap(0, 0, len,
			PROT_READ | PROT_EXEC | PROT_WRITE, MAP_PRIVATE, 0);
		up_write(&current->mm->mmap_sem);

		if (!textpos || IS_ERR_VALUE(textpos)) {
			if (!textpos)
				textpos = (unsigned long) -ENOMEM;
			printk("Unable to allocate RAM for process text/data, errno %d\n",
					(int)-textpos);
			ret = textpos;
			goto err;
		}

		realdatastart = textpos + ntohl(hdr->data_start);
		datapos = ALIGN(realdatastart +
				MAX_SHARED_LIBS * sizeof(unsigned long),
				FLAT_DATA_ALIGN);

		reloc = (unsigned long *)
			(datapos + (ntohl(hdr->reloc_start) - text_len));
		memp = textpos;
		memp_size = len;
#ifdef CONFIG_BINFMT_ZFLAT
		/*
		 * load it all in and treat it like a RAM load from now on
		 */
		if (flags & FLAT_FLAG_GZIP) {
			result = decompress_exec(bprm, sizeof (struct flat_hdr),
					 (((char *) textpos) + sizeof (struct flat_hdr)),
					 (text_len + data_len + (relocs * sizeof(unsigned long))
						  - sizeof (struct flat_hdr)),
					 0);
			memmove((void *) datapos, (void *) realdatastart,
					data_len + (relocs * sizeof(unsigned long)));
		} else if (flags & FLAT_FLAG_GZDATA) {
			fpos = 0;
			result = bprm->file->f_op->read(bprm->file,
					(char *) textpos, text_len, &fpos);
			if (!IS_ERR_VALUE(result))
				result = decompress_exec(bprm, text_len, (char *) datapos,
						 data_len + (relocs * sizeof(unsigned long)), 0);
		}
		else
#endif
		{
			fpos = 0;
			result = bprm->file->f_op->read(bprm->file,
					(char *) textpos, text_len, &fpos);
			if (!IS_ERR_VALUE(result)) {
				fpos = ntohl(hdr->data_start);
				result = bprm->file->f_op->read(bprm->file, (char *) datapos,
					data_len + (relocs * sizeof(unsigned long)), &fpos);
			}
		}
		if (IS_ERR_VALUE(result)) {
			printk("Unable to read code+data+bss, errno %d\n",(int)-result);
			do_munmap(current->mm, textpos, text_len + data_len + extra +
				MAX_SHARED_LIBS * sizeof(unsigned long));
			ret = result;
			goto err;
		}
	}

	if (flags & FLAT_FLAG_KTRACE)
		printk("Mapping is %x, Entry point is %x, data_start is %x\n",
			(int)textpos, 0x00ffffff&ntohl(hdr->entry), ntohl(hdr->data_start));

	/* The main program needs a little extra setup in the task structure */
	start_code = textpos + sizeof (struct flat_hdr);
	end_code = textpos + text_len;
	if (id == 0) {
		current->mm->start_code = start_code;
		current->mm->end_code = end_code;
		current->mm->start_data = datapos;
		current->mm->end_data = datapos + data_len;
		/*
		 * set up the brk stuff, uses any slack left in data/bss/stack
		 * allocation.  We put the brk after the bss (between the bss
		 * and stack) like other platforms.
		 * Userspace code relies on the stack pointer starting out at
		 * an address right at the end of a page.
		 */
		current->mm->start_brk = datapos + data_len + bss_len;
		current->mm->brk = (current->mm->start_brk + 3) & ~3;
		current->mm->context.end_brk = memp + memp_size - stack_len;
	}

	if (flags & FLAT_FLAG_KTRACE)
		printk("%s %s: TEXT=%x-%x DATA=%x-%x BSS=%x-%x\n",
			id ? "Lib" : "Load", bprm->filename,
			(int) start_code, (int) end_code,
			(int) datapos,
			(int) (datapos + data_len),
			(int) (datapos + data_len),
			(int) (((datapos + data_len + bss_len) + 3) & ~3));

	text_len -= sizeof(struct flat_hdr); /* the real code len */

	/* Store the current module values into the global library structure */
	libinfo->lib_list[id].start_code = start_code;
	libinfo->lib_list[id].start_data = datapos;
	libinfo->lib_list[id].start_brk = datapos + data_len + bss_len;
	libinfo->lib_list[id].text_len = text_len;
	libinfo->lib_list[id].loaded = 1;
	libinfo->lib_list[id].entry = (0x00ffffff & ntohl(hdr->entry)) + textpos;
	libinfo->lib_list[id].build_date = ntohl(hdr->build_date);
	
	/*
	 * We just load the allocations into some temporary memory to
	 * help simplify all this mumbo jumbo
	 *
	 * We've got two different sections of relocation entries.
	 * The first is the GOT which resides at the beginning of the data segment
	 * and is terminated with a -1.  This one can be relocated in place.
	 * The second is the extra relocation entries tacked after the image's
	 * data segment. These require a little more processing as the entry is
	 * really an offset into the image which contains an offset into the
	 * image.
	 */
	if (flags & FLAT_FLAG_GOTPIC) {
		for (rp = (unsigned long *)datapos; *rp != 0xffffffff; rp++) {
			unsigned long addr;
			if (*rp) {
				addr = calc_reloc(*rp, libinfo, id, 0);
				if (addr == RELOC_FAILED) {
					ret = -ENOEXEC;
					goto err;
				}
				*rp = addr;
			}
		}
	}

	/*
	 * Now run through the relocation entries.
	 * We've got to be careful here as C++ produces relocatable zero
	 * entries in the constructor and destructor tables which are then
	 * tested for being not zero (which will always occur unless we're
	 * based from address zero).  This causes an endless loop as __start
	 * is at zero.  The solution used is to not relocate zero addresses.
	 * This has the negative side effect of not allowing a global data
	 * reference to be statically initialised to _stext (I've moved
	 * __start to address 4 so that is okay).
	 */
	if (rev > OLD_FLAT_VERSION) {
		unsigned long persistent = 0;
		for (i=0; i < relocs; i++) {
			unsigned long addr, relval;

			/* Get the address of the pointer to be
			   relocated (of course, the address has to be
			   relocated first).  */
			relval = ntohl(reloc[i]);
			if (flat_set_persistent (relval, &persistent))
				continue;
			addr = flat_get_relocate_addr(relval);
			rp = (unsigned long *) calc_reloc(addr, libinfo, id, 1);
			if (rp == (unsigned long *)RELOC_FAILED) {
				ret = -ENOEXEC;
				goto err;
			}

			/* Get the pointer's value.  */
			addr = flat_get_addr_from_rp(rp, relval, flags,
							&persistent);
			if (addr != 0) {
				/*
				 * Do the relocation.  PIC relocs in the data section are
				 * already in target order
				 */
				if ((flags & FLAT_FLAG_GOTPIC) == 0)
					addr = ntohl(addr);
				addr = calc_reloc(addr, libinfo, id, 0);
				if (addr == RELOC_FAILED) {
					ret = -ENOEXEC;
					goto err;
				}

				/* Write back the relocated pointer.  */
				flat_put_addr_at_rp(rp, addr, relval);
			}
		}
	} else {
		for (i=0; i < relocs; i++)
			old_reloc(ntohl(reloc[i]));
	}
	
	flush_icache_range(start_code, end_code);

	/* zero the BSS,  BRK and stack areas */
	memset((void*)(datapos + data_len), 0, bss_len + 
			(memp + memp_size - stack_len -		/* end brk */
			libinfo->lib_list[id].start_brk) +	/* start brk */
			stack_len);

	return 0;
err:
	return ret;
}


/****************************************************************************/
#ifdef CONFIG_BINFMT_SHARED_FLAT

/*
 * Load a shared library into memory.  The library gets its own data
 * segment (including bss) but not argv/argc/environ.
 */

static int load_flat_shared_library(int id, struct lib_info *libs)
{
	struct linux_binprm bprm;
	int res;
	char buf[16];

	memset(&bprm, 0, sizeof(bprm));

	/* Create the file name */
	sprintf(buf, "/lib/lib%d.so", id);

	/* Open the file up */
	bprm.filename = buf;
	bprm.file = open_exec(bprm.filename);
	res = PTR_ERR(bprm.file);
	if (IS_ERR(bprm.file))
		return res;

	bprm.cred = prepare_exec_creds();
	res = -ENOMEM;
	if (!bprm.cred)
		goto out;

	/* We don't really care about recalculating credentials at this point
	 * as we're past the point of no return and are dealing with shared
	 * libraries.
	 */
	bprm.cred_prepared = 1;

	res = prepare_binprm(&bprm);

	if (!IS_ERR_VALUE(res))
		res = load_flat_file(&bprm, libs, id, NULL);

	abort_creds(bprm.cred);

out:
	allow_write_access(bprm.file);
	fput(bprm.file);

	return(res);
}

#endif /* CONFIG_BINFMT_SHARED_FLAT */
/****************************************************************************/

/*
 * These are the functions used to load flat style executables and shared
 * libraries.  There is no binary dependent code anywhere else.
 */

static int load_flat_binary(struct linux_binprm * bprm, struct pt_regs * regs)
{
	struct lib_info libinfo;
	unsigned long p = bprm->p;
	unsigned long stack_len;
	unsigned long start_addr;
	unsigned long *sp;
	int res;
	int i, j;

	memset(&libinfo, 0, sizeof(libinfo));
	/*
	 * We have to add the size of our arguments to our stack size
	 * otherwise it's too easy for users to create stack overflows
	 * by passing in a huge argument list.  And yes,  we have to be
	 * pedantic and include space for the argv/envp array as it may have
	 * a lot of entries.
	 */
#define TOP_OF_ARGS (PAGE_SIZE * MAX_ARG_PAGES - sizeof(void *))
	stack_len = TOP_OF_ARGS - bprm->p;             /* the strings */
	stack_len += (bprm->argc + 1) * sizeof(char *); /* the argv array */
	stack_len += (bprm->envc + 1) * sizeof(char *); /* the envp array */
	stack_len += FLAT_STACK_ALIGN - 1;  /* reserve for upcoming alignment */
	
	res = load_flat_file(bprm, &libinfo, 0, &stack_len);
	if (IS_ERR_VALUE(res))
		return res;
	
	/* Update data segment pointers for all libraries */
	for (i=0; i<MAX_SHARED_LIBS; i++)
		if (libinfo.lib_list[i].loaded)
			for (j=0; j<MAX_SHARED_LIBS; j++)
				(-(j+1))[(unsigned long *)(libinfo.lib_list[i].start_data)] =
					(libinfo.lib_list[j].loaded)?
						libinfo.lib_list[j].start_data:UNLOADED_LIB;

	install_exec_creds(bprm);
 	current->flags &= ~PF_FORKNOEXEC;

	set_binfmt(&flat_format);

	p = ((current->mm->context.end_brk + stack_len + 3) & ~3) - 4;
	DBG_FLT("p=%x\n", (int)p);

	/* copy the arg pages onto the stack, this could be more efficient :-) */
	for (i = TOP_OF_ARGS - 1; i >= bprm->p; i--)
		* (char *) --p =
			((char *) page_address(bprm->page[i/PAGE_SIZE]))[i % PAGE_SIZE];

	sp = (unsigned long *) create_flat_tables(p, bprm);
	
	/* Fake some return addresses to ensure the call chain will
	 * initialise library in order for us.  We are required to call
	 * lib 1 first, then 2, ... and finally the main program (id 0).
	 */
	start_addr = libinfo.lib_list[0].entry;

#ifdef CONFIG_BINFMT_SHARED_FLAT
	for (i = MAX_SHARED_LIBS-1; i>0; i--) {
		if (libinfo.lib_list[i].loaded) {
			/* Push previos first to call address */
			--sp;	put_user(start_addr, sp);
			start_addr = libinfo.lib_list[i].entry;
		}
	}
#endif
	
	/* Stash our initial stack pointer into the mm structure */
	current->mm->start_stack = (unsigned long )sp;

#ifdef FLAT_PLAT_INIT
	FLAT_PLAT_INIT(regs);
#endif
	DBG_FLT("start_thread(regs=0x%x, entry=0x%x, start_stack=0x%x)\n",
		(int)regs, (int)start_addr, (int)current->mm->start_stack);
	
	start_thread(regs, start_addr, current->mm->start_stack);

	return 0;
}

/****************************************************************************/

static int __init init_flat_binfmt(void)
{
	return register_binfmt(&flat_format);
}

/****************************************************************************/

core_initcall(init_flat_binfmt);

/****************************************************************************/
