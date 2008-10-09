/*
 * Copyright (c) 2003-2006, Cluster File Systems, Inc, info@clusterfs.com
 * Written by Alex Tomas <alex@clusterfs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public Licens
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-
 */


/*
 * mballoc.c contains the multiblocks allocation routines
 */

#include "mballoc.h"
/*
 * MUSTDO:
 *   - test ext4_ext_search_left() and ext4_ext_search_right()
 *   - search for metadata in few groups
 *
 * TODO v4:
 *   - normalization should take into account whether file is still open
 *   - discard preallocations if no free space left (policy?)
 *   - don't normalize tails
 *   - quota
 *   - reservation for superuser
 *
 * TODO v3:
 *   - bitmap read-ahead (proposed by Oleg Drokin aka green)
 *   - track min/max extents in each group for better group selection
 *   - mb_mark_used() may allocate chunk right after splitting buddy
 *   - tree of groups sorted by number of free blocks
 *   - error handling
 */

/*
 * The allocation request involve request for multiple number of blocks
 * near to the goal(block) value specified.
 *
 * During initialization phase of the allocator we decide to use the group
 * preallocation or inode preallocation depending on the size file. The
 * size of the file could be the resulting file size we would have after
 * allocation or the current file size which ever is larger. If the size is
 * less that sbi->s_mb_stream_request we select the group
 * preallocation. The default value of s_mb_stream_request is 16
 * blocks. This can also be tuned via
 * /proc/fs/ext4/<partition>/stream_req. The value is represented in terms
 * of number of blocks.
 *
 * The main motivation for having small file use group preallocation is to
 * ensure that we have small file closer in the disk.
 *
 * First stage the allocator looks at the inode prealloc list
 * ext4_inode_info->i_prealloc_list contain list of prealloc spaces for
 * this particular inode. The inode prealloc space is represented as:
 *
 * pa_lstart -> the logical start block for this prealloc space
 * pa_pstart -> the physical start block for this prealloc space
 * pa_len    -> lenght for this prealloc space
 * pa_free   ->  free space available in this prealloc space
 *
 * The inode preallocation space is used looking at the _logical_ start
 * block. If only the logical file block falls within the range of prealloc
 * space we will consume the particular prealloc space. This make sure that
 * that the we have contiguous physical blocks representing the file blocks
 *
 * The important thing to be noted in case of inode prealloc space is that
 * we don't modify the values associated to inode prealloc space except
 * pa_free.
 *
 * If we are not able to find blocks in the inode prealloc space and if we
 * have the group allocation flag set then we look at the locality group
 * prealloc space. These are per CPU prealloc list repreasented as
 *
 * ext4_sb_info.s_locality_groups[smp_processor_id()]
 *
 * The reason for having a per cpu locality group is to reduce the contention
 * between CPUs. It is possible to get scheduled at this point.
 *
 * The locality group prealloc space is used looking at whether we have
 * enough free space (pa_free) withing the prealloc space.
 *
 * If we can't allocate blocks via inode prealloc or/and locality group
 * prealloc then we look at the buddy cache. The buddy cache is represented
 * by ext4_sb_info.s_buddy_cache (struct inode) whose file offset gets
 * mapped to the buddy and bitmap information regarding different
 * groups. The buddy information is attached to buddy cache inode so that
 * we can access them through the page cache. The information regarding
 * each group is loaded via ext4_mb_load_buddy.  The information involve
 * block bitmap and buddy information. The information are stored in the
 * inode as:
 *
 *  {                        page                        }
 *  [ group 0 buddy][ group 0 bitmap] [group 1][ group 1]...
 *
 *
 * one block each for bitmap and buddy information.  So for each group we
 * take up 2 blocks. A page can contain blocks_per_page (PAGE_CACHE_SIZE /
 * blocksize) blocks.  So it can have information regarding groups_per_page
 * which is blocks_per_page/2
 *
 * The buddy cache inode is not stored on disk. The inode is thrown
 * away when the filesystem is unmounted.
 *
 * We look for count number of blocks in the buddy cache. If we were able
 * to locate that many free blocks we return with additional information
 * regarding rest of the contiguous physical block available
 *
 * Before allocating blocks via buddy cache we normalize the request
 * blocks. This ensure we ask for more blocks that we needed. The extra
 * blocks that we get after allocation is added to the respective prealloc
 * list. In case of inode preallocation we follow a list of heuristics
 * based on file size. This can be found in ext4_mb_normalize_request. If
 * we are doing a group prealloc we try to normalize the request to
 * sbi->s_mb_group_prealloc. Default value of s_mb_group_prealloc is set to
 * 512 blocks. This can be tuned via
 * /proc/fs/ext4/<partition/group_prealloc. The value is represented in
 * terms of number of blocks. If we have mounted the file system with -O
 * stripe=<value> option the group prealloc request is normalized to the
 * stripe value (sbi->s_stripe)
 *
 * The regular allocator(using the buddy cache) support few tunables.
 *
 * /proc/fs/ext4/<partition>/min_to_scan
 * /proc/fs/ext4/<partition>/max_to_scan
 * /proc/fs/ext4/<partition>/order2_req
 *
 * The regular allocator use buddy scan only if the request len is power of
 * 2 blocks and the order of allocation is >= sbi->s_mb_order2_reqs. The
 * value of s_mb_order2_reqs can be tuned via
 * /proc/fs/ext4/<partition>/order2_req.  If the request len is equal to
 * stripe size (sbi->s_stripe), we try to search for contigous block in
 * stripe size. This should result in better allocation on RAID setup. If
 * not we search in the specific group using bitmap for best extents. The
 * tunable min_to_scan and max_to_scan controll the behaviour here.
 * min_to_scan indicate how long the mballoc __must__ look for a best
 * extent and max_to_scanindicate how long the mballoc __can__ look for a
 * best extent in the found extents. Searching for the blocks starts with
 * the group specified as the goal value in allocation context via
 * ac_g_ex. Each group is first checked based on the criteria whether it
 * can used for allocation. ext4_mb_good_group explains how the groups are
 * checked.
 *
 * Both the prealloc space are getting populated as above. So for the first
 * request we will hit the buddy cache which will result in this prealloc
 * space getting filled. The prealloc space is then later used for the
 * subsequent request.
 */

/*
 * mballoc operates on the following data:
 *  - on-disk bitmap
 *  - in-core buddy (actually includes buddy and bitmap)
 *  - preallocation descriptors (PAs)
 *
 * there are two types of preallocations:
 *  - inode
 *    assiged to specific inode and can be used for this inode only.
 *    it describes part of inode's space preallocated to specific
 *    physical blocks. any block from that preallocated can be used
 *    independent. the descriptor just tracks number of blocks left
 *    unused. so, before taking some block from descriptor, one must
 *    make sure corresponded logical block isn't allocated yet. this
 *    also means that freeing any block within descriptor's range
 *    must discard all preallocated blocks.
 *  - locality group
 *    assigned to specific locality group which does not translate to
 *    permanent set of inodes: inode can join and leave group. space
 *    from this type of preallocation can be used for any inode. thus
 *    it's consumed from the beginning to the end.
 *
 * relation between them can be expressed as:
 *    in-core buddy = on-disk bitmap + preallocation descriptors
 *
 * this mean blocks mballoc considers used are:
 *  - allocated blocks (persistent)
 *  - preallocated blocks (non-persistent)
 *
 * consistency in mballoc world means that at any time a block is either
 * free or used in ALL structures. notice: "any time" should not be read
 * literally -- time is discrete and delimited by locks.
 *
 *  to keep it simple, we don't use block numbers, instead we count number of
 *  blocks: how many blocks marked used/free in on-disk bitmap, buddy and PA.
 *
 * all operations can be expressed as:
 *  - init buddy:			buddy = on-disk + PAs
 *  - new PA:				buddy += N; PA = N
 *  - use inode PA:			on-disk += N; PA -= N
 *  - discard inode PA			buddy -= on-disk - PA; PA = 0
 *  - use locality group PA		on-disk += N; PA -= N
 *  - discard locality group PA		buddy -= PA; PA = 0
 *  note: 'buddy -= on-disk - PA' is used to show that on-disk bitmap
 *        is used in real operation because we can't know actual used
 *        bits from PA, only from on-disk bitmap
 *
 * if we follow this strict logic, then all operations above should be atomic.
 * given some of them can block, we'd have to use something like semaphores
 * killing performance on high-end SMP hardware. let's try to relax it using
 * the following knowledge:
 *  1) if buddy is referenced, it's already initialized
 *  2) while block is used in buddy and the buddy is referenced,
 *     nobody can re-allocate that block
 *  3) we work on bitmaps and '+' actually means 'set bits'. if on-disk has
 *     bit set and PA claims same block, it's OK. IOW, one can set bit in
 *     on-disk bitmap if buddy has same bit set or/and PA covers corresponded
 *     block
 *
 * so, now we're building a concurrency table:
 *  - init buddy vs.
 *    - new PA
 *      blocks for PA are allocated in the buddy, buddy must be referenced
 *      until PA is linked to allocation group to avoid concurrent buddy init
 *    - use inode PA
 *      we need to make sure that either on-disk bitmap or PA has uptodate data
 *      given (3) we care that PA-=N operation doesn't interfere with init
 *    - discard inode PA
 *      the simplest way would be to have buddy initialized by the discard
 *    - use locality group PA
 *      again PA-=N must be serialized with init
 *    - discard locality group PA
 *      the simplest way would be to have buddy initialized by the discard
 *  - new PA vs.
 *    - use inode PA
 *      i_data_sem serializes them
 *    - discard inode PA
 *      discard process must wait until PA isn't used by another process
 *    - use locality group PA
 *      some mutex should serialize them
 *    - discard locality group PA
 *      discard process must wait until PA isn't used by another process
 *  - use inode PA
 *    - use inode PA
 *      i_data_sem or another mutex should serializes them
 *    - discard inode PA
 *      discard process must wait until PA isn't used by another process
 *    - use locality group PA
 *      nothing wrong here -- they're different PAs covering different blocks
 *    - discard locality group PA
 *      discard process must wait until PA isn't used by another process
 *
 * now we're ready to make few consequences:
 *  - PA is referenced and while it is no discard is possible
 *  - PA is referenced until block isn't marked in on-disk bitmap
 *  - PA changes only after on-disk bitmap
 *  - discard must not compete with init. either init is done before
 *    any discard or they're serialized somehow
 *  - buddy init as sum of on-disk bitmap and PAs is done atomically
 *
 * a special case when we've used PA to emptiness. no need to modify buddy
 * in this case, but we should care about concurrent init
 *
 */

 /*
 * Logic in few words:
 *
 *  - allocation:
 *    load group
 *    find blocks
 *    mark bits in on-disk bitmap
 *    release group
 *
 *  - use preallocation:
 *    find proper PA (per-inode or group)
 *    load group
 *    mark bits in on-disk bitmap
 *    release group
 *    release PA
 *
 *  - free:
 *    load group
 *    mark bits in on-disk bitmap
 *    release group
 *
 *  - discard preallocations in group:
 *    mark PAs deleted
 *    move them onto local list
 *    load on-disk bitmap
 *    load group
 *    remove PA from object (inode or locality group)
 *    mark free blocks in-core
 *
 *  - discard inode's preallocations:
 */

/*
 * Locking rules
 *
 * Locks:
 *  - bitlock on a group	(group)
 *  - object (inode/locality)	(object)
 *  - per-pa lock		(pa)
 *
 * Paths:
 *  - new pa
 *    object
 *    group
 *
 *  - find and use pa:
 *    pa
 *
 *  - release consumed pa:
 *    pa
 *    group
 *    object
 *
 *  - generate in-core bitmap:
 *    group
 *        pa
 *
 *  - discard all for given object (inode, locality group):
 *    object
 *        pa
 *    group
 *
 *  - discard all for given group:
 *    group
 *        pa
 *    group
 *        object
 *
 */

static inline void *mb_correct_addr_and_bit(int *bit, void *addr)
{
#if BITS_PER_LONG == 64
	*bit += ((unsigned long) addr & 7UL) << 3;
	addr = (void *) ((unsigned long) addr & ~7UL);
#elif BITS_PER_LONG == 32
	*bit += ((unsigned long) addr & 3UL) << 3;
	addr = (void *) ((unsigned long) addr & ~3UL);
#else
#error "how many bits you are?!"
#endif
	return addr;
}

static inline int mb_test_bit(int bit, void *addr)
{
	/*
	 * ext4_test_bit on architecture like powerpc
	 * needs unsigned long aligned address
	 */
	addr = mb_correct_addr_and_bit(&bit, addr);
	return ext4_test_bit(bit, addr);
}

static inline void mb_set_bit(int bit, void *addr)
{
	addr = mb_correct_addr_and_bit(&bit, addr);
	ext4_set_bit(bit, addr);
}

static inline void mb_set_bit_atomic(spinlock_t *lock, int bit, void *addr)
{
	addr = mb_correct_addr_and_bit(&bit, addr);
	ext4_set_bit_atomic(lock, bit, addr);
}

static inline void mb_clear_bit(int bit, void *addr)
{
	addr = mb_correct_addr_and_bit(&bit, addr);
	ext4_clear_bit(bit, addr);
}

static inline void mb_clear_bit_atomic(spinlock_t *lock, int bit, void *addr)
{
	addr = mb_correct_addr_and_bit(&bit, addr);
	ext4_clear_bit_atomic(lock, bit, addr);
}

static inline int mb_find_next_zero_bit(void *addr, int max, int start)
{
	int fix = 0, ret, tmpmax;
	addr = mb_correct_addr_and_bit(&fix, addr);
	tmpmax = max + fix;
	start += fix;

	ret = ext4_find_next_zero_bit(addr, tmpmax, start) - fix;
	if (ret > max)
		return max;
	return ret;
}

static inline int mb_find_next_bit(void *addr, int max, int start)
{
	int fix = 0, ret, tmpmax;
	addr = mb_correct_addr_and_bit(&fix, addr);
	tmpmax = max + fix;
	start += fix;

	ret = ext4_find_next_bit(addr, tmpmax, start) - fix;
	if (ret > max)
		return max;
	return ret;
}

static void *mb_find_buddy(struct ext4_buddy *e4b, int order, int *max)
{
	char *bb;

	BUG_ON(EXT4_MB_BITMAP(e4b) == EXT4_MB_BUDDY(e4b));
	BUG_ON(max == NULL);

	if (order > e4b->bd_blkbits + 1) {
		*max = 0;
		return NULL;
	}

	/* at order 0 we see each particular block */
	*max = 1 << (e4b->bd_blkbits + 3);
	if (order == 0)
		return EXT4_MB_BITMAP(e4b);

	bb = EXT4_MB_BUDDY(e4b) + EXT4_SB(e4b->bd_sb)->s_mb_offsets[order];
	*max = EXT4_SB(e4b->bd_sb)->s_mb_maxs[order];

	return bb;
}

#ifdef DOUBLE_CHECK
static void mb_free_blocks_double(struct inode *inode, struct ext4_buddy *e4b,
			   int first, int count)
{
	int i;
	struct super_block *sb = e4b->bd_sb;

	if (unlikely(e4b->bd_info->bb_bitmap == NULL))
		return;
	BUG_ON(!ext4_is_group_locked(sb, e4b->bd_group));
	for (i = 0; i < count; i++) {
		if (!mb_test_bit(first + i, e4b->bd_info->bb_bitmap)) {
			ext4_fsblk_t blocknr;
			blocknr = e4b->bd_group * EXT4_BLOCKS_PER_GROUP(sb);
			blocknr += first + i;
			blocknr +=
			    le32_to_cpu(EXT4_SB(sb)->s_es->s_first_data_block);

			ext4_error(sb, __func__, "double-free of inode"
				   " %lu's block %llu(bit %u in group %lu)\n",
				   inode ? inode->i_ino : 0, blocknr,
				   first + i, e4b->bd_group);
		}
		mb_clear_bit(first + i, e4b->bd_info->bb_bitmap);
	}
}

static void mb_mark_used_double(struct ext4_buddy *e4b, int first, int count)
{
	int i;

	if (unlikely(e4b->bd_info->bb_bitmap == NULL))
		return;
	BUG_ON(!ext4_is_group_locked(e4b->bd_sb, e4b->bd_group));
	for (i = 0; i < count; i++) {
		BUG_ON(mb_test_bit(first + i, e4b->bd_info->bb_bitmap));
		mb_set_bit(first + i, e4b->bd_info->bb_bitmap);
	}
}

static void mb_cmp_bitmaps(struct ext4_buddy *e4b, void *bitmap)
{
	if (memcmp(e4b->bd_info->bb_bitmap, bitmap, e4b->bd_sb->s_blocksize)) {
		unsigned char *b1, *b2;
		int i;
		b1 = (unsigned char *) e4b->bd_info->bb_bitmap;
		b2 = (unsigned char *) bitmap;
		for (i = 0; i < e4b->bd_sb->s_blocksize; i++) {
			if (b1[i] != b2[i]) {
				printk(KERN_ERR "corruption in group %lu "
				       "at byte %u(%u): %x in copy != %x "
				       "on disk/prealloc\n",
				       e4b->bd_group, i, i * 8, b1[i], b2[i]);
				BUG();
			}
		}
	}
}

#else
static inline void mb_free_blocks_double(struct inode *inode,
				struct ext4_buddy *e4b, int first, int count)
{
	return;
}
static inline void mb_mark_used_double(struct ext4_buddy *e4b,
						int first, int count)
{
	return;
}
static inline void mb_cmp_bitmaps(struct ext4_buddy *e4b, void *bitmap)
{
	return;
}
#endif

#ifdef AGGRESSIVE_CHECK

#define MB_CHECK_ASSERT(assert)						\
do {									\
	if (!(assert)) {						\
		printk(KERN_EMERG					\
			"Assertion failure in %s() at %s:%d: \"%s\"\n",	\
			function, file, line, # assert);		\
		BUG();							\
	}								\
} while (0)

static int __mb_check_buddy(struct ext4_buddy *e4b, char *file,
				const char *function, int line)
{
	struct super_block *sb = e4b->bd_sb;
	int order = e4b->bd_blkbits + 1;
	int max;
	int max2;
	int i;
	int j;
	int k;
	int count;
	struct ext4_group_info *grp;
	int fragments = 0;
	int fstart;
	struct list_head *cur;
	void *buddy;
	void *buddy2;

	if (!test_opt(sb, MBALLOC))
		return 0;

	{
		static int mb_check_counter;
		if (mb_check_counter++ % 100 != 0)
			return 0;
	}

	while (order > 1) {
		buddy = mb_find_buddy(e4b, order, &max);
		MB_CHECK_ASSERT(buddy);
		buddy2 = mb_find_buddy(e4b, order - 1, &max2);
		MB_CHECK_ASSERT(buddy2);
		MB_CHECK_ASSERT(buddy != buddy2);
		MB_CHECK_ASSERT(max * 2 == max2);

		count = 0;
		for (i = 0; i < max; i++) {

			if (mb_test_bit(i, buddy)) {
				/* only single bit in buddy2 may be 1 */
				if (!mb_test_bit(i << 1, buddy2)) {
					MB_CHECK_ASSERT(
						mb_test_bit((i<<1)+1, buddy2));
				} else if (!mb_test_bit((i << 1) + 1, buddy2)) {
					MB_CHECK_ASSERT(
						mb_test_bit(i << 1, buddy2));
				}
				continue;
			}

			/* both bits in buddy2 must be 0 */
			MB_CHECK_ASSERT(mb_test_bit(i << 1, buddy2));
			MB_CHECK_ASSERT(mb_test_bit((i << 1) + 1, buddy2));

			for (j = 0; j < (1 << order); j++) {
				k = (i * (1 << order)) + j;
				MB_CHECK_ASSERT(
					!mb_test_bit(k, EXT4_MB_BITMAP(e4b)));
			}
			count++;
		}
		MB_CHECK_ASSERT(e4b->bd_info->bb_counters[order] == count);
		order--;
	}

	fstart = -1;
	buddy = mb_find_buddy(e4b, 0, &max);
	for (i = 0; i < max; i++) {
		if (!mb_test_bit(i, buddy)) {
			MB_CHECK_ASSERT(i >= e4b->bd_info->bb_first_free);
			if (fstart == -1) {
				fragments++;
				fstart = i;
			}
			continue;
		}
		fstart = -1;
		/* check used bits only */
		for (j = 0; j < e4b->bd_blkbits + 1; j++) {
			buddy2 = mb_find_buddy(e4b, j, &max2);
			k = i >> j;
			MB_CHECK_ASSERT(k < max2);
			MB_CHECK_ASSERT(mb_test_bit(k, buddy2));
		}
	}
	MB_CHECK_ASSERT(!EXT4_MB_GRP_NEED_INIT(e4b->bd_info));
	MB_CHECK_ASSERT(e4b->bd_info->bb_fragments == fragments);

	grp = ext4_get_group_info(sb, e4b->bd_group);
	buddy = mb_find_buddy(e4b, 0, &max);
	list_for_each(cur, &grp->bb_prealloc_list) {
		ext4_group_t groupnr;
		struct ext4_prealloc_space *pa;
		pa = list_entry(cur, struct ext4_prealloc_space, pa_group_list);
		ext4_get_group_no_and_offset(sb, pa->pa_pstart, &groupnr, &k);
		MB_CHECK_ASSERT(groupnr == e4b->bd_group);
		for (i = 0; i < pa->pa_len; i++)
			MB_CHECK_ASSERT(mb_test_bit(k + i, buddy));
	}
	return 0;
}
#undef MB_CHECK_ASSERT
#define mb_check_buddy(e4b) __mb_check_buddy(e4b,	\
					__FILE__, __func__, __LINE__)
