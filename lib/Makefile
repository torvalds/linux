# SPDX-License-Identifier: GPL-2.0
#
# Makefile for some libs needed in the kernel.
#

ccflags-remove-$(CONFIG_FUNCTION_TRACER) += $(CC_FLAGS_FTRACE)

# These files are disabled because they produce lots of non-interesting and/or
# flaky coverage that is not a function of syscall inputs. For example,
# rbtree can be global and individual rotations don't correlate with inputs.
KCOV_INSTRUMENT_string.o := n
KCOV_INSTRUMENT_rbtree.o := n
KCOV_INSTRUMENT_list_debug.o := n
KCOV_INSTRUMENT_debugobjects.o := n
KCOV_INSTRUMENT_dynamic_debug.o := n
KCOV_INSTRUMENT_fault-inject.o := n

# string.o implements standard library functions like memset/memcpy etc.
# Use -ffreestanding to ensure that the compiler does not try to "optimize"
# them into calls to themselves.
CFLAGS_string.o := -ffreestanding

# Early boot use of cmdline, don't instrument it
ifdef CONFIG_AMD_MEM_ENCRYPT
KASAN_SANITIZE_string.o := n

CFLAGS_string.o += -fno-stack-protector
endif

# Used by KCSAN while enabled, avoid recursion.
KCSAN_SANITIZE_random32.o := n

lib-y := ctype.o string.o vsprintf.o cmdline.o \
	 rbtree.o radix-tree.o timerqueue.o xarray.o \
	 idr.o extable.o sha1.o irq_regs.o argv_split.o \
	 flex_proportions.o ratelimit.o show_mem.o \
	 is_single_threaded.o plist.o decompress.o kobject_uevent.o \
	 earlycpio.o seq_buf.o siphash.o dec_and_lock.o \
	 nmi_backtrace.o nodemask.o win_minmax.o memcat_p.o

lib-$(CONFIG_PRINTK) += dump_stack.o
lib-$(CONFIG_SMP) += cpumask.o

lib-y	+= kobject.o klist.o
obj-y	+= lockref.o

obj-y += bcd.o sort.o parser.o debug_locks.o random32.o \
	 bust_spinlocks.o kasprintf.o bitmap.o scatterlist.o \
	 list_sort.o uuid.o iov_iter.o clz_ctz.o \
	 bsearch.o find_bit.o llist.o memweight.o kfifo.o \
	 percpu-refcount.o rhashtable.o \
	 once.o refcount.o usercopy.o errseq.o bucket_locks.o \
	 generic-radix-tree.o
obj-$(CONFIG_STRING_SELFTEST) += test_string.o
obj-y += string_helpers.o
obj-$(CONFIG_TEST_STRING_HELPERS) += test-string_helpers.o
obj-y += hexdump.o
obj-$(CONFIG_TEST_HEXDUMP) += test_hexdump.o
obj-y += kstrtox.o
obj-$(CONFIG_FIND_BIT_BENCHMARK) += find_bit_benchmark.o
obj-$(CONFIG_TEST_BPF) += test_bpf.o
obj-$(CONFIG_TEST_FIRMWARE) += test_firmware.o
obj-$(CONFIG_TEST_BITOPS) += test_bitops.o
CFLAGS_test_bitops.o += -Werror
obj-$(CONFIG_TEST_SYSCTL) += test_sysctl.o
obj-$(CONFIG_TEST_HASH) += test_hash.o test_siphash.o
obj-$(CONFIG_TEST_IDA) += test_ida.o
obj-$(CONFIG_KASAN_KUNIT_TEST) += test_kasan.o
CFLAGS_test_kasan.o += -fno-builtin
CFLAGS_test_kasan.o += $(call cc-disable-warning, vla)
obj-$(CONFIG_TEST_KASAN_MODULE) += test_kasan_module.o
CFLAGS_test_kasan_module.o += -fno-builtin
obj-$(CONFIG_TEST_UBSAN) += test_ubsan.o
CFLAGS_test_ubsan.o += $(call cc-disable-warning, vla)
UBSAN_SANITIZE_test_ubsan.o := y
obj-$(CONFIG_TEST_KSTRTOX) += test-kstrtox.o
obj-$(CONFIG_TEST_LIST_SORT) += test_list_sort.o
obj-$(CONFIG_TEST_MIN_HEAP) += test_min_heap.o
obj-$(CONFIG_TEST_LKM) += test_module.o
obj-$(CONFIG_TEST_VMALLOC) += test_vmalloc.o
obj-$(CONFIG_TEST_OVERFLOW) += test_overflow.o
obj-$(CONFIG_TEST_RHASHTABLE) += test_rhashtable.o
obj-$(CONFIG_TEST_SORT) += test_sort.o
obj-$(CONFIG_TEST_USER_COPY) += test_user_copy.o
obj-$(CONFIG_TEST_STATIC_KEYS) += test_static_keys.o
obj-$(CONFIG_TEST_STATIC_KEYS) += test_static_key_base.o
obj-$(CONFIG_TEST_PRINTF) += test_printf.o
obj-$(CONFIG_TEST_BITMAP) += test_bitmap.o
obj-$(CONFIG_TEST_STRSCPY) += test_strscpy.o
obj-$(CONFIG_TEST_UUID) += test_uuid.o
obj-$(CONFIG_TEST_XARRAY) += test_xarray.o
obj-$(CONFIG_TEST_PARMAN) += test_parman.o
obj-$(CONFIG_TEST_KMOD) += test_kmod.o
obj-$(CONFIG_TEST_DEBUG_VIRTUAL) += test_debug_virtual.o
obj-$(CONFIG_TEST_MEMCAT_P) += test_memcat_p.o
obj-$(CONFIG_TEST_OBJAGG) += test_objagg.o
CFLAGS_test_stackinit.o += $(call cc-disable-warning, switch-unreachable)
obj-$(CONFIG_TEST_STACKINIT) += test_stackinit.o
obj-$(CONFIG_TEST_BLACKHOLE_DEV) += test_blackhole_dev.o
obj-$(CONFIG_TEST_MEMINIT) += test_meminit.o
obj-$(CONFIG_TEST_LOCKUP) += test_lockup.o
obj-$(CONFIG_TEST_HMM) += test_hmm.o
obj-$(CONFIG_TEST_FREE_PAGES) += test_free_pages.o

