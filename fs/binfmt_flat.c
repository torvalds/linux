// SPDX-License-Identifier: GPL-2.0
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

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/slab.h>
#include <linux/binfmts.h>
#include <linux/personality.h>
#include <linux/init.h>
#include <linux/flat.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include <asm/byteorder.h>
#include <asm/unaligned.h>
#include <asm/cacheflush.h>
#include <asm/page.h>
#include <asm/flat.h>

#ifndef flat_get_relocate_addr
#define flat_get_relocate_addr(rel)	(rel)
#endif

/****************************************************************************/

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

#ifdef CONFIG_BINFMT_SHARED_FLAT
#define	MAX_SHARED_LIBS			(4)
#else
#define	MAX_SHARED_LIBS			(1)
#endif

struct lib_info {
	struct {
		unsigned long start_code;		/* Start of text segment */
		unsigned long start_data;		/* Start of data segment */
		unsigned long start_brk;		/* End of data segment */
		unsigned long text_len;			/* Length of text segment */
		unsigned long entry;			/* Start address for this module */
		unsigned long build_date;		/* When this one was compiled */
		bool loaded;				/* Has this library been loaded? */
	} lib_list[MAX_SHARED_LIBS];
};

#ifdef CONFIG_BINFMT_SHARED_FLAT
static int load_flat_shared_library(int id, struct lib_info *p);
#endif

static int load_flat_binary(struct linux_binprm *);
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
	pr_warn("Process %s:%d received signr %d and should have core dumped\n",
		current->comm, current->pid, cprm->siginfo->si_signo);
	return 1;
}

/****************************************************************************/
/*
 * create_flat_tables() parses the env- and arg-strings in new user
 * memory and creates the pointer tables from them, and puts their
 * addresses on the "stack", recording the new stack pointer value.
 */