#else
#define mb_check_buddy(e4b)
#endif

/* FIXME!! need more doc */
static void ext4_mb_mark_free_simple(struct super_block *sb,
				void *buddy, unsigned first, int len,
					struct ext4_group_info *grp)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	unsigned short min;
	unsigned short max;
	unsigned short chunk;
	unsigned short border;

	BUG_ON(len > EXT4_BLOCKS_PER_GROUP(sb));

	border = 2 << sb->s_blocksize_bits;

	while (len > 0) {
		/* find how many blocks can be covered since this position */
		max = ffs(first | border) - 1;

		/* find how many blocks of power 2 we need to mark */
		min = fls(len) - 1;

		if (max < min)
			min = max;
		chunk = 1 << min;

		/* mark multiblock chunks only */
		grp->bb_counters[min]++;
		if (min > 0)
			mb_clear_bit(first >> min,
				     buddy + sbi->s_mb_offsets[min]);

		len -= chunk;
		first += chunk;
	}
}

static void ext4_mb_generate_buddy(struct super_block *sb,
				void *buddy, void *bitmap, ext4_group_t group)
{
	struct ext4_group_info *grp = ext4_get_group_info(sb, group);
	unsigned short max = EXT4_BLOCKS_PER_GROUP(sb);
	unsigned short i = 0;
	unsigned short first;
	unsigned short len;
	unsigned free = 0;
	unsigned fragments = 0;
	unsigned long long period = get_cycles();

	/* initialize buddy from bitmap which is aggregation
	 * of on-disk bitmap and preallocations */
	i = mb_find_next_zero_bit(bitmap, max, 0);
	grp->bb_first_free = i;
	while (i < max) {
		fragments++;
		first = i;
		i = mb_find_next_bit(bitmap, max, i);
		len = i - first;
		free += len;
		if (len > 1)
			ext4_mb_mark_free_simple(sb, buddy, first, len, grp);
		else
			grp->bb_counters[0]++;
		if (i < max)
			i = mb_find_next_zero_bit(bitmap, max, i);
	}
	grp->bb_fragments = fragments;

	if (free != grp->bb_free) {
		ext4_error(sb, __func__,
			"EXT4-fs: group %lu: %u blocks in bitmap, %u in gd\n",
			group, free, grp->bb_free);
		/*
		 * If we intent to continue, we consider group descritor
		 * corrupt and update bb_free using bitmap value
		 */
		grp->bb_free = free;
	}

	clear_bit(EXT4_GROUP_INFO_NEED_INIT_BIT, &(grp->bb_state));

	period = get_cycles() - period;
	spin_lock(&EXT4_SB(sb)->s_bal_lock);
	EXT4_SB(sb)->s_mb_buddies_generated++;
	EXT4_SB(sb)->s_mb_generation_time += period;
	spin_unlock(&EXT4_SB(sb)->s_bal_lock);
}

/* The buddy information is attached the buddy cache inode
 * for convenience. The information regarding each group
 * is loaded via ext4_mb_load_buddy. The information involve
 * block bitmap and buddy information. The information are
 * stored in the inode as
 *
 * {                        page                        }
 * [ group 0 buddy][ group 0 bitmap] [group 1][ group 1]...
 *
 *
 * one block each for bitmap and buddy information.
 * So for each group we take up 2 blocks. A page can
 * contain blocks_per_page (PAGE_CACHE_SIZE / blocksize)  blocks.
 * So it can have information regarding groups_per_page which
 * is blocks_per_page/2
 */

static int ext4_mb_init_cache(struct page *page, char *incore)
{
	int blocksize;
	int blocks_per_page;
	int groups_per_page;
	int err = 0;
	int i;
	ext4_group_t first_group;
	int first_block;
	struct super_block *sb;
	struct buffer_head *bhs;
	struct buffer_head **bh;
	struct inode *inode;
	char *data;
	char *bitmap;

	mb_debug("init page %lu\n", page->index);

	inode = page->mapping->host;
	sb = inode->i_sb;
	blocksize = 1 << inode->i_blkbits;
	blocks_per_page = PAGE_CACHE_SIZE / blocksize;

	groups_per_page = blocks_per_page >> 1;
	if (groups_per_page == 0)
		groups_per_page = 1;

	/* allocate buffer_heads to read bitmaps */
	if (groups_per_page > 1) {
		err = -ENOMEM;
		i = sizeof(struct buffer_head *) * groups_per_page;
		bh = kzalloc(i, GFP_NOFS);
		if (bh == NULL)
			goto out;
	} else
		bh = &bhs;

	first_group = page->index * blocks_per_page / 2;

	/* read all groups the page covers into the cache */
	for (i = 0; i < groups_per_page; i++) {
		struct ext4_group_desc *desc;

		if (first_group + i >= EXT4_SB(sb)->s_groups_count)
			break;

		err = -EIO;
		desc = ext4_get_group_desc(sb, first_group + i, NULL);
		if (desc == NULL)
			goto out;

		err = -ENOMEM;
		bh[i] = sb_getblk(sb, ext4_block_bitmap(sb, desc));
		if (bh[i] == NULL)
			goto out;

		if (bh_uptodate_or_lock(bh[i]))
			continue;

		spin_lock(sb_bgl_lock(EXT4_SB(sb), first_group + i));
		if (desc->bg_flags & cpu_to_le16(EXT4_BG_BLOCK_UNINIT)) {
			ext4_init_block_bitmap(sb, bh[i],
						first_group + i, desc);
			set_buffer_uptodate(bh[i]);
			unlock_buffer(bh[i]);
			spin_unlock(sb_bgl_lock(EXT4_SB(sb), first_group + i));
			continue;
		}
		spin_unlock(sb_bgl_lock(EXT4_SB(sb), first_group + i));
		get_bh(bh[i]);
		bh[i]->b_end_io = end_buffer_read_sync;
		submit_bh(READ, bh[i]);
		mb_debug("read bitmap for group %lu\n", first_group + i);
	}

	/* wait for I/O completion */
	for (i = 0; i < groups_per_page && bh[i]; i++)
		wait_on_buffer(bh[i]);

	err = -EIO;
	for (i = 0; i < groups_per_page && bh[i]; i++)
		if (!buffer_uptodate(bh[i]))
			goto out;

	err = 0;
	first_block = page->index * blocks_per_page;
	for (i = 0; i < blocks_per_page; i++) {
		int group;
		struct ext4_group_info *grinfo;

		group = (first_block + i) >> 1;
		if (group >= EXT4_SB(sb)->s_groups_count)
			break;

		/*
		 * data carry information regarding this
		 * particular group in the format specified
		 * above
		 *
		 */
		data = page_address(page) + (i * blocksize);
		bitmap = bh[group - first_group]->b_data;

		/*
		 * We place the buddy block and bitmap block
		 * close together
		 */
		if ((first_block + i) & 1) {
			/* this is block of buddy */
			BUG_ON(incore == NULL);
			mb_debug("put buddy for group %u in page %lu/%x\n",
				group, page->index, i * blocksize);
			memset(data, 0xff, blocksize);
			grinfo = ext4_get_group_info(sb, group);
			grinfo->bb_fragments = 0;
			memset(grinfo->bb_counters, 0,
			       sizeof(unsigned short)*(sb->s_blocksize_bits+2));
			/*
			 * incore got set to the group block bitmap below
			 */
			ext4_mb_generate_buddy(sb, data, incore, group);
			incore = NULL;
		} else {
			/* this is block of bitmap */
			BUG_ON(incore != NULL);
			mb_debug("put bitmap for group %u in page %lu/%x\n",
				group, page->index, i * blocksize);

			/* see comments in ext4_mb_put_pa() */
			ext4_lock_group(sb, group);
			memcpy(data, bitmap, blocksize);

			/* mark all preallocated blks used in in-core bitmap */
			ext4_mb_generate_from_pa(sb, data, group);
			ext4_unlock_group(sb, group);

			/* set incore so that the buddy information can be
			 * generated using this
			 */
			incore = data;
		}
	}
	SetPageUptodate(page);

out:
	if (bh) {
		for (i = 0; i < groups_per_page && bh[i]; i++)
			brelse(bh[i]);
		if (bh != &bhs)
			kfree(bh);
	}
	return err;
}

static noinline_for_stack int
ext4_mb_load_buddy(struct super_block *sb, ext4_group_t group,
					struct ext4_buddy *e4b)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct inode *inode = sbi->s_buddy_cache;
	int blocks_per_page;
	int block;
	int pnum;
	int poff;
	struct page *page;
	int ret;

	mb_debug("load group %lu\n", group);

	blocks_per_page = PAGE_CACHE_SIZE / sb->s_blocksize;

	e4b->bd_blkbits = sb->s_blocksize_bits;
	e4b->bd_info = ext4_get_group_info(sb, group);
	e4b->bd_sb = sb;
	e4b->bd_group = group;
	e4b->bd_buddy_page = NULL;
	e4b->bd_bitmap_page = NULL;

	/*
	 * the buddy cache inode stores the block bitmap
	 * and buddy information in consecutive blocks.
	 * So for each group we need two blocks.
	 */
	block = group * 2;
	pnum = block / blocks_per_page;
	poff = block % blocks_per_page;

	/* we could use find_or_create_page(), but it locks page
	 * what we'd like to avoid in fast path ... */
	page = find_get_page(inode->i_mapping, pnum);
	if (page == NULL || !PageUptodate(page)) {
		if (page)
			page_cache_release(page);
		page = find_or_create_page(inode->i_mapping, pnum, GFP_NOFS);
		if (page) {
			BUG_ON(page->mapping != inode->i_mapping);
			if (!PageUptodate(page)) {
				ret = ext4_mb_init_cache(page, NULL);
				if (ret) {
					unlock_page(page);
					goto err;
				}
				mb_cmp_bitmaps(e4b, page_address(page) +
					       (poff * sb->s_blocksize));
			}
			unlock_page(page);
		}
	}
	if (page == NULL || !PageUptodate(page)) {
		ret = -EIO;
		goto err;
	}
	e4b->bd_bitmap_page = page;
	e4b->bd_bitmap = page_address(page) + (poff * sb->s_blocksize);
	mark_page_accessed(page);

	block++;
	pnum = block / blocks_per_page;
	poff = block % blocks_per_page;

	page = find_get_page(inode->i_mapping, pnum);
	if (page == NULL || !PageUptodate(page)) {
		if (page)
			page_cache_release(page);
		page = find_or_create_page(inode->i_mapping, pnum, GFP_NOFS);
		if (page) {
			BUG_ON(page->mapping != inode->i_mapping);
			if (!PageUptodate(page)) {
				ret = ext4_mb_init_cache(page, e4b->bd_bitmap);
				if (ret) {
					unlock_page(page);
					goto err;
				}
			}
			unlock_page(page);
		}
	}
	if (page == NULL || !PageUptodate(page)) {
		ret = -EIO;
		goto err;
	}
	e4b->bd_buddy_page = page;
	e4b->bd_buddy = page_address(page) + (poff * sb->s_blocksize);
	mark_page_accessed(page);

	BUG_ON(e4b->bd_bitmap_page == NULL);
	BUG_ON(e4b->bd_buddy_page == NULL);

	return 0;

err:
	if (e4b->bd_bitmap_page)
		page_cache_release(e4b->bd_bitmap_page);
	if (e4b->bd_buddy_page)
		page_cache_release(e4b->bd_buddy_page);
	e4b->bd_buddy = NULL;
	e4b->bd_bitmap = NULL;
	return ret;
}

static void ext4_mb_release_desc(struct ext4_buddy *e4b)
{
	if (e4b->bd_bitmap_page)
		page_cache_release(e4b->bd_bitmap_page);
	if (e4b->bd_buddy_page)
		page_cache_release(e4b->bd_buddy_page);
}


static int mb_find_order_for_block(struct ext4_buddy *e4b, int block)
{
	int order = 1;
	void *bb;

	BUG_ON(EXT4_MB_BITMAP(e4b) == EXT4_MB_BUDDY(e4b));
	BUG_ON(block >= (1 << (e4b->bd_blkbits + 3)));

	bb = EXT4_MB_BUDDY(e4b);
	while (order <= e4b->bd_blkbits + 1) {
		block = block >> 1;
		if (!mb_test_bit(block, bb)) {
			/* this block is part of buddy of order 'order' */
			return order;
		}
		bb += 1 << (e4b->bd_blkbits - order);
		order++;
	}
	return 0;
}

static void mb_clear_bits(spinlock_t *lock, void *bm, int cur, int len)
{
	__u32 *addr;

	len = cur + len;
	while (cur < len) {
		if ((cur & 31) == 0 && (len - cur) >= 32) {
			/* fast path: clear whole word at once */
			addr = bm + (cur >> 3);
			*addr = 0;
			cur += 32;
			continue;
		}
		mb_clear_bit_atomic(lock, cur, bm);
		cur++;
	}
}

static void mb_set_bits(spinlock_t *lock, void *bm, int cur, int len)
{
	__u32 *addr;

	len = cur + len;
	while (cur < len) {
		if ((cur & 31) == 0 && (len - cur) >= 32) {
			/* fast path: set whole word at once */
			addr = bm + (cur >> 3);
			*addr = 0xffffffff;
			cur += 32;
			continue;
		}
		mb_set_bit_atomic(lock, cur, bm);
		cur++;
	}
}

static void mb_free_blocks(struct inode *inode, struct ext4_buddy *e4b,
			  int first, int count)
{
	int block = 0;
	int max = 0;
	int order;
	void *buddy;
	void *buddy2;
	struct super_block *sb = e4b->bd_sb;

	BUG_ON(first + count > (sb->s_blocksize << 3));
	BUG_ON(!ext4_is_group_locked(sb, e4b->bd_group));
	mb_check_buddy(e4b);
	mb_free_blocks_double(inode, e4b, first, count);

	e4b->bd_info->bb_free += count;
	if (first < e4b->bd_info->bb_first_free)
		e4b->bd_info->bb_first_free = first;

	/* let's maintain fragments counter */
	if (first != 0)
		block = !mb_test_bit(first - 1, EXT4_MB_BITMAP(e4b));
	if (first + count < EXT4_SB(sb)->s_mb_maxs[0])
		max = !mb_test_bit(first + count, EXT4_MB_BITMAP(e4b));
	if (block && max)
		e4b->bd_info->bb_fragments--;
	else if (!block && !max)
		e4b->bd_info->bb_fragments++;

	/* let's maintain buddy itself */
	while (count-- > 0) {
		block = first++;
		order = 0;

		if (!mb_test_bit(block, EXT4_MB_BITMAP(e4b))) {
			ext4_fsblk_t blocknr;
			blocknr = e4b->bd_group * EXT4_BLOCKS_PER_GROUP(sb);
			blocknr += block;
			blocknr +=
			    le32_to_cpu(EXT4_SB(sb)->s_es->s_first_data_block);
			ext4_unlock_group(sb, e4b->bd_group);
			ext4_error(sb, __func__, "double-free of inode"
				   " %lu's block %llu(bit %u in group %lu)\n",
				   inode ? inode->i_ino : 0, blocknr, block,
				   e4b->bd_group);
			ext4_lock_group(sb, e4b->bd_group);
		}
		mb_clear_bit(block, EXT4_MB_BITMAP(e4b));
		e4b->bd_info->bb_counters[order]++;

		/* start of the buddy */
		buddy = mb_find_buddy(e4b, order, &max);

		do {
			block &= ~1UL;
			if (mb_test_bit(block, buddy) ||
					mb_test_bit(block + 1, buddy))
				break;

			/* both the buddies are free, try to coalesce them */
			buddy2 = mb_find_buddy(e4b, order + 1, &max);

			if (!buddy2)
				break;

			if (order > 0) {
				/* for special purposes, we don't set
				 * free bits in bitmap */
				mb_set_bit(block, buddy);
				mb_set_bit(block + 1, buddy);
			}
			e4b->bd_info->bb_counters[order]--;
			e4b->bd_info->bb_counters[order]--;

			block = block >> 1;
			order++;
			e4b->bd_info->bb_counters[order]++;

			mb_clear_bit(block, buddy2);
			buddy = buddy2;
		} while (1);
	}
	mb_check_buddy(e4b);
}

static int mb_find_extent(struct ext4_buddy *e4b, int order, int block,
				int needed, struct ext4_free_extent *ex)
{
	int next = block;
	int max;
	int ord;
	void *buddy;

	BUG_ON(!ext4_is_group_locked(e4b->bd_sb, e4b->bd_group));
	BUG_ON(ex == NULL);

	buddy = mb_find_buddy(e4b, order, &max);
	BUG_ON(buddy == NULL);
	BUG_ON(block >= max);
	if (mb_test_bit(block, buddy)) {
		ex->fe_len = 0;
		ex->fe_start = 0;
		ex->fe_group = 0;
		return 0;
	}

	/* FIXME dorp order completely ? */
	if (likely(order == 0)) {
		/* find actual order */
		order = mb_find_order_for_block(e4b, block);
		block = block >> order;
	}

	ex->fe_len = 1 << order;
	ex->fe_start = block << order;
	ex->fe_group = e4b->bd_group;

	/* calc difference from given start */
	next = next - ex->fe_start;
	ex->fe_len -= next;
	ex->fe_start += next;

	while (needed > ex->fe_len &&
	       (buddy = mb_find_buddy(e4b, order, &max))) {

		if (block + 1 >= max)
			break;

		next = (block + 1) * (1 << order);
		if (mb_test_bit(next, EXT4_MB_BITMAP(e4b)))
			break;

		ord = mb_find_order_for_block(e4b, next);

		order = ord;
		block = next >> order;
		ex->fe_len += 1 << order;
	}

	BUG_ON(ex->fe_start + ex->fe_len > (1 << (e4b->bd_blkbits + 3)));
	return ex->fe_len;
}

static int mb_mark_used(struct ext4_buddy *e4b, struct ext4_free_extent *ex)
{
	int ord;
	int mlen = 0;
	int max = 0;
	int cur;
	int start = ex->fe_start;
	int len = ex->fe_len;
	unsigned ret = 0;
	int len0 = len;
	void *buddy;

	BUG_ON(start + len > (e4b->bd_sb->s_blocksize << 3));
	BUG_ON(e4b->bd_group != ex->fe_group);
	BUG_ON(!ext4_is_group_locked(e4b->bd_sb, e4b->bd_group));
	mb_check_buddy(e4b);
	mb_mark_used_double(e4b, start, len);

	e4b->bd_info->bb_free -= len;
	if (e4b->bd_info->bb_first_free == start)
		e4b->bd_info->bb_first_free += len;

	/* let's maintain fragments counter */
	if (start != 0)
		mlen = !mb_test_bit(start - 1, EXT4_MB_BITMAP(e4b));
	if (start + len < EXT4_SB(e4b->bd_sb)->s_mb_maxs[0])
		max = !mb_test_bit(start + len, EXT4_MB_BITMAP(e4b));
	if (mlen && max)
		e4b->bd_info->bb_fragments++;
	else if (!mlen && !max)
		e4b->bd_info->bb_fragments--;

	/* let's maintain buddy itself */
	while (len) {
		ord = mb_find_order_for_block(e4b, start);

		if (((start >> ord) << ord) == start && len >= (1 << ord)) {
			/* the whole chunk may be allocated at once! */
			mlen = 1 << ord;
			buddy = mb_find_buddy(e4b, ord, &max);
			BUG_ON((start >> ord) >= max);
			mb_set_bit(start >> ord, buddy);
			e4b->bd_info->bb_counters[ord]--;
			start += mlen;
			len -= mlen;
			BUG_ON(len < 0);
			continue;
		}

		/* store for history */
		if (ret == 0)
			ret = len | (ord << 16);

		/* we have to split large buddy */
		BUG_ON(ord <= 0);
		buddy = mb_find_buddy(e4b, ord, &max);
		mb_set_bit(start >> ord, buddy);
		e4b->bd_info->bb_counters[ord]--;

		ord--;
		cur = (start >> ord) & ~1U;
		buddy = mb_find_buddy(e4b, ord, &max);
		mb_clear_bit(cur, buddy);
		mb_clear_bit(cur + 1, buddy);
		e4b->bd_info->bb_counters[ord]++;
		e4b->bd_info->bb_counters[ord]++;
	}

	mb_set_bits(sb_bgl_lock(EXT4_SB(e4b->bd_sb), ex->fe_group),
			EXT4_MB_BITMAP(e4b), ex->fe_start, len0);
	mb_check_buddy(e4b);

	return ret;
}

