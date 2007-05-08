#
# Makefile for the linux memory manager.
#

mmu-y			:= nommu.o
mmu-$(CONFIG_MMU)	:= fremap.o highmem.o madvise.o memory.o mincore.o \
			   mlock.o mmap.o mprotect.o mremap.o msync.o rmap.o \
			   vmalloc.o

obj-y			:= bootmem.o filemap.o mempool.o oom_kill.o fadvise.o \
			   page_alloc.o page-writeback.o pdflush.o \
			   readahead.o swap.o truncate.o vmscan.o \
			   prio_tree.o util.o mmzone.o vmstat.o backing-dev.o \
			   $(mmu-y)

ifeq ($(CONFIG_MMU)$(CONFIG_BLOCK),yy)
obj-y			+= bounce.o
endif
obj-$(CONFIG_SWAP)	+= page_io.o swap_state.o swapfile.o thrash.o
obj-$(CONFIG_HUGETLBFS)	+= hugetlb.o
obj-$(CONFIG_NUMA) 	+= mempolicy.o
obj-$(CONFIG_SPARSEMEM)	+= sparse.o
obj-$(CONFIG_SHMEM) += shmem.o
obj-$(CONFIG_TMPFS_POSIX_ACL) += shmem_acl.o
obj-$(CONFIG_TINY_SHMEM) += tiny-shmem.o
obj-$(CONFIG_SLOB) += slob.o
obj-$(CONFIG_SLAB) += slab.o
obj-$(CONFIG_SLUB) += slub.o
obj-$(CONFIG_MEMORY_HOTPLUG) += memory_hotplug.o
obj-$(CONFIG_FS_XIP) += filemap_xip.o
obj-$(CONFIG_MIGRATION) += migrate.o
obj-$(CONFIG_SMP) += allocpercpu.o
obj-$(CONFIG_QUICKLIST) += quicklist.o

