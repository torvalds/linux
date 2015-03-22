
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
	test-libperl.bin		\
	test-libpython.bin		\
	test-libpython-version.bin	\
	test-libslang.bin		\
	test-libunwind.bin		\
	test-libunwind-debug-frame.bin	\
	test-pthread-attr-setaffinity-np.bin	\
	test-stackprotector-all.bin	\
	test-timerfd.bin		\
	test-libdw-dwarf-unwind.bin	\
	test-libbabeltrace.bin		\
	test-compile-32.bin		\
	test-compile-x32.bin		\
	test-zlib.bin

CC := $(CROSS_COMPILE)gcc -MD
PKG_CONFIG := $(CROSS_COMPILE)pkg-config

all: $(FILES)

__BUILD = $(CC) $(CFLAGS) -Wall -Werror -o $(OUTPUT)$@ $(patsubst %.bin,%.c,$@) $(LDFLAGS)
  BUILD = $(__BUILD) > $(OUTPUT)$(@:.bin=.make.output) 2>&1

###############################

test-all.bin:
	$(BUILD) -fstack-protector-all -O2 -D_FORTIFY_SOURCE=2 -ldw -lelf -lnuma -lelf -laudit -I/usr/include/slang -lslang $(shell $(PKG_CONFIG) --libs --cflags gtk+-2.0 2>/dev/null) $(FLAGS_PERL_EMBED) $(FLAGS_PYTHON_EMBED) -DPACKAGE='"perf"' -lbfd -ldl -lz

test-hello.bin:
	$(BUILD)

test-pthread-attr-setaffinity-np.bin:
	$(BUILD) -D_GNU_SOURCE -lpthread

test-stackprotector-all.bin:
	$(BUILD) -fstack-protector-all

test-fortify-source.bin:
	$(BUILD) -O2 -D_FORTIFY_SOURCE=2

test-bionic.bin:
	$(BUILD)

test-libelf.bin:
	$(BUILD) -lelf

test-glibc.bin:
	$(BUILD)

test-dwarf.bin:
	$(BUILD) -ldw

test-libelf-mmap.bin:
	$(BUILD) -lelf

test-libelf-getphdrnum.bin:
	$(BUILD) -lelf

test-libnuma.bin:
	$(BUILD) -lnuma

test-libunwind.bin:
	$(BUILD) -lelf

test-libunwind-debug-frame.bin:
	$(BUILD) -lelf

test-libaudit.bin:
	$(BUILD) -laudit

test-libslang.bin:
	$(BUILD) -I/usr/include/slang -lslang

test-gtk2.bin:
	$(BUILD) $(shell $(PKG_CONFIG) --libs --cflags gtk+-2.0 2>/dev/null)

test-gtk2-infobar.bin:
	$(BUILD) $(shell $(PKG_CONFIG) --libs --cflags gtk+-2.0 2>/dev/null)

grep-libs  = $(filter -l%,$(1))
strip-libs = $(filter-out -l%,$(1))

PERL_EMBED_LDOPTS = $(shell perl -MExtUtils::Embed -e ldopts 2>/dev/null)
PERL_EMBED_LDFLAGS = $(call strip-libs,$(PERL_EMBED_LDOPTS))
PERL_EMBED_LIBADD = $(call grep-libs,$(PERL_EMBED_LDOPTS))
PERL_EMBED_CCOPTS = `perl -MExtUtils::Embed -e ccopts 2>/dev/null`
FLAGS_PERL_EMBED=$(PERL_EMBED_CCOPTS) $(PERL_EMBED_LDOPTS)

test-libperl.bin:
	$(BUILD) $(FLAGS_PERL_EMBED)

test-libpython.bin:
	$(BUILD)

test-libpython-version.bin:
	$(BUILD)

test-libbfd.bin:
	$(BUILD) -DPACKAGE='"perf"' -lbfd -lz -liberty -ldl

test-liberty.bin:
	$(CC) -Wall -Werror -o $(OUTPUT)$@ test-libbfd.c -DPACKAGE='"perf"' -lbfd -ldl -liberty

test-liberty-z.bin:
	$(CC) -Wall -Werror -o $(OUTPUT)$@ test-libbfd.c -DPACKAGE='"perf"' -lbfd -ldl -liberty -lz

test-cplus-demangle.bin:
	$(BUILD) -liberty

test-backtrace.bin:
	$(BUILD)

test-timerfd.bin:
	$(BUILD)

test-libdw-dwarf-unwind.bin:
	$(BUILD) # -ldw provided by $(FEATURE_CHECK_LDFLAGS-libdw-dwarf-unwind)

test-libbabeltrace.bin:
	$(BUILD) # -lbabeltrace provided by $(FEATURE_CHECK_LDFLAGS-libbabeltrace)

test-sync-compare-and-swap.bin:
	$(BUILD)

test-compile-32.bin:
	$(CC) -m32 -o $(OUTPUT)$@ test-compile.c

test-compile-x32.bin:
	$(CC) -mx32 -o $(OUTPUT)$@ test-compile.c

test-zlib.bin:
	$(BUILD) -lz

-include *.d

###############################

clean:
	rm -f $(FILES) *.d $(FILES:.bin=.make.output)