/*
 * Must be called under group lock!
 */
static void ext4_mb_use_best_found(struct ext4_allocation_context *ac,
					struct ext4_buddy *e4b)
{
	struct ext4_sb_info *sbi = EXT4_SB(ac->ac_sb);
	int ret;

	BUG_ON(ac->ac_b_ex.fe_group != e4b->bd_group);
	BUG_ON(ac->ac_status == AC_STATUS_FOUND);

	ac->ac_b_ex.fe_len = min(ac->ac_b_ex.fe_len, ac->ac_g_ex.fe_len);
	ac->ac_b_ex.fe_logical = ac->ac_g_ex.fe_logical;
	ret = mb_mark_used(e4b, &ac->ac_b_ex);

	/* preallocation can change ac_b_ex, thus we store actually
	 * allocated blocks for history */
	ac->ac_f_ex = ac->ac_b_ex;

	ac->ac_status = AC_STATUS_FOUND;
	ac->ac_tail = ret & 0xffff;
	ac->ac_buddy = ret >> 16;

	/* XXXXXXX: SUCH A HORRIBLE **CK */
	/*FIXME!! Why ? */
	ac->ac_bitmap_page = e4b->bd_bitmap_page;
	get_page(ac->ac_bitmap_page);
	ac->ac_buddy_page = e4b->bd_buddy_page;
	get_page(ac->ac_buddy_page);

	/* store last allocated for subsequent stream allocation */
	if ((ac->ac_flags & EXT4_MB_HINT_DATA)) {
		spin_lock(&sbi->s_md_lock);
		sbi->s_mb_last_group = ac->ac_f_ex.fe_group;
		sbi->s_mb_last_start = ac->ac_f_ex.fe_start;
		spin_unlock(&sbi->s_md_lock);
	}
}

/*
 * regular allocator, for general purposes allocation
 */

static void ext4_mb_check_limits(struct ext4_allocation_context *ac,
					struct ext4_buddy *e4b,
					int finish_group)
{
	struct ext4_sb_info *sbi = EXT4_SB(ac->ac_sb);
	struct ext4_free_extent *bex = &ac->ac_b_ex;
	struct ext4_free_extent *gex = &ac->ac_g_ex;
	struct ext4_free_extent ex;
	int max;

	/*
	 * We don't want to scan for a whole year
	 */
	if (ac->ac_found > sbi->s_mb_max_to_scan &&
			!(ac->ac_flags & EXT4_MB_HINT_FIRST)) {
		ac->ac_status = AC_STATUS_BREAK;
		return;
	}

	/*
	 * Haven't found good chunk so far, let's continue
	 */
	if (bex->fe_len < gex->fe_len)
		return;

	if ((finish_group || ac->ac_found > sbi->s_mb_min_to_scan)
			&& bex->fe_group == e4b->bd_group) {
		/* recheck chunk's availability - we don't know
		 * when it was found (within this lock-unlock
		 * period or not) */
		max = mb_find_extent(e4b, 0, bex->fe_start, gex->fe_len, &ex);
		if (max >= gex->fe_len) {
			ext4_mb_use_best_found(ac, e4b);
			return;
		}
	}
}

/*
 * The routine checks whether found extent is good enough. If it is,
 * then the extent gets marked used and flag is set to the context
 * to stop scanning. Otherwise, the extent is compared with the
 * previous found extent and if new one is better, then it's stored
 * in the context. Later, the best found extent will be used, if
 * mballoc can't find good enough extent.
 *
 * FIXME: real allocation policy is to be designed yet!
 */
static void ext4_mb_measure_extent(struct ext4_allocation_context *ac,
					struct ext4_free_extent *ex,
					struct ext4_buddy *e4b)
{
	struct ext4_free_extent *bex = &ac->ac_b_ex;
	struct ext4_free_extent *gex = &ac->ac_g_ex;

	BUG_ON(ex->fe_len <= 0);
	BUG_ON(ex->fe_len >= EXT4_BLOCKS_PER_GROUP(ac->ac_sb));
	BUG_ON(ex->fe_start >= EXT4_BLOCKS_PER_GROUP(ac->ac_sb));
	BUG_ON(ac->ac_status != AC_STATUS_CONTINUE);

	ac->ac_found++;

	/*
	 * The special case - take what you catch first
	 */
	if (unlikely(ac->ac_flags & EXT4_MB_HINT_FIRST)) {
		*bex = *ex;
		ext4_mb_use_best_found(ac, e4b);
		return;
	}

	/*
	 * Let's check whether the chuck is good enough
	 */
	if (ex->fe_len == gex->fe_len) {
		*bex = *ex;
		ext4_mb_use_best_found(ac, e4b);
		return;
	}

	/*
	 * If this is first found extent, just store it in the context
	 */
	if (bex->fe_len == 0) {
		*bex = *ex;
		return;
	}

	/*
	 * If new found extent is better, store it in the context
	 */
	if (bex->fe_len < gex->fe_len) {
		/* if the request isn't satisfied, any found extent
		 * larger than previous best one is better */
		if (ex->fe_len > bex->fe_len)
			*bex = *ex;
	} else if (ex->fe_len > gex->fe_len) {
		/* if the request is satisfied, then we try to find
		 * an extent that still satisfy the request, but is
		 * smaller than previous one */
		if (ex->fe_len < bex->fe_len)
			*bex = *ex;
	}

	ext4_mb_check_limits(ac, e4b, 0);
}

static int ext4_mb_try_best_found(struct ext4_allocation_context *ac,
					struct ext4_buddy *e4b)
{
	struct ext4_free_extent ex = ac->ac_b_ex;
	ext4_group_t group = ex.fe_group;
	int max;
	int err;

	BUG_ON(ex.fe_len <= 0);
	err = ext4_mb_load_buddy(ac->ac_sb, group, e4b);
	if (err)
		return err;

	ext4_lock_group(ac->ac_sb, group);
	max = mb_find_extent(e4b, 0, ex.fe_start, ex.fe_len, &ex);

	if (max > 0) {
		ac->ac_b_ex = ex;
		ext4_mb_use_best_found(ac, e4b);
	}

	ext4_unlock_group(ac->ac_sb, group);
	ext4_mb_release_desc(e4b);

	return 0;
}

static int ext4_mb_find_by_goal(struct ext4_allocation_context *ac,
				struct ext4_buddy *e4b)
{
	ext4_group_t group = ac->ac_g_ex.fe_group;
	int max;
	int err;
	struct ext4_sb_info *sbi = EXT4_SB(ac->ac_sb);
	struct ext4_super_block *es = sbi->s_es;
	struct ext4_free_extent ex;

	if (!(ac->ac_flags & EXT4_MB_HINT_TRY_GOAL))
		return 0;

	err = ext4_mb_load_buddy(ac->ac_sb, group, e4b);
	if (err)
		return err;

	ext4_lock_group(ac->ac_sb, group);
	max = mb_find_extent(e4b, 0, ac->ac_g_ex.fe_start,
			     ac->ac_g_ex.fe_len, &ex);

	if (max >= ac->ac_g_ex.fe_len && ac->ac_g_ex.fe_len == sbi->s_stripe) {
		ext4_fsblk_t start;

		start = (e4b->bd_group * EXT4_BLOCKS_PER_GROUP(ac->ac_sb)) +
			ex.fe_start + le32_to_cpu(es->s_first_data_block);
		/* use do_div to get remainder (would be 64-bit modulo) */
		if (do_div(start, sbi->s_stripe) == 0) {
			ac->ac_found++;
			ac->ac_b_ex = ex;
			ext4_mb_use_best_found(ac, e4b);
		}
	} else if (max >= ac->ac_g_ex.fe_len) {
		BUG_ON(ex.fe_len <= 0);
		BUG_ON(ex.fe_group != ac->ac_g_ex.fe_group);
		BUG_ON(ex.fe_start != ac->ac_g_ex.fe_start);
		ac->ac_found++;
		ac->ac_b_ex = ex;
		ext4_mb_use_best_found(ac, e4b);
	} else if (max > 0 && (ac->ac_flags & EXT4_MB_HINT_MERGE)) {
		/* Sometimes, caller may want to merge even small
		 * number of blocks to an existing extent */
		BUG_ON(ex.fe_len <= 0);
		BUG_ON(ex.fe_group != ac->ac_g_ex.fe_group);
		BUG_ON(ex.fe_start != ac->ac_g_ex.fe_start);
		ac->ac_found++;
		ac->ac_b_ex = ex;
		ext4_mb_use_best_found(ac, e4b);
	}
	ext4_unlock_group(ac->ac_sb, group);
	ext4_mb_release_desc(e4b);

	return 0;
}

/*
 * The routine scans buddy structures (not bitmap!) from given order
 * to max order and tries to find big enough chunk to satisfy the req
 */
static void ext4_mb_simple_scan_group(struct ext4_allocation_context *ac,
					struct ext4_buddy *e4b)
{
	struct super_block *sb = ac->ac_sb;
	struct ext4_group_info *grp = e4b->bd_info;
	void *buddy;
	int i;
	int k;
	int max;

	BUG_ON(ac->ac_2order <= 0);
	for (i = ac->ac_2order; i <= sb->s_blocksize_bits + 1; i++) {
		if (grp->bb_counters[i] == 0)
			continue;

		buddy = mb_find_buddy(e4b, i, &max);
		BUG_ON(buddy == NULL);

		k = mb_find_next_zero_bit(buddy, max, 0);
		BUG_ON(k >= max);

		ac->ac_found++;

		ac->ac_b_ex.fe_len = 1 << i;
		ac->ac_b_ex.fe_start = k << i;
		ac->ac_b_ex.fe_group = e4b->bd_group;

		ext4_mb_use_best_found(ac, e4b);

		BUG_ON(ac->ac_b_ex.fe_len != ac->ac_g_ex.fe_len);

		if (EXT4_SB(sb)->s_mb_stats)
			atomic_inc(&EXT4_SB(sb)->s_bal_2orders);

		break;
	}
}

/*
 * The routine scans the group and measures all found extents.
 * In order to optimize scanning, caller must pass number of
 * free blocks in the group, so the routine can know upper limit.
 */
static void ext4_mb_complex_scan_group(struct ext4_allocation_context *ac,
					struct ext4_buddy *e4b)
{
	struct super_block *sb = ac->ac_sb;
	void *bitmap = EXT4_MB_BITMAP(e4b);
	struct ext4_free_extent ex;
	int i;
	int free;

	free = e4b->bd_info->bb_free;
	BUG_ON(free <= 0);

	i = e4b->bd_info->bb_first_free;

	while (free && ac->ac_status == AC_STATUS_CONTINUE) {
		i = mb_find_next_zero_bit(bitmap,
						EXT4_BLOCKS_PER_GROUP(sb), i);
		if (i >= EXT4_BLOCKS_PER_GROUP(sb)) {
			/*
			 * IF we have corrupt bitmap, we won't find any
			 * free blocks even though group info says we
			 * we have free blocks
			 */
			ext4_error(sb, __func__, "%d free blocks as per "
					"group info. But bitmap says 0\n",
					free);
			break;
		}

		mb_find_extent(e4b, 0, i, ac->ac_g_ex.fe_len, &ex);
		BUG_ON(ex.fe_len <= 0);
		if (free < ex.fe_len) {
			ext4_error(sb, __func__, "%d free blocks as per "
					"group info. But got %d blocks\n",
					free, ex.fe_len);
			/*
			 * The number of free blocks differs. This mostly
			 * indicate that the bitmap is corrupt. So exit
			 * without claiming the space.
			 */
			break;
		}

		ext4_mb_measure_extent(ac, &ex, e4b);

		i += ex.fe_len;
		free -= ex.fe_len;
	}

	ext4_mb_check_limits(ac, e4b, 1);
}

/*
 * This is a special case for storages like raid5
 * we try to find stripe-aligned chunks for stripe-size requests
 * XXX should do so at least for multiples of stripe size as well
 */
static void ext4_mb_scan_aligned(struct ext4_allocation_context *ac,
				 struct ext4_buddy *e4b)
{
	struct super_block *sb = ac->ac_sb;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	void *bitmap = EXT4_MB_BITMAP(e4b);
	struct ext4_free_extent ex;
	ext4_fsblk_t first_group_block;
	ext4_fsblk_t a;
	ext4_grpblk_t i;
	int max;

	BUG_ON(sbi->s_stripe == 0);

	/* find first stripe-aligned block in group */
	first_group_block = e4b->bd_group * EXT4_BLOCKS_PER_GROUP(sb)
		+ le32_to_cpu(sbi->s_es->s_first_data_block);
	a = first_group_block + sbi->s_stripe - 1;
	do_div(a, sbi->s_stripe);
	i = (a * sbi->s_stripe) - first_group_block;

	while (i < EXT4_BLOCKS_PER_GROUP(sb)) {
		if (!mb_test_bit(i, bitmap)) {
			max = mb_find_extent(e4b, 0, i, sbi->s_stripe, &ex);
			if (max >= sbi->s_stripe) {
				ac->ac_found++;
				ac->ac_b_ex = ex;
				ext4_mb_use_best_found(ac, e4b);
				break;
			}
		}
		i += sbi->s_stripe;
	}
}

static int ext4_mb_good_group(struct ext4_allocation_context *ac,
				ext4_group_t group, int cr)
{
	unsigned free, fragments;
	unsigned i, bits;
	struct ext4_group_desc *desc;
	struct ext4_group_info *grp = ext4_get_group_info(ac->ac_sb, group);

	BUG_ON(cr < 0 || cr >= 4);
	BUG_ON(EXT4_MB_GRP_NEED_INIT(grp));

	free = grp->bb_free;
	fragments = grp->bb_fragments;
	if (free == 0)
		return 0;
	if (fragments == 0)
		return 0;

	switch (cr) {
	case 0:
		BUG_ON(ac->ac_2order == 0);
		/* If this group is uninitialized, skip it initially */
		desc = ext4_get_group_desc(ac->ac_sb, group, NULL);
		if (desc->bg_flags & cpu_to_le16(EXT4_BG_BLOCK_UNINIT))
			return 0;

		bits = ac->ac_sb->s_blocksize_bits + 1;
		for (i = ac->ac_2order; i <= bits; i++)
			if (grp->bb_counters[i] > 0)
				return 1;
		break;
	case 1:
		if ((free / fragments) >= ac->ac_g_ex.fe_len)
			return 1;
		break;
	case 2:
		if (free >= ac->ac_g_ex.fe_len)
			return 1;
		break;
	case 3:
		return 1;
	default:
		BUG();
	}

	return 0;
}

static noinline_for_stack int
ext4_mb_regular_allocator(struct ext4_allocation_context *ac)
{
	ext4_group_t group;
	ext4_group_t i;
	int cr;
	int err = 0;
	int bsbits;
	struct ext4_sb_info *sbi;
	struct super_block *sb;
	struct ext4_buddy e4b;
	loff_t size, isize;

	sb = ac->ac_sb;
	sbi = EXT4_SB(sb);
	BUG_ON(ac->ac_status == AC_STATUS_FOUND);

	/* first, try the goal */
	err = ext4_mb_find_by_goal(ac, &e4b);
	if (err || ac->ac_status == AC_STATUS_FOUND)
		goto out;

	if (unlikely(ac->ac_flags & EXT4_MB_HINT_GOAL_ONLY))
		goto out;

	/*
	 * ac->ac2_order is set only if the fe_len is a power of 2
	 * if ac2_order is set we also set criteria to 0 so that we
	 * try exact allocation using buddy.
	 */
	i = fls(ac->ac_g_ex.fe_len);
	ac->ac_2order = 0;
	/*
	 * We search using buddy data only if the order of the request
	 * is greater than equal to the sbi_s_mb_order2_reqs
	 * You can tune it via /proc/fs/ext4/<partition>/order2_req
	 */
	if (i >= sbi->s_mb_order2_reqs) {
		/*
		 * This should tell if fe_len is exactly power of 2
		 */
		if ((ac->ac_g_ex.fe_len & (~(1 << (i - 1)))) == 0)
			ac->ac_2order = i - 1;
	}

	bsbits = ac->ac_sb->s_blocksize_bits;
	/* if stream allocation is enabled, use global goal */
	size = ac->ac_o_ex.fe_logical + ac->ac_o_ex.fe_len;
	isize = i_size_read(ac->ac_inode) >> bsbits;
	if (size < isize)
		size = isize;

	if (size < sbi->s_mb_stream_request &&
			(ac->ac_flags & EXT4_MB_HINT_DATA)) {
		/* TBD: may be hot point */
		spin_lock(&sbi->s_md_lock);
		ac->ac_g_ex.fe_group = sbi->s_mb_last_group;
		ac->ac_g_ex.fe_start = sbi->s_mb_last_start;
		spin_unlock(&sbi->s_md_lock);
	}
	/* Let's just scan groups to find more-less suitable blocks */
	cr = ac->ac_2order ? 0 : 1;
	/*
	 * cr == 0 try to get exact allocation,
	 * cr == 3  try to get anything
	 */
repeat:
	for (; cr < 4 && ac->ac_status == AC_STATUS_CONTINUE; cr++) {
		ac->ac_criteria = cr;
		/*
		 * searching for the right group start
		 * from the goal value specified
		 */
		group = ac->ac_g_ex.fe_group;

		for (i = 0; i < EXT4_SB(sb)->s_groups_count; group++, i++) {
			struct ext4_group_info *grp;
			struct ext4_group_desc *desc;

			if (group == EXT4_SB(sb)->s_groups_count)
				group = 0;

			/* quick check to skip empty groups */
			grp = ext4_get_group_info(ac->ac_sb, group);
			if (grp->bb_free == 0)
				continue;

			/*
			 * if the group is already init we check whether it is
			 * a good group and if not we don't load the buddy
			 */
			if (EXT4_MB_GRP_NEED_INIT(grp)) {
				/*
				 * we need full data about the group
				 * to make a good selection
				 */
				err = ext4_mb_load_buddy(sb, group, &e4b);
				if (err)
					goto out;
				ext4_mb_release_desc(&e4b);
			}

			/*
			 * If the particular group doesn't satisfy our
			 * criteria we continue with the next group
			 */
			if (!ext4_mb_good_group(ac, group, cr))
				continue;

			err = ext4_mb_load_buddy(sb, group, &e4b);
			if (err)
				goto out;

			ext4_lock_group(sb, group);
			if (!ext4_mb_good_group(ac, group, cr)) {
				/* someone did allocation from this group */
				ext4_unlock_group(sb, group);
				ext4_mb_release_desc(&e4b);
				continue;
			}

			ac->ac_groups_scanned++;
			desc = ext4_get_group_desc(sb, group, NULL);
			if (cr == 0 || (desc->bg_flags &
					cpu_to_le16(EXT4_BG_BLOCK_UNINIT) &&
					ac->ac_2order != 0))
				ext4_mb_simple_scan_group(ac, &e4b);
			else if (cr == 1 &&
					ac->ac_g_ex.fe_len == sbi->s_stripe)
				ext4_mb_scan_aligned(ac, &e4b);
			else
				ext4_mb_complex_scan_group(ac, &e4b);

			ext4_unlock_group(sb, group);
			ext4_mb_release_desc(&e4b);

			if (ac->ac_status != AC_STATUS_CONTINUE)
				break;
		}
	}

	if (ac->ac_b_ex.fe_len > 0 && ac->ac_status != AC_STATUS_FOUND &&
	    !(ac->ac_flags & EXT4_MB_HINT_FIRST)) {
		/*
		 * We've been searching too long. Let's try to allocate
		 * the best chunk we've found so far
		 */

		ext4_mb_try_best_found(ac, &e4b);
		if (ac->ac_status != AC_STATUS_FOUND) {
			/*
			 * Someone more lucky has already allocated it.
			 * The only thing we can do is just take first
			 * found block(s)
			printk(KERN_DEBUG "EXT4-fs: someone won our chunk\n");
			 */
			ac->ac_b_ex.fe_group = 0;
			ac->ac_b_ex.fe_start = 0;
			ac->ac_b_ex.fe_len = 0;
			ac->ac_status = AC_STATUS_CONTINUE;
			ac->ac_flags |= EXT4_MB_HINT_FIRST;
			cr = 3;
			atomic_inc(&sbi->s_mb_lost_chunks);
			goto repeat;
		}
	}
out:
	return err;
}

#ifdef EXT4_MB_HISTORY
struct ext4_mb_proc_session {
	struct ext4_mb_history *history;
	struct super_block *sb;
	int start;
	int max;
};

static void *ext4_mb_history_skip_empty(struct ext4_mb_proc_session *s,
					struct ext4_mb_history *hs,
					int first)
{
	if (hs == s->history + s->max)
		hs = s->history;
	if (!first && hs == s->history + s->start)
		return NULL;
	while (hs->orig.fe_len == 0) {
		hs++;
		if (hs == s->history + s->max)
			hs = s->history;
		if (hs == s->history + s->start)
			return NULL;
	}
	return hs;
}

