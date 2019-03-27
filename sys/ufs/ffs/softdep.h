/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 1998, 2000 Marshall Kirk McKusick. All Rights Reserved.
 *
 * The soft updates code is derived from the appendix of a University
 * of Michigan technical report (Gregory R. Ganger and Yale N. Patt,
 * "Soft Updates: A Solution to the Metadata Update Problem in File
 * Systems", CSE-TR-254-95, August 1995).
 *
 * Further information about soft updates can be obtained from:
 *
 *	Marshall Kirk McKusick		http://www.mckusick.com/softdep/
 *	1614 Oxford Street		mckusick@mckusick.com
 *	Berkeley, CA 94709-1608		+1-510-843-9542
 *	USA
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY MARSHALL KIRK MCKUSICK ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL MARSHALL KIRK MCKUSICK BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)softdep.h	9.7 (McKusick) 6/21/00
 * $FreeBSD$
 */

#include <sys/queue.h>

/*
 * Allocation dependencies are handled with undo/redo on the in-memory
 * copy of the data. A particular data dependency is eliminated when
 * it is ALLCOMPLETE: that is ATTACHED, DEPCOMPLETE, and COMPLETE.
 * 
 * The ATTACHED flag means that the data is not currently being written
 * to disk.
 * 
 * The UNDONE flag means that the data has been rolled back to a safe
 * state for writing to the disk. When the I/O completes, the data is
 * restored to its current form and the state reverts to ATTACHED.
 * The data must be locked throughout the rollback, I/O, and roll
 * forward so that the rolled back information is never visible to
 * user processes.
 *
 * The COMPLETE flag indicates that the item has been written. For example,
 * a dependency that requires that an inode be written will be marked
 * COMPLETE after the inode has been written to disk.
 * 
 * The DEPCOMPLETE flag indicates the completion of any other
 * dependencies such as the writing of a cylinder group map has been
 * completed. A dependency structure may be freed only when both it
 * and its dependencies have completed and any rollbacks that are in
 * progress have finished as indicated by the set of ALLCOMPLETE flags
 * all being set.
 * 
 * The two MKDIR flags indicate additional dependencies that must be done
 * when creating a new directory. MKDIR_BODY is cleared when the directory
 * data block containing the "." and ".." entries has been written.
 * MKDIR_PARENT is cleared when the parent inode with the increased link
 * count for ".." has been written. When both MKDIR flags have been
 * cleared, the DEPCOMPLETE flag is set to indicate that the directory
 * dependencies have been completed. The writing of the directory inode
 * itself sets the COMPLETE flag which then allows the directory entry for
 * the new directory to be written to disk. The RMDIR flag marks a dirrem
 * structure as representing the removal of a directory rather than a
 * file. When the removal dependencies are completed, additional work needs
 * to be done* (an additional decrement of the associated inode, and a
 * decrement of the parent inode).
 *
 * The DIRCHG flag marks a diradd structure as representing the changing
 * of an existing entry rather than the addition of a new one. When
 * the update is complete the dirrem associated with the inode for
 * the old name must be added to the worklist to do the necessary
 * reference count decrement.
 * 
 * The GOINGAWAY flag indicates that the data structure is frozen from
 * further change until its dependencies have been completed and its
 * resources freed after which it will be discarded.
 *
 * The IOSTARTED flag prevents multiple calls to the I/O start routine from
 * doing multiple rollbacks.
 *
 * The NEWBLOCK flag marks pagedep structures that have just been allocated,
 * so must be claimed by the inode before all dependencies are complete.
 *
 * The INPROGRESS flag marks worklist structures that are still on the
 * worklist, but are being considered for action by some process.
 *
 * The UFS1FMT flag indicates that the inode being processed is a ufs1 format.
 *
 * The EXTDATA flag indicates that the allocdirect describes an
 * extended-attributes dependency.
 *
 * The ONWORKLIST flag shows whether the structure is currently linked
 * onto a worklist.
 *
 * The UNLINK* flags track the progress of updating the on-disk linked
 * list of active but unlinked inodes. When an inode is first unlinked
 * it is marked as UNLINKED. When its on-disk di_freelink has been
 * written its UNLINKNEXT flags is set. When its predecessor in the
 * list has its di_freelink pointing at us its UNLINKPREV is set.
 * When the on-disk list can reach it from the superblock, its
 * UNLINKONLIST flag is set. Once all of these flags are set, it
 * is safe to let its last name be removed.
 */
#define	ATTACHED	0x000001
#define	UNDONE		0x000002
#define	COMPLETE	0x000004
#define	DEPCOMPLETE	0x000008
#define	MKDIR_PARENT	0x000010 /* diradd, mkdir, jaddref, jsegdep only */
#define	MKDIR_BODY	0x000020 /* diradd, mkdir, jaddref only */
#define	RMDIR		0x000040 /* dirrem only */
#define	DIRCHG		0x000080 /* diradd, dirrem only */
#define	GOINGAWAY	0x000100 /* indirdep, jremref only */
#define	IOSTARTED	0x000200 /* inodedep, pagedep, bmsafemap only */
#define	DELAYEDFREE	0x000400 /* allocindirect free delayed. */
#define	NEWBLOCK	0x000800 /* pagedep, jaddref only */
#define	INPROGRESS	0x001000 /* dirrem, freeblks, freefrag, freefile only */
#define	UFS1FMT		0x002000 /* indirdep only */
#define	EXTDATA		0x004000 /* allocdirect only */
#define	ONWORKLIST	0x008000
#define	IOWAITING	0x010000 /* Thread is waiting for IO to complete. */
#define	ONDEPLIST	0x020000 /* Structure is on a dependency list. */
#define	UNLINKED	0x040000 /* inodedep has been unlinked. */
#define	UNLINKNEXT	0x080000 /* inodedep has valid di_freelink */
#define	UNLINKPREV	0x100000 /* inodedep is pointed at in the unlink list */
#define	UNLINKONLIST	0x200000 /* inodedep is in the unlinked list on disk */
#define	UNLINKLINKS	(UNLINKNEXT | UNLINKPREV)
#define	WRITESUCCEEDED	0x400000 /* the disk write completed successfully */

#define	ALLCOMPLETE	(ATTACHED | COMPLETE | DEPCOMPLETE)

#define PRINT_SOFTDEP_FLAGS "\20\27writesucceeded\26unlinkonlist" \
	"\25unlinkprev\24unlinknext\23unlinked\22ondeplist\21iowaiting" \
	"\20onworklist\17extdata\16ufs1fmt\15inprogress\14newblock" \
	"\13delayedfree\12iostarted\11goingaway\10dirchg\7rmdir\6mkdir_body" \
	"\5mkdir_parent\4depcomplete\3complete\2undone\1attached"

/*
 * Values for each of the soft dependency types.
 */
