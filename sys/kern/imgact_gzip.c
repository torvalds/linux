/*-
 * SPDX-License-Identifier: Beerware
 *
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 */

/*
 * This module handles execution of a.out files which have been run through
 * "gzip".  This saves diskspace, but wastes cpu-cycles and VM.
 *
 * TODO:
 *	text-segments should be made R/O after being filled
 *	is the vm-stuff safe ?
 * 	should handle the entire header of gzip'ed stuff.
 *	inflate isn't quite reentrant yet...
 *	error-handling is a mess...
 *	so is the rest...
 *	tidy up unnecessary includes
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/exec.h>
#include <sys/imgact.h>
#include <sys/imgact_aout.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mman.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/racct.h>
#include <sys/resourcevar.h>
#include <sys/sysent.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/inflate.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>

struct imgact_gzip {
	struct image_params *ip;
	struct exec     a_out;
	int             error;
	int		gotheader;
	int             where;
	u_char         *inbuf;
	u_long          offset;
	u_long          output;
	u_long          len;
	int             idx;
	u_long          virtual_offset, file_offset, file_end, bss_size;
};

static int exec_gzip_imgact(struct image_params *imgp);
static int NextByte(void *vp);
static int do_aout_hdr(struct imgact_gzip *);
static int Flush(void *vp, u_char *, u_long siz);

static int
exec_gzip_imgact(struct image_params *imgp)
{
	int             error;
	const u_char   *p = (const u_char *) imgp->image_header;
	struct imgact_gzip igz;
	struct inflate  infl;
	struct vmspace *vmspace;

	/* If these four are not OK, it isn't a gzip file */
	if (p[0] != 0x1f)
		return -1;	/* 0    Simply magic	 */
	if (p[1] != 0x8b)
		return -1;	/* 1    Simply magic	 */
	if (p[2] != 0x08)
		return -1;	/* 2    Compression method	 */
	if (p[9] != 0x03)
		return -1;	/* 9    OS compressed on	 */

	/*
	 * If this one contains anything but a comment or a filename marker,
	 * we don't want to chew on it
	 */
	if (p[3] & ~(0x18))
		return ENOEXEC;	/* 3    Flags		 */

	/* These are of no use to us */
	/* 4-7  Timestamp		 */
	/* 8    Extra flags		 */

	bzero(&igz, sizeof igz);
	bzero(&infl, sizeof infl);
	infl.gz_private = (void *) &igz;
	infl.gz_input = NextByte;
	infl.gz_output = Flush;

	igz.ip = imgp;
	igz.idx = 10;

	if (p[3] & 0x08) {	/* skip a filename */
		while (p[igz.idx++])
			if (igz.idx >= PAGE_SIZE)
				return ENOEXEC;
	}
	if (p[3] & 0x10) {	/* skip a comment */
		while (p[igz.idx++])
			if (igz.idx >= PAGE_SIZE)
				return ENOEXEC;
	}
	igz.len = imgp->attr->va_size;

	error = inflate(&infl);

	/*
	 * The unzipped file may not even have been long enough to contain
	 * a header giving Flush() a chance to return error.  Check for this.
	 */
	if ( !igz.gotheader )
		return ENOEXEC;

	if ( !error ) {
		vmspace = imgp->proc->p_vmspace;
		error = vm_map_protect(&vmspace->vm_map,
			(vm_offset_t) vmspace->vm_taddr,
			(vm_offset_t) (vmspace->vm_taddr + 
				      (vmspace->vm_tsize << PAGE_SHIFT)) ,
			VM_PROT_READ|VM_PROT_EXECUTE,0);
	}

	if (igz.inbuf)
		kmap_free_wakeup(exec_map, (vm_offset_t)igz.inbuf, PAGE_SIZE);
	if (igz.error || error) {
		printf("Output=%lu ", igz.output);
		printf("Inflate_error=%d igz.error=%d where=%d\n",
		       error, igz.error, igz.where);
	}
	if (igz.error)
		return igz.error;
	if (error)
		return ENOEXEC;
	return 0;
}