#
# CFLAGS for compiling floating point code inside the kernel. x86/Makefile turns
# off the generation of FPU/SSE* instructions for kernel proper but FPU_FLAGS
# get appended last to CFLAGS and thus override those previous compiler options.
#
FPU_CFLAGS := -mhard-float -msse -msse2
ifdef CONFIG_CC_IS_GCC
# Stack alignment mismatch, proceed with caution.
# GCC < 7.1 cannot compile code using `double` and -mpreferred-stack-boundary=3
# (8B stack alignment).
# See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=53383
#
# The "-msse" in the first argument is there so that the
# -mpreferred-stack-boundary=3 build error:
#
#  -mpreferred-stack-boundary=3 is not between 4 and 12
#
# can be triggered. Otherwise gcc doesn't complain.
FPU_CFLAGS += $(call cc-option,-msse -mpreferred-stack-boundary=3,-mpreferred-stack-boundary=4)
endif

obj-$(CONFIG_TEST_FPU) += test_fpu.o
CFLAGS_test_fpu.o += $(FPU_CFLAGS)

obj-$(CONFIG_TEST_LIVEPATCH) += livepatch/

obj-$(CONFIG_KUNIT) += kunit/

ifeq ($(CONFIG_DEBUG_KOBJECT),y)
CFLAGS_kobject.o += -DDEBUG
CFLAGS_kobject_uevent.o += -DDEBUG
endif

obj-$(CONFIG_DEBUG_INFO_REDUCED) += debug_info.o
CFLAGS_debug_info.o += $(call cc-option, -femit-struct-debug-detailed=any)

obj-y += math/ crypto/

obj-$(CONFIG_GENERIC_IOMAP) += iomap.o
obj-$(CONFIG_GENERIC_PCI_IOMAP) += pci_iomap.o
obj-$(CONFIG_HAS_IOMEM) += iomap_copy.o devres.o
obj-$(CONFIG_CHECK_SIGNATURE) += check_signature.o
obj-$(CONFIG_DEBUG_LOCKING_API_SELFTESTS) += locking-selftest.o

lib-y += logic_pio.o

obj-$(CONFIG_GENERIC_HWEIGHT) += hweight.o

obj-$(CONFIG_BTREE) += btree.o
obj-$(CONFIG_INTERVAL_TREE) += interval_tree.o
obj-$(CONFIG_ASSOCIATIVE_ARRAY) += assoc_array.o
obj-$(CONFIG_DEBUG_PREEMPT) += smp_processor_id.o
obj-$(CONFIG_DEBUG_LIST) += list_debug.o
obj-$(CONFIG_DEBUG_OBJECTS) += debugobjects.o