#define	D_UNUSED	0
#define	D_FIRST		D_PAGEDEP
#define	D_PAGEDEP	1
#define	D_INODEDEP	2
#define	D_BMSAFEMAP	3
#define	D_NEWBLK	4
#define	D_ALLOCDIRECT	5
#define	D_INDIRDEP	6
#define	D_ALLOCINDIR	7
#define	D_FREEFRAG	8
#define	D_FREEBLKS	9
#define	D_FREEFILE	10
#define	D_DIRADD	11
#define	D_MKDIR		12
#define	D_DIRREM	13
#define	D_NEWDIRBLK	14
#define	D_FREEWORK	15
#define	D_FREEDEP	16
#define	D_JADDREF	17
#define	D_JREMREF	18
#define	D_JMVREF	19
#define	D_JNEWBLK	20
#define	D_JFREEBLK	21
#define	D_JFREEFRAG	22
#define	D_JSEG		23
#define	D_JSEGDEP	24
#define	D_SBDEP		25
#define	D_JTRUNC	26
#define	D_JFSYNC	27
#define	D_SENTINEL	28
#define	D_LAST		D_SENTINEL

/*
 * The workitem queue.
 * 
 * It is sometimes useful and/or necessary to clean up certain dependencies
 * in the background rather than during execution of an application process
 * or interrupt service routine. To realize this, we append dependency
 * structures corresponding to such tasks to a "workitem" queue. In a soft
 * updates implementation, most pending workitems should not wait for more
 * than a couple of seconds, so the filesystem syncer process awakens once
 * per second to process the items on the queue.
 */

/* LIST_HEAD(workhead, worklist);	-- declared in buf.h */

/*
 * Each request can be linked onto a work queue through its worklist structure.
 * To avoid the need for a pointer to the structure itself, this structure
 * MUST be declared FIRST in each type in which it appears! If more than one
 * worklist is needed in the structure, then a wk_data field must be added
 * and the macros below changed to use it.
 */
struct worklist {
	LIST_ENTRY(worklist)	wk_list;	/* list of work requests */
	struct mount		*wk_mp;		/* Mount we live in */
	unsigned int		wk_type:8,	/* type of request */
				wk_state:24;	/* state flags */
};
#define	WK_DATA(wk) ((void *)(wk))
#define	WK_PAGEDEP(wk) ((struct pagedep *)(wk))
#define	WK_INODEDEP(wk) ((struct inodedep *)(wk))
#define	WK_BMSAFEMAP(wk) ((struct bmsafemap *)(wk))
#define	WK_NEWBLK(wk)  ((struct newblk *)(wk))
#define	WK_ALLOCDIRECT(wk) ((struct allocdirect *)(wk))
#define	WK_INDIRDEP(wk) ((struct indirdep *)(wk))
#define	WK_ALLOCINDIR(wk) ((struct allocindir *)(wk))
#define	WK_FREEFRAG(wk) ((struct freefrag *)(wk))
#define	WK_FREEBLKS(wk) ((struct freeblks *)(wk))
#define	WK_FREEWORK(wk) ((struct freework *)(wk))
#define	WK_FREEFILE(wk) ((struct freefile *)(wk))
#define	WK_DIRADD(wk) ((struct diradd *)(wk))
#define	WK_MKDIR(wk) ((struct mkdir *)(wk))
#define	WK_DIRREM(wk) ((struct dirrem *)(wk))
#define	WK_NEWDIRBLK(wk) ((struct newdirblk *)(wk))
#define	WK_JADDREF(wk) ((struct jaddref *)(wk))
#define	WK_JREMREF(wk) ((struct jremref *)(wk))
#define	WK_JMVREF(wk) ((struct jmvref *)(wk))
#define	WK_JSEGDEP(wk) ((struct jsegdep *)(wk))
#define	WK_JSEG(wk) ((struct jseg *)(wk))
#define	WK_JNEWBLK(wk) ((struct jnewblk *)(wk))
#define	WK_JFREEBLK(wk) ((struct jfreeblk *)(wk))
#define	WK_FREEDEP(wk) ((struct freedep *)(wk))
#define	WK_JFREEFRAG(wk) ((struct jfreefrag *)(wk))
#define	WK_SBDEP(wk) ((struct sbdep *)(wk))
#define	WK_JTRUNC(wk) ((struct jtrunc *)(wk))
#define	WK_JFSYNC(wk) ((struct jfsync *)(wk))

/*
 * Various types of lists
 */
LIST_HEAD(dirremhd, dirrem);
LIST_HEAD(diraddhd, diradd);
LIST_HEAD(newblkhd, newblk);
LIST_HEAD(inodedephd, inodedep);
LIST_HEAD(allocindirhd, allocindir);
LIST_HEAD(allocdirecthd, allocdirect);
TAILQ_HEAD(allocdirectlst, allocdirect);
LIST_HEAD(indirdephd, indirdep);
LIST_HEAD(jaddrefhd, jaddref);
LIST_HEAD(jremrefhd, jremref);
LIST_HEAD(jmvrefhd, jmvref);
LIST_HEAD(jnewblkhd, jnewblk);
LIST_HEAD(jblkdephd, jblkdep);
LIST_HEAD(freeworkhd, freework);
TAILQ_HEAD(freeworklst, freework);
TAILQ_HEAD(jseglst, jseg);
TAILQ_HEAD(inoreflst, inoref);
TAILQ_HEAD(freeblklst, freeblks);

/*
 * The "pagedep" structure tracks the various dependencies related to
 * a particular directory page. If a directory page has any dependencies,
 * it will have a pagedep linked to its associated buffer. The
 * pd_dirremhd list holds the list of dirrem requests which decrement
 * inode reference counts. These requests are processed after the
 * directory page with the corresponding zero'ed entries has been
 * written. The pd_diraddhd list maintains the list of diradd requests
 * which cannot be committed until their corresponding inode has been
 * written to disk. Because a directory may have many new entries
 * being created, several lists are maintained hashed on bits of the
 * offset of the entry into the directory page to keep the lists from
 * getting too long. Once a new directory entry has been cleared to
 * be written, it is moved to the pd_pendinghd list. After the new
 * entry has been written to disk it is removed from the pd_pendinghd
 * list, any removed operations are done, and the dependency structure
 * is freed.
 */
#define	DAHASHSZ 5
#define	DIRADDHASH(offset) (((offset) >> 2) % DAHASHSZ)
struct pagedep {
	struct	worklist pd_list;	/* page buffer */
#	define	pd_state pd_list.wk_state /* check for multiple I/O starts */
	LIST_ENTRY(pagedep) pd_hash;	/* hashed lookup */
	ino_t	pd_ino;			/* associated file */
	ufs_lbn_t pd_lbn;		/* block within file */
	struct	newdirblk *pd_newdirblk; /* associated newdirblk if NEWBLOCK */
	struct	dirremhd pd_dirremhd;	/* dirrem's waiting for page */
	struct	diraddhd pd_diraddhd[DAHASHSZ]; /* diradd dir entry updates */
	struct	diraddhd pd_pendinghd;	/* directory entries awaiting write */
	struct	jmvrefhd pd_jmvrefhd;	/* Dependent journal writes. */
};