static int create_flat_tables(struct linux_binprm *bprm, unsigned long arg_start)
{
	char __user *p;
	unsigned long __user *sp;
	long i, len;

	p = (char __user *)arg_start;
	sp = (unsigned long __user *)current->mm->start_stack;

	sp -= bprm->envc + 1;
	sp -= bprm->argc + 1;
	if (IS_ENABLED(CONFIG_BINFMT_FLAT_ARGVP_ENVP_ON_STACK))
		sp -= 2; /* argvp + envp */
	sp -= 1;  /* &argc */

	current->mm->start_stack = (unsigned long)sp & -FLAT_STACK_ALIGN;
	sp = (unsigned long __user *)current->mm->start_stack;

	if (put_user(bprm->argc, sp++))
		return -EFAULT;
	if (IS_ENABLED(CONFIG_BINFMT_FLAT_ARGVP_ENVP_ON_STACK)) {
		unsigned long argv, envp;
		argv = (unsigned long)(sp + 2);
		envp = (unsigned long)(sp + 2 + bprm->argc + 1);
		if (put_user(argv, sp++) || put_user(envp, sp++))
			return -EFAULT;
	}

	current->mm->arg_start = (unsigned long)p;
	for (i = bprm->argc; i > 0; i--) {
		if (put_user((unsigned long)p, sp++))
			return -EFAULT;
		len = strnlen_user(p, MAX_ARG_STRLEN);
		if (!len || len > MAX_ARG_STRLEN)
			return -EINVAL;
		p += len;
	}
	if (put_user(0, sp++))
		return -EFAULT;
	current->mm->arg_end = (unsigned long)p;

	current->mm->env_start = (unsigned long) p;
	for (i = bprm->envc; i > 0; i--) {
		if (put_user((unsigned long)p, sp++))
			return -EFAULT;
		len = strnlen_user(p, MAX_ARG_STRLEN);
		if (!len || len > MAX_ARG_STRLEN)
			return -EINVAL;
		p += len;
	}
	if (put_user(0, sp++))
		return -EFAULT;
	current->mm->env_end = (unsigned long)p;

	return 0;
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

static int decompress_exec(struct linux_binprm *bprm, loff_t fpos, char *dst,
		long len, int fd)
{
	unsigned char *buf;
	z_stream strm;
	int ret, retval;

	pr_debug("decompress_exec(offset=%llx,buf=%p,len=%lx)\n", fpos, dst, len);

	memset(&strm, 0, sizeof(strm));
	strm.workspace = kmalloc(zlib_inflate_workspacesize(), GFP_KERNEL);
	if (!strm.workspace)
		return -ENOMEM;

	buf = kmalloc(LBUFSIZE, GFP_KERNEL);
	if (!buf) {
		retval = -ENOMEM;
		goto out_free;
	}

	/* Read in first chunk of data and parse gzip header. */
	ret = kernel_read(bprm->file, buf, LBUFSIZE, &fpos);

	strm.next_in = buf;
	strm.avail_in = ret;
	strm.total_in = 0;

	retval = -ENOEXEC;

	/* Check minimum size -- gzip header */
	if (ret < 10) {
		pr_debug("file too small?\n");
		goto out_free_buf;
	}

	/* Check gzip magic number */
	if ((buf[0] != 037) || ((buf[1] != 0213) && (buf[1] != 0236))) {
		pr_debug("unknown compression magic?\n");
		goto out_free_buf;
	}

	/* Check gzip method */
	if (buf[2] != 8) {
		pr_debug("unknown compression method?\n");
		goto out_free_buf;
	}
	/* Check gzip flags */
	if ((buf[3] & ENCRYPTED) || (buf[3] & CONTINUATION) ||
	    (buf[3] & RESERVED)) {
		pr_debug("unknown flags?\n");
		goto out_free_buf;
	}

	ret = 10;
	if (buf[3] & EXTRA_FIELD) {
		ret += 2 + buf[10] + (buf[11] << 8);
		if (unlikely(ret >= LBUFSIZE)) {
			pr_debug("buffer overflow (EXTRA)?\n");
			goto out_free_buf;
		}
	}
	if (buf[3] & ORIG_NAME) {
		while (ret < LBUFSIZE && buf[ret++] != 0)
			;
		if (unlikely(ret == LBUFSIZE)) {
			pr_debug("buffer overflow (ORIG_NAME)?\n");
			goto out_free_buf;
		}
	}
	if (buf[3] & COMMENT) {
		while (ret < LBUFSIZE && buf[ret++] != 0)
			;
		if (unlikely(ret == LBUFSIZE)) {
			pr_debug("buffer overflow (COMMENT)?\n");
			goto out_free_buf;
		}
	}

	strm.next_in += ret;
	strm.avail_in -= ret;

	strm.next_out = dst;
	strm.avail_out = len;
	strm.total_out = 0;

	if (zlib_inflateInit2(&strm, -MAX_WBITS) != Z_OK) {
		pr_debug("zlib init failed?\n");
		goto out_free_buf;
	}

	while ((ret = zlib_inflate(&strm, Z_NO_FLUSH)) == Z_OK) {
		ret = kernel_read(bprm->file, buf, LBUFSIZE, &fpos);
		if (ret <= 0)
			break;
		len -= ret;

		strm.next_in = buf;
		strm.avail_in = ret;
		strm.total_in = 0;
	}

	if (ret < 0) {
		pr_debug("decompression failed (%d), %s\n",
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
		pr_err("reference 0x%lx to shared library %d", r, id);
		goto failed;
	}
	if (curid != id) {
		if (internalp) {
			pr_err("reloc address 0x%lx not in same module "
			       "(%d != %d)", r, curid, id);
			goto failed;
		} else if (!p->lib_list[id].loaded &&
			   load_flat_shared_library(id, p) < 0) {
			pr_err("failed to load library %d", id);
			goto failed;
		}
		/* Check versioning information (i.e. time stamps) */
		if (p->lib_list[id].build_date && p->lib_list[curid].build_date &&
				p->lib_list[curid].build_date < p->lib_list[id].build_date) {
			pr_err("library %d is younger than %d", id, curid);
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

	if (r > start_brk - start_data + text_len) {
		pr_err("reloc outside program 0x%lx (0 - 0x%lx/0x%lx)",
		       r, start_brk-start_data+text_len, text_len);
		goto failed;
	}

	if (r < text_len)			/* In text segment */
		addr = r + start_code;
	else					/* In data segment */
		addr = r - text_len + start_data;

	/* Range checked already above so doing the range tests is redundant...*/
	return addr;

failed:
	pr_cont(", killing %s!\n", current->comm);
	send_sig(SIGSEGV, current, 0);

	return RELOC_FAILED;
}

/****************************************************************************/

#ifdef CONFIG_BINFMT_FLAT_OLD
static void old_reloc(unsigned long rl)
{
	static const char *segment[] = { "TEXT", "DATA", "BSS", "*UNKNOWN*" };
	flat_v2_reloc_t	r;
	unsigned long __user *ptr;
	unsigned long val;

	r.value = rl;
#if defined(CONFIG_COLDFIRE)
	ptr = (unsigned long __user *)(current->mm->start_code + r.reloc.offset);
#else
	ptr = (unsigned long __user *)(current->mm->start_data + r.reloc.offset);
#endif
	get_user(val, ptr);

	pr_debug("Relocation of variable at DATASEG+%x "
		 "(address %p, currently %lx) into segment %s\n",
		 r.reloc.offset, ptr, val, segment[r.reloc.type]);

	switch (r.reloc.type) {
	case OLD_FLAT_RELOC_TYPE_TEXT:
		val += current->mm->start_code;
		break;
	case OLD_FLAT_RELOC_TYPE_DATA:
		val += current->mm->start_data;
		break;
	case OLD_FLAT_RELOC_TYPE_BSS:
		val += current->mm->end_data;
		break;
	default:
		pr_err("Unknown relocation type=%x\n", r.reloc.type);
		break;
	}
	put_user(val, ptr);

	pr_debug("Relocation became %lx\n", val);
}
#endif /* CONFIG_BINFMT_FLAT_OLD */

/****************************************************************************/

static int load_flat_file(struct linux_binprm *bprm,
		struct lib_info *libinfo, int id, unsigned long *extra_stack)
{
	struct flat_hdr *hdr;
	unsigned long textpos, datapos, realdatastart;
	u32 text_len, data_len, bss_len, stack_len, full_data, flags;
	unsigned long len, memp, memp_size, extra, rlim;
	__be32 __user *reloc;
	u32 __user *rp;
	int i, rev, relocs;
	loff_t fpos;
	unsigned long start_code, end_code;
	ssize_t result;
	int ret;

	hdr = ((struct flat_hdr *) bprm->buf);		/* exec-header */

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
	full_data = data_len + relocs * sizeof(unsigned long);

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
		pr_info("Loading file: %s\n", bprm->filename);

#ifdef CONFIG_BINFMT_FLAT_OLD
	if (rev != FLAT_VERSION && rev != OLD_FLAT_VERSION) {
		pr_err("bad flat file version 0x%x (supported 0x%lx and 0x%lx)\n",
		       rev, FLAT_VERSION, OLD_FLAT_VERSION);
		ret = -ENOEXEC;
		goto err;
	}

	/* Don't allow old format executables to use shared libraries */
	if (rev == OLD_FLAT_VERSION && id != 0) {
		pr_err("shared libraries are not available before rev 0x%lx\n",
		       FLAT_VERSION);
		ret = -ENOEXEC;
		goto err;
	}

	/*
	 * fix up the flags for the older format,  there were all kinds
	 * of endian hacks,  this only works for the simple cases
	 */
	if (rev == OLD_FLAT_VERSION &&
	   (flags || IS_ENABLED(CONFIG_BINFMT_FLAT_OLD_ALWAYS_RAM)))
		flags = FLAT_FLAG_RAM;

#else /* CONFIG_BINFMT_FLAT_OLD */
	if (rev != FLAT_VERSION) {
		pr_err("bad flat file version 0x%x (supported 0x%lx)\n",
		       rev, FLAT_VERSION);
		ret = -ENOEXEC;
		goto err;
	}
#endif /* !CONFIG_BINFMT_FLAT_OLD */

	/*
	 * Make sure the header params are sane.
	 * 28 bits (256 MB) is way more than reasonable in this case.
	 * If some top bits are set we have probable binary corruption.
	*/
	if ((text_len | data_len | bss_len | stack_len | full_data) >> 28) {
		pr_err("bad header\n");
		ret = -ENOEXEC;
		goto err;
	}

#ifndef CONFIG_BINFMT_ZFLAT
	if (flags & (FLAT_FLAG_GZIP|FLAT_FLAG_GZDATA)) {
		pr_err("Support for ZFLAT executables is not enabled.\n");
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
		ret = begin_new_exec(bprm);
		if (ret)
			goto err;

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
	if (!IS_ENABLED(CONFIG_MMU) && !(flags & (FLAT_FLAG_RAM|FLAT_FLAG_GZIP))) {
		/*
		 * this should give us a ROM ptr,  but if it doesn't we don't
		 * really care
		 */
		pr_debug("ROM mapping of file (we hope)\n");

		textpos = vm_mmap(bprm->file, 0, text_len, PROT_READ|PROT_EXEC,
				  MAP_PRIVATE|MAP_EXECUTABLE, 0);
		if (!textpos || IS_ERR_VALUE(textpos)) {
			ret = textpos;
			if (!textpos)
				ret = -ENOMEM;
			pr_err("Unable to mmap process text, errno %d\n", ret);
			goto err;
		}

		len = data_len + extra + MAX_SHARED_LIBS * sizeof(unsigned long);
		len = PAGE_ALIGN(len);
		realdatastart = vm_mmap(NULL, 0, len,
			PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE, 0);

		if (realdatastart == 0 || IS_ERR_VALUE(realdatastart)) {
			ret = realdatastart;
			if (!realdatastart)
				ret = -ENOMEM;
			pr_err("Unable to allocate RAM for process data, "
			       "errno %d\n", ret);
			vm_munmap(textpos, text_len);
			goto err;
		}
		datapos = ALIGN(realdatastart +
				MAX_SHARED_LIBS * sizeof(unsigned long),
				FLAT_DATA_ALIGN);

		pr_debug("Allocated data+bss+stack (%u bytes): %lx\n",
			 data_len + bss_len + stack_len, datapos);

		fpos = ntohl(hdr->data_start);
#ifdef CONFIG_BINFMT_ZFLAT
		if (flags & FLAT_FLAG_GZDATA) {
			result = decompress_exec(bprm, fpos, (char *)datapos,
						 full_data, 0);
		} else
#endif
		{
			result = read_code(bprm->file, datapos, fpos,
					full_data);
		}
		if (IS_ERR_VALUE(result)) {
			ret = result;
			pr_err("Unable to read data+bss, errno %d\n", ret);
			vm_munmap(textpos, text_len);
			vm_munmap(realdatastart, len);
			goto err;
		}

		reloc = (__be32 __user *)
			(datapos + (ntohl(hdr->reloc_start) - text_len));
		memp = realdatastart;
		memp_size = len;
	} else {

		len = text_len + data_len + extra + MAX_SHARED_LIBS * sizeof(u32);
		len = PAGE_ALIGN(len);
		textpos = vm_mmap(NULL, 0, len,
			PROT_READ | PROT_EXEC | PROT_WRITE, MAP_PRIVATE, 0);

		if (!textpos || IS_ERR_VALUE(textpos)) {
			ret = textpos;
			if (!textpos)
				ret = -ENOMEM;
			pr_err("Unable to allocate RAM for process text/data, "
			       "errno %d\n", ret);
			goto err;
		}

		realdatastart = textpos + ntohl(hdr->data_start);
		datapos = ALIGN(realdatastart +
				MAX_SHARED_LIBS * sizeof(u32),
				FLAT_DATA_ALIGN);

		reloc = (__be32 __user *)
			(datapos + (ntohl(hdr->reloc_start) - text_len));
		memp = textpos;
		memp_size = len;
#ifdef CONFIG_BINFMT_ZFLAT
		/*
		 * load it all in and treat it like a RAM load from now on
		 */
		if (flags & FLAT_FLAG_GZIP) {
#ifndef CONFIG_MMU
			result = decompress_exec(bprm, sizeof(struct flat_hdr),
					 (((char *)textpos) + sizeof(struct flat_hdr)),
					 (text_len + full_data
						  - sizeof(struct flat_hdr)),
					 0);
			memmove((void *) datapos, (void *) realdatastart,
					full_data);
#else
			/*
			 * This is used on MMU systems mainly for testing.
			 * Let's use a kernel buffer to simplify things.
			 */
			long unz_text_len = text_len - sizeof(struct flat_hdr);
			long unz_len = unz_text_len + full_data;
			char *unz_data = vmalloc(unz_len);
			if (!unz_data) {
				result = -ENOMEM;
			} else {
				result = decompress_exec(bprm, sizeof(struct flat_hdr),
							 unz_data, unz_len, 0);
				if (result == 0 &&
				    (copy_to_user((void __user *)textpos + sizeof(struct flat_hdr),
						  unz_data, unz_text_len) ||
				     copy_to_user((void __user *)datapos,
						  unz_data + unz_text_len, full_data)))
					result = -EFAULT;
				vfree(unz_data);
			}
#endif
		} else if (flags & FLAT_FLAG_GZDATA) {
			result = read_code(bprm->file, textpos, 0, text_len);
			if (!IS_ERR_VALUE(result)) {
#ifndef CONFIG_MMU
				result = decompress_exec(bprm, text_len, (char *) datapos,
						 full_data, 0);
#else
				char *unz_data = vmalloc(full_data);
				if (!unz_data) {
					result = -ENOMEM;
				} else {
					result = decompress_exec(bprm, text_len,
						       unz_data, full_data, 0);
					if (result == 0 &&
					    copy_to_user((void __user *)datapos,
							 unz_data, full_data))
						result = -EFAULT;
					vfree(unz_data);
				}
#endif
			}
		} else
#endif /* CONFIG_BINFMT_ZFLAT */
		{
			result = read_code(bprm->file, textpos, 0, text_len);
			if (!IS_ERR_VALUE(result))
				result = read_code(bprm->file, datapos,
						   ntohl(hdr->data_start),
						   full_data);
		}
		if (IS_ERR_VALUE(result)) {
			ret = result;
			pr_err("Unable to read code+data+bss, errno %d\n", ret);
			vm_munmap(textpos, text_len + data_len + extra +
				MAX_SHARED_LIBS * sizeof(u32));
			goto err;
		}
	}

	start_code = textpos + sizeof(struct flat_hdr);
	end_code = textpos + text_len;
	text_len -= sizeof(struct flat_hdr); /* the real code len */

	/* The main program needs a little extra setup in the task structure */
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
#ifndef CONFIG_MMU
		current->mm->context.end_brk = memp + memp_size - stack_len;
#endif
	}

	if (flags & FLAT_FLAG_KTRACE) {
		pr_info("Mapping is %lx, Entry point is %x, data_start is %x\n",
			textpos, 0x00ffffff&ntohl(hdr->entry), ntohl(hdr->data_start));
		pr_info("%s %s: TEXT=%lx-%lx DATA=%lx-%lx BSS=%lx-%lx\n",
			id ? "Lib" : "Load", bprm->filename,
			start_code, end_code, datapos, datapos + data_len,
			datapos + data_len, (datapos + data_len + bss_len + 3) & ~3);
	}

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
		for (rp = (u32 __user *)datapos; ; rp++) {
			u32 addr, rp_val;
			if (get_user(rp_val, rp))
				return -EFAULT;
			if (rp_val == 0xffffffff)
				break;
			if (rp_val) {
				addr = calc_reloc(rp_val, libinfo, id, 0);
				if (addr == RELOC_FAILED) {
					ret = -ENOEXEC;
					goto err;
				}
				if (put_user(addr, rp))
					return -EFAULT;
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
		for (i = 0; i < relocs; i++) {
			u32 addr, relval;
			__be32 tmp;

			/*
			 * Get the address of the pointer to be
			 * relocated (of course, the address has to be
			 * relocated first).
			 */
			if (get_user(tmp, reloc + i))
				return -EFAULT;
			relval = ntohl(tmp);
			addr = flat_get_relocate_addr(relval);
			rp = (u32 __user *)calc_reloc(addr, libinfo, id, 1);
			if (rp == (u32 __user *)RELOC_FAILED) {
				ret = -ENOEXEC;
				goto err;
			}

			/* Get the pointer's value.  */
			ret = flat_get_addr_from_rp(rp, relval, flags, &addr);
			if (unlikely(ret))
				goto err;

			if (addr != 0) {
				/*
				 * Do the relocation.  PIC relocs in the data section are
				 * already in target order
				 */
				if ((flags & FLAT_FLAG_GOTPIC) == 0) {
					/*
					 * Meh, the same value can have a different
					 * byte order based on a flag..
					 */
					addr = ntohl((__force __be32)addr);
				}
				addr = calc_reloc(addr, libinfo, id, 0);
				if (addr == RELOC_FAILED) {
					ret = -ENOEXEC;
					goto err;
				}

				/* Write back the relocated pointer.  */
				ret = flat_put_addr_at_rp(rp, addr, relval);
				if (unlikely(ret))
					goto err;
			}
		}
#ifdef CONFIG_BINFMT_FLAT_OLD
	} else {
		for (i = 0; i < relocs; i++) {
			__be32 relval;
			if (get_user(relval, reloc + i))
				return -EFAULT;
			old_reloc(ntohl(relval));
		}
#endif /* CONFIG_BINFMT_FLAT_OLD */
	}

	flush_icache_user_range(start_code, end_code);

	/* zero the BSS,  BRK and stack areas */
	if (clear_user((void __user *)(datapos + data_len), bss_len +
		       (memp + memp_size - stack_len -		/* end brk */
		       libinfo->lib_list[id].start_brk) +	/* start brk */
		       stack_len))
		return -EFAULT;

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
	/*
	 * This is a fake bprm struct; only the members "buf", "file" and
	 * "filename" are actually used.
	 */
	struct linux_binprm bprm;
	int res;
	char buf[16];
	loff_t pos = 0;

	memset(&bprm, 0, sizeof(bprm));

	/* Create the file name */
	sprintf(buf, "/lib/lib%d.so", id);

	/* Open the file up */
	bprm.filename = buf;
	bprm.file = open_exec(bprm.filename);
	res = PTR_ERR(bprm.file);
	if (IS_ERR(bprm.file))
		return res;

	res = kernel_read(bprm.file, bprm.buf, BINPRM_BUF_SIZE, &pos);

	if (res >= 0)
		res = load_flat_file(&bprm, libs, id, NULL);

	allow_write_access(bprm.file);
	fput(bprm.file);

	return res;
}

#endif /* CONFIG_BINFMT_SHARED_FLAT */
/****************************************************************************/

/*
 * These are the functions used to load flat style executables and shared
 * libraries.  There is no binary dependent code anywhere else.
 */

static int load_flat_binary(struct linux_binprm *bprm)
{
	struct lib_info libinfo;
	struct pt_regs *regs = current_pt_regs();
	unsigned long stack_len = 0;
	unsigned long start_addr;
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
#ifndef CONFIG_MMU
	stack_len += PAGE_SIZE * MAX_ARG_PAGES - bprm->p; /* the strings */
#endif
	stack_len += (bprm->argc + 1) * sizeof(char *);   /* the argv array */
	stack_len += (bprm->envc + 1) * sizeof(char *);   /* the envp array */
	stack_len = ALIGN(stack_len, FLAT_STACK_ALIGN);

	res = load_flat_file(bprm, &libinfo, 0, &stack_len);
	if (res < 0)
		return res;

	/* Update data segment pointers for all libraries */
	for (i = 0; i < MAX_SHARED_LIBS; i++) {
		if (!libinfo.lib_list[i].loaded)
			continue;
		for (j = 0; j < MAX_SHARED_LIBS; j++) {
			unsigned long val = libinfo.lib_list[j].loaded ?
				libinfo.lib_list[j].start_data : UNLOADED_LIB;
			unsigned long __user *p = (unsigned long __user *)
				libinfo.lib_list[i].start_data;
			p -= j + 1;
			if (put_user(val, p))
				return -EFAULT;
		}
	}

	set_binfmt(&flat_format);

#ifdef CONFIG_MMU
	res = setup_arg_pages(bprm, STACK_TOP, EXSTACK_DEFAULT);
	if (!res)
		res = create_flat_tables(bprm, bprm->p);
#else
	/* Stash our initial stack pointer into the mm structure */
	current->mm->start_stack =
		((current->mm->context.end_brk + stack_len + 3) & ~3) - 4;
	pr_debug("sp=%lx\n", current->mm->start_stack);

	/* copy the arg pages onto the stack */
	res = transfer_args_to_stack(bprm, &current->mm->start_stack);
	if (!res)
		res = create_flat_tables(bprm, current->mm->start_stack);
#endif
	if (res)
		return res;

	/* Fake some return addresses to ensure the call chain will
	 * initialise library in order for us.  We are required to call
	 * lib 1 first, then 2, ... and finally the main program (id 0).
	 */
	start_addr = libinfo.lib_list[0].entry;

#ifdef CONFIG_BINFMT_SHARED_FLAT
	for (i = MAX_SHARED_LIBS-1; i > 0; i--) {
		if (libinfo.lib_list[i].loaded) {
			/* Push previos first to call address */
			unsigned long __user *sp;
			current->mm->start_stack -= sizeof(unsigned long);
			sp = (unsigned long __user *)current->mm->start_stack;
			if (put_user(start_addr, sp))
				return -EFAULT;
			start_addr = libinfo.lib_list[i].entry;
		}
	}
#endif

#ifdef FLAT_PLAT_INIT
	FLAT_PLAT_INIT(regs);
#endif

	finalize_exec(bprm);
	pr_debug("start_thread(regs=0x%p, entry=0x%lx, start_stack=0x%lx)\n",
		 regs, start_addr, current->mm->start_stack);
	start_thread(regs, start_addr, current->mm->start_stack);

	return 0;
}

/****************************************************************************/

static int __init init_flat_binfmt(void)
{
	register_binfmt(&flat_format);
	return 0;
}
core_initcall(init_flat_binfmt);

/****************************************************************************/