static void *ext4_mb_seq_history_start(struct seq_file *seq, loff_t *pos)
{
	struct ext4_mb_proc_session *s = seq->private;
	struct ext4_mb_history *hs;
	int l = *pos;

	if (l == 0)
		return SEQ_START_TOKEN;
	hs = ext4_mb_history_skip_empty(s, s->history + s->start, 1);
	if (!hs)
		return NULL;
	while (--l && (hs = ext4_mb_history_skip_empty(s, ++hs, 0)) != NULL);
	return hs;
}

static void *ext4_mb_seq_history_next(struct seq_file *seq, void *v,
				      loff_t *pos)
{
	struct ext4_mb_proc_session *s = seq->private;
	struct ext4_mb_history *hs = v;

	++*pos;
	if (v == SEQ_START_TOKEN)
		return ext4_mb_history_skip_empty(s, s->history + s->start, 1);
	else
		return ext4_mb_history_skip_empty(s, ++hs, 0);
}

static int ext4_mb_seq_history_show(struct seq_file *seq, void *v)
{
	char buf[25], buf2[25], buf3[25], *fmt;
	struct ext4_mb_history *hs = v;

	if (v == SEQ_START_TOKEN) {
		seq_printf(seq, "%-5s %-8s %-23s %-23s %-23s %-5s "
				"%-5s %-2s %-5s %-5s %-5s %-6s\n",
			  "pid", "inode", "original", "goal", "result", "found",
			   "grps", "cr", "flags", "merge", "tail", "broken");
		return 0;
	}

	if (hs->op == EXT4_MB_HISTORY_ALLOC) {
		fmt = "%-5u %-8u %-23s %-23s %-23s %-5u %-5u %-2u "
			"%-5u %-5s %-5u %-6u\n";
		sprintf(buf2, "%lu/%d/%u@%u", hs->result.fe_group,
			hs->result.fe_start, hs->result.fe_len,
			hs->result.fe_logical);
		sprintf(buf, "%lu/%d/%u@%u", hs->orig.fe_group,
			hs->orig.fe_start, hs->orig.fe_len,
			hs->orig.fe_logical);
		sprintf(buf3, "%lu/%d/%u@%u", hs->goal.fe_group,
			hs->goal.fe_start, hs->goal.fe_len,
			hs->goal.fe_logical);
		seq_printf(seq, fmt, hs->pid, hs->ino, buf, buf3, buf2,
				hs->found, hs->groups, hs->cr, hs->flags,
				hs->merged ? "M" : "", hs->tail,
				hs->buddy ? 1 << hs->buddy : 0);
	} else if (hs->op == EXT4_MB_HISTORY_PREALLOC) {
		fmt = "%-5u %-8u %-23s %-23s %-23s\n";
		sprintf(buf2, "%lu/%d/%u@%u", hs->result.fe_group,
			hs->result.fe_start, hs->result.fe_len,
			hs->result.fe_logical);
		sprintf(buf, "%lu/%d/%u@%u", hs->orig.fe_group,
			hs->orig.fe_start, hs->orig.fe_len,
			hs->orig.fe_logical);
		seq_printf(seq, fmt, hs->pid, hs->ino, buf, "", buf2);
	} else if (hs->op == EXT4_MB_HISTORY_DISCARD) {
		sprintf(buf2, "%lu/%d/%u", hs->result.fe_group,
			hs->result.fe_start, hs->result.fe_len);
		seq_printf(seq, "%-5u %-8u %-23s discard\n",
				hs->pid, hs->ino, buf2);
	} else if (hs->op == EXT4_MB_HISTORY_FREE) {
		sprintf(buf2, "%lu/%d/%u", hs->result.fe_group,
			hs->result.fe_start, hs->result.fe_len);
		seq_printf(seq, "%-5u %-8u %-23s free\n",
				hs->pid, hs->ino, buf2);
	}
	return 0;
}

static void ext4_mb_seq_history_stop(struct seq_file *seq, void *v)
{
}

static struct seq_operations ext4_mb_seq_history_ops = {
	.start  = ext4_mb_seq_history_start,
	.next   = ext4_mb_seq_history_next,
	.stop   = ext4_mb_seq_history_stop,
	.show   = ext4_mb_seq_history_show,
};

static int ext4_mb_seq_history_open(struct inode *inode, struct file *file)
{
	struct super_block *sb = PDE(inode)->data;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_mb_proc_session *s;
	int rc;
	int size;

	if (unlikely(sbi->s_mb_history == NULL))
		return -ENOMEM;
	s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (s == NULL)
		return -ENOMEM;
	s->sb = sb;
	size = sizeof(struct ext4_mb_history) * sbi->s_mb_history_max;
	s->history = kmalloc(size, GFP_KERNEL);
	if (s->history == NULL) {
		kfree(s);
		return -ENOMEM;
	}

	spin_lock(&sbi->s_mb_history_lock);
	memcpy(s->history, sbi->s_mb_history, size);
	s->max = sbi->s_mb_history_max;
	s->start = sbi->s_mb_history_cur % s->max;
	spin_unlock(&sbi->s_mb_history_lock);

	rc = seq_open(file, &ext4_mb_seq_history_ops);
	if (rc == 0) {
		struct seq_file *m = (struct seq_file *)file->private_data;
		m->private = s;
	} else {
		kfree(s->history);
		kfree(s);
	}
	return rc;

}

static int ext4_mb_seq_history_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = (struct seq_file *)file->private_data;
	struct ext4_mb_proc_session *s = seq->private;
	kfree(s->history);
	kfree(s);
	return seq_release(inode, file);
}

static ssize_t ext4_mb_seq_history_write(struct file *file,
				const char __user *buffer,
				size_t count, loff_t *ppos)
{
	struct seq_file *seq = (struct seq_file *)file->private_data;
	struct ext4_mb_proc_session *s = seq->private;
	struct super_block *sb = s->sb;
	char str[32];
	int value;

	if (count >= sizeof(str)) {
		printk(KERN_ERR "EXT4-fs: %s string too long, max %u bytes\n",
				"mb_history", (int)sizeof(str));
		return -EOVERFLOW;
	}

	if (copy_from_user(str, buffer, count))
		return -EFAULT;

	value = simple_strtol(str, NULL, 0);
	if (value < 0)
		return -ERANGE;
	EXT4_SB(sb)->s_mb_history_filter = value;

	return count;
}

static struct file_operations ext4_mb_seq_history_fops = {
	.owner		= THIS_MODULE,
	.open		= ext4_mb_seq_history_open,
	.read		= seq_read,
	.write		= ext4_mb_seq_history_write,
	.llseek		= seq_lseek,
	.release	= ext4_mb_seq_history_release,
};

static void *ext4_mb_seq_groups_start(struct seq_file *seq, loff_t *pos)
{
	struct super_block *sb = seq->private;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	ext4_group_t group;

	if (*pos < 0 || *pos >= sbi->s_groups_count)
		return NULL;

	group = *pos + 1;
	return (void *) group;
}

static void *ext4_mb_seq_groups_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct super_block *sb = seq->private;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	ext4_group_t group;

	++*pos;
	if (*pos < 0 || *pos >= sbi->s_groups_count)
		return NULL;
	group = *pos + 1;
	return (void *) group;;
}

static int ext4_mb_seq_groups_show(struct seq_file *seq, void *v)
{
	struct super_block *sb = seq->private;
	long group = (long) v;
	int i;
	int err;
	struct ext4_buddy e4b;
	struct sg {
		struct ext4_group_info info;
		unsigned short counters[16];
	} sg;

	group--;
	if (group == 0)
		seq_printf(seq, "#%-5s: %-5s %-5s %-5s "
				"[ %-5s %-5s %-5s %-5s %-5s %-5s %-5s "
				  "%-5s %-5s %-5s %-5s %-5s %-5s %-5s ]\n",
			   "group", "free", "frags", "first",
			   "2^0", "2^1", "2^2", "2^3", "2^4", "2^5", "2^6",
			   "2^7", "2^8", "2^9", "2^10", "2^11", "2^12", "2^13");

	i = (sb->s_blocksize_bits + 2) * sizeof(sg.info.bb_counters[0]) +
		sizeof(struct ext4_group_info);
	err = ext4_mb_load_buddy(sb, group, &e4b);
	if (err) {
		seq_printf(seq, "#%-5lu: I/O error\n", group);
		return 0;
	}
	ext4_lock_group(sb, group);
	memcpy(&sg, ext4_get_group_info(sb, group), i);
	ext4_unlock_group(sb, group);
	ext4_mb_release_desc(&e4b);

	seq_printf(seq, "#%-5lu: %-5u %-5u %-5u [", group, sg.info.bb_free,
			sg.info.bb_fragments, sg.info.bb_first_free);
	for (i = 0; i <= 13; i++)
		seq_printf(seq, " %-5u", i <= sb->s_blocksize_bits + 1 ?
				sg.info.bb_counters[i] : 0);
	seq_printf(seq, " ]\n");

	return 0;
}

static void ext4_mb_seq_groups_stop(struct seq_file *seq, void *v)
{
}

static struct seq_operations ext4_mb_seq_groups_ops = {
	.start  = ext4_mb_seq_groups_start,
	.next   = ext4_mb_seq_groups_next,
	.stop   = ext4_mb_seq_groups_stop,
	.show   = ext4_mb_seq_groups_show,
};

static int ext4_mb_seq_groups_open(struct inode *inode, struct file *file)
{
	struct super_block *sb = PDE(inode)->data;
	int rc;

	rc = seq_open(file, &ext4_mb_seq_groups_ops);
	if (rc == 0) {
		struct seq_file *m = (struct seq_file *)file->private_data;
		m->private = sb;
	}
	return rc;

}

static struct file_operations ext4_mb_seq_groups_fops = {
	.owner		= THIS_MODULE,
	.open		= ext4_mb_seq_groups_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static void ext4_mb_history_release(struct super_block *sb)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	remove_proc_entry("mb_groups", sbi->s_mb_proc);
	remove_proc_entry("mb_history", sbi->s_mb_proc);

	kfree(sbi->s_mb_history);
}

static void ext4_mb_history_init(struct super_block *sb)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	int i;

	if (sbi->s_mb_proc != NULL) {
		proc_create_data("mb_history", S_IRUGO, sbi->s_mb_proc,
				 &ext4_mb_seq_history_fops, sb);
		proc_create_data("mb_groups", S_IRUGO, sbi->s_mb_proc,
				 &ext4_mb_seq_groups_fops, sb);
	}

	sbi->s_mb_history_max = 1000;
	sbi->s_mb_history_cur = 0;
	spin_lock_init(&sbi->s_mb_history_lock);
	i = sbi->s_mb_history_max * sizeof(struct ext4_mb_history);
	sbi->s_mb_history = kzalloc(i, GFP_KERNEL);
	/* if we can't allocate history, then we simple won't use it */
}

static noinline_for_stack void
ext4_mb_store_history(struct ext4_allocation_context *ac)
{
	struct ext4_sb_info *sbi = EXT4_SB(ac->ac_sb);
	struct ext4_mb_history h;

	if (unlikely(sbi->s_mb_history == NULL))
		return;

	if (!(ac->ac_op & sbi->s_mb_history_filter))
		return;

	h.op = ac->ac_op;
	h.pid = current->pid;
	h.ino = ac->ac_inode ? ac->ac_inode->i_ino : 0;
	h.orig = ac->ac_o_ex;
	h.result = ac->ac_b_ex;
	h.flags = ac->ac_flags;
	h.found = ac->ac_found;
	h.groups = ac->ac_groups_scanned;
	h.cr = ac->ac_criteria;
	h.tail = ac->ac_tail;
	h.buddy = ac->ac_buddy;
	h.merged = 0;
	if (ac->ac_op == EXT4_MB_HISTORY_ALLOC) {
		if (ac->ac_g_ex.fe_start == ac->ac_b_ex.fe_start &&
				ac->ac_g_ex.fe_group == ac->ac_b_ex.fe_group)
			h.merged = 1;
		h.goal = ac->ac_g_ex;
		h.result = ac->ac_f_ex;
	}

	spin_lock(&sbi->s_mb_history_lock);
	memcpy(sbi->s_mb_history + sbi->s_mb_history_cur, &h, sizeof(h));
	if (++sbi->s_mb_history_cur >= sbi->s_mb_history_max)
		sbi->s_mb_history_cur = 0;
	spin_unlock(&sbi->s_mb_history_lock);
}

#else
#define ext4_mb_history_release(sb)
#define ext4_mb_history_init(sb)
#endif


/* Create and initialize ext4_group_info data for the given group. */
int ext4_mb_add_groupinfo(struct super_block *sb, ext4_group_t group,
			  struct ext4_group_desc *desc)
{
	int i, len;
	int metalen = 0;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_group_info **meta_group_info;

	/*
	 * First check if this group is the first of a reserved block.
	 * If it's true, we have to allocate a new table of pointers
	 * to ext4_group_info structures
	 */
	if (group % EXT4_DESC_PER_BLOCK(sb) == 0) {
		metalen = sizeof(*meta_group_info) <<
			EXT4_DESC_PER_BLOCK_BITS(sb);
		meta_group_info = kmalloc(metalen, GFP_KERNEL);
		if (meta_group_info == NULL) {
			printk(KERN_ERR "EXT4-fs: can't allocate mem for a "
			       "buddy group\n");
			goto exit_meta_group_info;
		}
		sbi->s_group_info[group >> EXT4_DESC_PER_BLOCK_BITS(sb)] =
			meta_group_info;
	}

	/*
	 * calculate needed size. if change bb_counters size,
	 * don't forget about ext4_mb_generate_buddy()
	 */
	len = offsetof(typeof(**meta_group_info),
		       bb_counters[sb->s_blocksize_bits + 2]);

	meta_group_info =
		sbi->s_group_info[group >> EXT4_DESC_PER_BLOCK_BITS(sb)];
	i = group & (EXT4_DESC_PER_BLOCK(sb) - 1);

	meta_group_info[i] = kzalloc(len, GFP_KERNEL);
	if (meta_group_info[i] == NULL) {
		printk(KERN_ERR "EXT4-fs: can't allocate buddy mem\n");
		goto exit_group_info;
	}
	set_bit(EXT4_GROUP_INFO_NEED_INIT_BIT,
		&(meta_group_info[i]->bb_state));

	/*
	 * initialize bb_free to be able to skip
	 * empty groups without initialization
	 */
	if (desc->bg_flags & cpu_to_le16(EXT4_BG_BLOCK_UNINIT)) {
		meta_group_info[i]->bb_free =
			ext4_free_blocks_after_init(sb, group, desc);
	} else {
		meta_group_info[i]->bb_free =
			le16_to_cpu(desc->bg_free_blocks_count);
	}

	INIT_LIST_HEAD(&meta_group_info[i]->bb_prealloc_list);

#ifdef DOUBLE_CHECK
	{
		struct buffer_head *bh;
		meta_group_info[i]->bb_bitmap =
			kmalloc(sb->s_blocksize, GFP_KERNEL);
		BUG_ON(meta_group_info[i]->bb_bitmap == NULL);
		bh = ext4_read_block_bitmap(sb, group);
		BUG_ON(bh == NULL);
		memcpy(meta_group_info[i]->bb_bitmap, bh->b_data,
			sb->s_blocksize);
		put_bh(bh);
	}
#endif

	return 0;

exit_group_info:
	/* If a meta_group_info table has been allocated, release it now */
	if (group % EXT4_DESC_PER_BLOCK(sb) == 0)
		kfree(sbi->s_group_info[group >> EXT4_DESC_PER_BLOCK_BITS(sb)]);
exit_meta_group_info:
	return -ENOMEM;
} /* ext4_mb_add_groupinfo */

/*
 * Add a group to the existing groups.
 * This function is used for online resize
 */
int ext4_mb_add_more_groupinfo(struct super_block *sb, ext4_group_t group,
			       struct ext4_group_desc *desc)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct inode *inode = sbi->s_buddy_cache;
	int blocks_per_page;
	int block;
	int pnum;
	struct page *page;
	int err;

	/* Add group based on group descriptor*/
	err = ext4_mb_add_groupinfo(sb, group, desc);
	if (err)
		return err;

	/*
	 * Cache pages containing dynamic mb_alloc datas (buddy and bitmap
	 * datas) are set not up to date so that they will be re-initilaized
	 * during the next call to ext4_mb_load_buddy
	 */

	/* Set buddy page as not up to date */
	blocks_per_page = PAGE_CACHE_SIZE / sb->s_blocksize;
	block = group * 2;
	pnum = block / blocks_per_page;
	page = find_get_page(inode->i_mapping, pnum);
	if (page != NULL) {
		ClearPageUptodate(page);
		page_cache_release(page);
	}

	/* Set bitmap page as not up to date */
	block++;
	pnum = block / blocks_per_page;
	page = find_get_page(inode->i_mapping, pnum);
	if (page != NULL) {
		ClearPageUptodate(page);
		page_cache_release(page);
	}

	return 0;
}

/*
 * Update an existing group.
 * This function is used for online resize
 */
void ext4_mb_update_group_info(struct ext4_group_info *grp, ext4_grpblk_t add)
{
	grp->bb_free += add;
}

static int ext4_mb_init_backend(struct super_block *sb)
{
	ext4_group_t i;
	int metalen;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_super_block *es = sbi->s_es;
	int num_meta_group_infos;
	int num_meta_group_infos_max;
	int array_size;
	struct ext4_group_info **meta_group_info;
	struct ext4_group_desc *desc;

	/* This is the number of blocks used by GDT */
	num_meta_group_infos = (sbi->s_groups_count + EXT4_DESC_PER_BLOCK(sb) -
				1) >> EXT4_DESC_PER_BLOCK_BITS(sb);

	/*
	 * This is the total number of blocks used by GDT including
	 * the number of reserved blocks for GDT.
	 * The s_group_info array is allocated with this value
	 * to allow a clean online resize without a complex
	 * manipulation of pointer.
	 * The drawback is the unused memory when no resize
	 * occurs but it's very low in terms of pages
	 * (see comments below)
	 * Need to handle this properly when META_BG resizing is allowed
	 */
	num_meta_group_infos_max = num_meta_group_infos +
				le16_to_cpu(es->s_reserved_gdt_blocks);

	/*
	 * array_size is the size of s_group_info array. We round it
	 * to the next power of two because this approximation is done
	 * internally by kmalloc so we can have some more memory
	 * for free here (e.g. may be used for META_BG resize).
	 */
	array_size = 1;
	while (array_size < sizeof(*sbi->s_group_info) *
	       num_meta_group_infos_max)
		array_size = array_size << 1;
	/* An 8TB filesystem with 64-bit pointers requires a 4096 byte
	 * kmalloc. A 128kb malloc should suffice for a 256TB filesystem.
	 * So a two level scheme suffices for now. */
	sbi->s_group_info = kmalloc(array_size, GFP_KERNEL);
	if (sbi->s_group_info == NULL) {
		printk(KERN_ERR "EXT4-fs: can't allocate buddy meta group\n");
		return -ENOMEM;
	}
	sbi->s_buddy_cache = new_inode(sb);
	if (sbi->s_buddy_cache == NULL) {
		printk(KERN_ERR "EXT4-fs: can't get new inode\n");
		goto err_freesgi;
	}
	EXT4_I(sbi->s_buddy_cache)->i_disksize = 0;

	metalen = sizeof(*meta_group_info) << EXT4_DESC_PER_BLOCK_BITS(sb);
	for (i = 0; i < num_meta_group_infos; i++) {
		if ((i + 1) == num_meta_group_infos)
			metalen = sizeof(*meta_group_info) *
				(sbi->s_groups_count -
					(i << EXT4_DESC_PER_BLOCK_BITS(sb)));
		meta_group_info = kmalloc(metalen, GFP_KERNEL);
		if (meta_group_info == NULL) {
			printk(KERN_ERR "EXT4-fs: can't allocate mem for a "
			       "buddy group\n");
			goto err_freemeta;
		}
		sbi->s_group_info[i] = meta_group_info;
	}

	for (i = 0; i < sbi->s_groups_count; i++) {
		desc = ext4_get_group_desc(sb, i, NULL);
		if (desc == NULL) {
			printk(KERN_ERR
				"EXT4-fs: can't read descriptor %lu\n", i);
			goto err_freebuddy;
		}
		if (ext4_mb_add_groupinfo(sb, i, desc) != 0)
			goto err_freebuddy;
	}

	return 0;

err_freebuddy:
	while (i-- > 0)
		kfree(ext4_get_group_info(sb, i));
	i = num_meta_group_infos;
err_freemeta:
	while (i-- > 0)
		kfree(sbi->s_group_info[i]);
	iput(sbi->s_buddy_cache);
err_freesgi:
	kfree(sbi->s_group_info);
	return -ENOMEM;
}