/*
 * The "inodedep" structure tracks the set of dependencies associated
 * with an inode. One task that it must manage is delayed operations
 * (i.e., work requests that must be held until the inodedep's associated
 * inode has been written to disk). Getting an inode from its incore 
 * state to the disk requires two steps to be taken by the filesystem
 * in this order: first the inode must be copied to its disk buffer by
 * the VOP_UPDATE operation; second the inode's buffer must be written
 * to disk. To ensure that both operations have happened in the required
 * order, the inodedep maintains two lists. Delayed operations are
 * placed on the id_inowait list. When the VOP_UPDATE is done, all
 * operations on the id_inowait list are moved to the id_bufwait list.
 * When the buffer is written, the items on the id_bufwait list can be
 * safely moved to the work queue to be processed. A second task of the
 * inodedep structure is to track the status of block allocation within
 * the inode.  Each block that is allocated is represented by an
 * "allocdirect" structure (see below). It is linked onto the id_newinoupdt
 * list until both its contents and its allocation in the cylinder
 * group map have been written to disk. Once these dependencies have been
 * satisfied, it is removed from the id_newinoupdt list and any followup
 * actions such as releasing the previous block or fragment are placed
 * on the id_inowait list. When an inode is updated (a VOP_UPDATE is
 * done), the "inodedep" structure is linked onto the buffer through
 * its worklist. Thus, it will be notified when the buffer is about
 * to be written and when it is done. At the update time, all the
 * elements on the id_newinoupdt list are moved to the id_inoupdt list
 * since those changes are now relevant to the copy of the inode in the
 * buffer. Also at update time, the tasks on the id_inowait list are
 * moved to the id_bufwait list so that they will be executed when
 * the updated inode has been written to disk. When the buffer containing
 * the inode is written to disk, any updates listed on the id_inoupdt
 * list are rolled back as they are not yet safe. Following the write,
 * the changes are once again rolled forward and any actions on the
 * id_bufwait list are processed (since those actions are now safe).
 * The entries on the id_inoupdt and id_newinoupdt lists must be kept
 * sorted by logical block number to speed the calculation of the size
 * of the rolled back inode (see explanation in initiate_write_inodeblock).
 * When a directory entry is created, it is represented by a diradd.
 * The diradd is added to the id_inowait list as it cannot be safely
 * written to disk until the inode that it represents is on disk. After
 * the inode is written, the id_bufwait list is processed and the diradd
 * entries are moved to the id_pendinghd list where they remain until
 * the directory block containing the name has been written to disk.
 * The purpose of keeping the entries on the id_pendinghd list is so that
 * the softdep_fsync function can find and push the inode's directory
 * name(s) as part of the fsync operation for that file.
 */
struct inodedep {
	struct	worklist id_list;	/* buffer holding inode block */
#	define	id_state id_list.wk_state /* inode dependency state */
	LIST_ENTRY(inodedep) id_hash;	/* hashed lookup */
	TAILQ_ENTRY(inodedep) id_unlinked;	/* Unlinked but ref'd inodes */
	struct	fs *id_fs;		/* associated filesystem */
	ino_t	id_ino;			/* dependent inode */
	nlink_t	id_nlinkdelta;		/* saved effective link count */
	nlink_t	id_savednlink;		/* Link saved during rollback */
	LIST_ENTRY(inodedep) id_deps;	/* bmsafemap's list of inodedep's */
	struct	bmsafemap *id_bmsafemap; /* related bmsafemap (if pending) */
	struct	diradd *id_mkdiradd;	/* diradd for a mkdir. */
	struct	inoreflst id_inoreflst;	/* Inode reference adjustments. */
	long	id_savedextsize;	/* ext size saved during rollback */
	off_t	id_savedsize;		/* file size saved during rollback */
	struct	dirremhd id_dirremhd;	/* Removals pending. */
	struct	workhead id_pendinghd;	/* entries awaiting directory write */
	struct	workhead id_bufwait;	/* operations after inode written */
	struct	workhead id_inowait;	/* operations waiting inode update */
	struct	allocdirectlst id_inoupdt; /* updates before inode written */
	struct	allocdirectlst id_newinoupdt; /* updates when inode written */
	struct	allocdirectlst id_extupdt; /* extdata updates pre-inode write */
	struct	allocdirectlst id_newextupdt; /* extdata updates at ino write */
	struct	freeblklst id_freeblklst; /* List of partial truncates. */
	union {
	struct	ufs1_dinode *idu_savedino1; /* saved ufs1_dinode contents */
	struct	ufs2_dinode *idu_savedino2; /* saved ufs2_dinode contents */
	} id_un;
};
#define	id_savedino1 id_un.idu_savedino1
#define	id_savedino2 id_un.idu_savedino2

/*
 * A "bmsafemap" structure maintains a list of dependency structures
 * that depend on the update of a particular cylinder group map.
 * It has lists for newblks, allocdirects, allocindirs, and inodedeps.
 * It is attached to the buffer of a cylinder group block when any of
 * these things are allocated from the cylinder group. It is freed
 * after the cylinder group map is written and the state of its
 * dependencies are updated with DEPCOMPLETE to indicate that it has
 * been processed.
 */
struct bmsafemap {
	struct	worklist sm_list;	/* cylgrp buffer */
#	define	sm_state sm_list.wk_state
	LIST_ENTRY(bmsafemap) sm_hash;	/* Hash links. */
	LIST_ENTRY(bmsafemap) sm_next;	/* Mount list. */
	int	sm_cg;
	struct	buf *sm_buf;		/* associated buffer */
	struct	allocdirecthd sm_allocdirecthd; /* allocdirect deps */
	struct	allocdirecthd sm_allocdirectwr; /* writing allocdirect deps */
	struct	allocindirhd sm_allocindirhd; /* allocindir deps */
	struct	allocindirhd sm_allocindirwr; /* writing allocindir deps */
	struct	inodedephd sm_inodedephd; /* inodedep deps */
	struct	inodedephd sm_inodedepwr; /* writing inodedep deps */
	struct	newblkhd sm_newblkhd;	/* newblk deps */
	struct	newblkhd sm_newblkwr;	/* writing newblk deps */
	struct	jaddrefhd sm_jaddrefhd;	/* Pending inode allocations. */
	struct	jnewblkhd sm_jnewblkhd;	/* Pending block allocations. */
	struct	workhead sm_freehd;	/* Freedep deps. */
	struct	workhead sm_freewr;	/* Written freedeps. */
};

/*
 * A "newblk" structure is attached to a bmsafemap structure when a block
 * or fragment is allocated from a cylinder group. Its state is set to
 * DEPCOMPLETE when its cylinder group map is written. It is converted to
 * an allocdirect or allocindir allocation once the allocator calls the
 * appropriate setup function. It will initially be linked onto a bmsafemap
 * list. Once converted it can be linked onto the lists described for
 * allocdirect or allocindir as described below.
 */ 
