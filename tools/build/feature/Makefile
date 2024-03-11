# SPDX-License-Identifier: GPL-2.0
include ../../scripts/Makefile.include

FILES=                                          \
         test-all.bin                           \
         test-backtrace.bin                     \
         test-bionic.bin                        \
         test-dwarf.bin                         \
         test-dwarf_getlocations.bin            \
         test-dwarf_getcfi.bin                  \
         test-eventfd.bin                       \
         test-fortify-source.bin                \
         test-get_current_dir_name.bin          \
         test-glibc.bin                         \
         test-gtk2.bin                          \
         test-gtk2-infobar.bin                  \
         test-hello.bin                         \
         test-libaudit.bin                      \
         test-libbfd.bin                        \
         test-libbfd-buildid.bin		\
         test-disassembler-four-args.bin        \
         test-disassembler-init-styled.bin	\
         test-reallocarray.bin			\
         test-libbfd-liberty.bin                \
         test-libbfd-liberty-z.bin              \
         test-cplus-demangle.bin                \
         test-cxa-demangle.bin                  \
         test-libcap.bin			\
         test-libelf.bin                        \
         test-libelf-getphdrnum.bin             \
         test-libelf-gelf_getnote.bin           \
         test-libelf-getshdrstrndx.bin          \
         test-libdebuginfod.bin                 \
         test-libnuma.bin                       \
         test-numa_num_possible_cpus.bin        \
         test-libperl.bin                       \
         test-libpython.bin                     \
         test-libslang.bin                      \
         test-libslang-include-subdir.bin       \
         test-libtraceevent.bin                 \
         test-libtracefs.bin                    \
         test-libcrypto.bin                     \
         test-libunwind.bin                     \
         test-libunwind-debug-frame.bin         \
         test-libunwind-x86.bin                 \
         test-libunwind-x86_64.bin              \
         test-libunwind-arm.bin                 \
         test-libunwind-aarch64.bin             \
         test-libunwind-debug-frame-arm.bin     \
         test-libunwind-debug-frame-aarch64.bin \
         test-pthread-attr-setaffinity-np.bin   \
         test-pthread-barrier.bin		\
         test-stackprotector-all.bin            \
         test-timerfd.bin                       \
         test-libdw-dwarf-unwind.bin            \
         test-libbabeltrace.bin                 \
         test-compile-32.bin                    \
         test-compile-x32.bin                   \
         test-zlib.bin                          \
         test-lzma.bin                          \
         test-bpf.bin                           \
         test-libbpf.bin                        \
         test-get_cpuid.bin                     \
         test-sdt.bin                           \
         test-cxx.bin                           \
         test-gettid.bin			\
         test-jvmti.bin				\
         test-jvmti-cmlr.bin			\
         test-scandirat.bin			\
         test-sched_getcpu.bin			\
         test-setns.bin				\
         test-libopencsd.bin			\
         test-clang.bin				\
         test-llvm.bin				\
         test-llvm-version.bin			\
         test-libaio.bin			\
         test-libzstd.bin			\
         test-clang-bpf-co-re.bin		\
         test-file-handle.bin			\
         test-libpfm4.bin

FILES := $(addprefix $(OUTPUT),$(FILES))

PKG_CONFIG ?= $(CROSS_COMPILE)pkg-config

all: $(FILES)

__BUILD = $(CC) $(CFLAGS) -MD -Wall -Werror -o $@ $(patsubst %.bin,%.c,$(@F)) $(LDFLAGS)
  BUILD = $(__BUILD) > $(@:.bin=.make.output) 2>&1
  BUILD_BFD = $(BUILD) -DPACKAGE='"perf"' -lbfd -ldl
  BUILD_ALL = $(BUILD) -fstack-protector-all -O2 -D_FORTIFY_SOURCE=2 -ldw -lelf -lnuma -lelf -lslang $(FLAGS_PERL_EMBED) $(FLAGS_PYTHON_EMBED) -DPACKAGE='"perf"' -lbfd -ldl -lz -llzma -lzstd -lcap