int ext4_mb_init(struct super_block *sb, int needs_recovery)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	unsigned i, j;
	unsigned offset;
	unsigned max;
	int ret;

	if (!test_opt(sb, MBALLOC))
		return 0;

	i = (sb->s_blocksize_bits + 2) * sizeof(unsigned short);

	sbi->s_mb_offsets = kmalloc(i, GFP_KERNEL);
	if (sbi->s_mb_offsets == NULL) {
		clear_opt(sbi->s_mount_opt, MBALLOC);
		return -ENOMEM;
	}
	sbi->s_mb_maxs = kmalloc(i, GFP_KERNEL);
	if (sbi->s_mb_maxs == NULL) {
		clear_opt(sbi->s_mount_opt, MBALLOC);
		kfree(sbi->s_mb_maxs);
		return -ENOMEM;
	}

	/* order 0 is regular bitmap */
	sbi->s_mb_maxs[0] = sb->s_blocksize << 3;
	sbi->s_mb_offsets[0] = 0;

	i = 1;
	offset = 0;
	max = sb->s_blocksize << 2;
	do {
		sbi->s_mb_offsets[i] = offset;
		sbi->s_mb_maxs[i] = max;
		offset += 1 << (sb->s_blocksize_bits - i);
		max = max >> 1;
		i++;
	} while (i <= sb->s_blocksize_bits + 1);

	/* init file for buddy data */
	ret = ext4_mb_init_backend(sb);
	if (ret != 0) {
		clear_opt(sbi->s_mount_opt, MBALLOC);
		kfree(sbi->s_mb_offsets);
		kfree(sbi->s_mb_maxs);
		return ret;
	}

	spin_lock_init(&sbi->s_md_lock);
	INIT_LIST_HEAD(&sbi->s_active_transaction);
	INIT_LIST_HEAD(&sbi->s_closed_transaction);
	INIT_LIST_HEAD(&sbi->s_committed_transaction);
	spin_lock_init(&sbi->s_bal_lock);

	sbi->s_mb_max_to_scan = MB_DEFAULT_MAX_TO_SCAN;
	sbi->s_mb_min_to_scan = MB_DEFAULT_MIN_TO_SCAN;
	sbi->s_mb_stats = MB_DEFAULT_STATS;
	sbi->s_mb_stream_request = MB_DEFAULT_STREAM_THRESHOLD;
	sbi->s_mb_order2_reqs = MB_DEFAULT_ORDER2_REQS;
	sbi->s_mb_history_filter = EXT4_MB_HISTORY_DEFAULT;
	sbi->s_mb_group_prealloc = MB_DEFAULT_GROUP_PREALLOC;

	i = sizeof(struct ext4_locality_group) * nr_cpu_ids;
	sbi->s_locality_groups = kmalloc(i, GFP_KERNEL);
	if (sbi->s_locality_groups == NULL) {
		clear_opt(sbi->s_mount_opt, MBALLOC);
		kfree(sbi->s_mb_offsets);
		kfree(sbi->s_mb_maxs);
		return -ENOMEM;
	}
	for (i = 0; i < nr_cpu_ids; i++) {
		struct ext4_locality_group *lg;
		lg = &sbi->s_locality_groups[i];
		mutex_init(&lg->lg_mutex);
		for (j = 0; j < PREALLOC_TB_SIZE; j++)
			INIT_LIST_HEAD(&lg->lg_prealloc_list[j]);
		spin_lock_init(&lg->lg_prealloc_lock);
	}

	ext4_mb_init_per_dev_proc(sb);
	ext4_mb_history_init(sb);

	printk(KERN_INFO "EXT4-fs: mballoc enabled\n");
	return 0;
}

/* need to called with ext4 group lock (ext4_lock_group) */
static void ext4_mb_cleanup_pa(struct ext4_group_info *grp)
{
	struct ext4_prealloc_space *pa;
	struct list_head *cur, *tmp;
	int count = 0;

	list_for_each_safe(cur, tmp, &grp->bb_prealloc_list) {
		pa = list_entry(cur, struct ext4_prealloc_space, pa_group_list);
		list_del(&pa->pa_group_list);
		count++;
		kfree(pa);
	}
	if (count)
		mb_debug("mballoc: %u PAs left\n", count);

}

int ext4_mb_release(struct super_block *sb)
{
	ext4_group_t i;
	int num_meta_group_infos;
	struct ext4_group_info *grinfo;
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	if (!test_opt(sb, MBALLOC))
		return 0;

	/* release freed, non-committed blocks */
	spin_lock(&sbi->s_md_lock);
	list_splice_init(&sbi->s_closed_transaction,
			&sbi->s_committed_transaction);
	list_splice_init(&sbi->s_active_transaction,
			&sbi->s_committed_transaction);
	spin_unlock(&sbi->s_md_lock);
	ext4_mb_free_committed_blocks(sb);

	if (sbi->s_group_info) {
		for (i = 0; i < sbi->s_groups_count; i++) {
			grinfo = ext4_get_group_info(sb, i);
#ifdef DOUBLE_CHECK
			kfree(grinfo->bb_bitmap);
#endif
			ext4_lock_group(sb, i);
			ext4_mb_cleanup_pa(grinfo);
			ext4_unlock_group(sb, i);
			kfree(grinfo);
		}
		num_meta_group_infos = (sbi->s_groups_count +
				EXT4_DESC_PER_BLOCK(sb) - 1) >>
			EXT4_DESC_PER_BLOCK_BITS(sb);
		for (i = 0; i < num_meta_group_infos; i++)
			kfree(sbi->s_group_info[i]);
		kfree(sbi->s_group_info);
	}
	kfree(sbi->s_mb_offsets);
	kfree(sbi->s_mb_maxs);
	if (sbi->s_buddy_cache)
		iput(sbi->s_buddy_cache);
	if (sbi->s_mb_stats) {
		printk(KERN_INFO
		       "EXT4-fs: mballoc: %u blocks %u reqs (%u success)\n",
				atomic_read(&sbi->s_bal_allocated),
				atomic_read(&sbi->s_bal_reqs),
				atomic_read(&sbi->s_bal_success));
		printk(KERN_INFO
		      "EXT4-fs: mballoc: %u extents scanned, %u goal hits, "
				"%u 2^N hits, %u breaks, %u lost\n",
				atomic_read(&sbi->s_bal_ex_scanned),
				atomic_read(&sbi->s_bal_goals),
				atomic_read(&sbi->s_bal_2orders),
				atomic_read(&sbi->s_bal_breaks),
				atomic_read(&sbi->s_mb_lost_chunks));
		printk(KERN_INFO
		       "EXT4-fs: mballoc: %lu generated and it took %Lu\n",
				sbi->s_mb_buddies_generated++,
				sbi->s_mb_generation_time);
		printk(KERN_INFO
		       "EXT4-fs: mballoc: %u preallocated, %u discarded\n",
				atomic_read(&sbi->s_mb_preallocated),
				atomic_read(&sbi->s_mb_discarded));
	}

	kfree(sbi->s_locality_groups);

	ext4_mb_history_release(sb);
	ext4_mb_destroy_per_dev_proc(sb);

	return 0;
}

static noinline_for_stack void
ext4_mb_free_committed_blocks(struct super_block *sb)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	int err;
	int i;
	int count = 0;
	int count2 = 0;
	struct ext4_free_metadata *md;
	struct ext4_buddy e4b;

	if (list_empty(&sbi->s_committed_transaction))
		return;

	/* there is committed blocks to be freed yet */
	do {
		/* get next array of blocks */
		md = NULL;
		spin_lock(&sbi->s_md_lock);
		if (!list_empty(&sbi->s_committed_transaction)) {
			md = list_entry(sbi->s_committed_transaction.next,
					struct ext4_free_metadata, list);
			list_del(&md->list);
		}
		spin_unlock(&sbi->s_md_lock);

		if (md == NULL)
			break;

		mb_debug("gonna free %u blocks in group %lu (0x%p):",
				md->num, md->group, md);

		err = ext4_mb_load_buddy(sb, md->group, &e4b);
		/* we expect to find existing buddy because it's pinned */
		BUG_ON(err != 0);

		/* there are blocks to put in buddy to make them really free */
		count += md->num;
		count2++;
		ext4_lock_group(sb, md->group);
		for (i = 0; i < md->num; i++) {
			mb_debug(" %u", md->blocks[i]);
			mb_free_blocks(NULL, &e4b, md->blocks[i], 1);
		}
		mb_debug("\n");
		ext4_unlock_group(sb, md->group);

		/* balance refcounts from ext4_mb_free_metadata() */
		page_cache_release(e4b.bd_buddy_page);
		page_cache_release(e4b.bd_bitmap_page);

		kfree(md);
		ext4_mb_release_desc(&e4b);

	} while (md);

	mb_debug("freed %u blocks in %u structures\n", count, count2);
}

#define EXT4_MB_STATS_NAME		"stats"
#define EXT4_MB_MAX_TO_SCAN_NAME	"max_to_scan"
#define EXT4_MB_MIN_TO_SCAN_NAME	"min_to_scan"
#define EXT4_MB_ORDER2_REQ		"order2_req"
#define EXT4_MB_STREAM_REQ		"stream_req"
#define EXT4_MB_GROUP_PREALLOC		"group_prealloc"



#define MB_PROC_FOPS(name)					\
static int ext4_mb_##name##_proc_show(struct seq_file *m, void *v)	\
{								\
	struct ext4_sb_info *sbi = m->private;			\
								\
	seq_printf(m, "%ld\n", sbi->s_mb_##name);		\
	return 0;						\
}								\
								\
static int ext4_mb_##name##_proc_open(struct inode *inode, struct file *file)\
{								\
	return single_open(file, ext4_mb_##name##_proc_show, PDE(inode)->data);\
}								\
								\
static ssize_t ext4_mb_##name##_proc_write(struct file *file,	\
		const char __user *buf, size_t cnt, loff_t *ppos)	\
{								\
	struct ext4_sb_info *sbi = PDE(file->f_path.dentry->d_inode)->data;\
	char str[32];						\
	long value;						\
	if (cnt >= sizeof(str))					\
		return -EINVAL;					\
	if (copy_from_user(str, buf, cnt))			\
		return -EFAULT;					\
	value = simple_strtol(str, NULL, 0);			\
	if (value <= 0)						\
		return -ERANGE;					\
	sbi->s_mb_##name = value;				\
	return cnt;						\
}								\
								\
static const struct file_operations ext4_mb_##name##_proc_fops = {	\
	.owner		= THIS_MODULE,				\
	.open		= ext4_mb_##name##_proc_open,		\
	.read		= seq_read,				\
	.llseek		= seq_lseek,				\
	.release	= single_release,			\
	.write		= ext4_mb_##name##_proc_write,		\
};

MB_PROC_FOPS(stats);
MB_PROC_FOPS(max_to_scan);
MB_PROC_FOPS(min_to_scan);
MB_PROC_FOPS(order2_reqs);
MB_PROC_FOPS(stream_request);
MB_PROC_FOPS(group_prealloc);

#define	MB_PROC_HANDLER(name, var)					\
do {									\
	proc = proc_create_data(name, mode, sbi->s_mb_proc,		\
				&ext4_mb_##var##_proc_fops, sbi);	\
	if (proc == NULL) {						\
		printk(KERN_ERR "EXT4-fs: can't to create %s\n", name);	\
		goto err_out;						\
	}								\
} while (0)

static int ext4_mb_init_per_dev_proc(struct super_block *sb)
{
	mode_t mode = S_IFREG | S_IRUGO | S_IWUSR;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct proc_dir_entry *proc;
	char devname[64];

	if (proc_root_ext4 == NULL) {
		sbi->s_mb_proc = NULL;
		return -EINVAL;
	}
	bdevname(sb->s_bdev, devname);
	sbi->s_mb_proc = proc_mkdir(devname, proc_root_ext4);

	MB_PROC_HANDLER(EXT4_MB_STATS_NAME, stats);
	MB_PROC_HANDLER(EXT4_MB_MAX_TO_SCAN_NAME, max_to_scan);
	MB_PROC_HANDLER(EXT4_MB_MIN_TO_SCAN_NAME, min_to_scan);
	MB_PROC_HANDLER(EXT4_MB_ORDER2_REQ, order2_reqs);
	MB_PROC_HANDLER(EXT4_MB_STREAM_REQ, stream_request);
	MB_PROC_HANDLER(EXT4_MB_GROUP_PREALLOC, group_prealloc);

	return 0;

err_out:
	printk(KERN_ERR "EXT4-fs: Unable to create %s\n", devname);
	remove_proc_entry(EXT4_MB_GROUP_PREALLOC, sbi->s_mb_proc);
	remove_proc_entry(EXT4_MB_STREAM_REQ, sbi->s_mb_proc);
	remove_proc_entry(EXT4_MB_ORDER2_REQ, sbi->s_mb_proc);
	remove_proc_entry(EXT4_MB_MIN_TO_SCAN_NAME, sbi->s_mb_proc);
	remove_proc_entry(EXT4_MB_MAX_TO_SCAN_NAME, sbi->s_mb_proc);
	remove_proc_entry(EXT4_MB_STATS_NAME, sbi->s_mb_proc);
	remove_proc_entry(devname, proc_root_ext4);
	sbi->s_mb_proc = NULL;

	return -ENOMEM;
}

static int ext4_mb_destroy_per_dev_proc(struct super_block *sb)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	char devname[64];

	if (sbi->s_mb_proc == NULL)
		return -EINVAL;

	bdevname(sb->s_bdev, devname);
	remove_proc_entry(EXT4_MB_GROUP_PREALLOC, sbi->s_mb_proc);
	remove_proc_entry(EXT4_MB_STREAM_REQ, sbi->s_mb_proc);
	remove_proc_entry(EXT4_MB_ORDER2_REQ, sbi->s_mb_proc);
	remove_proc_entry(EXT4_MB_MIN_TO_SCAN_NAME, sbi->s_mb_proc);
	remove_proc_entry(EXT4_MB_MAX_TO_SCAN_NAME, sbi->s_mb_proc);
	remove_proc_entry(EXT4_MB_STATS_NAME, sbi->s_mb_proc);
	remove_proc_entry(devname, proc_root_ext4);

	return 0;
}

int __init init_ext4_mballoc(void)
{
	ext4_pspace_cachep =
		kmem_cache_create("ext4_prealloc_space",
				     sizeof(struct ext4_prealloc_space),
				     0, SLAB_RECLAIM_ACCOUNT, NULL);
	if (ext4_pspace_cachep == NULL)
		return -ENOMEM;

	ext4_ac_cachep =
		kmem_cache_create("ext4_alloc_context",
				     sizeof(struct ext4_allocation_context),
				     0, SLAB_RECLAIM_ACCOUNT, NULL);
	if (ext4_ac_cachep == NULL) {
		kmem_cache_destroy(ext4_pspace_cachep);
		return -ENOMEM;
	}
#ifdef CONFIG_PROC_FS
	proc_root_ext4 = proc_mkdir("fs/ext4", NULL);
	if (proc_root_ext4 == NULL)
		printk(KERN_ERR "EXT4-fs: Unable to create fs/ext4\n");
#endif
	return 0;
}

void exit_ext4_mballoc(void)
{
	/* XXX: synchronize_rcu(); */
	kmem_cache_destroy(ext4_pspace_cachep);
	kmem_cache_destroy(ext4_ac_cachep);
#ifdef CONFIG_PROC_FS
	remove_proc_entry("fs/ext4", NULL);
#endif
}


/*
 * Check quota and mark choosed space (ac->ac_b_ex) non-free in bitmaps
 * Returns 0 if success or error code
 */
static noinline_for_stack int
ext4_mb_mark_diskspace_used(struct ext4_allocation_context *ac,
				handle_t *handle)
{
	struct buffer_head *bitmap_bh = NULL;
	struct ext4_super_block *es;
	struct ext4_group_desc *gdp;
	struct buffer_head *gdp_bh;
	struct ext4_sb_info *sbi;
	struct super_block *sb;
	ext4_fsblk_t block;
	int err, len;

	BUG_ON(ac->ac_status != AC_STATUS_FOUND);
	BUG_ON(ac->ac_b_ex.fe_len <= 0);

	sb = ac->ac_sb;
	sbi = EXT4_SB(sb);
	es = sbi->s_es;


	err = -EIO;
	bitmap_bh = ext4_read_block_bitmap(sb, ac->ac_b_ex.fe_group);
	if (!bitmap_bh)
		goto out_err;

	err = ext4_journal_get_write_access(handle, bitmap_bh);
	if (err)
		goto out_err;

	err = -EIO;
	gdp = ext4_get_group_desc(sb, ac->ac_b_ex.fe_group, &gdp_bh);
	if (!gdp)
		goto out_err;

	ext4_debug("using block group %lu(%d)\n", ac->ac_b_ex.fe_group,
			gdp->bg_free_blocks_count);

	err = ext4_journal_get_write_access(handle, gdp_bh);
	if (err)
		goto out_err;

	block = ac->ac_b_ex.fe_group * EXT4_BLOCKS_PER_GROUP(sb)
		+ ac->ac_b_ex.fe_start
		+ le32_to_cpu(es->s_first_data_block);

	len = ac->ac_b_ex.fe_len;
	if (in_range(ext4_block_bitmap(sb, gdp), block, len) ||
	    in_range(ext4_inode_bitmap(sb, gdp), block, len) ||
	    in_range(block, ext4_inode_table(sb, gdp),
		     EXT4_SB(sb)->s_itb_per_group) ||
	    in_range(block + len - 1, ext4_inode_table(sb, gdp),
		     EXT4_SB(sb)->s_itb_per_group)) {
		ext4_error(sb, __func__,
			   "Allocating block in system zone - block = %llu",
			   block);
		/* File system mounted not to panic on error
		 * Fix the bitmap and repeat the block allocation
		 * We leak some of the blocks here.
		 */
		mb_set_bits(sb_bgl_lock(sbi, ac->ac_b_ex.fe_group),
				bitmap_bh->b_data, ac->ac_b_ex.fe_start,
				ac->ac_b_ex.fe_len);
		err = ext4_journal_dirty_metadata(handle, bitmap_bh);
		if (!err)
			err = -EAGAIN;
		goto out_err;
	}
#ifdef AGGRESSIVE_CHECK
	{
		int i;
		for (i = 0; i < ac->ac_b_ex.fe_len; i++) {
			BUG_ON(mb_test_bit(ac->ac_b_ex.fe_start + i,
						bitmap_bh->b_data));
		}
	}
#endif
	mb_set_bits(sb_bgl_lock(sbi, ac->ac_b_ex.fe_group), bitmap_bh->b_data,
				ac->ac_b_ex.fe_start, ac->ac_b_ex.fe_len);

	spin_lock(sb_bgl_lock(sbi, ac->ac_b_ex.fe_group));
	if (gdp->bg_flags & cpu_to_le16(EXT4_BG_BLOCK_UNINIT)) {
		gdp->bg_flags &= cpu_to_le16(~EXT4_BG_BLOCK_UNINIT);
		gdp->bg_free_blocks_count =
			cpu_to_le16(ext4_free_blocks_after_init(sb,
						ac->ac_b_ex.fe_group,
						gdp));
	}
	le16_add_cpu(&gdp->bg_free_blocks_count, -ac->ac_b_ex.fe_len);
	gdp->bg_checksum = ext4_group_desc_csum(sbi, ac->ac_b_ex.fe_group, gdp);
	spin_unlock(sb_bgl_lock(sbi, ac->ac_b_ex.fe_group));

	/*
	 * free blocks account has already be reduced/reserved
	 * at write_begin() time for delayed allocation
	 * do not double accounting
	 */
	if (!(ac->ac_flags & EXT4_MB_DELALLOC_RESERVED) &&
			ac->ac_o_ex.fe_len != ac->ac_b_ex.fe_len) {
		/*
		 * we allocated less blocks than we calimed
		 * Add the difference back
		 */
		percpu_counter_add(&sbi->s_freeblocks_counter,
				ac->ac_o_ex.fe_len - ac->ac_b_ex.fe_len);
	}

	if (sbi->s_log_groups_per_flex) {
		ext4_group_t flex_group = ext4_flex_group(sbi,
							  ac->ac_b_ex.fe_group);
		spin_lock(sb_bgl_lock(sbi, flex_group));
		sbi->s_flex_groups[flex_group].free_blocks -= ac->ac_b_ex.fe_len;
		spin_unlock(sb_bgl_lock(sbi, flex_group));
	}

	err = ext4_journal_dirty_metadata(handle, bitmap_bh);
	if (err)
		goto out_err;
	err = ext4_journal_dirty_metadata(handle, gdp_bh);

out_err:
	sb->s_dirt = 1;
	brelse(bitmap_bh);
	return err;
}

/*
 * here we normalize request for locality group
 * Group request are normalized to s_strip size if we set the same via mount
 * option. If not we set it to s_mb_group_prealloc which can be configured via
 * /proc/fs/ext4/<partition>/group_prealloc
 *
 * XXX: should we try to preallocate more than the group has now?
 */