struct newblk {
	struct	worklist nb_list;	/* See comment above. */
#	define	nb_state nb_list.wk_state
	LIST_ENTRY(newblk) nb_hash;	/* Hashed lookup. */
	LIST_ENTRY(newblk) nb_deps;	/* Bmsafemap's list of newblks. */
	struct	jnewblk *nb_jnewblk;	/* New block journal entry. */
	struct	bmsafemap *nb_bmsafemap;/* Cylgrp dep (if pending). */
	struct	freefrag *nb_freefrag;	/* Fragment to be freed (if any). */
	struct	indirdephd nb_indirdeps; /* Children indirect blocks. */
	struct	workhead nb_newdirblk;	/* Dir block to notify when written. */
	struct	workhead nb_jwork;	/* Journal work pending. */
	ufs2_daddr_t	nb_newblkno;	/* New value of block pointer. */
};

/*
 * An "allocdirect" structure is attached to an "inodedep" when a new block
 * or fragment is allocated and pointed to by the inode described by
 * "inodedep". The worklist is linked to the buffer that holds the block.
 * When the block is first allocated, it is linked to the bmsafemap
 * structure associated with the buffer holding the cylinder group map
 * from which it was allocated. When the cylinder group map is written
 * to disk, ad_state has the DEPCOMPLETE flag set. When the block itself
 * is written, the COMPLETE flag is set. Once both the cylinder group map
 * and the data itself have been written, it is safe to write the inode
 * that claims the block. If there was a previous fragment that had been
 * allocated before the file was increased in size, the old fragment may
 * be freed once the inode claiming the new block is written to disk.
 * This ad_fragfree request is attached to the id_inowait list of the
 * associated inodedep (pointed to by ad_inodedep) for processing after
 * the inode is written. When a block is allocated to a directory, an
 * fsync of a file whose name is within that block must ensure not only
 * that the block containing the file name has been written, but also
 * that the on-disk inode references that block. When a new directory
 * block is created, we allocate a newdirblk structure which is linked
 * to the associated allocdirect (on its ad_newdirblk list). When the
 * allocdirect has been satisfied, the newdirblk structure is moved to
 * the inodedep id_bufwait list of its directory to await the inode
 * being written. When the inode is written, the directory entries are
 * fully committed and can be deleted from their pagedep->id_pendinghd
 * and inodedep->id_pendinghd lists.
 */
struct allocdirect {
	struct	newblk ad_block;	/* Common block logic */
#	define	ad_list ad_block.nb_list /* block pointer worklist */
#	define	ad_state ad_list.wk_state /* block pointer state */
	TAILQ_ENTRY(allocdirect) ad_next; /* inodedep's list of allocdirect's */
	struct	inodedep *ad_inodedep;	/* associated inodedep */
	ufs2_daddr_t	ad_oldblkno;	/* old value of block pointer */
	int		ad_offset;	/* Pointer offset in parent. */
	long		ad_newsize;	/* size of new block */
	long		ad_oldsize;	/* size of old block */
};
#define	ad_newblkno	ad_block.nb_newblkno
#define	ad_freefrag	ad_block.nb_freefrag
#define	ad_newdirblk	ad_block.nb_newdirblk

/*
 * A single "indirdep" structure manages all allocation dependencies for
 * pointers in an indirect block. The up-to-date state of the indirect
 * block is stored in ir_savedata. The set of pointers that may be safely
 * written to the disk is stored in ir_savebp. The state field is used
 * only to track whether the buffer is currently being written (in which
 * case it is not safe to update ir_savebp). Ir_deplisthd contains the
 * list of allocindir structures, one for each block that needs to be
 * written to disk. Once the block and its bitmap allocation have been
 * written the safecopy can be updated to reflect the allocation and the
 * allocindir structure freed. If ir_state indicates that an I/O on the
 * indirect block is in progress when ir_savebp is to be updated, the
 * update is deferred by placing the allocindir on the ir_donehd list.
 * When the I/O on the indirect block completes, the entries on the
 * ir_donehd list are processed by updating their corresponding ir_savebp
 * pointers and then freeing the allocindir structure.
 */
struct indirdep {
	struct	worklist ir_list;	/* buffer holding indirect block */
#	define	ir_state ir_list.wk_state /* indirect block pointer state */
	LIST_ENTRY(indirdep) ir_next;	/* alloc{direct,indir} list */
	TAILQ_HEAD(, freework) ir_trunc;	/* List of truncations. */
	caddr_t	ir_saveddata;		/* buffer cache contents */
	struct	buf *ir_savebp;		/* buffer holding safe copy */
	struct	buf *ir_bp;		/* buffer holding live copy */
	struct	allocindirhd ir_completehd; /* waiting for indirdep complete */
	struct	allocindirhd ir_writehd; /* Waiting for the pointer write. */
	struct	allocindirhd ir_donehd;	/* done waiting to update safecopy */
	struct	allocindirhd ir_deplisthd; /* allocindir deps for this block */
	struct	freeblks *ir_freeblks;	/* Freeblks that frees this indir. */
};

/*
 * An "allocindir" structure is attached to an "indirdep" when a new block
 * is allocated and pointed to by the indirect block described by the
 * "indirdep". The worklist is linked to the buffer that holds the new block.
 * When the block is first allocated, it is linked to the bmsafemap
 * structure associated with the buffer holding the cylinder group map
 * from which it was allocated. When the cylinder group map is written
 * to disk, ai_state has the DEPCOMPLETE flag set. When the block itself
 * is written, the COMPLETE flag is set. Once both the cylinder group map
 * and the data itself have been written, it is safe to write the entry in
 * the indirect block that claims the block; the "allocindir" dependency 
 * can then be freed as it is no longer applicable.
 */
struct allocindir {
	struct	newblk ai_block;	/* Common block area */
#	define	ai_state ai_block.nb_list.wk_state /* indirect pointer state */
	LIST_ENTRY(allocindir) ai_next;	/* indirdep's list of allocindir's */
	struct	indirdep *ai_indirdep;	/* address of associated indirdep */
	ufs2_daddr_t	ai_oldblkno;	/* old value of block pointer */
	ufs_lbn_t	ai_lbn;		/* Logical block number. */
	int		ai_offset;	/* Pointer offset in parent. */
};
#define	ai_newblkno	ai_block.nb_newblkno
#define	ai_freefrag	ai_block.nb_freefrag
#define	ai_newdirblk	ai_block.nb_newdirblk

/*
 * The allblk union is used to size the newblk structure on allocation so
 * that it may be any one of three types.
 */
union allblk {
	struct	allocindir ab_allocindir;
	struct	allocdirect ab_allocdirect;
	struct	newblk	ab_newblk;
};

/*
 * A "freefrag" structure is attached to an "inodedep" when a previously
 * allocated fragment is replaced with a larger fragment, rather than extended.
 * The "freefrag" structure is constructed and attached when the replacement
 * block is first allocated. It is processed after the inode claiming the
 * bigger block that replaces it has been written to disk.
 */
