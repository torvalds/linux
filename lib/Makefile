#
# Makefile for some libs needed in the kernel.
#

ifdef CONFIG_FUNCTION_TRACER
ORIG_CFLAGS := $(KBUILD_CFLAGS)
KBUILD_CFLAGS = $(subst $(CC_FLAGS_FTRACE),,$(ORIG_CFLAGS))
endif

# These files are disabled because they produce lots of non-interesting and/or
# flaky coverage that is not a function of syscall inputs. For example,
# rbtree can be global and individual rotations don't correlate with inputs.
KCOV_INSTRUMENT_string.o := n
KCOV_INSTRUMENT_rbtree.o := n
KCOV_INSTRUMENT_list_debug.o := n
KCOV_INSTRUMENT_debugobjects.o := n
KCOV_INSTRUMENT_dynamic_debug.o := n
# Kernel does not boot if we instrument this file as it uses custom calling
# convention (see CONFIG_ARCH_HWEIGHT_CFLAGS).
KCOV_INSTRUMENT_hweight.o := n

lib-y := ctype.o string.o vsprintf.o cmdline.o \
	 rbtree.o radix-tree.o dump_stack.o timerqueue.o\
	 idr.o int_sqrt.o extable.o \
	 sha1.o md5.o irq_regs.o argv_split.o \
	 flex_proportions.o ratelimit.o show_mem.o \
	 is_single_threaded.o plist.o decompress.o kobject_uevent.o \
	 earlycpio.o seq_buf.o nmi_backtrace.o nodemask.o

obj-$(CONFIG_ARCH_HAS_DEBUG_STRICT_USER_COPY_CHECKS) += usercopy.o
lib-$(CONFIG_MMU) += ioremap.o
lib-$(CONFIG_SMP) += cpumask.o
lib-$(CONFIG_HAS_DMA) += dma-noop.o

lib-y	+= kobject.o klist.o
obj-y	+= lockref.o

obj-y += bcd.o div64.o sort.o parser.o halfmd4.o debug_locks.o random32.o \
	 bust_spinlocks.o kasprintf.o bitmap.o scatterlist.o \
	 gcd.o lcm.o list_sort.o uuid.o flex_array.o iov_iter.o clz_ctz.o \
	 bsearch.o find_bit.o llist.o memweight.o kfifo.o \
	 percpu-refcount.o percpu_ida.o rhashtable.o reciprocal_div.o \
	 once.o
obj-y += string_helpers.o
obj-$(CONFIG_TEST_STRING_HELPERS) += test-string_helpers.o
obj-y += hexdump.o
obj-$(CONFIG_TEST_HEXDUMP) += test_hexdump.o
obj-y += kstrtox.o
obj-$(CONFIG_TEST_BPF) += test_bpf.o
obj-$(CONFIG_TEST_FIRMWARE) += test_firmware.o
obj-$(CONFIG_TEST_HASH) += test_hash.o
obj-$(CONFIG_TEST_KASAN) += test_kasan.o
obj-$(CONFIG_TEST_KSTRTOX) += test-kstrtox.o
obj-$(CONFIG_TEST_LKM) += test_module.o
obj-$(CONFIG_TEST_RHASHTABLE) += test_rhashtable.o
obj-$(CONFIG_TEST_USER_COPY) += test_user_copy.o
obj-$(CONFIG_TEST_STATIC_KEYS) += test_static_keys.o
obj-$(CONFIG_TEST_STATIC_KEYS) += test_static_key_base.o
obj-$(CONFIG_TEST_PRINTF) += test_printf.o
obj-$(CONFIG_TEST_BITMAP) += test_bitmap.o
obj-$(CONFIG_TEST_UUID) += test_uuid.o

ifeq ($(CONFIG_DEBUG_KOBJECT),y)
CFLAGS_kobject.o += -DDEBUG
CFLAGS_kobject_uevent.o += -DDEBUG
endif