static void ext4_mb_normalize_group_request(struct ext4_allocation_context *ac)
{
	struct super_block *sb = ac->ac_sb;
	struct ext4_locality_group *lg = ac->ac_lg;

	BUG_ON(lg == NULL);
	if (EXT4_SB(sb)->s_stripe)
		ac->ac_g_ex.fe_len = EXT4_SB(sb)->s_stripe;
	else
		ac->ac_g_ex.fe_len = EXT4_SB(sb)->s_mb_group_prealloc;
	mb_debug("#%u: goal %u blocks for locality group\n",
		current->pid, ac->ac_g_ex.fe_len);
}

/*
 * Normalization means making request better in terms of
 * size and alignment
 */
static noinline_for_stack void
ext4_mb_normalize_request(struct ext4_allocation_context *ac,
				struct ext4_allocation_request *ar)
{
	int bsbits, max;
	ext4_lblk_t end;
	loff_t size, orig_size, start_off;
	ext4_lblk_t start, orig_start;
	struct ext4_inode_info *ei = EXT4_I(ac->ac_inode);
	struct ext4_prealloc_space *pa;

	/* do normalize only data requests, metadata requests
	   do not need preallocation */
	if (!(ac->ac_flags & EXT4_MB_HINT_DATA))
		return;

	/* sometime caller may want exact blocks */
	if (unlikely(ac->ac_flags & EXT4_MB_HINT_GOAL_ONLY))
		return;

	/* caller may indicate that preallocation isn't
	 * required (it's a tail, for example) */
	if (ac->ac_flags & EXT4_MB_HINT_NOPREALLOC)
		return;

	if (ac->ac_flags & EXT4_MB_HINT_GROUP_ALLOC) {
		ext4_mb_normalize_group_request(ac);
		return ;
	}

	bsbits = ac->ac_sb->s_blocksize_bits;

	/* first, let's learn actual file size
	 * given current request is allocated */
	size = ac->ac_o_ex.fe_logical + ac->ac_o_ex.fe_len;
	size = size << bsbits;
	if (size < i_size_read(ac->ac_inode))
		size = i_size_read(ac->ac_inode);

	/* max size of free chunks */
	max = 2 << bsbits;

#define NRL_CHECK_SIZE(req, size, max, chunk_size)	\
		(req <= (size) || max <= (chunk_size))

	/* first, try to predict filesize */
	/* XXX: should this table be tunable? */
	start_off = 0;
	if (size <= 16 * 1024) {
		size = 16 * 1024;
	} else if (size <= 32 * 1024) {
		size = 32 * 1024;
	} else if (size <= 64 * 1024) {
		size = 64 * 1024;
	} else if (size <= 128 * 1024) {
		size = 128 * 1024;
	} else if (size <= 256 * 1024) {
		size = 256 * 1024;
	} else if (size <= 512 * 1024) {
		size = 512 * 1024;
	} else if (size <= 1024 * 1024) {
		size = 1024 * 1024;
	} else if (NRL_CHECK_SIZE(size, 4 * 1024 * 1024, max, 2 * 1024)) {
		start_off = ((loff_t)ac->ac_o_ex.fe_logical >>
						(21 - bsbits)) << 21;
		size = 2 * 1024 * 1024;
	} else if (NRL_CHECK_SIZE(size, 8 * 1024 * 1024, max, 4 * 1024)) {
		start_off = ((loff_t)ac->ac_o_ex.fe_logical >>
							(22 - bsbits)) << 22;
		size = 4 * 1024 * 1024;
	} else if (NRL_CHECK_SIZE(ac->ac_o_ex.fe_len,
					(8<<20)>>bsbits, max, 8 * 1024)) {
		start_off = ((loff_t)ac->ac_o_ex.fe_logical >>
							(23 - bsbits)) << 23;
		size = 8 * 1024 * 1024;
	} else {
		start_off = (loff_t)ac->ac_o_ex.fe_logical << bsbits;
		size	  = ac->ac_o_ex.fe_len << bsbits;
	}
	orig_size = size = size >> bsbits;
	orig_start = start = start_off >> bsbits;

	/* don't cover already allocated blocks in selected range */
	if (ar->pleft && start <= ar->lleft) {
		size -= ar->lleft + 1 - start;
		start = ar->lleft + 1;
	}
	if (ar->pright && start + size - 1 >= ar->lright)
		size -= start + size - ar->lright;

	end = start + size;

	/* check we don't cross already preallocated blocks */
	rcu_read_lock();
	list_for_each_entry_rcu(pa, &ei->i_prealloc_list, pa_inode_list) {
		unsigned long pa_end;

		if (pa->pa_deleted)
			continue;
		spin_lock(&pa->pa_lock);
		if (pa->pa_deleted) {
			spin_unlock(&pa->pa_lock);
			continue;
		}

		pa_end = pa->pa_lstart + pa->pa_len;

		/* PA must not overlap original request */
		BUG_ON(!(ac->ac_o_ex.fe_logical >= pa_end ||
			ac->ac_o_ex.fe_logical < pa->pa_lstart));

		/* skip PA normalized request doesn't overlap with */
		if (pa->pa_lstart >= end) {
			spin_unlock(&pa->pa_lock);
			continue;
		}
		if (pa_end <= start) {
			spin_unlock(&pa->pa_lock);
			continue;
		}
		BUG_ON(pa->pa_lstart <= start && pa_end >= end);

		if (pa_end <= ac->ac_o_ex.fe_logical) {
			BUG_ON(pa_end < start);
			start = pa_end;
		}

		if (pa->pa_lstart > ac->ac_o_ex.fe_logical) {
			BUG_ON(pa->pa_lstart > end);
			end = pa->pa_lstart;
		}
		spin_unlock(&pa->pa_lock);
	}
	rcu_read_unlock();
	size = end - start;

	/* XXX: extra loop to check we really don't overlap preallocations */
	rcu_read_lock();
	list_for_each_entry_rcu(pa, &ei->i_prealloc_list, pa_inode_list) {
		unsigned long pa_end;
		spin_lock(&pa->pa_lock);
		if (pa->pa_deleted == 0) {
			pa_end = pa->pa_lstart + pa->pa_len;
			BUG_ON(!(start >= pa_end || end <= pa->pa_lstart));
		}
		spin_unlock(&pa->pa_lock);
	}
	rcu_read_unlock();

	if (start + size <= ac->ac_o_ex.fe_logical &&
			start > ac->ac_o_ex.fe_logical) {
		printk(KERN_ERR "start %lu, size %lu, fe_logical %lu\n",
			(unsigned long) start, (unsigned long) size,
			(unsigned long) ac->ac_o_ex.fe_logical);
	}
	BUG_ON(start + size <= ac->ac_o_ex.fe_logical &&
			start > ac->ac_o_ex.fe_logical);
	BUG_ON(size <= 0 || size >= EXT4_BLOCKS_PER_GROUP(ac->ac_sb));

	/* now prepare goal request */

	/* XXX: is it better to align blocks WRT to logical
	 * placement or satisfy big request as is */
	ac->ac_g_ex.fe_logical = start;
	ac->ac_g_ex.fe_len = size;

	/* define goal start in order to merge */
	if (ar->pright && (ar->lright == (start + size))) {
		/* merge to the right */
		ext4_get_group_no_and_offset(ac->ac_sb, ar->pright - size,
						&ac->ac_f_ex.fe_group,
						&ac->ac_f_ex.fe_start);
		ac->ac_flags |= EXT4_MB_HINT_TRY_GOAL;
	}
	if (ar->pleft && (ar->lleft + 1 == start)) {
		/* merge to the left */
		ext4_get_group_no_and_offset(ac->ac_sb, ar->pleft + 1,
						&ac->ac_f_ex.fe_group,
						&ac->ac_f_ex.fe_start);
		ac->ac_flags |= EXT4_MB_HINT_TRY_GOAL;
	}

	mb_debug("goal: %u(was %u) blocks at %u\n", (unsigned) size,
		(unsigned) orig_size, (unsigned) start);
}

static void ext4_mb_collect_stats(struct ext4_allocation_context *ac)
{
	struct ext4_sb_info *sbi = EXT4_SB(ac->ac_sb);

	if (sbi->s_mb_stats && ac->ac_g_ex.fe_len > 1) {
		atomic_inc(&sbi->s_bal_reqs);
		atomic_add(ac->ac_b_ex.fe_len, &sbi->s_bal_allocated);
		if (ac->ac_o_ex.fe_len >= ac->ac_g_ex.fe_len)
			atomic_inc(&sbi->s_bal_success);
		atomic_add(ac->ac_found, &sbi->s_bal_ex_scanned);
		if (ac->ac_g_ex.fe_start == ac->ac_b_ex.fe_start &&
				ac->ac_g_ex.fe_group == ac->ac_b_ex.fe_group)
			atomic_inc(&sbi->s_bal_goals);
		if (ac->ac_found > sbi->s_mb_max_to_scan)
			atomic_inc(&sbi->s_bal_breaks);
	}

	ext4_mb_store_history(ac);
}

/*
 * use blocks preallocated to inode
 */
static void ext4_mb_use_inode_pa(struct ext4_allocation_context *ac,
				struct ext4_prealloc_space *pa)
{
	ext4_fsblk_t start;
	ext4_fsblk_t end;
	int len;

	/* found preallocated blocks, use them */
	start = pa->pa_pstart + (ac->ac_o_ex.fe_logical - pa->pa_lstart);
	end = min(pa->pa_pstart + pa->pa_len, start + ac->ac_o_ex.fe_len);
	len = end - start;
	ext4_get_group_no_and_offset(ac->ac_sb, start, &ac->ac_b_ex.fe_group,
					&ac->ac_b_ex.fe_start);
	ac->ac_b_ex.fe_len = len;
	ac->ac_status = AC_STATUS_FOUND;
	ac->ac_pa = pa;

	BUG_ON(start < pa->pa_pstart);
	BUG_ON(start + len > pa->pa_pstart + pa->pa_len);
	BUG_ON(pa->pa_free < len);
	pa->pa_free -= len;

	mb_debug("use %llu/%u from inode pa %p\n", start, len, pa);
}

/*
 * use blocks preallocated to locality group
 */
static void ext4_mb_use_group_pa(struct ext4_allocation_context *ac,
				struct ext4_prealloc_space *pa)
{
	unsigned int len = ac->ac_o_ex.fe_len;

	ext4_get_group_no_and_offset(ac->ac_sb, pa->pa_pstart,
					&ac->ac_b_ex.fe_group,
					&ac->ac_b_ex.fe_start);
	ac->ac_b_ex.fe_len = len;
	ac->ac_status = AC_STATUS_FOUND;
	ac->ac_pa = pa;

	/* we don't correct pa_pstart or pa_plen here to avoid
	 * possible race when the group is being loaded concurrently
	 * instead we correct pa later, after blocks are marked
	 * in on-disk bitmap -- see ext4_mb_release_context()
	 * Other CPUs are prevented from allocating from this pa by lg_mutex
	 */
	mb_debug("use %u/%u from group pa %p\n", pa->pa_lstart-len, len, pa);
}

/*
 * Return the prealloc space that have minimal distance
 * from the goal block. @cpa is the prealloc
 * space that is having currently known minimal distance
 * from the goal block.
 */
static struct ext4_prealloc_space *
ext4_mb_check_group_pa(ext4_fsblk_t goal_block,
			struct ext4_prealloc_space *pa,
			struct ext4_prealloc_space *cpa)
{
	ext4_fsblk_t cur_distance, new_distance;

	if (cpa == NULL) {
		atomic_inc(&pa->pa_count);
		return pa;
	}
	cur_distance = abs(goal_block - cpa->pa_pstart);
	new_distance = abs(goal_block - pa->pa_pstart);

	if (cur_distance < new_distance)
		return cpa;

	/* drop the previous reference */
	atomic_dec(&cpa->pa_count);
	atomic_inc(&pa->pa_count);
	return pa;
}

/*
 * search goal blocks in preallocated space
 */
static noinline_for_stack int
ext4_mb_use_preallocated(struct ext4_allocation_context *ac)
{
	int order, i;
	struct ext4_inode_info *ei = EXT4_I(ac->ac_inode);
	struct ext4_locality_group *lg;
	struct ext4_prealloc_space *pa, *cpa = NULL;
	ext4_fsblk_t goal_block;

	/* only data can be preallocated */
	if (!(ac->ac_flags & EXT4_MB_HINT_DATA))
		return 0;

	/* first, try per-file preallocation */
	rcu_read_lock();
	list_for_each_entry_rcu(pa, &ei->i_prealloc_list, pa_inode_list) {

		/* all fields in this condition don't change,
		 * so we can skip locking for them */
		if (ac->ac_o_ex.fe_logical < pa->pa_lstart ||
			ac->ac_o_ex.fe_logical >= pa->pa_lstart + pa->pa_len)
			continue;

		/* found preallocated blocks, use them */
		spin_lock(&pa->pa_lock);
		if (pa->pa_deleted == 0 && pa->pa_free) {
			atomic_inc(&pa->pa_count);
			ext4_mb_use_inode_pa(ac, pa);
			spin_unlock(&pa->pa_lock);
			ac->ac_criteria = 10;
			rcu_read_unlock();
			return 1;
		}
		spin_unlock(&pa->pa_lock);
	}
	rcu_read_unlock();

	/* can we use group allocation? */
	if (!(ac->ac_flags & EXT4_MB_HINT_GROUP_ALLOC))
		return 0;

	/* inode may have no locality group for some reason */
	lg = ac->ac_lg;
	if (lg == NULL)
		return 0;
	order  = fls(ac->ac_o_ex.fe_len) - 1;
	if (order > PREALLOC_TB_SIZE - 1)
		/* The max size of hash table is PREALLOC_TB_SIZE */
		order = PREALLOC_TB_SIZE - 1;

	goal_block = ac->ac_g_ex.fe_group * EXT4_BLOCKS_PER_GROUP(ac->ac_sb) +
		     ac->ac_g_ex.fe_start +
		     le32_to_cpu(EXT4_SB(ac->ac_sb)->s_es->s_first_data_block);
	/*
	 * search for the prealloc space that is having
	 * minimal distance from the goal block.
	 */
	for (i = order; i < PREALLOC_TB_SIZE; i++) {
		rcu_read_lock();
		list_for_each_entry_rcu(pa, &lg->lg_prealloc_list[i],
					pa_inode_list) {
			spin_lock(&pa->pa_lock);
			if (pa->pa_deleted == 0 &&
					pa->pa_free >= ac->ac_o_ex.fe_len) {

				cpa = ext4_mb_check_group_pa(goal_block,
								pa, cpa);
			}
			spin_unlock(&pa->pa_lock);
		}
		rcu_read_unlock();
	}
	if (cpa) {
		ext4_mb_use_group_pa(ac, cpa);
		ac->ac_criteria = 20;
		return 1;
	}
	return 0;
}

/*
 * the function goes through all preallocation in this group and marks them
 * used in in-core bitmap. buddy must be generated from this bitmap
 * Need to be called with ext4 group lock (ext4_lock_group)
 */
static void ext4_mb_generate_from_pa(struct super_block *sb, void *bitmap,
					ext4_group_t group)
{
	struct ext4_group_info *grp = ext4_get_group_info(sb, group);
	struct ext4_prealloc_space *pa;
	struct list_head *cur;
	ext4_group_t groupnr;
	ext4_grpblk_t start;
	int preallocated = 0;
	int count = 0;
	int len;

	/* all form of preallocation discards first load group,
	 * so the only competing code is preallocation use.
	 * we don't need any locking here
	 * notice we do NOT ignore preallocations with pa_deleted
	 * otherwise we could leave used blocks available for
	 * allocation in buddy when concurrent ext4_mb_put_pa()
	 * is dropping preallocation
	 */
	list_for_each(cur, &grp->bb_prealloc_list) {
		pa = list_entry(cur, struct ext4_prealloc_space, pa_group_list);
		spin_lock(&pa->pa_lock);
		ext4_get_group_no_and_offset(sb, pa->pa_pstart,
					     &groupnr, &start);
		len = pa->pa_len;
		spin_unlock(&pa->pa_lock);
		if (unlikely(len == 0))
			continue;
		BUG_ON(groupnr != group);
		mb_set_bits(sb_bgl_lock(EXT4_SB(sb), group),
						bitmap, start, len);
		preallocated += len;
		count++;
	}
	mb_debug("prellocated %u for group %lu\n", preallocated, group);
}

static void ext4_mb_pa_callback(struct rcu_head *head)
{
	struct ext4_prealloc_space *pa;
	pa = container_of(head, struct ext4_prealloc_space, u.pa_rcu);
	kmem_cache_free(ext4_pspace_cachep, pa);
}

/*
 * drops a reference to preallocated space descriptor
 * if this was the last reference and the space is consumed
 */
static void ext4_mb_put_pa(struct ext4_allocation_context *ac,
			struct super_block *sb, struct ext4_prealloc_space *pa)
{
	unsigned long grp;

	if (!atomic_dec_and_test(&pa->pa_count) || pa->pa_free != 0)
		return;

	/* in this short window concurrent discard can set pa_deleted */
	spin_lock(&pa->pa_lock);
	if (pa->pa_deleted == 1) {
		spin_unlock(&pa->pa_lock);
		return;
	}

	pa->pa_deleted = 1;
	spin_unlock(&pa->pa_lock);

	/* -1 is to protect from crossing allocation group */
	ext4_get_group_no_and_offset(sb, pa->pa_pstart - 1, &grp, NULL);

	/*
	 * possible race:
	 *
	 *  P1 (buddy init)			P2 (regular allocation)
	 *					find block B in PA
	 *  copy on-disk bitmap to buddy
	 *  					mark B in on-disk bitmap
	 *					drop PA from group
	 *  mark all PAs in buddy
	 *
	 * thus, P1 initializes buddy with B available. to prevent this
	 * we make "copy" and "mark all PAs" atomic and serialize "drop PA"
	 * against that pair
	 */
	ext4_lock_group(sb, grp);
	list_del(&pa->pa_group_list);
	ext4_unlock_group(sb, grp);

	spin_lock(pa->pa_obj_lock);
	list_del_rcu(&pa->pa_inode_list);
	spin_unlock(pa->pa_obj_lock);

	call_rcu(&(pa)->u.pa_rcu, ext4_mb_pa_callback);
}

/*
 * creates new preallocated space for given inode
 */
static noinline_for_stack int
ext4_mb_new_inode_pa(struct ext4_allocation_context *ac)
{
	struct super_block *sb = ac->ac_sb;
	struct ext4_prealloc_space *pa;
	struct ext4_group_info *grp;
	struct ext4_inode_info *ei;

	/* preallocate only when found space is larger then requested */
	BUG_ON(ac->ac_o_ex.fe_len >= ac->ac_b_ex.fe_len);
	BUG_ON(ac->ac_status != AC_STATUS_FOUND);
	BUG_ON(!S_ISREG(ac->ac_inode->i_mode));

	pa = kmem_cache_alloc(ext4_pspace_cachep, GFP_NOFS);
	if (pa == NULL)
		return -ENOMEM;

	if (ac->ac_b_ex.fe_len < ac->ac_g_ex.fe_len) {
		int winl;
		int wins;
		int win;
		int offs;

		/* we can't allocate as much as normalizer wants.
		 * so, found space must get proper lstart
		 * to cover original request */
		BUG_ON(ac->ac_g_ex.fe_logical > ac->ac_o_ex.fe_logical);
		BUG_ON(ac->ac_g_ex.fe_len < ac->ac_o_ex.fe_len);

		/* we're limited by original request in that
		 * logical block must be covered any way
		 * winl is window we can move our chunk within */
		winl = ac->ac_o_ex.fe_logical - ac->ac_g_ex.fe_logical;

		/* also, we should cover whole original request */
		wins = ac->ac_b_ex.fe_len - ac->ac_o_ex.fe_len;

		/* the smallest one defines real window */
		win = min(winl, wins);

		offs = ac->ac_o_ex.fe_logical % ac->ac_b_ex.fe_len;
		if (offs && offs < win)
			win = offs;

		ac->ac_b_ex.fe_logical = ac->ac_o_ex.fe_logical - win;
		BUG_ON(ac->ac_o_ex.fe_logical < ac->ac_b_ex.fe_logical);
		BUG_ON(ac->ac_o_ex.fe_len > ac->ac_b_ex.fe_len);
	}

	/* preallocation can change ac_b_ex, thus we store actually
	 * allocated blocks for history */
	ac->ac_f_ex = ac->ac_b_ex;

	pa->pa_lstart = ac->ac_b_ex.fe_logical;
	pa->pa_pstart = ext4_grp_offs_to_block(sb, &ac->ac_b_ex);
	pa->pa_len = ac->ac_b_ex.fe_len;
	pa->pa_free = pa->pa_len;
	atomic_set(&pa->pa_count, 1);
	spin_lock_init(&pa->pa_lock);
	pa->pa_deleted = 0;
	pa->pa_linear = 0;

	mb_debug("new inode pa %p: %llu/%u for %u\n", pa,
			pa->pa_pstart, pa->pa_len, pa->pa_lstart);

	ext4_mb_use_inode_pa(ac, pa);
	atomic_add(pa->pa_free, &EXT4_SB(sb)->s_mb_preallocated);

	ei = EXT4_I(ac->ac_inode);
	grp = ext4_get_group_info(sb, ac->ac_b_ex.fe_group);

	pa->pa_obj_lock = &ei->i_prealloc_lock;
	pa->pa_inode = ac->ac_inode;

	ext4_lock_group(sb, ac->ac_b_ex.fe_group);
	list_add(&pa->pa_group_list, &grp->bb_prealloc_list);
	ext4_unlock_group(sb, ac->ac_b_ex.fe_group);

	spin_lock(pa->pa_obj_lock);
	list_add_rcu(&pa->pa_inode_list, &ei->i_prealloc_list);
	spin_unlock(pa->pa_obj_lock);

	return 0;
}

