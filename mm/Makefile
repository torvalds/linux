#
# Makefile for the linux memory manager.
#

mmu-y			:= nommu.o
mmu-$(CONFIG_MMU)	:= fremap.o highmem.o madvise.o memory.o mincore.o \
			   mlock.o mmap.o mprotect.o mremap.o msync.o rmap.o \
			   vmalloc.o pagewalk.o pgtable-generic.o

obj-y			:= filemap.o mempool.o oom_kill.o fadvise.o \
			   maccess.o page-writeback.o \
			   readahead.o swap.o truncate.o vmscan.o shmem.o \
			   prio_tree.o util.o mmzone.o vmstat.o backing-dev.o \
			   mm_init.o mmu_context.o percpu.o \
			   $(mmu-y)

ifdef CONFIG_SLP
	obj-y		+= page_alloc-slp.o page_isolation-slp.o
else
ifdef CONFIG_DMA_CMA
	obj-y		+= page_alloc-cma.o page_isolation-cma.o
else
	obj-y		+= page_alloc.o page_isolation.o
endif
endif

obj-y += init-mm.o

ifdef CONFIG_NO_BOOTMEM
	obj-y		+= nobootmem.o
else
	obj-y		+= bootmem.o
endif

obj-$(CONFIG_HAVE_MEMBLOCK) += memblock.o

obj-$(CONFIG_BOUNCE)	+= bounce.o
obj-$(CONFIG_SWAP)	+= page_io.o swap_state.o swapfile.o thrash.o
obj-$(CONFIG_HAS_DMA)	+= dmapool.o
obj-$(CONFIG_HUGETLBFS)	+= hugetlb.o
obj-$(CONFIG_NUMA) 	+= mempolicy.o
obj-$(CONFIG_SPARSEMEM)	+= sparse.o
obj-$(CONFIG_SPARSEMEM_VMEMMAP) += sparse-vmemmap.o
obj-$(CONFIG_ASHMEM) += ashmem.o
obj-$(CONFIG_SLOB) += slob.o
ifdef CONFIG_SLP
obj-$(CONFIG_COMPACTION) += compaction-slp.o
else
ifdef CONFIG_DMA_CMA
obj-$(CONFIG_COMPACTION) += compaction-cma.o
else
obj-$(CONFIG_COMPACTION) += compaction.o
endif
endif
obj-$(CONFIG_MMU_NOTIFIER) += mmu_notifier.o
obj-$(CONFIG_KSM) += ksm.o
obj-$(CONFIG_PAGE_POISONING) += debug-pagealloc.o
obj-$(CONFIG_SLAB) += slab.o
obj-$(CONFIG_SLUB) += slub.o
obj-$(CONFIG_SLQB) += slqb.o
obj-$(CONFIG_KMEMCHECK) += kmemcheck.o
obj-$(CONFIG_FAILSLAB) += failslab.o
ifdef CONFIG_SLP
obj-$(CONFIG_MEMORY_HOTPLUG) += memory_hotplug-slp.o
else
obj-$(CONFIG_MEMORY_HOTPLUG) += memory_hotplug.o
endif
obj-$(CONFIG_FS_XIP) += filemap_xip.o
ifdef CONFIG_DMA_CMA
obj-$(CONFIG_MIGRATION) += migrate-cma.o
else
obj-$(CONFIG_MIGRATION) += migrate.o
endif
obj-$(CONFIG_QUICKLIST) += quicklist.o
obj-$(CONFIG_TRANSPARENT_HUGEPAGE) += huge_memory.o
obj-$(CONFIG_CGROUP_MEM_RES_CTLR) += memcontrol.o page_cgroup.o
obj-$(CONFIG_MEMORY_FAILURE) += memory-failure.o
obj-$(CONFIG_HWPOISON_INJECT) += hwpoison-inject.o
obj-$(CONFIG_DEBUG_KMEMLEAK) += kmemleak.o
obj-$(CONFIG_DEBUG_KMEMLEAK_TEST) += kmemleak-test.o
obj-$(CONFIG_CLEANCACHE) += cleancache.o
obj-$(CONFIG_CMA) += cma.o
obj-$(CONFIG_CMA_BEST_FIT) += cma-best-fit.o