struct freefrag {
	struct	worklist ff_list;	/* id_inowait or delayed worklist */
#	define	ff_state ff_list.wk_state
	struct	worklist *ff_jdep;	/* Associated journal entry. */
	struct	workhead ff_jwork;	/* Journal work pending. */
	ufs2_daddr_t ff_blkno;		/* fragment physical block number */
	long	ff_fragsize;		/* size of fragment being deleted */
	ino_t	ff_inum;		/* owning inode number */
	enum	vtype ff_vtype;		/* owning inode's file type */
	int	ff_key;			/* trim key when deleted */
};

/*
 * A "freeblks" structure is attached to an "inodedep" when the
 * corresponding file's length is reduced to zero. It records all
 * the information needed to free the blocks of a file after its
 * zero'ed inode has been written to disk.  The actual work is done
 * by child freework structures which are responsible for individual
 * inode pointers while freeblks is responsible for retiring the
 * entire operation when it is complete and holding common members.
 */
struct freeblks {
	struct	worklist fb_list;	/* id_inowait or delayed worklist */
#	define	fb_state fb_list.wk_state /* inode and dirty block state */
	TAILQ_ENTRY(freeblks) fb_next;	/* List of inode truncates. */
	struct	jblkdephd fb_jblkdephd;	/* Journal entries pending */
	struct	workhead fb_freeworkhd;	/* Work items pending */
	struct	workhead fb_jwork;	/* Journal work pending */
	struct	vnode *fb_devvp;	/* filesystem device vnode */
#ifdef QUOTA
	struct	dquot *fb_quota[MAXQUOTAS]; /* quotas to be adjusted */
#endif
	uint64_t fb_modrev;		/* Inode revision at start of trunc. */
	off_t	fb_len;			/* Length we're truncating to. */
	ufs2_daddr_t fb_chkcnt;		/* Blocks released. */
	ino_t	fb_inum;		/* inode owner of blocks */
	enum	vtype fb_vtype;		/* inode owner's file type */
	uid_t	fb_uid;			/* uid of previous owner of blocks */
	int	fb_ref;			/* Children outstanding. */
	int	fb_cgwait;		/* cg writes outstanding. */
};

/*
 * A "freework" structure handles the release of a tree of blocks or a single
 * block.  Each indirect block in a tree is allocated its own freework
 * structure so that the indirect block may be freed only when all of its
 * children are freed.  In this way we enforce the rule that an allocated
 * block must have a valid path to a root that is journaled.  Each child
 * block acquires a reference and when the ref hits zero the parent ref
 * is decremented.  If there is no parent the freeblks ref is decremented.
 */
struct freework {
	struct	worklist fw_list;		/* Delayed worklist. */
#	define	fw_state fw_list.wk_state
	LIST_ENTRY(freework) fw_segs;		/* Seg list. */
	TAILQ_ENTRY(freework) fw_next;		/* Hash/Trunc list. */
	struct	jnewblk	 *fw_jnewblk;		/* Journal entry to cancel. */
	struct	freeblks *fw_freeblks;		/* Root of operation. */
	struct	freework *fw_parent;		/* Parent indirect. */
	struct	indirdep *fw_indir;		/* indirect block. */
	ufs2_daddr_t	 fw_blkno;		/* Our block #. */
	ufs_lbn_t	 fw_lbn;		/* Original lbn before free. */
	uint16_t	 fw_frags;		/* Number of frags. */
	uint16_t	 fw_ref;		/* Number of children out. */
	uint16_t	 fw_off;		/* Current working position. */
	uint16_t	 fw_start;		/* Start of partial truncate. */
};

/*
 * A "freedep" structure is allocated to track the completion of a bitmap
 * write for a freework.  One freedep may cover many freed blocks so long
 * as they reside in the same cylinder group.  When the cg is written
 * the freedep decrements the ref on the freework which may permit it
 * to be freed as well.
 */
struct freedep {
	struct	worklist fd_list;	/* Delayed worklist. */
	struct	freework *fd_freework;	/* Parent freework. */
};

/*
 * A "freefile" structure is attached to an inode when its
 * link count is reduced to zero. It marks the inode as free in
 * the cylinder group map after the zero'ed inode has been written
 * to disk and any associated blocks and fragments have been freed.
 */
struct freefile {
	struct	worklist fx_list;	/* id_inowait or delayed worklist */
	mode_t	fx_mode;		/* mode of inode */
	ino_t	fx_oldinum;		/* inum of the unlinked file */
	struct	vnode *fx_devvp;	/* filesystem device vnode */
	struct	workhead fx_jwork;	/* journal work pending. */
};

/*
 * A "diradd" structure is linked to an "inodedep" id_inowait list when a
 * new directory entry is allocated that references the inode described
 * by "inodedep". When the inode itself is written (either the initial
 * allocation for new inodes or with the increased link count for
 * existing inodes), the COMPLETE flag is set in da_state. If the entry
 * is for a newly allocated inode, the "inodedep" structure is associated
 * with a bmsafemap which prevents the inode from being written to disk
 * until the cylinder group has been updated. Thus the da_state COMPLETE
 * flag cannot be set until the inode bitmap dependency has been removed.
 * When creating a new file, it is safe to write the directory entry that
 * claims the inode once the referenced inode has been written. Since
 * writing the inode clears the bitmap dependencies, the DEPCOMPLETE flag
 * in the diradd can be set unconditionally when creating a file. When
 * creating a directory, there are two additional dependencies described by
 * mkdir structures (see their description below). When these dependencies
 * are resolved the DEPCOMPLETE flag is set in the diradd structure.
 * If there are multiple links created to the same inode, there will be
 * a separate diradd structure created for each link. The diradd is
 * linked onto the pg_diraddhd list of the pagedep for the directory
 * page that contains the entry. When a directory page is written,
 * the pg_diraddhd list is traversed to rollback any entries that are
 * not yet ready to be written to disk. If a directory entry is being
 * changed (by rename) rather than added, the DIRCHG flag is set and
 * the da_previous entry points to the entry that will be "removed"
 * once the new entry has been committed. During rollback, entries
 * with da_previous are replaced with the previous inode number rather
 * than zero.
 *
 * The overlaying of da_pagedep and da_previous is done to keep the
 * structure down. If a da_previous entry is present, the pointer to its
 * pagedep is available in the associated dirrem entry. If the DIRCHG flag
 * is set, the da_previous entry is valid; if not set the da_pagedep entry
 * is valid. The DIRCHG flag never changes; it is set when the structure
 * is created if appropriate and is never cleared.
 */
struct diradd {
	struct	worklist da_list;	/* id_inowait or id_pendinghd list */
#	define	da_state da_list.wk_state /* state of the new directory entry */
	LIST_ENTRY(diradd) da_pdlist;	/* pagedep holding directory block */
	doff_t	da_offset;		/* offset of new dir entry in dir blk */
	ino_t	da_newinum;		/* inode number for the new dir entry */
	union {
	struct	dirrem *dau_previous;	/* entry being replaced in dir change */
	struct	pagedep *dau_pagedep;	/* pagedep dependency for addition */
	} da_un;
	struct workhead da_jwork;	/* Journal work awaiting completion. */
};
#define	da_previous da_un.dau_previous
#define	da_pagedep da_un.dau_pagedep