__BUILDXX = $(CXX) $(CXXFLAGS) -MD -Wall -Werror -o $@ $(patsubst %.bin,%.cpp,$(@F)) $(LDFLAGS)
  BUILDXX = $(__BUILDXX) > $(@:.bin=.make.output) 2>&1

###############################

$(OUTPUT)test-all.bin:
	$(BUILD_ALL) || $(BUILD_ALL) -lopcodes -liberty

$(OUTPUT)test-hello.bin:
	$(BUILD)

$(OUTPUT)test-pthread-attr-setaffinity-np.bin:
	$(BUILD) -D_GNU_SOURCE -lpthread

$(OUTPUT)test-pthread-barrier.bin:
	$(BUILD) -lpthread

$(OUTPUT)test-stackprotector-all.bin:
	$(BUILD) -fstack-protector-all

$(OUTPUT)test-fortify-source.bin:
	$(BUILD) -O2 -D_FORTIFY_SOURCE=2

$(OUTPUT)test-bionic.bin:
	$(BUILD)

$(OUTPUT)test-libcap.bin:
	$(BUILD) -lcap

$(OUTPUT)test-libelf.bin:
	$(BUILD) -lelf

$(OUTPUT)test-eventfd.bin:
	$(BUILD)

$(OUTPUT)test-get_current_dir_name.bin:
	$(BUILD)

$(OUTPUT)test-glibc.bin:
	$(BUILD)

$(OUTPUT)test-scandirat.bin:
	$(BUILD)

$(OUTPUT)test-sched_getcpu.bin:
	$(BUILD)

$(OUTPUT)test-setns.bin:
	$(BUILD)

$(OUTPUT)test-libopencsd.bin:
	$(BUILD) # -lopencsd_c_api -lopencsd provided by
		 # $(FEATURE_CHECK_LDFLAGS-libopencsd)

DWARFLIBS := -ldw
ifeq ($(findstring -static,${LDFLAGS}),-static)
DWARFLIBS += -lelf -lebl -lz -llzma -lbz2
endif

$(OUTPUT)test-dwarf.bin:
	$(BUILD) $(DWARFLIBS)

$(OUTPUT)test-dwarf_getlocations.bin:
	$(BUILD) $(DWARFLIBS)

$(OUTPUT)test-dwarf_getcfi.bin:
	$(BUILD) $(DWARFLIBS)

$(OUTPUT)test-libelf-getphdrnum.bin:
	$(BUILD) -lelf

$(OUTPUT)test-libelf-gelf_getnote.bin:
	$(BUILD) -lelf

$(OUTPUT)test-libelf-getshdrstrndx.bin:
	$(BUILD) -lelf

$(OUTPUT)test-libdebuginfod.bin:
	$(BUILD) -ldebuginfod

$(OUTPUT)test-libnuma.bin:
	$(BUILD) -lnuma

$(OUTPUT)test-numa_num_possible_cpus.bin:
	$(BUILD) -lnuma

$(OUTPUT)test-libunwind.bin:
	$(BUILD) -lelf

$(OUTPUT)test-libunwind-debug-frame.bin:
	$(BUILD) -lelf
$(OUTPUT)test-libunwind-x86.bin:
	$(BUILD) -lelf -lunwind-x86

$(OUTPUT)test-libunwind-x86_64.bin:
	$(BUILD) -lelf -lunwind-x86_64

$(OUTPUT)test-libunwind-arm.bin:
	$(BUILD) -lelf -lunwind-arm

$(OUTPUT)test-libunwind-aarch64.bin:
	$(BUILD) -lelf -lunwind-aarch64

$(OUTPUT)test-libunwind-debug-frame-arm.bin:
	$(BUILD) -lelf -lunwind-arm

$(OUTPUT)test-libunwind-debug-frame-aarch64.bin:
	$(BUILD) -lelf -lunwind-aarch64

$(OUTPUT)test-libaudit.bin:
	$(BUILD) -laudit

$(OUTPUT)test-libslang.bin:
	$(BUILD) -lslang

