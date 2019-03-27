/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2009-2012 Semihalf
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
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/lock.h>
#include <sys/vnode.h>
#include <sys/mount.h>

#include <dev/nand/nandsim_chip.h>
#include <dev/nand/nandsim_swap.h>

static int  init_block_state(struct chip_swap *);
static void destroy_block_state(struct chip_swap *);

static int  create_buffers(struct chip_swap *);
static void destroy_buffers(struct chip_swap *);

static int  swap_file_open(struct chip_swap *, const char *);
static void swap_file_close(struct chip_swap *);
static int  swap_file_write(struct chip_swap *, struct block_state *);
static int  swap_file_read(struct chip_swap *, struct block_state *);

#define	CHIP_SWAP_CMODE		0600
#define	CHIP_SWAP_BLOCKSPACES	2

static int
init_block_state(struct chip_swap *swap)
{
	struct block_state *blk_state;
	int i;

	if (swap == NULL)
		return (-1);

	blk_state = malloc(swap->nof_blks * sizeof(struct block_state),
	    M_NANDSIM, M_WAITOK | M_ZERO);

	for (i = 0; i < swap->nof_blks; i++)
		blk_state[i].offset = 0xffffffff;

	swap->blk_state = blk_state;

	return (0);
}

static void
destroy_block_state(struct chip_swap *swap)
{

	if (swap == NULL)
		return;

	if (swap->blk_state != NULL)
		free(swap->blk_state, M_NANDSIM);
}

static int
create_buffers(struct chip_swap *swap)
{
	struct block_space *block_space;
	void *block;
	int i;

	for (i = 0; i < CHIP_SWAP_BLOCKSPACES; i++) {
		block_space = malloc(sizeof(*block_space), M_NANDSIM, M_WAITOK);
		block = malloc(swap->blk_size, M_NANDSIM, M_WAITOK);
		block_space->blk_ptr = block;
		SLIST_INSERT_HEAD(&swap->free_bs, block_space, free_link);
		nand_debug(NDBG_SIM,"created blk_space %p[%p]\n", block_space,
		    block);
	}

	if (i == 0)
		return (-1);

	return (0);
}

static void
destroy_buffers(struct chip_swap *swap)
{
	struct block_space *blk_space;

	if (swap == NULL)
		return;

	blk_space = SLIST_FIRST(&swap->free_bs);
	while (blk_space) {
		SLIST_REMOVE_HEAD(&swap->free_bs, free_link);
		nand_debug(NDBG_SIM,"destroyed blk_space %p[%p]\n",
		    blk_space, blk_space->blk_ptr);
		free(blk_space->blk_ptr, M_NANDSIM);
		free(blk_space, M_NANDSIM);
		blk_space = SLIST_FIRST(&swap->free_bs);
	}

	blk_space = STAILQ_FIRST(&swap->used_bs);
	while (blk_space) {
		STAILQ_REMOVE_HEAD(&swap->used_bs, used_link);
		nand_debug(NDBG_SIM,"destroyed blk_space %p[%p]\n",
		    blk_space, blk_space->blk_ptr);
		free(blk_space->blk_ptr, M_NANDSIM);
		free(blk_space, M_NANDSIM);
		blk_space = STAILQ_FIRST(&swap->used_bs);
	}
}

static int
swap_file_open(struct chip_swap *swap, const char *swap_file)
{
	struct nameidata nd;
	int flags, error;

	NDINIT(&nd, LOOKUP, NOFOLLOW, UIO_SYSSPACE, swap_file,
	    curthread);

	flags = FWRITE | FREAD | O_NOFOLLOW | O_CREAT | O_TRUNC;

	error = vn_open(&nd, &flags, CHIP_SWAP_CMODE, NULL);
	if (error) {
		nand_debug(NDBG_SIM,"Cannot create swap file %s", swap_file);
		NDFREE(&nd, NDF_ONLY_PNBUF);
		return (error);
	}

	swap->swap_cred = crhold(curthread->td_ucred);
	NDFREE(&nd, NDF_ONLY_PNBUF);

	/* We just unlock so we hold a reference */
	VOP_UNLOCK(nd.ni_vp, 0);

	swap->swap_vp = nd.ni_vp;

	return (0);
}

static void
swap_file_close(struct chip_swap *swap)
{

	if (swap == NULL)
		return;

	if (swap->swap_vp == NULL)
		return;

	vn_close(swap->swap_vp, FWRITE, swap->swap_cred, curthread);
	crfree(swap->swap_cred);
}

static int
swap_file_write(struct chip_swap *swap, struct block_state *blk_state)
{
	struct block_space *blk_space;
	struct thread *td;
	struct mount *mp;
	struct vnode *vp;
	struct uio auio;
	struct iovec aiov;

	if (swap == NULL || blk_state == NULL)
		return (-1);

	blk_space = blk_state->blk_sp;
	if (blk_state->offset == -1) {
		blk_state->offset = swap->swap_offset;
		swap->swap_offset += swap->blk_size;
	}

	nand_debug(NDBG_SIM,"saving %p[%p] at %x\n",
	    blk_space, blk_space->blk_ptr, blk_state->offset);

	bzero(&aiov, sizeof(aiov));
	bzero(&auio, sizeof(auio));

	aiov.iov_base = blk_space->blk_ptr;
	aiov.iov_len = swap->blk_size;
	td = curthread;
	vp = swap->swap_vp;

	auio.uio_iov = &aiov;
	auio.uio_offset = blk_state->offset;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_rw = UIO_WRITE;
	auio.uio_iovcnt = 1;
	auio.uio_resid = swap->blk_size;
	auio.uio_td = td;

	vn_start_write(vp, &mp, V_WAIT);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	VOP_WRITE(vp, &auio, IO_UNIT, swap->swap_cred);
	VOP_UNLOCK(vp, 0);
	vn_finished_write(mp);

	return (0);
}