/*
 * Two "mkdir" structures are needed to track the additional dependencies
 * associated with creating a new directory entry. Normally a directory
 * addition can be committed as soon as the newly referenced inode has been
 * written to disk with its increased link count. When a directory is
 * created there are two additional dependencies: writing the directory
 * data block containing the "." and ".." entries (MKDIR_BODY) and writing
 * the parent inode with the increased link count for ".." (MKDIR_PARENT).
 * These additional dependencies are tracked by two mkdir structures that
 * reference the associated "diradd" structure. When they have completed,
 * they set the DEPCOMPLETE flag on the diradd so that it knows that its
 * extra dependencies have been completed. The md_state field is used only
 * to identify which type of dependency the mkdir structure is tracking.
 * It is not used in the mainline code for any purpose other than consistency
 * checking. All the mkdir structures in the system are linked together on
 * a list. This list is needed so that a diradd can find its associated
 * mkdir structures and deallocate them if it is prematurely freed (as for
 * example if a mkdir is immediately followed by a rmdir of the same directory).
 * Here, the free of the diradd must traverse the list to find the associated
 * mkdir structures that reference it. The deletion would be faster if the
 * diradd structure were simply augmented to have two pointers that referenced
 * the associated mkdir's. However, this would increase the size of the diradd
 * structure to speed a very infrequent operation.
 */
struct mkdir {
	struct	worklist md_list;	/* id_inowait or buffer holding dir */
#	define	md_state md_list.wk_state /* type: MKDIR_PARENT or MKDIR_BODY */
	struct	diradd *md_diradd;	/* associated diradd */
	struct	jaddref *md_jaddref;	/* dependent jaddref. */
	struct	buf *md_buf;		/* MKDIR_BODY: buffer holding dir */
	LIST_ENTRY(mkdir) md_mkdirs;	/* list of all mkdirs */
};

/*
 * A "dirrem" structure describes an operation to decrement the link
 * count on an inode. The dirrem structure is attached to the pg_dirremhd
 * list of the pagedep for the directory page that contains the entry.
 * It is processed after the directory page with the deleted entry has
 * been written to disk.
 */
struct dirrem {
	struct	worklist dm_list;	/* delayed worklist */
#	define	dm_state dm_list.wk_state /* state of the old directory entry */
	LIST_ENTRY(dirrem) dm_next;	/* pagedep's list of dirrem's */
	LIST_ENTRY(dirrem) dm_inonext;	/* inodedep's list of dirrem's */
	struct	jremrefhd dm_jremrefhd;	/* Pending remove reference deps. */
	ino_t	dm_oldinum;		/* inum of the removed dir entry */
	doff_t	dm_offset;		/* offset of removed dir entry in blk */
	union {
	struct	pagedep *dmu_pagedep;	/* pagedep dependency for remove */
	ino_t	dmu_dirinum;		/* parent inode number (for rmdir) */
	} dm_un;
	struct workhead dm_jwork;	/* Journal work awaiting completion. */
};
#define	dm_pagedep dm_un.dmu_pagedep
#define	dm_dirinum dm_un.dmu_dirinum

/*
 * A "newdirblk" structure tracks the progress of a newly allocated
 * directory block from its creation until it is claimed by its on-disk
 * inode. When a block is allocated to a directory, an fsync of a file
 * whose name is within that block must ensure not only that the block
 * containing the file name has been written, but also that the on-disk
 * inode references that block. When a new directory block is created,
 * we allocate a newdirblk structure which is linked to the associated
 * allocdirect (on its ad_newdirblk list). When the allocdirect has been
 * satisfied, the newdirblk structure is moved to the inodedep id_bufwait
 * list of its directory to await the inode being written. When the inode
 * is written, the directory entries are fully committed and can be
 * deleted from their pagedep->id_pendinghd and inodedep->id_pendinghd
 * lists. Note that we could track directory blocks allocated to indirect
 * blocks using a similar scheme with the allocindir structures. Rather
 * than adding this level of complexity, we simply write those newly 
 * allocated indirect blocks synchronously as such allocations are rare.
 * In the case of a new directory the . and .. links are tracked with
 * a mkdir rather than a pagedep.  In this case we track the mkdir
 * so it can be released when it is written.  A workhead is used
 * to simplify canceling a mkdir that is removed by a subsequent dirrem.
 */
struct newdirblk {
	struct	worklist db_list;	/* id_inowait or pg_newdirblk */
#	define	db_state db_list.wk_state
	struct	pagedep *db_pagedep;	/* associated pagedep */
	struct	workhead db_mkdir;
};

/*
 * The inoref structure holds the elements common to jaddref and jremref
 * so they may easily be queued in-order on the inodedep.
 */
struct inoref {
	struct	worklist if_list;	/* Journal pending or jseg entries. */
#	define	if_state if_list.wk_state
	TAILQ_ENTRY(inoref) if_deps;	/* Links for inodedep. */
	struct	jsegdep	*if_jsegdep;	/* Will track our journal record. */
	off_t		if_diroff;	/* Directory offset. */
	ino_t		if_ino;		/* Inode number. */
	ino_t		if_parent;	/* Parent inode number. */
	nlink_t		if_nlink;	/* nlink before addition. */
	uint16_t	if_mode;	/* File mode, needed for IFMT. */
};

/*
 * A "jaddref" structure tracks a new reference (link count) on an inode
 * and prevents the link count increase and bitmap allocation until a
 * journal entry can be written.  Once the journal entry is written,
 * the inode is put on the pendinghd of the bmsafemap and a diradd or
 * mkdir entry is placed on the bufwait list of the inode.  The DEPCOMPLETE
 * flag is used to indicate that all of the required information for writing
 * the journal entry is present.  MKDIR_BODY and MKDIR_PARENT are used to
 * differentiate . and .. links from regular file names.  NEWBLOCK indicates
 * a bitmap is still pending.  If a new reference is canceled by a delete
 * prior to writing the journal the jaddref write is canceled and the
 * structure persists to prevent any disk-visible changes until it is
 * ultimately released when the file is freed or the link is dropped again.
 */
struct jaddref {
	struct	inoref	ja_ref;		/* see inoref above. */
#	define	ja_list	ja_ref.if_list	/* Jrnl pending, id_inowait, dm_jwork.*/
#	define	ja_state ja_ref.if_list.wk_state
	LIST_ENTRY(jaddref) ja_bmdeps;	/* Links for bmsafemap. */
	union {
		struct	diradd	*jau_diradd;	/* Pending diradd. */
		struct	mkdir	*jau_mkdir;	/* MKDIR_{PARENT,BODY} */
	} ja_un;
};
#define	ja_diradd	ja_un.jau_diradd
#define	ja_mkdir	ja_un.jau_mkdir
#define	ja_diroff	ja_ref.if_diroff
#define	ja_ino		ja_ref.if_ino
#define	ja_parent	ja_ref.if_parent
#define	ja_mode		ja_ref.if_mode

/*
 * A "jremref" structure tracks a removed reference (unlink) on an
 * inode and prevents the directory remove from proceeding until the
 * journal entry is written.  Once the journal has been written the remove
 * may proceed as normal. 
 */