$(OUTPUT)test-libslang-include-subdir.bin:
	$(BUILD) -lslang

$(OUTPUT)test-libtraceevent.bin:
	$(BUILD) -ltraceevent

$(OUTPUT)test-libtracefs.bin:
	 $(BUILD) $(shell $(PKG_CONFIG) --cflags libtraceevent 2>/dev/null) -ltracefs

$(OUTPUT)test-libcrypto.bin:
	$(BUILD) -lcrypto

$(OUTPUT)test-gtk2.bin:
	$(BUILD) $(shell $(PKG_CONFIG) --libs --cflags gtk+-2.0 2>/dev/null) -Wno-deprecated-declarations

$(OUTPUT)test-gtk2-infobar.bin:
	$(BUILD) $(shell $(PKG_CONFIG) --libs --cflags gtk+-2.0 2>/dev/null)

grep-libs  = $(filter -l%,$(1))
strip-libs = $(filter-out -l%,$(1))

PERL_EMBED_LDOPTS = $(shell perl -MExtUtils::Embed -e ldopts 2>/dev/null)
PERL_EMBED_LDFLAGS = $(call strip-libs,$(PERL_EMBED_LDOPTS))
PERL_EMBED_LIBADD = $(call grep-libs,$(PERL_EMBED_LDOPTS))
PERL_EMBED_CCOPTS = $(shell perl -MExtUtils::Embed -e ccopts 2>/dev/null)
FLAGS_PERL_EMBED=$(PERL_EMBED_CCOPTS) $(PERL_EMBED_LDOPTS)

ifeq ($(CC_NO_CLANG), 0)
  PERL_EMBED_LDOPTS := $(filter-out -specs=%,$(PERL_EMBED_LDOPTS))
  PERL_EMBED_CCOPTS := $(filter-out -flto=auto -ffat-lto-objects, $(PERL_EMBED_CCOPTS))
  PERL_EMBED_CCOPTS := $(filter-out -specs=%,$(PERL_EMBED_CCOPTS))
  FLAGS_PERL_EMBED += -Wno-compound-token-split-by-macro
endif

$(OUTPUT)test-libperl.bin:
	$(BUILD) $(FLAGS_PERL_EMBED)

$(OUTPUT)test-libpython.bin:
	$(BUILD) $(FLAGS_PYTHON_EMBED)

$(OUTPUT)test-libbfd.bin:
	$(BUILD_BFD)

$(OUTPUT)test-libbfd-buildid.bin:
	$(BUILD_BFD) || $(BUILD_BFD) -liberty || $(BUILD_BFD) -liberty -lz

$(OUTPUT)test-disassembler-four-args.bin:
	$(BUILD_BFD) -lopcodes || $(BUILD_BFD) -lopcodes -liberty || \
	$(BUILD_BFD) -lopcodes -liberty -lz

$(OUTPUT)test-disassembler-init-styled.bin:
	$(BUILD_BFD) -lopcodes || $(BUILD_BFD) -lopcodes -liberty || \
	$(BUILD_BFD) -lopcodes -liberty -lz

$(OUTPUT)test-reallocarray.bin:
	$(BUILD)

$(OUTPUT)test-libbfd-liberty.bin:
	$(CC) $(CFLAGS) -Wall -Werror -o $@ test-libbfd.c -DPACKAGE='"perf"' $(LDFLAGS) -lbfd -ldl -liberty

$(OUTPUT)test-libbfd-liberty-z.bin:
	$(CC) $(CFLAGS) -Wall -Werror -o $@ test-libbfd.c -DPACKAGE='"perf"' $(LDFLAGS) -lbfd -ldl -liberty -lz

$(OUTPUT)test-cplus-demangle.bin:
	$(BUILD) -liberty

$(OUTPUT)test-cxa-demangle.bin:
	$(BUILDXX)

$(OUTPUT)test-backtrace.bin:
	$(BUILD)

$(OUTPUT)test-timerfd.bin:
	$(BUILD)

