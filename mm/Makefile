#
# Makefile for the linux memory manager.
#

KASAN_SANITIZE_slab_common.o := n
KASAN_SANITIZE_slab.o := n
KASAN_SANITIZE_slub.o := n

# These files are disabled because they produce non-interesting and/or
# flaky coverage that is not a function of syscall inputs. E.g. slab is out of
# free pages, or a task is migrated between nodes.
KCOV_INSTRUMENT_slab_common.o := n
KCOV_INSTRUMENT_slob.o := n
KCOV_INSTRUMENT_slab.o := n
KCOV_INSTRUMENT_slub.o := n
KCOV_INSTRUMENT_page_alloc.o := n
KCOV_INSTRUMENT_debug-pagealloc.o := n
KCOV_INSTRUMENT_kmemleak.o := n
KCOV_INSTRUMENT_kmemcheck.o := n
KCOV_INSTRUMENT_memcontrol.o := n
KCOV_INSTRUMENT_mmzone.o := n
KCOV_INSTRUMENT_vmstat.o := n

mmu-y			:= nommu.o
mmu-$(CONFIG_MMU)	:= gup.o highmem.o memory.o mincore.o \
			   mlock.o mmap.o mprotect.o mremap.o msync.o \
			   page_vma_mapped.o pagewalk.o pgtable-generic.o \
			   rmap.o vmalloc.o


ifdef CONFIG_CROSS_MEMORY_ATTACH
mmu-$(CONFIG_MMU)	+= process_vm_access.o
endif

obj-y			:= filemap.o mempool.o oom_kill.o \
			   maccess.o page_alloc.o page-writeback.o \
			   readahead.o swap.o truncate.o vmscan.o shmem.o \
			   util.o mmzone.o vmstat.o backing-dev.o \
			   mm_init.o mmu_context.o percpu.o slab_common.o \
			   compaction.o vmacache.o swap_slots.o \
			   interval_tree.o list_lru.o workingset.o \
			   debug.o $(mmu-y)

obj-y += init-mm.o

ifdef CONFIG_NO_BOOTMEM
	obj-y		+= nobootmem.o
else
	obj-y		+= bootmem.o
endif

obj-$(CONFIG_ADVISE_SYSCALLS)	+= fadvise.o
ifdef CONFIG_MMU
	obj-$(CONFIG_ADVISE_SYSCALLS)	+= madvise.o
endif
obj-$(CONFIG_HAVE_MEMBLOCK) += memblock.o

obj-$(CONFIG_SWAP)	+= page_io.o swap_state.o swapfile.o
obj-$(CONFIG_FRONTSWAP)	+= frontswap.o
obj-$(CONFIG_ZSWAP)	+= zswap.o
obj-$(CONFIG_HAS_DMA)	+= dmapool.o
obj-$(CONFIG_HUGETLBFS)	+= hugetlb.o
obj-$(CONFIG_NUMA) 	+= mempolicy.o
obj-$(CONFIG_SPARSEMEM)	+= sparse.o
obj-$(CONFIG_SPARSEMEM_VMEMMAP) += sparse-vmemmap.o
obj-$(CONFIG_SLOB) += slob.o
obj-$(CONFIG_MMU_NOTIFIER) += mmu_notifier.o
obj-$(CONFIG_KSM) += ksm.o
obj-$(CONFIG_PAGE_POISONING) += page_poison.o
obj-$(CONFIG_SLAB) += slab.o
obj-$(CONFIG_SLUB) += slub.o
obj-$(CONFIG_KMEMCHECK) += kmemcheck.o
obj-$(CONFIG_KASAN)	+= kasan/
obj-$(CONFIG_FAILSLAB) += failslab.o
obj-$(CONFIG_MEMORY_HOTPLUG) += memory_hotplug.o
obj-$(CONFIG_MEMTEST)		+= memtest.o
obj-$(CONFIG_MIGRATION) += migrate.o
obj-$(CONFIG_QUICKLIST) += quicklist.o
obj-$(CONFIG_TRANSPARENT_HUGEPAGE) += huge_memory.o khugepaged.o
obj-$(CONFIG_PAGE_COUNTER) += page_counter.o
obj-$(CONFIG_MEMCG) += memcontrol.o vmpressure.o
obj-$(CONFIG_MEMCG_SWAP) += swap_cgroup.o
obj-$(CONFIG_CGROUP_HUGETLB) += hugetlb_cgroup.o
obj-$(CONFIG_MEMORY_FAILURE) += memory-failure.o
obj-$(CONFIG_HWPOISON_INJECT) += hwpoison-inject.o
obj-$(CONFIG_DEBUG_KMEMLEAK) += kmemleak.o
obj-$(CONFIG_DEBUG_KMEMLEAK_TEST) += kmemleak-test.o
obj-$(CONFIG_DEBUG_RODATA_TEST) += rodata_test.o
obj-$(CONFIG_PAGE_OWNER) += page_owner.o
obj-$(CONFIG_CLEANCACHE) += cleancache.o
obj-$(CONFIG_MEMORY_ISOLATION) += page_isolation.o
obj-$(CONFIG_ZPOOL)	+= zpool.o
obj-$(CONFIG_ZBUD)	+= zbud.o
obj-$(CONFIG_ZSMALLOC)	+= zsmalloc.o
obj-$(CONFIG_Z3FOLD)	+= z3fold.o
obj-$(CONFIG_GENERIC_EARLY_IOREMAP) += early_ioremap.o
obj-$(CONFIG_CMA)	+= cma.o
obj-$(CONFIG_MEMORY_BALLOON) += balloon_compaction.o
obj-$(CONFIG_PAGE_EXTENSION) += page_ext.o
obj-$(CONFIG_CMA_DEBUGFS) += cma_debug.o
obj-$(CONFIG_USERFAULTFD) += userfaultfd.o
obj-$(CONFIG_IDLE_PAGE_TRACKING) += page_idle.o
obj-$(CONFIG_FRAME_VECTOR) += frame_vector.o
obj-$(CONFIG_DEBUG_PAGE_REF) += debug_page_ref.o
obj-$(CONFIG_HARDENED_USERCOPY) += usercopy.o
