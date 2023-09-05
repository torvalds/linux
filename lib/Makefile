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

lib-y := ctype.o string.o vsprintf.o cmdline.o \
	 rbtree.o radix-tree.o timerqueue.o xarray.o \
	 maple_tree.o idr.o extable.o irq_regs.o argv_split.o \
	 flex_proportions.o ratelimit.o \
	 is_single_threaded.o plist.o decompress.o kobject_uevent.o \
	 earlycpio.o seq_buf.o siphash.o dec_and_lock.o \
	 nmi_backtrace.o win_minmax.o memcat_p.o \
	 buildid.o

lib-$(CONFIG_PRINTK) += dump_stack.o
lib-$(CONFIG_SMP) += cpumask.o

lib-y	+= kobject.o klist.o
obj-y	+= lockref.o

obj-y += bcd.o sort.o parser.o debug_locks.o random32.o \
	 bust_spinlocks.o kasprintf.o bitmap.o scatterlist.o \
	 list_sort.o uuid.o iov_iter.o clz_ctz.o \
	 bsearch.o find_bit.o llist.o memweight.o kfifo.o \
	 percpu-refcount.o rhashtable.o base64.o \
	 once.o refcount.o rcuref.o usercopy.o errseq.o bucket_locks.o \
	 generic-radix-tree.o
obj-$(CONFIG_STRING_SELFTEST) += test_string.o
obj-y += string_helpers.o
obj-$(CONFIG_TEST_STRING_HELPERS) += test-string_helpers.o
obj-y += hexdump.o
obj-$(CONFIG_TEST_HEXDUMP) += test_hexdump.o
obj-y += kstrtox.o
obj-$(CONFIG_FIND_BIT_BENCHMARK) += find_bit_benchmark.o
obj-$(CONFIG_TEST_BPF) += test_bpf.o
test_dhry-objs := dhry_1.o dhry_2.o dhry_run.o
obj-$(CONFIG_TEST_DHRY) += test_dhry.o
obj-$(CONFIG_TEST_FIRMWARE) += test_firmware.o
obj-$(CONFIG_TEST_BITOPS) += test_bitops.o
CFLAGS_test_bitops.o += -Werror
obj-$(CONFIG_CPUMASK_KUNIT_TEST) += cpumask_kunit.o
obj-$(CONFIG_TEST_SYSCTL) += test_sysctl.o
obj-$(CONFIG_HASH_KUNIT_TEST) += test_hash.o
obj-$(CONFIG_TEST_IDA) += test_ida.o
obj-$(CONFIG_TEST_UBSAN) += test_ubsan.o
CFLAGS_test_ubsan.o += $(call cc-disable-warning, vla)
UBSAN_SANITIZE_test_ubsan.o := y
obj-$(CONFIG_TEST_KSTRTOX) += test-kstrtox.o
obj-$(CONFIG_TEST_LIST_SORT) += test_list_sort.o
obj-$(CONFIG_TEST_MIN_HEAP) += test_min_heap.o
obj-$(CONFIG_TEST_LKM) += test_module.o
obj-$(CONFIG_TEST_VMALLOC) += test_vmalloc.o
obj-$(CONFIG_TEST_RHASHTABLE) += test_rhashtable.o
obj-$(CONFIG_TEST_SORT) += test_sort.o
obj-$(CONFIG_TEST_USER_COPY) += test_user_copy.o
obj-$(CONFIG_TEST_STATIC_KEYS) += test_static_keys.o
obj-$(CONFIG_TEST_STATIC_KEYS) += test_static_key_base.o
obj-$(CONFIG_TEST_DYNAMIC_DEBUG) += test_dynamic_debug.o
obj-$(CONFIG_TEST_PRINTF) += test_printf.o
obj-$(CONFIG_TEST_SCANF) += test_scanf.o

obj-$(CONFIG_TEST_BITMAP) += test_bitmap.o
ifeq ($(CONFIG_CC_IS_CLANG)$(CONFIG_KASAN),yy)
# FIXME: Clang breaks test_bitmap_const_eval when KASAN and GCOV are enabled
GCOV_PROFILE_test_bitmap.o := n
endif

