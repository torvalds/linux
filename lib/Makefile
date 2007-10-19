#
# Makefile for some libs needed in the kernel.
#

lib-y := ctype.o string.o vsprintf.o cmdline.o \
	 rbtree.o radix-tree.o dump_stack.o \
	 idr.o int_sqrt.o bitmap.o extable.o prio_tree.o \
	 sha1.o irq_regs.o reciprocal_div.o argv_split.o \
	 proportions.o prio_heap.o

lib-$(CONFIG_MMU) += ioremap.o
lib-$(CONFIG_SMP) += cpumask.o

lib-y	+= kobject.o kref.o klist.o

obj-y += div64.o sort.o parser.o halfmd4.o debug_locks.o random32.o \
	 bust_spinlocks.o hexdump.o kasprintf.o

ifeq ($(CONFIG_DEBUG_KOBJECT),y)
CFLAGS_kobject.o += -DDEBUG
CFLAGS_kobject_uevent.o += -DDEBUG
endif

lib-$(CONFIG_HOTPLUG) += kobject_uevent.o
obj-$(CONFIG_GENERIC_IOMAP) += iomap.o
obj-$(CONFIG_HAS_IOMEM) += iomap_copy.o devres.o
obj-$(CONFIG_CHECK_SIGNATURE) += check_signature.o
obj-$(CONFIG_DEBUG_LOCKING_API_SELFTESTS) += locking-selftest.o
obj-$(CONFIG_DEBUG_SPINLOCK) += spinlock_debug.o
lib-$(CONFIG_RWSEM_GENERIC_SPINLOCK) += rwsem-spinlock.o
lib-$(CONFIG_RWSEM_XCHGADD_ALGORITHM) += rwsem.o
lib-$(CONFIG_SEMAPHORE_SLEEPERS) += semaphore-sleepers.o
lib-$(CONFIG_GENERIC_FIND_NEXT_BIT) += find_next_bit.o
obj-$(CONFIG_GENERIC_HWEIGHT) += hweight.o
obj-$(CONFIG_LOCK_KERNEL) += kernel_lock.o
obj-$(CONFIG_PLIST) += plist.o
obj-$(CONFIG_DEBUG_PREEMPT) += smp_processor_id.o
obj-$(CONFIG_DEBUG_LIST) += list_debug.o

ifneq ($(CONFIG_HAVE_DEC_LOCK),y)
  lib-y += dec_and_lock.o
endif

obj-$(CONFIG_BITREVERSE) += bitrev.o
obj-$(CONFIG_CRC_CCITT)	+= crc-ccitt.o
obj-$(CONFIG_CRC16)	+= crc16.o
obj-$(CONFIG_CRC_ITU_T)	+= crc-itu-t.o
obj-$(CONFIG_CRC32)	+= crc32.o
obj-$(CONFIG_CRC7)	+= crc7.o
obj-$(CONFIG_LIBCRC32C)	+= libcrc32c.o
obj-$(CONFIG_GENERIC_ALLOCATOR) += genalloc.o

obj-$(CONFIG_ZLIB_INFLATE) += zlib_inflate/
obj-$(CONFIG_ZLIB_DEFLATE) += zlib_deflate/
obj-$(CONFIG_REED_SOLOMON) += reed_solomon/
obj-$(CONFIG_LZO_COMPRESS) += lzo/
obj-$(CONFIG_LZO_DECOMPRESS) += lzo/

obj-$(CONFIG_TEXTSEARCH) += textsearch.o
obj-$(CONFIG_TEXTSEARCH_KMP) += ts_kmp.o
obj-$(CONFIG_TEXTSEARCH_BM) += ts_bm.o
obj-$(CONFIG_TEXTSEARCH_FSM) += ts_fsm.o
obj-$(CONFIG_SMP) += percpu_counter.o
obj-$(CONFIG_AUDIT_GENERIC) += audit.o

obj-$(CONFIG_SWIOTLB) += swiotlb.o
obj-$(CONFIG_FAULT_INJECTION) += fault-inject.o

lib-$(CONFIG_GENERIC_BUG) += bug.o

hostprogs-y	:= gen_crc32table
clean-files	:= crc32table.h

$(obj)/crc32.o: $(obj)/crc32table.h

quiet_cmd_crc32 = GEN     $@
      cmd_crc32 = $< > $@

$(obj)/crc32table.h: $(obj)/gen_crc32table
	$(call cmd,crc32)