struct jremref {
	struct	inoref	jr_ref;		/* see inoref above. */
#	define	jr_list	jr_ref.if_list	/* Linked to softdep_journal_pending. */
#	define	jr_state jr_ref.if_list.wk_state
	LIST_ENTRY(jremref) jr_deps;	/* Links for dirrem. */
	struct	dirrem	*jr_dirrem;	/* Back pointer to dirrem. */
};

/*
 * A "jmvref" structure tracks a name relocations within the same
 * directory block that occur as a result of directory compaction.
 * It prevents the updated directory entry from being written to disk
 * until the journal entry is written. Once the journal has been
 * written the compacted directory may be written to disk.
 */
struct jmvref {
	struct	worklist jm_list;	/* Linked to softdep_journal_pending. */
	LIST_ENTRY(jmvref) jm_deps;	/* Jmvref on pagedep. */
	struct pagedep	*jm_pagedep;	/* Back pointer to pagedep. */
	ino_t		jm_parent;	/* Containing directory inode number. */
	ino_t		jm_ino;		/* Inode number of our entry. */
	off_t		jm_oldoff;	/* Our old offset in directory. */
	off_t		jm_newoff;	/* Our new offset in directory. */
};

/*
 * A "jnewblk" structure tracks a newly allocated block or fragment and
 * prevents the direct or indirect block pointer as well as the cg bitmap
 * from being written until it is logged.  After it is logged the jsegdep
 * is attached to the allocdirect or allocindir until the operation is
 * completed or reverted.  If the operation is reverted prior to the journal
 * write the jnewblk structure is maintained to prevent the bitmaps from
 * reaching the disk.  Ultimately the jnewblk structure will be passed
 * to the free routine as the in memory cg is modified back to the free
 * state at which time it can be released. It may be held on any of the
 * fx_jwork, fw_jwork, fb_jwork, ff_jwork, nb_jwork, or ir_jwork lists.
 */
struct jnewblk {
	struct	worklist jn_list;	/* See lists above. */
#	define	jn_state jn_list.wk_state
	struct	jsegdep	*jn_jsegdep;	/* Will track our journal record. */
	LIST_ENTRY(jnewblk) jn_deps;	/* Jnewblks on sm_jnewblkhd. */
	struct	worklist *jn_dep;	/* Dependency to ref completed seg. */
	ufs_lbn_t	jn_lbn;		/* Lbn to which allocated. */
	ufs2_daddr_t	jn_blkno;	/* Blkno allocated */
	ino_t		jn_ino;		/* Ino to which allocated. */
	int		jn_oldfrags;	/* Previous fragments when extended. */
	int		jn_frags;	/* Number of fragments. */
};

/*
 * A "jblkdep" structure tracks jfreeblk and jtrunc records attached to a
 * freeblks structure.
 */
struct jblkdep {
	struct	worklist jb_list;	/* For softdep journal pending. */
	struct	jsegdep *jb_jsegdep;	/* Reference to the jseg. */
	struct	freeblks *jb_freeblks;	/* Back pointer to freeblks. */
	LIST_ENTRY(jblkdep) jb_deps;	/* Dep list on freeblks. */

};

/*
 * A "jfreeblk" structure tracks the journal write for freeing a block
 * or tree of blocks.  The block pointer must not be cleared in the inode
 * or indirect prior to the jfreeblk being written to the journal.
 */
struct jfreeblk {
	struct	jblkdep	jf_dep;		/* freeblks linkage. */
	ufs_lbn_t	jf_lbn;		/* Lbn from which blocks freed. */
	ufs2_daddr_t	jf_blkno;	/* Blkno being freed. */
	ino_t		jf_ino;		/* Ino from which blocks freed. */
	int		jf_frags;	/* Number of frags being freed. */
};

/*
 * A "jfreefrag" tracks the freeing of a single block when a fragment is
 * extended or an indirect page is replaced.  It is not part of a larger
 * freeblks operation.
 */
struct jfreefrag {
	struct	worklist fr_list;	/* Linked to softdep_journal_pending. */
#	define	fr_state fr_list.wk_state
	struct	jsegdep	*fr_jsegdep;	/* Will track our journal record. */
	struct freefrag	*fr_freefrag;	/* Back pointer to freefrag. */
	ufs_lbn_t	fr_lbn;		/* Lbn from which frag freed. */
	ufs2_daddr_t	fr_blkno;	/* Blkno being freed. */
	ino_t		fr_ino;		/* Ino from which frag freed. */
	int		fr_frags;	/* Size of frag being freed. */
};

/*
 * A "jtrunc" journals the intent to truncate an inode's data or extent area.
 */
struct jtrunc {
	struct	jblkdep	jt_dep;		/* freeblks linkage. */
	off_t		jt_size;	/* Final file size. */
	int		jt_extsize;	/* Final extent size. */
	ino_t		jt_ino;		/* Ino being truncated. */
};

/*
 * A "jfsync" journals the completion of an fsync which invalidates earlier
 * jtrunc records in the journal.
 */
struct jfsync {
	struct worklist	jfs_list;	/* For softdep journal pending. */
	off_t		jfs_size;	/* Sync file size. */
	int		jfs_extsize;	/* Sync extent size. */
	ino_t		jfs_ino;	/* ino being synced. */
};

/*
 * A "jsegdep" structure tracks a single reference to a written journal
 * segment so the journal space can be reclaimed when all dependencies
 * have been written. It can hang off of id_inowait, dm_jwork, da_jwork,
 * nb_jwork, ff_jwork, or fb_jwork lists.
 */
struct jsegdep {
	struct	worklist jd_list;	/* See above for lists. */
#	define	jd_state jd_list.wk_state
	struct	jseg	*jd_seg;	/* Our journal record. */
};

/*
 * A "jseg" structure contains all of the journal records written in a
 * single disk write.  The jaddref and jremref structures are linked into
 * js_entries so thay may be completed when the write completes.  The
 * js_entries also include the write dependency structures: jmvref,
 * jnewblk, jfreeblk, jfreefrag, and jtrunc.  The js_refs field counts
 * the number of entries on the js_entries list. Thus there is a single
 * jseg entry to describe each journal write.
 */
struct jseg {
	struct	worklist js_list;	/* b_deps link for journal */
#	define	js_state js_list.wk_state
	struct	workhead js_entries;	/* Entries awaiting write */
	LIST_HEAD(, freework) js_indirs;/* List of indirects in this seg. */
	TAILQ_ENTRY(jseg) js_next;	/* List of all unfinished segments. */
	struct	jblocks *js_jblocks;	/* Back pointer to block/seg list */
	struct	buf *js_buf;		/* Buffer while unwritten */
	uint64_t js_seq;		/* Journal record sequence number. */
	uint64_t js_oldseq;		/* Oldest valid sequence number. */
	int	js_size;		/* Size of journal record in bytes. */
	int	js_cnt;			/* Total items allocated. */
	int	js_refs;		/* Count of js_entries items. */
};

