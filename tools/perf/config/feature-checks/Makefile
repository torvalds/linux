
FILES=					\
	test-all.bin			\
	test-backtrace.bin		\
	test-bionic.bin			\
	test-dwarf.bin			\
	test-fortify-source.bin		\
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
	test-stackprotector-all.bin	\
	test-timerfd.bin		\
	test-libdw-dwarf-unwind.bin

CC := $(CROSS_COMPILE)gcc -MD
PKG_CONFIG := $(CROSS_COMPILE)pkg-config

all: $(FILES)

BUILD = $(CC) $(CFLAGS) -o $(OUTPUT)$@ $(patsubst %.bin,%.c,$@) $(LDFLAGS)

###############################

test-all.bin:
	$(BUILD) -Werror -fstack-protector-all -O2 -Werror -D_FORTIFY_SOURCE=2 -ldw -lelf -lnuma -lelf -laudit -I/usr/include/slang -lslang $(shell $(PKG_CONFIG) --libs --cflags gtk+-2.0 2>/dev/null) $(FLAGS_PERL_EMBED) $(FLAGS_PYTHON_EMBED) -DPACKAGE='"perf"' -lbfd -ldl

test-hello.bin:
	$(BUILD)

test-stackprotector-all.bin:
	$(BUILD) -Werror -fstack-protector-all

test-fortify-source.bin:
	$(BUILD) -O2 -Werror -D_FORTIFY_SOURCE=2

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

override PYTHON := python
override PYTHON_CONFIG := python-config

escape-for-shell-sq =  $(subst ','\'',$(1))
shell-sq = '$(escape-for-shell-sq)'

PYTHON_CONFIG_SQ = $(call shell-sq,$(PYTHON_CONFIG))

PYTHON_EMBED_LDOPTS = $(shell $(PYTHON_CONFIG_SQ) --ldflags 2>/dev/null)
PYTHON_EMBED_LDFLAGS = $(call strip-libs,$(PYTHON_EMBED_LDOPTS))
PYTHON_EMBED_LIBADD = $(call grep-libs,$(PYTHON_EMBED_LDOPTS))
PYTHON_EMBED_CCOPTS = $(shell $(PYTHON_CONFIG_SQ) --cflags 2>/dev/null)
FLAGS_PYTHON_EMBED = $(PYTHON_EMBED_CCOPTS) $(PYTHON_EMBED_LDOPTS)

test-libpython.bin:
	$(BUILD) $(FLAGS_PYTHON_EMBED)

test-libpython-version.bin:
	$(BUILD) $(FLAGS_PYTHON_EMBED)

test-libbfd.bin:
	$(BUILD) -DPACKAGE='"perf"' -lbfd -lz -liberty -ldl

test-liberty.bin:
	$(CC) -o $(OUTPUT)$@ test-libbfd.c -DPACKAGE='"perf"' -lbfd -ldl -liberty

test-liberty-z.bin:
	$(CC) -o $(OUTPUT)$@ test-libbfd.c -DPACKAGE='"perf"' -lbfd -ldl -liberty -lz

test-cplus-demangle.bin:
	$(BUILD) -liberty

test-backtrace.bin:
	$(BUILD)

test-timerfd.bin:
	$(BUILD)

test-libdw-dwarf-unwind.bin:
	$(BUILD)

-include *.d

###############################

clean:
	rm -f $(FILES) *.d