obj-$(CONFIG_DEBUG_INFO_REDUCED) += debug_info.o
CFLAGS_debug_info.o += $(call cc-option, -femit-struct-debug-detailed=any)

obj-$(CONFIG_GENERIC_IOMAP) += iomap.o
obj-$(CONFIG_GENERIC_PCI_IOMAP) += pci_iomap.o
obj-$(CONFIG_HAS_IOMEM) += iomap_copy.o devres.o
obj-$(CONFIG_CHECK_SIGNATURE) += check_signature.o
obj-$(CONFIG_DEBUG_LOCKING_API_SELFTESTS) += locking-selftest.o

GCOV_PROFILE_hweight.o := n
CFLAGS_hweight.o = $(subst $(quote),,$(CONFIG_ARCH_HWEIGHT_CFLAGS))
obj-$(CONFIG_GENERIC_HWEIGHT) += hweight.o

obj-$(CONFIG_BTREE) += btree.o
obj-$(CONFIG_INTERVAL_TREE) += interval_tree.o
obj-$(CONFIG_ASSOCIATIVE_ARRAY) += assoc_array.o
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

obj-$(CONFIG_842_COMPRESS) += 842/
obj-$(CONFIG_842_DECOMPRESS) += 842/
obj-$(CONFIG_ZLIB_INFLATE) += zlib_inflate/
obj-$(CONFIG_ZLIB_DEFLATE) += zlib_deflate/
obj-$(CONFIG_REED_SOLOMON) += reed_solomon/
obj-$(CONFIG_BCH) += bch.o
obj-$(CONFIG_LZO_COMPRESS) += lzo/
obj-$(CONFIG_LZO_DECOMPRESS) += lzo/
obj-$(CONFIG_LZ4_COMPRESS) += lz4/
obj-$(CONFIG_LZ4HC_COMPRESS) += lz4/
obj-$(CONFIG_LZ4_DECOMPRESS) += lz4/
obj-$(CONFIG_XZ_DEC) += xz/
obj-$(CONFIG_RAID6_PQ) += raid6/

lib-$(CONFIG_DECOMPRESS_GZIP) += decompress_inflate.o
lib-$(CONFIG_DECOMPRESS_BZIP2) += decompress_bunzip2.o
lib-$(CONFIG_DECOMPRESS_LZMA) += decompress_unlzma.o
lib-$(CONFIG_DECOMPRESS_XZ) += decompress_unxz.o
lib-$(CONFIG_DECOMPRESS_LZO) += decompress_unlzo.o
lib-$(CONFIG_DECOMPRESS_LZ4) += decompress_unlz4.o

obj-$(CONFIG_TEXTSEARCH) += textsearch.o
obj-$(CONFIG_TEXTSEARCH_KMP) += ts_kmp.o
obj-$(CONFIG_TEXTSEARCH_BM) += ts_bm.o
obj-$(CONFIG_TEXTSEARCH_FSM) += ts_fsm.o
obj-$(CONFIG_SMP) += percpu_counter.o
obj-$(CONFIG_AUDIT_GENERIC) += audit.o
obj-$(CONFIG_AUDIT_COMPAT_GENERIC) += compat_audit.o

obj-$(CONFIG_SWIOTLB) += swiotlb.o
obj-$(CONFIG_IOMMU_HELPER) += iommu-helper.o iommu-common.o
obj-$(CONFIG_FAULT_INJECTION) += fault-inject.o
obj-$(CONFIG_NOTIFIER_ERROR_INJECTION) += notifier-error-inject.o
obj-$(CONFIG_CPU_NOTIFIER_ERROR_INJECT) += cpu-notifier-error-inject.o
obj-$(CONFIG_PM_NOTIFIER_ERROR_INJECT) += pm-notifier-error-inject.o
obj-$(CONFIG_NETDEV_NOTIFIER_ERROR_INJECT) += netdev-notifier-error-inject.o
obj-$(CONFIG_MEMORY_NOTIFIER_ERROR_INJECT) += memory-notifier-error-inject.o
obj-$(CONFIG_OF_RECONFIG_NOTIFIER_ERROR_INJECT) += \
	of-reconfig-notifier-error-inject.o