obj-$(CONFIG_BITREVERSE) += bitrev.o
obj-$(CONFIG_LINEAR_RANGES) += linear_ranges.o
obj-$(CONFIG_PACKING)	+= packing.o
obj-$(CONFIG_CRC_CCITT)	+= crc-ccitt.o
obj-$(CONFIG_CRC16)	+= crc16.o
obj-$(CONFIG_CRC_T10DIF)+= crc-t10dif.o
obj-$(CONFIG_CRC_ITU_T)	+= crc-itu-t.o
obj-$(CONFIG_CRC32)	+= crc32.o
obj-$(CONFIG_CRC64)     += crc64.o
obj-$(CONFIG_CRC32_SELFTEST)	+= crc32test.o
obj-$(CONFIG_CRC4)	+= crc4.o
obj-$(CONFIG_CRC7)	+= crc7.o
obj-$(CONFIG_LIBCRC32C)	+= libcrc32c.o
obj-$(CONFIG_CRC8)	+= crc8.o
obj-$(CONFIG_XXHASH)	+= xxhash.o
obj-$(CONFIG_GENERIC_ALLOCATOR) += genalloc.o

obj-$(CONFIG_842_COMPRESS) += 842/
obj-$(CONFIG_842_DECOMPRESS) += 842/
obj-$(CONFIG_ZLIB_INFLATE) += zlib_inflate/
obj-$(CONFIG_ZLIB_DEFLATE) += zlib_deflate/
obj-$(CONFIG_ZLIB_DFLTCC) += zlib_dfltcc/
obj-$(CONFIG_REED_SOLOMON) += reed_solomon/
obj-$(CONFIG_BCH) += bch.o
obj-$(CONFIG_LZO_COMPRESS) += lzo/
obj-$(CONFIG_LZO_DECOMPRESS) += lzo/
obj-$(CONFIG_LZ4_COMPRESS) += lz4/
obj-$(CONFIG_LZ4HC_COMPRESS) += lz4/
obj-$(CONFIG_LZ4_DECOMPRESS) += lz4/
obj-$(CONFIG_ZSTD_COMPRESS) += zstd/
obj-$(CONFIG_ZSTD_DECOMPRESS) += zstd/
obj-$(CONFIG_XZ_DEC) += xz/
obj-$(CONFIG_RAID6_PQ) += raid6/

lib-$(CONFIG_DECOMPRESS_GZIP) += decompress_inflate.o
lib-$(CONFIG_DECOMPRESS_BZIP2) += decompress_bunzip2.o
lib-$(CONFIG_DECOMPRESS_LZMA) += decompress_unlzma.o
lib-$(CONFIG_DECOMPRESS_XZ) += decompress_unxz.o
lib-$(CONFIG_DECOMPRESS_LZO) += decompress_unlzo.o
lib-$(CONFIG_DECOMPRESS_LZ4) += decompress_unlz4.o
lib-$(CONFIG_DECOMPRESS_ZSTD) += decompress_unzstd.o

obj-$(CONFIG_TEXTSEARCH) += textsearch.o
obj-$(CONFIG_TEXTSEARCH_KMP) += ts_kmp.o
obj-$(CONFIG_TEXTSEARCH_BM) += ts_bm.o
obj-$(CONFIG_TEXTSEARCH_FSM) += ts_fsm.o
obj-$(CONFIG_SMP) += percpu_counter.o
obj-$(CONFIG_AUDIT_GENERIC) += audit.o
obj-$(CONFIG_AUDIT_COMPAT_GENERIC) += compat_audit.o

obj-$(CONFIG_IOMMU_HELPER) += iommu-helper.o
obj-$(CONFIG_FAULT_INJECTION) += fault-inject.o
obj-$(CONFIG_FAULT_INJECTION_USERCOPY) += fault-inject-usercopy.o
obj-$(CONFIG_NOTIFIER_ERROR_INJECTION) += notifier-error-inject.o
obj-$(CONFIG_PM_NOTIFIER_ERROR_INJECT) += pm-notifier-error-inject.o
obj-$(CONFIG_NETDEV_NOTIFIER_ERROR_INJECT) += netdev-notifier-error-inject.o
obj-$(CONFIG_MEMORY_NOTIFIER_ERROR_INJECT) += memory-notifier-error-inject.o
obj-$(CONFIG_OF_RECONFIG_NOTIFIER_ERROR_INJECT) += \
	of-reconfig-notifier-error-inject.o
obj-$(CONFIG_FUNCTION_ERROR_INJECTION) += error-inject.o

lib-$(CONFIG_GENERIC_BUG) += bug.o

obj-$(CONFIG_HAVE_ARCH_TRACEHOOK) += syscall.o

obj-$(CONFIG_DYNAMIC_DEBUG_CORE) += dynamic_debug.o
obj-$(CONFIG_SYMBOLIC_ERRNAME) += errname.o

obj-$(CONFIG_NLATTR) += nlattr.o

obj-$(CONFIG_LRU_CACHE) += lru_cache.o

obj-$(CONFIG_GENERIC_CSUM) += checksum.o

obj-$(CONFIG_GENERIC_ATOMIC64) += atomic64.o

obj-$(CONFIG_ATOMIC64_SELFTEST) += atomic64_test.o