/*
 * creates new preallocated space for locality group inodes belongs to
 */
static noinline_for_stack int
ext4_mb_new_group_pa(struct ext4_allocation_context *ac)
{
	struct super_block *sb = ac->ac_sb;
	struct ext4_locality_group *lg;
	struct ext4_prealloc_space *pa;
	struct ext4_group_info *grp;

	/* preallocate only when found space is larger then requested */
	BUG_ON(ac->ac_o_ex.fe_len >= ac->ac_b_ex.fe_len);
	BUG_ON(ac->ac_status != AC_STATUS_FOUND);
	BUG_ON(!S_ISREG(ac->ac_inode->i_mode));

	BUG_ON(ext4_pspace_cachep == NULL);
	pa = kmem_cache_alloc(ext4_pspace_cachep, GFP_NOFS);
	if (pa == NULL)
		return -ENOMEM;

	/* preallocation can change ac_b_ex, thus we store actually
	 * allocated blocks for history */
	ac->ac_f_ex = ac->ac_b_ex;

	pa->pa_pstart = ext4_grp_offs_to_block(sb, &ac->ac_b_ex);
	pa->pa_lstart = pa->pa_pstart;
	pa->pa_len = ac->ac_b_ex.fe_len;
	pa->pa_free = pa->pa_len;
	atomic_set(&pa->pa_count, 1);
	spin_lock_init(&pa->pa_lock);
	INIT_LIST_HEAD(&pa->pa_inode_list);
	pa->pa_deleted = 0;
	pa->pa_linear = 1;

	mb_debug("new group pa %p: %llu/%u for %u\n", pa,
			pa->pa_pstart, pa->pa_len, pa->pa_lstart);

	ext4_mb_use_group_pa(ac, pa);
	atomic_add(pa->pa_free, &EXT4_SB(sb)->s_mb_preallocated);

	grp = ext4_get_group_info(sb, ac->ac_b_ex.fe_group);
	lg = ac->ac_lg;
	BUG_ON(lg == NULL);

	pa->pa_obj_lock = &lg->lg_prealloc_lock;
	pa->pa_inode = NULL;

	ext4_lock_group(sb, ac->ac_b_ex.fe_group);
	list_add(&pa->pa_group_list, &grp->bb_prealloc_list);
	ext4_unlock_group(sb, ac->ac_b_ex.fe_group);

	/*
	 * We will later add the new pa to the right bucket
	 * after updating the pa_free in ext4_mb_release_context
	 */
	return 0;
}

static int ext4_mb_new_preallocation(struct ext4_allocation_context *ac)
{
	int err;

	if (ac->ac_flags & EXT4_MB_HINT_GROUP_ALLOC)
		err = ext4_mb_new_group_pa(ac);
	else
		err = ext4_mb_new_inode_pa(ac);
	return err;
}

/*
 * finds all unused blocks in on-disk bitmap, frees them in
 * in-core bitmap and buddy.
 * @pa must be unlinked from inode and group lists, so that
 * nobody else can find/use it.
 * the caller MUST hold group/inode locks.
 * TODO: optimize the case when there are no in-core structures yet
 */
static noinline_for_stack int
ext4_mb_release_inode_pa(struct ext4_buddy *e4b, struct buffer_head *bitmap_bh,
			struct ext4_prealloc_space *pa,
			struct ext4_allocation_context *ac)
{
	struct super_block *sb = e4b->bd_sb;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	unsigned long end;
	unsigned long next;
	ext4_group_t group;
	ext4_grpblk_t bit;
	sector_t start;
	int err = 0;
	int free = 0;

	BUG_ON(pa->pa_deleted == 0);
	ext4_get_group_no_and_offset(sb, pa->pa_pstart, &group, &bit);
	BUG_ON(group != e4b->bd_group && pa->pa_len != 0);
	end = bit + pa->pa_len;

	if (ac) {
		ac->ac_sb = sb;
		ac->ac_inode = pa->pa_inode;
		ac->ac_op = EXT4_MB_HISTORY_DISCARD;
	}

	while (bit < end) {
		bit = mb_find_next_zero_bit(bitmap_bh->b_data, end, bit);
		if (bit >= end)
			break;
		next = mb_find_next_bit(bitmap_bh->b_data, end, bit);
		start = group * EXT4_BLOCKS_PER_GROUP(sb) + bit +
				le32_to_cpu(sbi->s_es->s_first_data_block);
		mb_debug("    free preallocated %u/%u in group %u\n",
				(unsigned) start, (unsigned) next - bit,
				(unsigned) group);
		free += next - bit;

		if (ac) {
			ac->ac_b_ex.fe_group = group;
			ac->ac_b_ex.fe_start = bit;
			ac->ac_b_ex.fe_len = next - bit;
			ac->ac_b_ex.fe_logical = 0;
			ext4_mb_store_history(ac);
		}

		mb_free_blocks(pa->pa_inode, e4b, bit, next - bit);
		bit = next + 1;
	}
	if (free != pa->pa_free) {
		printk(KERN_CRIT "pa %p: logic %lu, phys. %lu, len %lu\n",
			pa, (unsigned long) pa->pa_lstart,
			(unsigned long) pa->pa_pstart,
			(unsigned long) pa->pa_len);
		ext4_error(sb, __func__, "free %u, pa_free %u\n",
						free, pa->pa_free);
		/*
		 * pa is already deleted so we use the value obtained
		 * from the bitmap and continue.
		 */
	}
	atomic_add(free, &sbi->s_mb_discarded);

	return err;
}

static noinline_for_stack int
ext4_mb_release_group_pa(struct ext4_buddy *e4b,
				struct ext4_prealloc_space *pa,
				struct ext4_allocation_context *ac)
{
	struct super_block *sb = e4b->bd_sb;
	ext4_group_t group;
	ext4_grpblk_t bit;

	if (ac)
		ac->ac_op = EXT4_MB_HISTORY_DISCARD;

	BUG_ON(pa->pa_deleted == 0);
	ext4_get_group_no_and_offset(sb, pa->pa_pstart, &group, &bit);
	BUG_ON(group != e4b->bd_group && pa->pa_len != 0);
	mb_free_blocks(pa->pa_inode, e4b, bit, pa->pa_len);
	atomic_add(pa->pa_len, &EXT4_SB(sb)->s_mb_discarded);

	if (ac) {
		ac->ac_sb = sb;
		ac->ac_inode = NULL;
		ac->ac_b_ex.fe_group = group;
		ac->ac_b_ex.fe_start = bit;
		ac->ac_b_ex.fe_len = pa->pa_len;
		ac->ac_b_ex.fe_logical = 0;
		ext4_mb_store_history(ac);
	}

	return 0;
}

/*
 * releases all preallocations in given group
 *
 * first, we need to decide discard policy:
 * - when do we discard
 *   1) ENOSPC
 * - how many do we discard
 *   1) how many requested
 */
static noinline_for_stack int
ext4_mb_discard_group_preallocations(struct super_block *sb,
					ext4_group_t group, int needed)
{
	struct ext4_group_info *grp = ext4_get_group_info(sb, group);
	struct buffer_head *bitmap_bh = NULL;
	struct ext4_prealloc_space *pa, *tmp;
	struct ext4_allocation_context *ac;
	struct list_head list;
	struct ext4_buddy e4b;
	int err;
	int busy = 0;
	int free = 0;

	mb_debug("discard preallocation for group %lu\n", group);

	if (list_empty(&grp->bb_prealloc_list))
		return 0;

	bitmap_bh = ext4_read_block_bitmap(sb, group);
	if (bitmap_bh == NULL) {
		ext4_error(sb, __func__, "Error in reading block "
				"bitmap for %lu\n", group);
		return 0;
	}

	err = ext4_mb_load_buddy(sb, group, &e4b);
	if (err) {
		ext4_error(sb, __func__, "Error in loading buddy "
				"information for %lu\n", group);
		put_bh(bitmap_bh);
		return 0;
	}

	if (needed == 0)
		needed = EXT4_BLOCKS_PER_GROUP(sb) + 1;

	INIT_LIST_HEAD(&list);
	ac = kmem_cache_alloc(ext4_ac_cachep, GFP_NOFS);
repeat:
	ext4_lock_group(sb, group);
	list_for_each_entry_safe(pa, tmp,
				&grp->bb_prealloc_list, pa_group_list) {
		spin_lock(&pa->pa_lock);
		if (atomic_read(&pa->pa_count)) {
			spin_unlock(&pa->pa_lock);
			busy = 1;
			continue;
		}
		if (pa->pa_deleted) {
			spin_unlock(&pa->pa_lock);
			continue;
		}

		/* seems this one can be freed ... */
		pa->pa_deleted = 1;

		/* we can trust pa_free ... */
		free += pa->pa_free;

		spin_unlock(&pa->pa_lock);

		list_del(&pa->pa_group_list);
		list_add(&pa->u.pa_tmp_list, &list);
	}

	/* if we still need more blocks and some PAs were used, try again */
	if (free < needed && busy) {
		busy = 0;
		ext4_unlock_group(sb, group);
		/*
		 * Yield the CPU here so that we don't get soft lockup
		 * in non preempt case.
		 */
		yield();
		goto repeat;
	}

	/* found anything to free? */
	if (list_empty(&list)) {
		BUG_ON(free != 0);
		goto out;
	}

	/* now free all selected PAs */
	list_for_each_entry_safe(pa, tmp, &list, u.pa_tmp_list) {

		/* remove from object (inode or locality group) */
		spin_lock(pa->pa_obj_lock);
		list_del_rcu(&pa->pa_inode_list);
		spin_unlock(pa->pa_obj_lock);

		if (pa->pa_linear)
			ext4_mb_release_group_pa(&e4b, pa, ac);
		else
			ext4_mb_release_inode_pa(&e4b, bitmap_bh, pa, ac);

		list_del(&pa->u.pa_tmp_list);
		call_rcu(&(pa)->u.pa_rcu, ext4_mb_pa_callback);
	}

out:
	ext4_unlock_group(sb, group);
	if (ac)
		kmem_cache_free(ext4_ac_cachep, ac);
	ext4_mb_release_desc(&e4b);
	put_bh(bitmap_bh);
	return free;
}

/*
 * releases all non-used preallocated blocks for given inode
 *
 * It's important to discard preallocations under i_data_sem
 * We don't want another block to be served from the prealloc
 * space when we are discarding the inode prealloc space.
 *
 * FIXME!! Make sure it is valid at all the call sites
 */
void ext4_mb_discard_inode_preallocations(struct inode *inode)
{
	struct ext4_inode_info *ei = EXT4_I(inode);
	struct super_block *sb = inode->i_sb;
	struct buffer_head *bitmap_bh = NULL;
	struct ext4_prealloc_space *pa, *tmp;
	struct ext4_allocation_context *ac;
	ext4_group_t group = 0;
	struct list_head list;
	struct ext4_buddy e4b;
	int err;

	if (!test_opt(sb, MBALLOC) || !S_ISREG(inode->i_mode)) {
		/*BUG_ON(!list_empty(&ei->i_prealloc_list));*/
		return;
	}

	mb_debug("discard preallocation for inode %lu\n", inode->i_ino);

	INIT_LIST_HEAD(&list);

	ac = kmem_cache_alloc(ext4_ac_cachep, GFP_NOFS);
repeat:
	/* first, collect all pa's in the inode */
	spin_lock(&ei->i_prealloc_lock);
	while (!list_empty(&ei->i_prealloc_list)) {
		pa = list_entry(ei->i_prealloc_list.next,
				struct ext4_prealloc_space, pa_inode_list);
		BUG_ON(pa->pa_obj_lock != &ei->i_prealloc_lock);
		spin_lock(&pa->pa_lock);
		if (atomic_read(&pa->pa_count)) {
			/* this shouldn't happen often - nobody should
			 * use preallocation while we're discarding it */
			spin_unlock(&pa->pa_lock);
			spin_unlock(&ei->i_prealloc_lock);
			printk(KERN_ERR "uh-oh! used pa while discarding\n");
			WARN_ON(1);
			schedule_timeout_uninterruptible(HZ);
			goto repeat;

		}
		if (pa->pa_deleted == 0) {
			pa->pa_deleted = 1;
			spin_unlock(&pa->pa_lock);
			list_del_rcu(&pa->pa_inode_list);
			list_add(&pa->u.pa_tmp_list, &list);
			continue;
		}

		/* someone is deleting pa right now */
		spin_unlock(&pa->pa_lock);
		spin_unlock(&ei->i_prealloc_lock);

		/* we have to wait here because pa_deleted
		 * doesn't mean pa is already unlinked from
		 * the list. as we might be called from
		 * ->clear_inode() the inode will get freed
		 * and concurrent thread which is unlinking
		 * pa from inode's list may access already
		 * freed memory, bad-bad-bad */

		/* XXX: if this happens too often, we can
		 * add a flag to force wait only in case
		 * of ->clear_inode(), but not in case of
		 * regular truncate */
		schedule_timeout_uninterruptible(HZ);
		goto repeat;
	}
	spin_unlock(&ei->i_prealloc_lock);

	list_for_each_entry_safe(pa, tmp, &list, u.pa_tmp_list) {
		BUG_ON(pa->pa_linear != 0);
		ext4_get_group_no_and_offset(sb, pa->pa_pstart, &group, NULL);

		err = ext4_mb_load_buddy(sb, group, &e4b);
		if (err) {
			ext4_error(sb, __func__, "Error in loading buddy "
					"information for %lu\n", group);
			continue;
		}

		bitmap_bh = ext4_read_block_bitmap(sb, group);
		if (bitmap_bh == NULL) {
			ext4_error(sb, __func__, "Error in reading block "
					"bitmap for %lu\n", group);
			ext4_mb_release_desc(&e4b);
			continue;
		}

		ext4_lock_group(sb, group);
		list_del(&pa->pa_group_list);
		ext4_mb_release_inode_pa(&e4b, bitmap_bh, pa, ac);
		ext4_unlock_group(sb, group);

		ext4_mb_release_desc(&e4b);
		put_bh(bitmap_bh);

		list_del(&pa->u.pa_tmp_list);
		call_rcu(&(pa)->u.pa_rcu, ext4_mb_pa_callback);
	}
	if (ac)
		kmem_cache_free(ext4_ac_cachep, ac);
}

/*
 * finds all preallocated spaces and return blocks being freed to them
 * if preallocated space becomes full (no block is used from the space)
 * then the function frees space in buddy
 * XXX: at the moment, truncate (which is the only way to free blocks)
 * discards all preallocations
 */
static void ext4_mb_return_to_preallocation(struct inode *inode,
					struct ext4_buddy *e4b,
					sector_t block, int count)
{
	BUG_ON(!list_empty(&EXT4_I(inode)->i_prealloc_list));
}
#ifdef MB_DEBUG
static void ext4_mb_show_ac(struct ext4_allocation_context *ac)
{
	struct super_block *sb = ac->ac_sb;
	ext4_group_t i;

	printk(KERN_ERR "EXT4-fs: Can't allocate:"
			" Allocation context details:\n");
	printk(KERN_ERR "EXT4-fs: status %d flags %d\n",
			ac->ac_status, ac->ac_flags);
	printk(KERN_ERR "EXT4-fs: orig %lu/%lu/%lu@%lu, goal %lu/%lu/%lu@%lu, "
			"best %lu/%lu/%lu@%lu cr %d\n",
			(unsigned long)ac->ac_o_ex.fe_group,
			(unsigned long)ac->ac_o_ex.fe_start,
			(unsigned long)ac->ac_o_ex.fe_len,
			(unsigned long)ac->ac_o_ex.fe_logical,
			(unsigned long)ac->ac_g_ex.fe_group,
			(unsigned long)ac->ac_g_ex.fe_start,
			(unsigned long)ac->ac_g_ex.fe_len,
			(unsigned long)ac->ac_g_ex.fe_logical,
			(unsigned long)ac->ac_b_ex.fe_group,
			(unsigned long)ac->ac_b_ex.fe_start,
			(unsigned long)ac->ac_b_ex.fe_len,
			(unsigned long)ac->ac_b_ex.fe_logical,
			(int)ac->ac_criteria);
	printk(KERN_ERR "EXT4-fs: %lu scanned, %d found\n", ac->ac_ex_scanned,
		ac->ac_found);
	printk(KERN_ERR "EXT4-fs: groups: \n");
	for (i = 0; i < EXT4_SB(sb)->s_groups_count; i++) {
		struct ext4_group_info *grp = ext4_get_group_info(sb, i);
		struct ext4_prealloc_space *pa;
		ext4_grpblk_t start;
		struct list_head *cur;
		ext4_lock_group(sb, i);
		list_for_each(cur, &grp->bb_prealloc_list) {
			pa = list_entry(cur, struct ext4_prealloc_space,
					pa_group_list);
			spin_lock(&pa->pa_lock);
			ext4_get_group_no_and_offset(sb, pa->pa_pstart,
						     NULL, &start);
			spin_unlock(&pa->pa_lock);
			printk(KERN_ERR "PA:%lu:%d:%u \n", i,
							start, pa->pa_len);
		}
		ext4_unlock_group(sb, i);

		if (grp->bb_free == 0)
			continue;
		printk(KERN_ERR "%lu: %d/%d \n",
		       i, grp->bb_free, grp->bb_fragments);
	}
	printk(KERN_ERR "\n");
}
#else
static inline void ext4_mb_show_ac(struct ext4_allocation_context *ac)
{
	return;
}
#endif

/*
 * We use locality group preallocation for small size file. The size of the
 * file is determined by the current size or the resulting size after
 * allocation which ever is larger
 *
 * One can tune this size via /proc/fs/ext4/<partition>/stream_req
 */
static void ext4_mb_group_or_file(struct ext4_allocation_context *ac)
{
	struct ext4_sb_info *sbi = EXT4_SB(ac->ac_sb);
	int bsbits = ac->ac_sb->s_blocksize_bits;
	loff_t size, isize;

	if (!(ac->ac_flags & EXT4_MB_HINT_DATA))
		return;

	size = ac->ac_o_ex.fe_logical + ac->ac_o_ex.fe_len;
	isize = i_size_read(ac->ac_inode) >> bsbits;
	size = max(size, isize);

	/* don't use group allocation for large files */
	if (size >= sbi->s_mb_stream_request)
		return;

	if (unlikely(ac->ac_flags & EXT4_MB_HINT_GOAL_ONLY))
		return;

	BUG_ON(ac->ac_lg != NULL);
	/*
	 * locality group prealloc space are per cpu. The reason for having
	 * per cpu locality group is to reduce the contention between block
	 * request from multiple CPUs.
	 */
	ac->ac_lg = &sbi->s_locality_groups[get_cpu()];
	put_cpu();

	/* we're going to use group allocation */
	ac->ac_flags |= EXT4_MB_HINT_GROUP_ALLOC;

	/* serialize all allocations in the group */
	mutex_lock(&ac->ac_lg->lg_mutex);
}

static noinline_for_stack int
ext4_mb_initialize_context(struct ext4_allocation_context *ac,
				struct ext4_allocation_request *ar)
{
	struct super_block *sb = ar->inode->i_sb;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_super_block *es = sbi->s_es;
	ext4_group_t group;
	unsigned long len;
	unsigned long goal;
	ext4_grpblk_t block;

	/* we can't allocate > group size */
	len = ar->len;

	/* just a dirty hack to filter too big requests  */
	if (len >= EXT4_BLOCKS_PER_GROUP(sb) - 10)
		len = EXT4_BLOCKS_PER_GROUP(sb) - 10;

	/* start searching from the goal */
	goal = ar->goal;
	if (goal < le32_to_cpu(es->s_first_data_block) ||
			goal >= ext4_blocks_count(es))
		goal = le32_to_cpu(es->s_first_data_block);
	ext4_get_group_no_and_offset(sb, goal, &group, &block);

