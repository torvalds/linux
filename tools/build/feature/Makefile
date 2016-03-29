FILES=					\
	test-all.bin			\
	test-backtrace.bin		\
	test-bionic.bin			\
	test-dwarf.bin			\
	test-fortify-source.bin		\
	test-sync-compare-and-swap.bin	\
	test-glibc.bin			\
	test-gtk2.bin			\
	test-gtk2-infobar.bin		\
	test-hello.bin			\
	test-libaudit.bin		\
	test-libbfd.bin			\
	test-liberty.bin		\
	test-liberty-z.bin		\
	test-cplus-demangle.bin		\
	test-libelf.bin			\
	test-libelf-getphdrnum.bin	\
	test-libelf-mmap.bin		\
	test-libnuma.bin		\
	test-numa_num_possible_cpus.bin	\
	test-libperl.bin		\
	test-libpython.bin		\
	test-libpython-version.bin	\
	test-libslang.bin		\
	test-libcrypto.bin		\
	test-libunwind.bin		\
	test-libunwind-debug-frame.bin	\
	test-pthread-attr-setaffinity-np.bin	\
	test-stackprotector-all.bin	\
	test-timerfd.bin		\
	test-libdw-dwarf-unwind.bin	\
	test-libbabeltrace.bin		\
	test-compile-32.bin		\
	test-compile-x32.bin		\
	test-zlib.bin			\
	test-lzma.bin			\
	test-bpf.bin			\
	test-get_cpuid.bin

FILES := $(addprefix $(OUTPUT),$(FILES))

CC := $(CROSS_COMPILE)gcc -MD
PKG_CONFIG := $(CROSS_COMPILE)pkg-config

all: $(FILES)

__BUILD = $(CC) $(CFLAGS) -Wall -Werror -o $@ $(patsubst %.bin,%.c,$(@F)) $(LDFLAGS)
  BUILD = $(__BUILD) > $(@:.bin=.make.output) 2>&1

###############################

$(OUTPUT)test-all.bin:
	$(BUILD) -fstack-protector-all -O2 -D_FORTIFY_SOURCE=2 -ldw -lelf -lnuma -lelf -laudit -I/usr/include/slang -lslang $(shell $(PKG_CONFIG) --libs --cflags gtk+-2.0 2>/dev/null) $(FLAGS_PERL_EMBED) $(FLAGS_PYTHON_EMBED) -DPACKAGE='"perf"' -lbfd -ldl -lz -llzma

$(OUTPUT)test-hello.bin:
	$(BUILD)

$(OUTPUT)test-pthread-attr-setaffinity-np.bin:
	$(BUILD) -D_GNU_SOURCE -lpthread

$(OUTPUT)test-stackprotector-all.bin:
	$(BUILD) -fstack-protector-all

$(OUTPUT)test-fortify-source.bin:
	$(BUILD) -O2 -D_FORTIFY_SOURCE=2

$(OUTPUT)test-bionic.bin:
	$(BUILD)

$(OUTPUT)test-libelf.bin:
	$(BUILD) -lelf

$(OUTPUT)test-glibc.bin:
	$(BUILD)

DWARFLIBS := -ldw
ifeq ($(findstring -static,${LDFLAGS}),-static)
DWARFLIBS += -lelf -lebl -lz -llzma -lbz2
endif

$(OUTPUT)test-dwarf.bin:
	$(BUILD) $(DWARFLIBS)

$(OUTPUT)test-libelf-mmap.bin:
	$(BUILD) -lelf

$(OUTPUT)test-libelf-getphdrnum.bin:
	$(BUILD) -lelf

$(OUTPUT)test-libnuma.bin:
	$(BUILD) -lnuma

$(OUTPUT)test-numa_num_possible_cpus.bin:
	$(BUILD) -lnuma

$(OUTPUT)test-libunwind.bin:
	$(BUILD) -lelf

$(OUTPUT)test-libunwind-debug-frame.bin:
	$(BUILD) -lelf

$(OUTPUT)test-libaudit.bin:
	$(BUILD) -laudit

$(OUTPUT)test-libslang.bin:
	$(BUILD) -I/usr/include/slang -lslang

$(OUTPUT)test-libcrypto.bin:
	$(BUILD) -lcrypto

$(OUTPUT)test-gtk2.bin:
	$(BUILD) $(shell $(PKG_CONFIG) --libs --cflags gtk+-2.0 2>/dev/null)

$(OUTPUT)test-gtk2-infobar.bin:
	$(BUILD) $(shell $(PKG_CONFIG) --libs --cflags gtk+-2.0 2>/dev/null)

grep-libs  = $(filter -l%,$(1))
strip-libs = $(filter-out -l%,$(1))

PERL_EMBED_LDOPTS = $(shell perl -MExtUtils::Embed -e ldopts 2>/dev/null)
PERL_EMBED_LDFLAGS = $(call strip-libs,$(PERL_EMBED_LDOPTS))
PERL_EMBED_LIBADD = $(call grep-libs,$(PERL_EMBED_LDOPTS))
PERL_EMBED_CCOPTS = `perl -MExtUtils::Embed -e ccopts 2>/dev/null`
FLAGS_PERL_EMBED=$(PERL_EMBED_CCOPTS) $(PERL_EMBED_LDOPTS)

$(OUTPUT)test-libperl.bin:
	$(BUILD) $(FLAGS_PERL_EMBED)

$(OUTPUT)test-libpython.bin:
	$(BUILD)

$(OUTPUT)test-libpython-version.bin:
	$(BUILD)

$(OUTPUT)test-libbfd.bin:
	$(BUILD) -DPACKAGE='"perf"' -lbfd -lz -liberty -ldl

$(OUTPUT)test-liberty.bin:
	$(CC) $(CFLAGS) -Wall -Werror -o $@ test-libbfd.c -DPACKAGE='"perf"' $(LDFLAGS) -lbfd -ldl -liberty

$(OUTPUT)test-liberty-z.bin:
	$(CC) $(CFLAGS) -Wall -Werror -o $@ test-libbfd.c -DPACKAGE='"perf"' $(LDFLAGS) -lbfd -ldl -liberty -lz

$(OUTPUT)test-cplus-demangle.bin:
	$(BUILD) -liberty

$(OUTPUT)test-backtrace.bin:
	$(BUILD)

$(OUTPUT)test-timerfd.bin:
	$(BUILD)

$(OUTPUT)test-libdw-dwarf-unwind.bin:
	$(BUILD) # -ldw provided by $(FEATURE_CHECK_LDFLAGS-libdw-dwarf-unwind)

$(OUTPUT)test-libbabeltrace.bin:
	$(BUILD) # -lbabeltrace provided by $(FEATURE_CHECK_LDFLAGS-libbabeltrace)

$(OUTPUT)test-sync-compare-and-swap.bin:
	$(BUILD)

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

-include $(OUTPUT)*.d

###############################

clean:
	rm -f $(FILES) $(OUTPUT)*.d $(FILES:.bin=.make.output)