obj-$(CONFIG_CPU_RMAP) += cpu_rmap.o

obj-$(CONFIG_DQL) += dynamic_queue_limits.o

obj-$(CONFIG_GLOB) += glob.o
obj-$(CONFIG_GLOB_SELFTEST) += globtest.o

obj-$(CONFIG_MPILIB) += mpi/
obj-$(CONFIG_DIMLIB) += dim/
obj-$(CONFIG_SIGNATURE) += digsig.o

lib-$(CONFIG_CLZ_TAB) += clz_tab.o

obj-$(CONFIG_GENERIC_STRNCPY_FROM_USER) += strncpy_from_user.o
obj-$(CONFIG_GENERIC_STRNLEN_USER) += strnlen_user.o

obj-$(CONFIG_GENERIC_NET_UTILS) += net_utils.o

obj-$(CONFIG_SG_SPLIT) += sg_split.o
obj-$(CONFIG_SG_POOL) += sg_pool.o
obj-$(CONFIG_MEMREGION) += memregion.o
obj-$(CONFIG_STMP_DEVICE) += stmp_device.o
obj-$(CONFIG_IRQ_POLL) += irq_poll.o

# stackdepot.c should not be instrumented or call instrumented functions.
# Prevent the compiler from calling builtins like memcmp() or bcmp() from this
# file.
CFLAGS_stackdepot.o += -fno-builtin
obj-$(CONFIG_STACKDEPOT) += stackdepot.o
KASAN_SANITIZE_stackdepot.o := n
KCOV_INSTRUMENT_stackdepot.o := n

libfdt_files = fdt.o fdt_ro.o fdt_wip.o fdt_rw.o fdt_sw.o fdt_strerror.o \
	       fdt_empty_tree.o fdt_addresses.o
$(foreach file, $(libfdt_files), \
	$(eval CFLAGS_$(file) = -I $(srctree)/scripts/dtc/libfdt))
lib-$(CONFIG_LIBFDT) += $(libfdt_files)

lib-$(CONFIG_BOOT_CONFIG) += bootconfig.o

obj-$(CONFIG_RBTREE_TEST) += rbtree_test.o
obj-$(CONFIG_INTERVAL_TREE_TEST) += interval_tree_test.o

obj-$(CONFIG_PERCPU_TEST) += percpu_test.o

obj-$(CONFIG_ASN1) += asn1_decoder.o

obj-$(CONFIG_FONT_SUPPORT) += fonts/

hostprogs	:= gen_crc32table
hostprogs	+= gen_crc64table
clean-files	:= crc32table.h
clean-files	+= crc64table.h

$(obj)/crc32.o: $(obj)/crc32table.h

quiet_cmd_crc32 = GEN     $@
      cmd_crc32 = $< > $@

$(obj)/crc32table.h: $(obj)/gen_crc32table
	$(call cmd,crc32)

$(obj)/crc64.o: $(obj)/crc64table.h

quiet_cmd_crc64 = GEN     $@
      cmd_crc64 = $< > $@

$(obj)/crc64table.h: $(obj)/gen_crc64table
	$(call cmd,crc64)

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
ifneq ($(CONFIG_UBSAN_TRAP),y)
obj-$(CONFIG_UBSAN) += ubsan.o
endif

UBSAN_SANITIZE_ubsan.o := n
KASAN_SANITIZE_ubsan.o := n
KCSAN_SANITIZE_ubsan.o := n
CFLAGS_ubsan.o := -fno-stack-protector $(DISABLE_STACKLEAK_PLUGIN)

obj-$(CONFIG_SBITMAP) += sbitmap.o

obj-$(CONFIG_PARMAN) += parman.o

# GCC library routines
obj-$(CONFIG_GENERIC_LIB_ASHLDI3) += ashldi3.o
obj-$(CONFIG_GENERIC_LIB_ASHRDI3) += ashrdi3.o
obj-$(CONFIG_GENERIC_LIB_LSHRDI3) += lshrdi3.o
obj-$(CONFIG_GENERIC_LIB_MULDI3) += muldi3.o
obj-$(CONFIG_GENERIC_LIB_CMPDI2) += cmpdi2.o
obj-$(CONFIG_GENERIC_LIB_UCMPDI2) += ucmpdi2.o
obj-$(CONFIG_OBJAGG) += objagg.o

# pldmfw library
obj-$(CONFIG_PLDMFW) += pldmfw/

# KUnit tests
obj-$(CONFIG_BITFIELD_KUNIT) += bitfield_kunit.o
obj-$(CONFIG_LIST_KUNIT_TEST) += list-test.o
obj-$(CONFIG_LINEAR_RANGES_TEST) += test_linear_ranges.o
obj-$(CONFIG_BITS_TEST) += test_bits.o