static int
do_aout_hdr(struct imgact_gzip * gz)
{
	int             error;
	struct vmspace *vmspace;
	vm_offset_t     vmaddr;

	/*
	 * Set file/virtual offset based on a.out variant. We do two cases:
	 * host byte order and network byte order (for NetBSD compatibility)
	 */
	switch ((int) (gz->a_out.a_midmag & 0xffff)) {
	case ZMAGIC:
		gz->virtual_offset = 0;
		if (gz->a_out.a_text) {
			gz->file_offset = PAGE_SIZE;
		} else {
			/* Bill's "screwball mode" */
			gz->file_offset = 0;
		}
		break;
	case QMAGIC:
		gz->virtual_offset = PAGE_SIZE;
		gz->file_offset = 0;
		break;
	default:
		/* NetBSD compatibility */
		switch ((int) (ntohl(gz->a_out.a_midmag) & 0xffff)) {
		case ZMAGIC:
		case QMAGIC:
			gz->virtual_offset = PAGE_SIZE;
			gz->file_offset = 0;
			break;
		default:
			gz->where = __LINE__;
			return (-1);
		}
	}

	gz->bss_size = roundup(gz->a_out.a_bss, PAGE_SIZE);

	/*
	 * Check various fields in header for validity/bounds.
	 */
	if (			/* entry point must lay with text region */
	    gz->a_out.a_entry < gz->virtual_offset ||
	    gz->a_out.a_entry >= gz->virtual_offset + gz->a_out.a_text ||

	/* text and data size must each be page rounded */
	    gz->a_out.a_text & PAGE_MASK || gz->a_out.a_data & PAGE_MASK) {
		gz->where = __LINE__;
		return (-1);
	}
	/*
	 * text/data/bss must not exceed limits
	 */
	PROC_LOCK(gz->ip->proc);
	if (			/* text can't exceed maximum text size */
	    gz->a_out.a_text > maxtsiz ||

	/* data + bss can't exceed rlimit */
	    gz->a_out.a_data + gz->bss_size >
	    lim_cur_proc(gz->ip->proc, RLIMIT_DATA) ||
	    racct_set(gz->ip->proc, RACCT_DATA,
	    gz->a_out.a_data + gz->bss_size) != 0) {
		PROC_UNLOCK(gz->ip->proc);
		gz->where = __LINE__;
		return (ENOMEM);
	}
	PROC_UNLOCK(gz->ip->proc);
	/* Find out how far we should go */
	gz->file_end = gz->file_offset + gz->a_out.a_text + gz->a_out.a_data;

	/*
	 * Avoid a possible deadlock if the current address space is destroyed
	 * and that address space maps the locked vnode.  In the common case,
	 * the locked vnode's v_usecount is decremented but remains greater
	 * than zero.  Consequently, the vnode lock is not needed by vrele().
	 * However, in cases where the vnode lock is external, such as nullfs,
	 * v_usecount may become zero.
	 */
	VOP_UNLOCK(gz->ip->vp, 0);

	/*
	 * Destroy old process VM and create a new one (with a new stack)
	 */
	error = exec_new_vmspace(gz->ip, &aout_sysvec);

	vn_lock(gz->ip->vp, LK_EXCLUSIVE | LK_RETRY);
	if (error) {
		gz->where = __LINE__;
		return (error);
	}

	vmspace = gz->ip->proc->p_vmspace;

	vmaddr = gz->virtual_offset;

	error = vm_mmap(&vmspace->vm_map,
			&vmaddr,
			gz->a_out.a_text + gz->a_out.a_data,
			VM_PROT_ALL, VM_PROT_ALL, MAP_ANON | MAP_FIXED,
			OBJT_DEFAULT,
			NULL,
			0);

	if (error) {
		gz->where = __LINE__;
		return (error);
	}

	if (gz->bss_size != 0) {
		/*
		 * Allocate demand-zeroed area for uninitialized data.
		 * "bss" = 'block started by symbol' - named after the 
		 * IBM 7090 instruction of the same name.
		 */
		vmaddr = gz->virtual_offset + gz->a_out.a_text + 
			gz->a_out.a_data;
		error = vm_map_find(&vmspace->vm_map, NULL, 0, &vmaddr,
		    gz->bss_size, 0, VMFS_NO_SPACE, VM_PROT_ALL, VM_PROT_ALL,
		    0);
		if (error) {
			gz->where = __LINE__;
			return (error);
		}
	}
	/* Fill in process VM information */
	vmspace->vm_tsize = gz->a_out.a_text >> PAGE_SHIFT;
	vmspace->vm_dsize = (gz->a_out.a_data + gz->bss_size) >> PAGE_SHIFT;
	vmspace->vm_taddr = (caddr_t) (uintptr_t) gz->virtual_offset;
	vmspace->vm_daddr = (caddr_t) (uintptr_t)
			    (gz->virtual_offset + gz->a_out.a_text);

	/* Fill in image_params */
	gz->ip->interpreted = 0;
	gz->ip->entry_addr = gz->a_out.a_entry;

	gz->ip->proc->p_sysent = &aout_sysvec;

	return 0;
}