obj-$(CONFIG_TEST_UUID) += test_uuid.o
obj-$(CONFIG_TEST_XARRAY) += test_xarray.o
obj-$(CONFIG_TEST_MAPLE_TREE) += test_maple_tree.o
obj-$(CONFIG_TEST_PARMAN) += test_parman.o
obj-$(CONFIG_TEST_KMOD) += test_kmod.o
obj-$(CONFIG_TEST_DEBUG_VIRTUAL) += test_debug_virtual.o
obj-$(CONFIG_TEST_MEMCAT_P) += test_memcat_p.o
obj-$(CONFIG_TEST_OBJAGG) += test_objagg.o
obj-$(CONFIG_TEST_BLACKHOLE_DEV) += test_blackhole_dev.o
obj-$(CONFIG_TEST_MEMINIT) += test_meminit.o
obj-$(CONFIG_TEST_LOCKUP) += test_lockup.o
obj-$(CONFIG_TEST_HMM) += test_hmm.o
obj-$(CONFIG_TEST_FREE_PAGES) += test_free_pages.o
obj-$(CONFIG_KPROBES_SANITY_TEST) += test_kprobes.o
obj-$(CONFIG_TEST_REF_TRACKER) += test_ref_tracker.o
CFLAGS_test_fprobe.o += $(CC_FLAGS_FTRACE)
obj-$(CONFIG_FPROBE_SANITY_TEST) += test_fprobe.o
#
# CFLAGS for compiling floating point code inside the kernel. x86/Makefile turns
# off the generation of FPU/SSE* instructions for kernel proper but FPU_FLAGS
# get appended last to CFLAGS and thus override those previous compiler options.
#
FPU_CFLAGS := -msse -msse2
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
FPU_CFLAGS += -mhard-float
FPU_CFLAGS += $(call cc-option,-msse -mpreferred-stack-boundary=3,-mpreferred-stack-boundary=4)
endif

obj-$(CONFIG_TEST_FPU) += test_fpu.o
CFLAGS_test_fpu.o += $(FPU_CFLAGS)

obj-$(CONFIG_TEST_LIVEPATCH) += livepatch/

# Some KUnit files (hooks.o) need to be built-in even when KUnit is a module,
# so we can't just use obj-$(CONFIG_KUNIT).
ifdef CONFIG_KUNIT
obj-y += kunit/
endif

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

lib-$(CONFIG_INDIRECT_IOMEM) += logic_iomem.o

obj-$(CONFIG_TRACE_MMIO_ACCESS) += trace_readwrite.o

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
obj-$(CONFIG_CRC64_ROCKSOFT) += crc64-rocksoft.o
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
#ensure exported functions have prototypes
CFLAGS_dynamic_debug.o := -DDYNAMIC_DEBUG_MODULE

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

obj-$(CONFIG_POLYNOMIAL) += polynomial.o

# stackdepot.c should not be instrumented or call instrumented functions.
# Prevent the compiler from calling builtins like memcmp() or bcmp() from this
# file.
CFLAGS_stackdepot.o += -fno-builtin
obj-$(CONFIG_STACKDEPOT) += stackdepot.o
KASAN_SANITIZE_stackdepot.o := n
# In particular, instrumenting stackdepot.c with KMSAN will result in infinite
# recursion.
KMSAN_SANITIZE_stackdepot.o := n
KCOV_INSTRUMENT_stackdepot.o := n

obj-$(CONFIG_REF_TRACKER) += ref_tracker.o

libfdt_files = fdt.o fdt_ro.o fdt_wip.o fdt_rw.o fdt_sw.o fdt_strerror.o \
	       fdt_empty_tree.o fdt_addresses.o
$(foreach file, $(libfdt_files), \
	$(eval CFLAGS_$(file) = -I $(srctree)/scripts/dtc/libfdt))
lib-$(CONFIG_LIBFDT) += $(libfdt_files)

obj-$(CONFIG_BOOT_CONFIG) += bootconfig.o
obj-$(CONFIG_BOOT_CONFIG_EMBED) += bootconfig-data.o

$(obj)/bootconfig-data.o: $(obj)/default.bconf

targets += default.bconf
filechk_defbconf = cat $(or $(real-prereqs), /dev/null)
$(obj)/default.bconf: $(CONFIG_BOOT_CONFIG_EMBED_FILE) FORCE
	$(call filechk,defbconf)

obj-$(CONFIG_RBTREE_TEST) += rbtree_test.o
obj-$(CONFIG_INTERVAL_TREE_TEST) += interval_tree_test.o

obj-$(CONFIG_PERCPU_TEST) += percpu_test.o

obj-$(CONFIG_ASN1) += asn1_decoder.o
obj-$(CONFIG_ASN1_ENCODER) += asn1_encoder.o

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
obj-$(CONFIG_UBSAN) += ubsan.o