/*
 * A 'sbdep' structure tracks the head of the free inode list and
 * superblock writes.  This makes sure the superblock is always pointing at
 * the first possible unlinked inode for the suj recovery process.  If a
 * block write completes and we discover a new head is available the buf
 * is dirtied and the dep is kept. See the description of the UNLINK*
 * flags above for more details.
 */
struct sbdep {
	struct	worklist sb_list;	/* b_dep linkage */
	struct	fs	*sb_fs;		/* Filesystem pointer within buf. */
	struct	ufsmount *sb_ump;	/* Our mount structure */
};

/*
 * Private journaling structures.
 */
struct jblocks {
	struct jseglst	jb_segs;	/* TAILQ of current segments. */
	struct jseg	*jb_writeseg;	/* Next write to complete. */
	struct jseg	*jb_oldestseg;	/* Oldest segment with valid entries. */
	struct jextent	*jb_extent;	/* Extent array. */
	uint64_t	jb_nextseq;	/* Next sequence number. */
	uint64_t	jb_oldestwrseq;	/* Oldest written sequence number. */
	uint8_t		jb_needseg;	/* Need a forced segment. */
	uint8_t		jb_suspended;	/* Did journal suspend writes? */
	int		jb_avail;	/* Available extents. */
	int		jb_used;	/* Last used extent. */
	int		jb_head;	/* Allocator head. */
	int		jb_off;		/* Allocator extent offset. */
	int		jb_blocks;	/* Total disk blocks covered. */
	int		jb_free;	/* Total disk blocks free. */
	int		jb_min;		/* Minimum free space. */
	int		jb_low;		/* Low on space. */
	int		jb_age;		/* Insertion time of oldest rec. */
};

struct jextent {
	ufs2_daddr_t	je_daddr;	/* Disk block address. */
	int		je_blocks;	/* Disk block count. */
};

/*
 * Hash table declarations.
 */
LIST_HEAD(mkdirlist, mkdir);
LIST_HEAD(pagedep_hashhead, pagedep);
LIST_HEAD(inodedep_hashhead, inodedep);
LIST_HEAD(newblk_hashhead, newblk);
LIST_HEAD(bmsafemap_hashhead, bmsafemap);
TAILQ_HEAD(indir_hashhead, freework);

/*
 * Per-filesystem soft dependency data.
 * Allocated at mount and freed at unmount.
 */
struct mount_softdeps {
	struct	rwlock sd_fslock;		/* softdep lock */
	struct	workhead sd_workitem_pending;	/* softdep work queue */
	struct	worklist *sd_worklist_tail;	/* Tail pointer for above */
	struct	workhead sd_journal_pending;	/* journal work queue */
	struct	worklist *sd_journal_tail;	/* Tail pointer for above */
	struct	jblocks *sd_jblocks;		/* Journal block information */
	struct	inodedeplst sd_unlinked;	/* Unlinked inodes */
	struct	bmsafemaphd sd_dirtycg;		/* Dirty CGs */
	struct	mkdirlist sd_mkdirlisthd;	/* Track mkdirs */
	struct	pagedep_hashhead *sd_pdhash;	/* pagedep hash table */
	u_long	sd_pdhashsize;			/* pagedep hash table size-1 */
	long	sd_pdnextclean;			/* next hash bucket to clean */
	struct	inodedep_hashhead *sd_idhash;	/* inodedep hash table */
	u_long	sd_idhashsize;			/* inodedep hash table size-1 */
	long	sd_idnextclean;			/* next hash bucket to clean */
	struct	newblk_hashhead *sd_newblkhash;	/* newblk hash table */
	u_long	sd_newblkhashsize;		/* newblk hash table size-1 */
	struct	bmsafemap_hashhead *sd_bmhash;	/* bmsafemap hash table */
	u_long	sd_bmhashsize;			/* bmsafemap hash table size-1*/
	struct	indir_hashhead *sd_indirhash;	/* indir hash table */
	u_long	sd_indirhashsize;		/* indir hash table size-1 */
	int	sd_on_journal;			/* Items on the journal list */
	int	sd_on_worklist;			/* Items on the worklist */
	int	sd_deps;			/* Total dependency count */
	int	sd_accdeps;			/* accumulated dep count */
	int	sd_req;				/* Wakeup when deps hits 0. */
	int	sd_flags;			/* comm with flushing thread */
	int	sd_cleanups;			/* Calls to cleanup */
	struct	thread *sd_flushtd;		/* thread handling flushing */
	TAILQ_ENTRY(mount_softdeps) sd_next;	/* List of softdep filesystem */
	struct	ufsmount *sd_ump;		/* our ufsmount structure */
	u_long	sd_curdeps[D_LAST + 1];		/* count of current deps */
};
/*
 * Flags for communicating with the syncer thread.
 */
#define FLUSH_EXIT	0x0001	/* time to exit */
#define FLUSH_CLEANUP	0x0002	/* need to clear out softdep structures */
#define	FLUSH_STARTING	0x0004	/* flush thread not yet started */
#define	FLUSH_RC_ACTIVE	0x0008	/* a thread is flushing the mount point */

/*
 * Keep the old names from when these were in the ufsmount structure.
 */
#define	softdep_workitem_pending	um_softdep->sd_workitem_pending
#define	softdep_worklist_tail		um_softdep->sd_worklist_tail
#define	softdep_journal_pending		um_softdep->sd_journal_pending
#define	softdep_journal_tail		um_softdep->sd_journal_tail
#define	softdep_jblocks			um_softdep->sd_jblocks
#define	softdep_unlinked		um_softdep->sd_unlinked
#define	softdep_dirtycg			um_softdep->sd_dirtycg
#define	softdep_mkdirlisthd		um_softdep->sd_mkdirlisthd
#define	pagedep_hashtbl			um_softdep->sd_pdhash
#define	pagedep_hash_size		um_softdep->sd_pdhashsize
#define	pagedep_nextclean		um_softdep->sd_pdnextclean
#define	inodedep_hashtbl		um_softdep->sd_idhash
#define	inodedep_hash_size		um_softdep->sd_idhashsize
#define	inodedep_nextclean		um_softdep->sd_idnextclean
#define	newblk_hashtbl			um_softdep->sd_newblkhash
#define	newblk_hash_size		um_softdep->sd_newblkhashsize
#define	bmsafemap_hashtbl		um_softdep->sd_bmhash
#define	bmsafemap_hash_size		um_softdep->sd_bmhashsize
#define	indir_hashtbl			um_softdep->sd_indirhash
#define	indir_hash_size			um_softdep->sd_indirhashsize
#define	softdep_on_journal		um_softdep->sd_on_journal
#define	softdep_on_worklist		um_softdep->sd_on_worklist
#define	softdep_deps			um_softdep->sd_deps
#define	softdep_accdeps			um_softdep->sd_accdeps
#define	softdep_req			um_softdep->sd_req
#define	softdep_flags			um_softdep->sd_flags
#define	softdep_flushtd			um_softdep->sd_flushtd
#define	softdep_curdeps			um_softdep->sd_curdeps