$(OUTPUT)test-libdw-dwarf-unwind.bin:
	$(BUILD) # -ldw provided by $(FEATURE_CHECK_LDFLAGS-libdw-dwarf-unwind)

$(OUTPUT)test-libbabeltrace.bin:
	$(BUILD) # -lbabeltrace provided by $(FEATURE_CHECK_LDFLAGS-libbabeltrace)

$(OUTPUT)test-compile-32.bin:
	$(CC) -m32 -o $@ test-compile.c

$(OUTPUT)test-compile-x32.bin:
	$(CC) -mx32 -o $@ test-compile.c

$(OUTPUT)test-zlib.bin:
	$(BUILD) -lz

$(OUTPUT)test-lzma.bin:
	$(BUILD) -llzma

$(OUTPUT)test-get_cpuid.bin:
	$(BUILD)

$(OUTPUT)test-bpf.bin:
	$(BUILD)

$(OUTPUT)test-libbpf.bin:
	$(BUILD) -lbpf

$(OUTPUT)test-libbpf-btf__load_from_kernel_by_id.bin:
	$(BUILD) -lbpf

$(OUTPUT)test-libbpf-bpf_prog_load.bin:
	$(BUILD) -lbpf

$(OUTPUT)test-libbpf-bpf_map_create.bin:
	$(BUILD) -lbpf

$(OUTPUT)test-libbpf-bpf_object__next_program.bin:
	$(BUILD) -lbpf

$(OUTPUT)test-libbpf-bpf_object__next_map.bin:
	$(BUILD) -lbpf

$(OUTPUT)test-libbpf-bpf_program__set_insns.bin:
	$(BUILD) -lbpf

$(OUTPUT)test-libbpf-btf__raw_data.bin:
	$(BUILD) -lbpf

$(OUTPUT)test-sdt.bin:
	$(BUILD)

$(OUTPUT)test-cxx.bin:
	$(BUILDXX) -std=gnu++11

$(OUTPUT)test-gettid.bin:
	$(BUILD)

$(OUTPUT)test-jvmti.bin:
	$(BUILD)

$(OUTPUT)test-jvmti-cmlr.bin:
	$(BUILD)

$(OUTPUT)test-llvm.bin:
	$(BUILDXX) -std=gnu++17 				\
		-I$(shell $(LLVM_CONFIG) --includedir) 		\
		-L$(shell $(LLVM_CONFIG) --libdir)		\
		$(shell $(LLVM_CONFIG) --libs Core BPF)		\
		$(shell $(LLVM_CONFIG) --system-libs)		\
		> $(@:.bin=.make.output) 2>&1

$(OUTPUT)test-llvm-version.bin:
	$(BUILDXX) -std=gnu++17					\
		-I$(shell $(LLVM_CONFIG) --includedir)		\
		> $(@:.bin=.make.output) 2>&1

$(OUTPUT)test-clang.bin:
	$(BUILDXX) -std=gnu++17					\
		-I$(shell $(LLVM_CONFIG) --includedir) 		\
		-L$(shell $(LLVM_CONFIG) --libdir)		\
		-Wl,--start-group -lclang-cpp -Wl,--end-group	\
		$(shell $(LLVM_CONFIG) --libs Core option)	\
		$(shell $(LLVM_CONFIG) --system-libs)		\
		> $(@:.bin=.make.output) 2>&1

-include $(OUTPUT)*.d

$(OUTPUT)test-libaio.bin:
	$(BUILD) -lrt

$(OUTPUT)test-libzstd.bin:
	$(BUILD) -lzstd

$(OUTPUT)test-clang-bpf-co-re.bin:
	$(CLANG) -S -g --target=bpf -o - $(patsubst %.bin,%.c,$(@F)) |	\
		grep BTF_KIND_VAR

$(OUTPUT)test-file-handle.bin:
	$(BUILD)

$(OUTPUT)test-libpfm4.bin:
	$(BUILD) -lpfm

###############################

clean:
	rm -f $(FILES) $(OUTPUT)*.d $(FILES:.bin=.make.output)