lib-$(CONFIG_GENERIC_BUG) += bug.o

obj-$(CONFIG_HAVE_ARCH_TRACEHOOK) += syscall.o

obj-$(CONFIG_DYNAMIC_DEBUG) += dynamic_debug.o

obj-$(CONFIG_NLATTR) += nlattr.o

obj-$(CONFIG_LRU_CACHE) += lru_cache.o

obj-$(CONFIG_DMA_API_DEBUG) += dma-debug.o

obj-$(CONFIG_GENERIC_CSUM) += checksum.o

obj-$(CONFIG_GENERIC_ATOMIC64) += atomic64.o

obj-$(CONFIG_ATOMIC64_SELFTEST) += atomic64_test.o

obj-$(CONFIG_CPU_RMAP) += cpu_rmap.o

obj-$(CONFIG_CORDIC) += cordic.o

obj-$(CONFIG_DQL) += dynamic_queue_limits.o

obj-$(CONFIG_GLOB) += glob.o

obj-$(CONFIG_MPILIB) += mpi/
obj-$(CONFIG_SIGNATURE) += digsig.o

lib-$(CONFIG_CLZ_TAB) += clz_tab.o

obj-$(CONFIG_DDR) += jedec_ddr_data.o

obj-$(CONFIG_GENERIC_STRNCPY_FROM_USER) += strncpy_from_user.o
obj-$(CONFIG_GENERIC_STRNLEN_USER) += strnlen_user.o

obj-$(CONFIG_GENERIC_NET_UTILS) += net_utils.o

obj-$(CONFIG_SG_SPLIT) += sg_split.o
obj-$(CONFIG_SG_POOL) += sg_pool.o
obj-$(CONFIG_STMP_DEVICE) += stmp_device.o
obj-$(CONFIG_IRQ_POLL) += irq_poll.o

obj-$(CONFIG_STACKDEPOT) += stackdepot.o
KASAN_SANITIZE_stackdepot.o := n

libfdt_files = fdt.o fdt_ro.o fdt_wip.o fdt_rw.o fdt_sw.o fdt_strerror.o \
	       fdt_empty_tree.o
$(foreach file, $(libfdt_files), \
	$(eval CFLAGS_$(file) = -I$(src)/../scripts/dtc/libfdt))
lib-$(CONFIG_LIBFDT) += $(libfdt_files)

obj-$(CONFIG_RBTREE_TEST) += rbtree_test.o
obj-$(CONFIG_INTERVAL_TREE_TEST) += interval_tree_test.o

obj-$(CONFIG_PERCPU_TEST) += percpu_test.o

obj-$(CONFIG_ASN1) += asn1_decoder.o

obj-$(CONFIG_FONT_SUPPORT) += fonts/

hostprogs-y	:= gen_crc32table
clean-files	:= crc32table.h

$(obj)/crc32.o: $(obj)/crc32table.h

quiet_cmd_crc32 = GEN     $@
      cmd_crc32 = $< > $@

$(obj)/crc32table.h: $(obj)/gen_crc32table
	$(call cmd,crc32)

#
# Build a fast OID lookip registry from include/linux/oid_registry.h
#
obj-$(CONFIG_OID_REGISTRY) += oid_registry.o

$(obj)/oid_registry.o: $(obj)/oid_registry_data.c

$(obj)/oid_registry_data.c: $(srctree)/include/linux/oid_registry.h \
			    $(src)/build_OID_registry
	$(call cmd,build_OID_registry)

quiet_cmd_build_OID_registry = GEN     $@
      cmd_build_OID_registry = perl $(srctree)/$(src)/build_OID_registry $< $@

clean-files	+= oid_registry_data.c

obj-$(CONFIG_UCS2_STRING) += ucs2_string.o
obj-$(CONFIG_UBSAN) += ubsan.o

UBSAN_SANITIZE_ubsan.o := n