UBSAN_SANITIZE_ubsan.o := n
KASAN_SANITIZE_ubsan.o := n
KCSAN_SANITIZE_ubsan.o := n
CFLAGS_ubsan.o := -fno-stack-protector $(DISABLE_STACKLEAK_PLUGIN)

obj-$(CONFIG_SBITMAP) += sbitmap.o

obj-$(CONFIG_PARMAN) += parman.o

obj-y += group_cpus.o

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
CFLAGS_bitfield_kunit.o := $(DISABLE_STRUCTLEAK_PLUGIN)
obj-$(CONFIG_BITFIELD_KUNIT) += bitfield_kunit.o
obj-$(CONFIG_CHECKSUM_KUNIT) += checksum_kunit.o
obj-$(CONFIG_LIST_KUNIT_TEST) += list-test.o
obj-$(CONFIG_HASHTABLE_KUNIT_TEST) += hashtable_test.o
obj-$(CONFIG_LINEAR_RANGES_TEST) += test_linear_ranges.o
obj-$(CONFIG_BITS_TEST) += test_bits.o
obj-$(CONFIG_CMDLINE_KUNIT_TEST) += cmdline_kunit.o
obj-$(CONFIG_SLUB_KUNIT_TEST) += slub_kunit.o
obj-$(CONFIG_MEMCPY_KUNIT_TEST) += memcpy_kunit.o
obj-$(CONFIG_IS_SIGNED_TYPE_KUNIT_TEST) += is_signed_type_kunit.o
CFLAGS_overflow_kunit.o = $(call cc-disable-warning, tautological-constant-out-of-range-compare)
obj-$(CONFIG_OVERFLOW_KUNIT_TEST) += overflow_kunit.o
CFLAGS_stackinit_kunit.o += $(call cc-disable-warning, switch-unreachable)
obj-$(CONFIG_STACKINIT_KUNIT_TEST) += stackinit_kunit.o
CFLAGS_fortify_kunit.o += $(call cc-disable-warning, unsequenced)
CFLAGS_fortify_kunit.o += $(DISABLE_STRUCTLEAK_PLUGIN)
obj-$(CONFIG_FORTIFY_KUNIT_TEST) += fortify_kunit.o
obj-$(CONFIG_STRCAT_KUNIT_TEST) += strcat_kunit.o
obj-$(CONFIG_STRSCPY_KUNIT_TEST) += strscpy_kunit.o
obj-$(CONFIG_SIPHASH_KUNIT_TEST) += siphash_kunit.o

obj-$(CONFIG_GENERIC_LIB_DEVMEM_IS_ALLOWED) += devmem_is_allowed.o

# FORTIFY_SOURCE compile-time behavior tests
TEST_FORTIFY_SRCS = $(wildcard $(srctree)/$(src)/test_fortify/*-*.c)
TEST_FORTIFY_LOGS = $(patsubst $(srctree)/$(src)/%.c, %.log, $(TEST_FORTIFY_SRCS))
TEST_FORTIFY_LOG = test_fortify.log

quiet_cmd_test_fortify = TEST    $@
      cmd_test_fortify = $(CONFIG_SHELL) $(srctree)/scripts/test_fortify.sh \
			$< $@ "$(NM)" $(CC) $(c_flags) \
			$(call cc-disable-warning,fortify-source) \
			-DKBUILD_EXTRA_WARN1

targets += $(TEST_FORTIFY_LOGS)
clean-files += $(TEST_FORTIFY_LOGS)
clean-files += $(addsuffix .o, $(TEST_FORTIFY_LOGS))
$(obj)/test_fortify/%.log: $(src)/test_fortify/%.c \
			   $(src)/test_fortify/test_fortify.h \
			   $(srctree)/include/linux/fortify-string.h \
			   $(srctree)/scripts/test_fortify.sh \
			   FORCE
	$(call if_changed,test_fortify)

quiet_cmd_gen_fortify_log = GEN     $@
      cmd_gen_fortify_log = cat </dev/null $(filter-out FORCE,$^) 2>/dev/null > $@ || true

targets += $(TEST_FORTIFY_LOG)
clean-files += $(TEST_FORTIFY_LOG)
$(obj)/$(TEST_FORTIFY_LOG): $(addprefix $(obj)/, $(TEST_FORTIFY_LOGS)) FORCE
	$(call if_changed,gen_fortify_log)

# Fake dependency to trigger the fortify tests.
ifeq ($(CONFIG_FORTIFY_SOURCE),y)
$(obj)/string.o: $(obj)/$(TEST_FORTIFY_LOG)
endif
