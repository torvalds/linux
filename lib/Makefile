#
# Makefile for some libs needed in the kernel.
#

ifdef CONFIG_FUNCTION_TRACER
ORIG_CFLAGS := $(KBUILD_CFLAGS)
KBUILD_CFLAGS = $(subst -pg,,$(ORIG_CFLAGS))
endif

lib-y := ctype.o string.o vsprintf.o cmdline.o \
	 rbtree.o radix-tree.o dump_stack.o timerqueue.o\
	 idr.o int_sqrt.o extable.o prio_tree.o \
	 sha1.o md5.o irq_regs.o reciprocal_div.o argv_split.o \
	 proportions.o prio_heap.o ratelimit.o show_mem.o \
	 is_single_threaded.o plist.o decompress.o

lib-$(CONFIG_MMU) += ioremap.o
lib-$(CONFIG_SMP) += cpumask.o

lib-y	+= kobject.o klist.o

obj-y += bcd.o div64.o sort.o parser.o halfmd4.o debug_locks.o random32.o \
	 bust_spinlocks.o hexdump.o kasprintf.o bitmap.o scatterlist.o \
	 string_helpers.o gcd.o lcm.o list_sort.o uuid.o flex_array.o \
	 bsearch.o find_last_bit.o find_next_bit.o llist.o
obj-y += kstrtox.o
obj-$(CONFIG_TEST_KSTRTOX) += test-kstrtox.o

ifeq ($(CONFIG_DEBUG_KOBJECT),y)
CFLAGS_kobject.o += -DDEBUG
CFLAGS_kobject_uevent.o += -DDEBUG
endif

lib-$(CONFIG_HOTPLUG) += kobject_uevent.o
obj-$(CONFIG_GENERIC_IOMAP) += iomap.o
obj-$(CONFIG_GENERIC_PCI_IOMAP) += pci_iomap.o
obj-$(CONFIG_HAS_IOMEM) += iomap_copy.o devres.o
obj-$(CONFIG_CHECK_SIGNATURE) += check_signature.o
obj-$(CONFIG_DEBUG_LOCKING_API_SELFTESTS) += locking-selftest.o
obj-$(CONFIG_DEBUG_SPINLOCK) += spinlock_debug.o
lib-$(CONFIG_RWSEM_GENERIC_SPINLOCK) += rwsem-spinlock.o
lib-$(CONFIG_RWSEM_XCHGADD_ALGORITHM) += rwsem.o

CFLAGS_hweight.o = $(subst $(quote),,$(CONFIG_ARCH_HWEIGHT_CFLAGS))
obj-$(CONFIG_GENERIC_HWEIGHT) += hweight.o

obj-$(CONFIG_BTREE) += btree.o
obj-$(CONFIG_DEBUG_PREEMPT) += smp_processor_id.o
obj-$(CONFIG_DEBUG_LIST) += list_debug.o
obj-$(CONFIG_DEBUG_OBJECTS) += debugobjects.o

ifneq ($(CONFIG_HAVE_DEC_LOCK),y)
  lib-y += dec_and_lock.o
endif

obj-$(CONFIG_BITREVERSE) += bitrev.o
obj-$(CONFIG_RATIONAL)	+= rational.o
obj-$(CONFIG_CRC_CCITT)	+= crc-ccitt.o
obj-$(CONFIG_CRC16)	+= crc16.o
obj-$(CONFIG_CRC_T10DIF)+= crc-t10dif.o
obj-$(CONFIG_CRC_ITU_T)	+= crc-itu-t.o
obj-$(CONFIG_CRC32)	+= crc32.o
obj-$(CONFIG_CRC7)	+= crc7.o
obj-$(CONFIG_LIBCRC32C)	+= libcrc32c.o
obj-$(CONFIG_CRC8)	+= crc8.o
obj-$(CONFIG_GENERIC_ALLOCATOR) += genalloc.o

obj-$(CONFIG_ZLIB_INFLATE) += zlib_inflate/
obj-$(CONFIG_ZLIB_DEFLATE) += zlib_deflate/
obj-$(CONFIG_REED_SOLOMON) += reed_solomon/
obj-$(CONFIG_BCH) += bch.o
obj-$(CONFIG_LZO_COMPRESS) += lzo/
obj-$(CONFIG_LZO_DECOMPRESS) += lzo/
obj-$(CONFIG_XZ_DEC) += xz/
obj-$(CONFIG_RAID6_PQ) += raid6/

lib-$(CONFIG_DECOMPRESS_GZIP) += decompress_inflate.o
lib-$(CONFIG_DECOMPRESS_BZIP2) += decompress_bunzip2.o
lib-$(CONFIG_DECOMPRESS_LZMA) += decompress_unlzma.o
lib-$(CONFIG_DECOMPRESS_XZ) += decompress_unxz.o
lib-$(CONFIG_DECOMPRESS_LZO) += decompress_unlzo.o

obj-$(CONFIG_TEXTSEARCH) += textsearch.o
obj-$(CONFIG_TEXTSEARCH_KMP) += ts_kmp.o
obj-$(CONFIG_TEXTSEARCH_BM) += ts_bm.o
obj-$(CONFIG_TEXTSEARCH_FSM) += ts_fsm.o
obj-$(CONFIG_SMP) += percpu_counter.o
obj-$(CONFIG_AUDIT_GENERIC) += audit.o

obj-$(CONFIG_SWIOTLB) += swiotlb.o
obj-$(CONFIG_IOMMU_HELPER) += iommu-helper.o
obj-$(CONFIG_FAULT_INJECTION) += fault-inject.o
obj-$(CONFIG_CPU_NOTIFIER_ERROR_INJECT) += cpu-notifier-error-inject.o

lib-$(CONFIG_GENERIC_BUG) += bug.o

obj-$(CONFIG_HAVE_ARCH_TRACEHOOK) += syscall.o

obj-$(CONFIG_DYNAMIC_DEBUG) += dynamic_debug.o

obj-$(CONFIG_NLATTR) += nlattr.o

obj-$(CONFIG_LRU_CACHE) += lru_cache.o

obj-$(CONFIG_DMA_API_DEBUG) += dma-debug.o

obj-$(CONFIG_GENERIC_CSUM) += checksum.o

obj-$(CONFIG_GENERIC_ATOMIC64) += atomic64.o

obj-$(CONFIG_ATOMIC64_SELFTEST) += atomic64_test.o

obj-$(CONFIG_AVERAGE) += average.o

obj-$(CONFIG_CPU_RMAP) += cpu_rmap.o

obj-$(CONFIG_CORDIC) += cordic.o

obj-$(CONFIG_DQL) += dynamic_queue_limits.o

obj-$(CONFIG_MPILIB) += mpi/
obj-$(CONFIG_SIGNATURE) += digsig.o

obj-$(CONFIG_CLZ_TAB) += clz_tab.o

obj-$(CONFIG_GENERIC_STRNCPY_FROM_USER) += strncpy_from_user.o

hostprogs-y	:= gen_crc32table
clean-files	:= crc32table.h

$(obj)/crc32.o: $(obj)/crc32table.h

quiet_cmd_crc32 = GEN     $@
      cmd_crc32 = $< > $@

$(obj)/crc32table.h: $(obj)/gen_crc32table
	$(call cmd,crc32)