static int
NextByte(void *vp)
{
	int             error;
	struct imgact_gzip *igz = (struct imgact_gzip *) vp;

	if (igz->idx >= igz->len) {
		igz->where = __LINE__;
		return GZ_EOF;
	}
	if (igz->inbuf && igz->idx < (igz->offset + PAGE_SIZE)) {
		return igz->inbuf[(igz->idx++) - igz->offset];
	}
	if (igz->inbuf)
		kmap_free_wakeup(exec_map, (vm_offset_t)igz->inbuf, PAGE_SIZE);
	igz->offset = igz->idx & ~PAGE_MASK;

	error = vm_mmap(exec_map,	/* map */
			(vm_offset_t *) & igz->inbuf,	/* address */
			PAGE_SIZE,	/* size */
			VM_PROT_READ,	/* protection */
			VM_PROT_READ,	/* max protection */
			0,	/* flags */
			OBJT_VNODE,	/* handle type */
			igz->ip->vp,	/* vnode */
			igz->offset);	/* offset */
	if (error) {
		igz->where = __LINE__;
		igz->error = error;
		return GZ_EOF;
	}
	return igz->inbuf[(igz->idx++) - igz->offset];
}

static int
Flush(void *vp, u_char * ptr, u_long siz)
{
	struct imgact_gzip *gz = (struct imgact_gzip *) vp;
	u_char         *p = ptr, *q;
	int             i;

	/* First, find an a.out-header. */
	if (gz->output < sizeof gz->a_out) {
		q = (u_char *) & gz->a_out;
		i = min(siz, sizeof gz->a_out - gz->output);
		bcopy(p, q + gz->output, i);
		gz->output += i;
		p += i;
		siz -= i;
		if (gz->output == sizeof gz->a_out) {
			gz->gotheader = 1;
			i = do_aout_hdr(gz);
			if (i == -1) {
				if (!gz->where)
					gz->where = __LINE__;
				gz->error = ENOEXEC;
				return ENOEXEC;
			} else if (i) {
				gz->where = __LINE__;
				gz->error = i;
				return ENOEXEC;
			}
			if (gz->file_offset == 0) {
				q = (u_char *) (uintptr_t) gz->virtual_offset;
				copyout(&gz->a_out, q, sizeof gz->a_out);
			}
		}
	}
	/* Skip over zero-padded first PAGE if needed */
	if (gz->output < gz->file_offset &&
	    gz->output + siz > gz->file_offset) {
		i = min(siz, gz->file_offset - gz->output);
		gz->output += i;
		p += i;
		siz -= i;
	}
	if (gz->output >= gz->file_offset && gz->output < gz->file_end) {
		i = min(siz, gz->file_end - gz->output);
		q = (u_char *) (uintptr_t)
		    (gz->virtual_offset + gz->output - gz->file_offset);
		copyout(p, q, i);
		gz->output += i;
		p += i;
		siz -= i;
	}
	gz->output += siz;
	return 0;
}


/*
 * Tell kern_execve.c about it, with a little help from the linker.
 */
static struct execsw gzip_execsw = {
	.ex_imgact = exec_gzip_imgact,
	.ex_name = "gzip"
};
EXEC_SET(execgzip, gzip_execsw);