	/* set up allocation goals */
	ac->ac_b_ex.fe_logical = ar->logical;
	ac->ac_b_ex.fe_group = 0;
	ac->ac_b_ex.fe_start = 0;
	ac->ac_b_ex.fe_len = 0;
	ac->ac_status = AC_STATUS_CONTINUE;
	ac->ac_groups_scanned = 0;
	ac->ac_ex_scanned = 0;
	ac->ac_found = 0;
	ac->ac_sb = sb;
	ac->ac_inode = ar->inode;
	ac->ac_o_ex.fe_logical = ar->logical;
	ac->ac_o_ex.fe_group = group;
	ac->ac_o_ex.fe_start = block;
	ac->ac_o_ex.fe_len = len;
	ac->ac_g_ex.fe_logical = ar->logical;
	ac->ac_g_ex.fe_group = group;
	ac->ac_g_ex.fe_start = block;
	ac->ac_g_ex.fe_len = len;
	ac->ac_f_ex.fe_len = 0;
	ac->ac_flags = ar->flags;
	ac->ac_2order = 0;
	ac->ac_criteria = 0;
	ac->ac_pa = NULL;
	ac->ac_bitmap_page = NULL;
	ac->ac_buddy_page = NULL;
	ac->ac_lg = NULL;

	/* we have to define context: we'll we work with a file or
	 * locality group. this is a policy, actually */
	ext4_mb_group_or_file(ac);

	mb_debug("init ac: %u blocks @ %u, goal %u, flags %x, 2^%d, "
			"left: %u/%u, right %u/%u to %swritable\n",
			(unsigned) ar->len, (unsigned) ar->logical,
			(unsigned) ar->goal, ac->ac_flags, ac->ac_2order,
			(unsigned) ar->lleft, (unsigned) ar->pleft,
			(unsigned) ar->lright, (unsigned) ar->pright,
			atomic_read(&ar->inode->i_writecount) ? "" : "non-");
	return 0;

}

static noinline_for_stack void
ext4_mb_discard_lg_preallocations(struct super_block *sb,
					struct ext4_locality_group *lg,
					int order, int total_entries)
{
	ext4_group_t group = 0;
	struct ext4_buddy e4b;
	struct list_head discard_list;
	struct ext4_prealloc_space *pa, *tmp;
	struct ext4_allocation_context *ac;

	mb_debug("discard locality group preallocation\n");

	INIT_LIST_HEAD(&discard_list);
	ac = kmem_cache_alloc(ext4_ac_cachep, GFP_NOFS);

	spin_lock(&lg->lg_prealloc_lock);
	list_for_each_entry_rcu(pa, &lg->lg_prealloc_list[order],
						pa_inode_list) {
		spin_lock(&pa->pa_lock);
		if (atomic_read(&pa->pa_count)) {
			/*
			 * This is the pa that we just used
			 * for block allocation. So don't
			 * free that
			 */
			spin_unlock(&pa->pa_lock);
			continue;
		}
		if (pa->pa_deleted) {
			spin_unlock(&pa->pa_lock);
			continue;
		}
		/* only lg prealloc space */
		BUG_ON(!pa->pa_linear);

		/* seems this one can be freed ... */
		pa->pa_deleted = 1;
		spin_unlock(&pa->pa_lock);

		list_del_rcu(&pa->pa_inode_list);
		list_add(&pa->u.pa_tmp_list, &discard_list);

		total_entries--;
		if (total_entries <= 5) {
			/*
			 * we want to keep only 5 entries
			 * allowing it to grow to 8. This
			 * mak sure we don't call discard
			 * soon for this list.
			 */
			break;
		}
	}
	spin_unlock(&lg->lg_prealloc_lock);

	list_for_each_entry_safe(pa, tmp, &discard_list, u.pa_tmp_list) {

		ext4_get_group_no_and_offset(sb, pa->pa_pstart, &group, NULL);
		if (ext4_mb_load_buddy(sb, group, &e4b)) {
			ext4_error(sb, __func__, "Error in loading buddy "
					"information for %lu\n", group);
			continue;
		}
		ext4_lock_group(sb, group);
		list_del(&pa->pa_group_list);
		ext4_mb_release_group_pa(&e4b, pa, ac);
		ext4_unlock_group(sb, group);

		ext4_mb_release_desc(&e4b);
		list_del(&pa->u.pa_tmp_list);
		call_rcu(&(pa)->u.pa_rcu, ext4_mb_pa_callback);
	}
	if (ac)
		kmem_cache_free(ext4_ac_cachep, ac);
}

/*
 * We have incremented pa_count. So it cannot be freed at this
 * point. Also we hold lg_mutex. So no parallel allocation is
 * possible from this lg. That means pa_free cannot be updated.
 *
 * A parallel ext4_mb_discard_group_preallocations is possible.
 * which can cause the lg_prealloc_list to be updated.
 */

static void ext4_mb_add_n_trim(struct ext4_allocation_context *ac)
{
	int order, added = 0, lg_prealloc_count = 1;
	struct super_block *sb = ac->ac_sb;
	struct ext4_locality_group *lg = ac->ac_lg;
	struct ext4_prealloc_space *tmp_pa, *pa = ac->ac_pa;

	order = fls(pa->pa_free) - 1;
	if (order > PREALLOC_TB_SIZE - 1)
		/* The max size of hash table is PREALLOC_TB_SIZE */
		order = PREALLOC_TB_SIZE - 1;
	/* Add the prealloc space to lg */
	rcu_read_lock();
	list_for_each_entry_rcu(tmp_pa, &lg->lg_prealloc_list[order],
						pa_inode_list) {
		spin_lock(&tmp_pa->pa_lock);
		if (tmp_pa->pa_deleted) {
			spin_unlock(&pa->pa_lock);
			continue;
		}
		if (!added && pa->pa_free < tmp_pa->pa_free) {
			/* Add to the tail of the previous entry */
			list_add_tail_rcu(&pa->pa_inode_list,
						&tmp_pa->pa_inode_list);
			added = 1;
			/*
			 * we want to count the total
			 * number of entries in the list
			 */
		}
		spin_unlock(&tmp_pa->pa_lock);
		lg_prealloc_count++;
	}
	if (!added)
		list_add_tail_rcu(&pa->pa_inode_list,
					&lg->lg_prealloc_list[order]);
	rcu_read_unlock();

	/* Now trim the list to be not more than 8 elements */
	if (lg_prealloc_count > 8) {
		ext4_mb_discard_lg_preallocations(sb, lg,
						order, lg_prealloc_count);
		return;
	}
	return ;
}

/*
 * release all resource we used in allocation
 */
static int ext4_mb_release_context(struct ext4_allocation_context *ac)
{
	struct ext4_prealloc_space *pa = ac->ac_pa;
	if (pa) {
		if (pa->pa_linear) {
			/* see comment in ext4_mb_use_group_pa() */
			spin_lock(&pa->pa_lock);
			pa->pa_pstart += ac->ac_b_ex.fe_len;
			pa->pa_lstart += ac->ac_b_ex.fe_len;
			pa->pa_free -= ac->ac_b_ex.fe_len;
			pa->pa_len -= ac->ac_b_ex.fe_len;
			spin_unlock(&pa->pa_lock);
			/*
			 * We want to add the pa to the right bucket.
			 * Remove it from the list and while adding
			 * make sure the list to which we are adding
			 * doesn't grow big.
			 */
			if (likely(pa->pa_free)) {
				spin_lock(pa->pa_obj_lock);
				list_del_rcu(&pa->pa_inode_list);
				spin_unlock(pa->pa_obj_lock);
				ext4_mb_add_n_trim(ac);
			}
		}
		ext4_mb_put_pa(ac, ac->ac_sb, pa);
	}
	if (ac->ac_bitmap_page)
		page_cache_release(ac->ac_bitmap_page);
	if (ac->ac_buddy_page)
		page_cache_release(ac->ac_buddy_page);
	if (ac->ac_flags & EXT4_MB_HINT_GROUP_ALLOC)
		mutex_unlock(&ac->ac_lg->lg_mutex);
	ext4_mb_collect_stats(ac);
	return 0;
}

static int ext4_mb_discard_preallocations(struct super_block *sb, int needed)
{
	ext4_group_t i;
	int ret;
	int freed = 0;

	for (i = 0; i < EXT4_SB(sb)->s_groups_count && needed > 0; i++) {
		ret = ext4_mb_discard_group_preallocations(sb, i, needed);
		freed += ret;
		needed -= ret;
	}

	return freed;
}

/*
 * Main entry point into mballoc to allocate blocks
 * it tries to use preallocation first, then falls back
 * to usual allocation
 */
ext4_fsblk_t ext4_mb_new_blocks(handle_t *handle,
				 struct ext4_allocation_request *ar, int *errp)
{
	struct ext4_allocation_context *ac = NULL;
	struct ext4_sb_info *sbi;
	struct super_block *sb;
	ext4_fsblk_t block = 0;
	int freed;
	int inquota;

	sb = ar->inode->i_sb;
	sbi = EXT4_SB(sb);

	if (!test_opt(sb, MBALLOC)) {
		block = ext4_old_new_blocks(handle, ar->inode, ar->goal,
					    &(ar->len), errp);
		return block;
	}
	if (!EXT4_I(ar->inode)->i_delalloc_reserved_flag) {
		/*
		 * With delalloc we already reserved the blocks
		 */
		if (ext4_claim_free_blocks(sbi, ar->len)) {
			*errp = -ENOSPC;
			return 0;
		}
	}
	while (ar->len && DQUOT_ALLOC_BLOCK(ar->inode, ar->len)) {
		ar->flags |= EXT4_MB_HINT_NOPREALLOC;
		ar->len--;
	}
	if (ar->len == 0) {
		*errp = -EDQUOT;
		return 0;
	}
	inquota = ar->len;

	if (EXT4_I(ar->inode)->i_delalloc_reserved_flag)
		ar->flags |= EXT4_MB_DELALLOC_RESERVED;

	ac = kmem_cache_alloc(ext4_ac_cachep, GFP_NOFS);
	if (!ac) {
		ar->len = 0;
		*errp = -ENOMEM;
		goto out1;
	}

	ext4_mb_poll_new_transaction(sb, handle);

	*errp = ext4_mb_initialize_context(ac, ar);
	if (*errp) {
		ar->len = 0;
		goto out2;
	}

	ac->ac_op = EXT4_MB_HISTORY_PREALLOC;
	if (!ext4_mb_use_preallocated(ac)) {
		ac->ac_op = EXT4_MB_HISTORY_ALLOC;
		ext4_mb_normalize_request(ac, ar);
repeat:
		/* allocate space in core */
		ext4_mb_regular_allocator(ac);

		/* as we've just preallocated more space than
		 * user requested orinally, we store allocated
		 * space in a special descriptor */
		if (ac->ac_status == AC_STATUS_FOUND &&
				ac->ac_o_ex.fe_len < ac->ac_b_ex.fe_len)
			ext4_mb_new_preallocation(ac);
	}

	if (likely(ac->ac_status == AC_STATUS_FOUND)) {
		*errp = ext4_mb_mark_diskspace_used(ac, handle);
		if (*errp ==  -EAGAIN) {
			ac->ac_b_ex.fe_group = 0;
			ac->ac_b_ex.fe_start = 0;
			ac->ac_b_ex.fe_len = 0;
			ac->ac_status = AC_STATUS_CONTINUE;
			goto repeat;
		} else if (*errp) {
			ac->ac_b_ex.fe_len = 0;
			ar->len = 0;
			ext4_mb_show_ac(ac);
		} else {
			block = ext4_grp_offs_to_block(sb, &ac->ac_b_ex);
			ar->len = ac->ac_b_ex.fe_len;
		}
	} else {
		freed  = ext4_mb_discard_preallocations(sb, ac->ac_o_ex.fe_len);
		if (freed)
			goto repeat;
		*errp = -ENOSPC;
		ac->ac_b_ex.fe_len = 0;
		ar->len = 0;
		ext4_mb_show_ac(ac);
	}

	ext4_mb_release_context(ac);

out2:
	kmem_cache_free(ext4_ac_cachep, ac);
out1:
	if (ar->len < inquota)
		DQUOT_FREE_BLOCK(ar->inode, inquota - ar->len);

	return block;
}
static void ext4_mb_poll_new_transaction(struct super_block *sb,
						handle_t *handle)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	if (sbi->s_last_transaction == handle->h_transaction->t_tid)
		return;

	/* new transaction! time to close last one and free blocks for
	 * committed transaction. we know that only transaction can be
	 * active, so previos transaction can be being logged and we
	 * know that transaction before previous is known to be already
	 * logged. this means that now we may free blocks freed in all
	 * transactions before previous one. hope I'm clear enough ... */

	spin_lock(&sbi->s_md_lock);
	if (sbi->s_last_transaction != handle->h_transaction->t_tid) {
		mb_debug("new transaction %lu, old %lu\n",
				(unsigned long) handle->h_transaction->t_tid,
				(unsigned long) sbi->s_last_transaction);
		list_splice_init(&sbi->s_closed_transaction,
				&sbi->s_committed_transaction);
		list_splice_init(&sbi->s_active_transaction,
				&sbi->s_closed_transaction);
		sbi->s_last_transaction = handle->h_transaction->t_tid;
	}
	spin_unlock(&sbi->s_md_lock);

	ext4_mb_free_committed_blocks(sb);
}

static noinline_for_stack int
ext4_mb_free_metadata(handle_t *handle, struct ext4_buddy *e4b,
			  ext4_group_t group, ext4_grpblk_t block, int count)
{
	struct ext4_group_info *db = e4b->bd_info;
	struct super_block *sb = e4b->bd_sb;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_free_metadata *md;
	int i;

	BUG_ON(e4b->bd_bitmap_page == NULL);
	BUG_ON(e4b->bd_buddy_page == NULL);

	ext4_lock_group(sb, group);
	for (i = 0; i < count; i++) {
		md = db->bb_md_cur;
		if (md && db->bb_tid != handle->h_transaction->t_tid) {
			db->bb_md_cur = NULL;
			md = NULL;
		}

		if (md == NULL) {
			ext4_unlock_group(sb, group);
			md = kmalloc(sizeof(*md), GFP_NOFS);
			if (md == NULL)
				return -ENOMEM;
			md->num = 0;
			md->group = group;

			ext4_lock_group(sb, group);
			if (db->bb_md_cur == NULL) {
				spin_lock(&sbi->s_md_lock);
				list_add(&md->list, &sbi->s_active_transaction);
				spin_unlock(&sbi->s_md_lock);
				/* protect buddy cache from being freed,
				 * otherwise we'll refresh it from
				 * on-disk bitmap and lose not-yet-available
				 * blocks */
				page_cache_get(e4b->bd_buddy_page);
				page_cache_get(e4b->bd_bitmap_page);
				db->bb_md_cur = md;
				db->bb_tid = handle->h_transaction->t_tid;
				mb_debug("new md 0x%p for group %lu\n",
						md, md->group);
			} else {
				kfree(md);
				md = db->bb_md_cur;
			}
		}

		BUG_ON(md->num >= EXT4_BB_MAX_BLOCKS);
		md->blocks[md->num] = block + i;
		md->num++;
		if (md->num == EXT4_BB_MAX_BLOCKS) {
			/* no more space, put full container on a sb's list */
			db->bb_md_cur = NULL;
		}
	}
	ext4_unlock_group(sb, group);
	return 0;
}

/*
 * Main entry point into mballoc to free blocks
 */
void ext4_mb_free_blocks(handle_t *handle, struct inode *inode,
			unsigned long block, unsigned long count,
			int metadata, unsigned long *freed)
{
	struct buffer_head *bitmap_bh = NULL;
	struct super_block *sb = inode->i_sb;
	struct ext4_allocation_context *ac = NULL;
	struct ext4_group_desc *gdp;
	struct ext4_super_block *es;
	unsigned long overflow;
	ext4_grpblk_t bit;
	struct buffer_head *gd_bh;
	ext4_group_t block_group;
	struct ext4_sb_info *sbi;
	struct ext4_buddy e4b;
	int err = 0;
	int ret;

	*freed = 0;

	ext4_mb_poll_new_transaction(sb, handle);

	sbi = EXT4_SB(sb);
	es = EXT4_SB(sb)->s_es;
	if (block < le32_to_cpu(es->s_first_data_block) ||
	    block + count < block ||
	    block + count > ext4_blocks_count(es)) {
		ext4_error(sb, __func__,
			    "Freeing blocks not in datazone - "
			    "block = %lu, count = %lu", block, count);
		goto error_return;
	}

	ext4_debug("freeing block %lu\n", block);

	ac = kmem_cache_alloc(ext4_ac_cachep, GFP_NOFS);
	if (ac) {
		ac->ac_op = EXT4_MB_HISTORY_FREE;
		ac->ac_inode = inode;
		ac->ac_sb = sb;
	}

do_more:
	overflow = 0;
	ext4_get_group_no_and_offset(sb, block, &block_group, &bit);

	/*
	 * Check to see if we are freeing blocks across a group
	 * boundary.
	 */
	if (bit + count > EXT4_BLOCKS_PER_GROUP(sb)) {
		overflow = bit + count - EXT4_BLOCKS_PER_GROUP(sb);
		count -= overflow;
	}
	bitmap_bh = ext4_read_block_bitmap(sb, block_group);
	if (!bitmap_bh) {
		err = -EIO;
		goto error_return;
	}
	gdp = ext4_get_group_desc(sb, block_group, &gd_bh);
	if (!gdp) {
		err = -EIO;
		goto error_return;
	}

	if (in_range(ext4_block_bitmap(sb, gdp), block, count) ||
	    in_range(ext4_inode_bitmap(sb, gdp), block, count) ||
	    in_range(block, ext4_inode_table(sb, gdp),
		      EXT4_SB(sb)->s_itb_per_group) ||
	    in_range(block + count - 1, ext4_inode_table(sb, gdp),
		      EXT4_SB(sb)->s_itb_per_group)) {

		ext4_error(sb, __func__,
			   "Freeing blocks in system zone - "
			   "Block = %lu, count = %lu", block, count);
		/* err = 0. ext4_std_error should be a no op */
		goto error_return;
	}

	BUFFER_TRACE(bitmap_bh, "getting write access");
	err = ext4_journal_get_write_access(handle, bitmap_bh);
	if (err)
		goto error_return;

	/*
	 * We are about to modify some metadata.  Call the journal APIs
	 * to unshare ->b_data if a currently-committing transaction is
	 * using it
	 */
	BUFFER_TRACE(gd_bh, "get_write_access");
	err = ext4_journal_get_write_access(handle, gd_bh);
	if (err)
		goto error_return;

	err = ext4_mb_load_buddy(sb, block_group, &e4b);
	if (err)
		goto error_return;

#ifdef AGGRESSIVE_CHECK
	{
		int i;
		for (i = 0; i < count; i++)
			BUG_ON(!mb_test_bit(bit + i, bitmap_bh->b_data));
	}
#endif
	mb_clear_bits(sb_bgl_lock(sbi, block_group), bitmap_bh->b_data,
			bit, count);

	/* We dirtied the bitmap block */
	BUFFER_TRACE(bitmap_bh, "dirtied bitmap block");
	err = ext4_journal_dirty_metadata(handle, bitmap_bh);

	if (ac) {
		ac->ac_b_ex.fe_group = block_group;
		ac->ac_b_ex.fe_start = bit;
		ac->ac_b_ex.fe_len = count;
		ext4_mb_store_history(ac);
	}

	if (metadata) {
		/* blocks being freed are metadata. these blocks shouldn't
		 * be used until this transaction is committed */
		ext4_mb_free_metadata(handle, &e4b, block_group, bit, count);
	} else {
		ext4_lock_group(sb, block_group);
		mb_free_blocks(inode, &e4b, bit, count);
		ext4_mb_return_to_preallocation(inode, &e4b, block, count);
		ext4_unlock_group(sb, block_group);
	}

	spin_lock(sb_bgl_lock(sbi, block_group));
	le16_add_cpu(&gdp->bg_free_blocks_count, count);
	gdp->bg_checksum = ext4_group_desc_csum(sbi, block_group, gdp);
	spin_unlock(sb_bgl_lock(sbi, block_group));
	percpu_counter_add(&sbi->s_freeblocks_counter, count);

	if (sbi->s_log_groups_per_flex) {
		ext4_group_t flex_group = ext4_flex_group(sbi, block_group);
		spin_lock(sb_bgl_lock(sbi, flex_group));
		sbi->s_flex_groups[flex_group].free_blocks += count;
		spin_unlock(sb_bgl_lock(sbi, flex_group));
	}

	ext4_mb_release_desc(&e4b);

	*freed += count;

	/* And the group descriptor block */
	BUFFER_TRACE(gd_bh, "dirtied group descriptor block");
	ret = ext4_journal_dirty_metadata(handle, gd_bh);
	if (!err)
		err = ret;

	if (overflow && !err) {
		block += count;
		count = overflow;
		put_bh(bitmap_bh);
		goto do_more;
	}
	sb->s_dirt = 1;
error_return:
	brelse(bitmap_bh);
	ext4_std_error(sb, err);
	if (ac)
		kmem_cache_free(ext4_ac_cachep, ac);
	return;
}