static int
swap_file_read(struct chip_swap *swap, struct block_state *blk_state)
{
	struct block_space *blk_space;
	struct thread *td;
	struct vnode *vp;
	struct uio auio;
	struct iovec aiov;

	if (swap == NULL || blk_state == NULL)
		return (-1);

	blk_space = blk_state->blk_sp;

	nand_debug(NDBG_SIM,"restore %p[%p] at %x\n",
	    blk_space, blk_space->blk_ptr, blk_state->offset);

	bzero(&aiov, sizeof(aiov));
	bzero(&auio, sizeof(auio));

	aiov.iov_base = blk_space->blk_ptr;
	aiov.iov_len = swap->blk_size;
	td = curthread;
	vp = swap->swap_vp;

	auio.uio_iov = &aiov;
	auio.uio_offset = blk_state->offset;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_rw = UIO_READ;
	auio.uio_iovcnt = 1;
	auio.uio_resid = swap->blk_size;
	auio.uio_td = td;

	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	VOP_READ(vp, &auio, 0, swap->swap_cred);
	VOP_UNLOCK(vp, 0);

	return (0);
}

struct chip_swap *
nandsim_swap_init(const char *swap_file, uint32_t nof_blks, uint32_t blk_size)
{
	struct chip_swap *swap;
	int err = 0;

	if ((swap_file == NULL) || (nof_blks == 0) || (blk_size == 0))
		return (NULL);

	swap = malloc(sizeof(*swap), M_NANDSIM, M_WAITOK | M_ZERO);

	SLIST_INIT(&swap->free_bs);
	STAILQ_INIT(&swap->used_bs);
	swap->blk_size = blk_size;
	swap->nof_blks = nof_blks;

	err = init_block_state(swap);
	if (err) {
		nandsim_swap_destroy(swap);
		return (NULL);
	}

	err = create_buffers(swap);
	if (err) {
		nandsim_swap_destroy(swap);
		return (NULL);
	}

	err = swap_file_open(swap, swap_file);
	if (err) {
		nandsim_swap_destroy(swap);
		return (NULL);
	}

	return (swap);
}

void
nandsim_swap_destroy(struct chip_swap *swap)
{

	if (swap == NULL)
		return;

	destroy_block_state(swap);
	destroy_buffers(swap);
	swap_file_close(swap);
	free(swap, M_NANDSIM);
}

struct block_space *
get_bs(struct chip_swap *swap, uint32_t block, uint8_t writing)
{
	struct block_state *blk_state, *old_blk_state = NULL;
	struct block_space *blk_space;

	if (swap == NULL || (block >= swap->nof_blks))
		return (NULL);

	blk_state = &swap->blk_state[block];
	nand_debug(NDBG_SIM,"blk_state %x\n", blk_state->status);

	if (blk_state->status & BLOCK_ALLOCATED) {
		blk_space = blk_state->blk_sp;
	} else {
		blk_space = SLIST_FIRST(&swap->free_bs);
		if (blk_space) {
			SLIST_REMOVE_HEAD(&swap->free_bs, free_link);
			STAILQ_INSERT_TAIL(&swap->used_bs, blk_space,
			    used_link);
		} else {
			blk_space = STAILQ_FIRST(&swap->used_bs);
			old_blk_state = blk_space->blk_state;
			STAILQ_REMOVE_HEAD(&swap->used_bs, used_link);
			STAILQ_INSERT_TAIL(&swap->used_bs, blk_space,
			    used_link);
			if (old_blk_state->status & BLOCK_DIRTY) {
				swap_file_write(swap, old_blk_state);
				old_blk_state->status &= ~BLOCK_DIRTY;
				old_blk_state->status |= BLOCK_SWAPPED;
			}
		}
	}

	if (blk_space == NULL)
		return (NULL);

	if (old_blk_state != NULL) {
		old_blk_state->status &= ~BLOCK_ALLOCATED;
		old_blk_state->blk_sp = NULL;
	}

	blk_state->blk_sp = blk_space;
	blk_space->blk_state = blk_state;

	if (!(blk_state->status & BLOCK_ALLOCATED)) {
		if (blk_state->status & BLOCK_SWAPPED)
			swap_file_read(swap, blk_state);
		else
			memset(blk_space->blk_ptr, 0xff, swap->blk_size);
		blk_state->status |= BLOCK_ALLOCATED;
	}

	if (writing)
		blk_state->status |= BLOCK_DIRTY;

	nand_debug(NDBG_SIM,"get_bs returned %p[%p] state %x\n", blk_space,
	    blk_space->blk_ptr, blk_state->status);

	return (blk_space);
}
