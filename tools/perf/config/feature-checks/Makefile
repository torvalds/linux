
FILES=					\
	test-all			\
	test-backtrace			\
	test-bionic			\
	test-dwarf			\
	test-fortify-source		\
	test-glibc			\
	test-gtk2			\
	test-gtk2-infobar		\
	test-hello			\
	test-libaudit			\
	test-libbfd			\
	test-liberty			\
	test-liberty-z			\
	test-cplus-demangle		\
	test-libelf			\
	test-libelf-getphdrnum		\
	test-libelf-mmap		\
	test-libnuma			\
	test-libperl			\
	test-libpython			\
	test-libpython-version		\
	test-libslang			\
	test-libunwind			\
	test-on-exit			\
	test-stackprotector-all		\
	test-stackprotector		\
	test-timerfd

CC := $(CC) -MD

all: $(FILES)

BUILD = $(CC) $(CFLAGS) $(LDFLAGS) -o $(OUTPUT)$@ $@.c

###############################

test-all:
	$(BUILD) -Werror -fstack-protector -fstack-protector-all -O2 -Werror -D_FORTIFY_SOURCE=2 -ldw -lelf -lnuma $(LIBUNWIND_LIBS) -lelf -laudit -I/usr/include/slang -lslang $(shell pkg-config --libs --cflags gtk+-2.0 2>/dev/null) $(FLAGS_PERL_EMBED) $(FLAGS_PYTHON_EMBED) -DPACKAGE='"perf"' -lbfd -ldl

test-hello:
	$(BUILD)

test-stackprotector-all:
	$(BUILD) -Werror -fstack-protector-all

test-stackprotector:
	$(BUILD) -Werror -fstack-protector -Wstack-protector

test-fortify-source:
	$(BUILD) -O2 -Werror -D_FORTIFY_SOURCE=2

test-bionic:
	$(BUILD)

test-libelf:
	$(BUILD) -lelf

test-glibc:
	$(BUILD)

test-dwarf:
	$(BUILD) -ldw

test-libelf-mmap:
	$(BUILD) -lelf

test-libelf-getphdrnum:
	$(BUILD) -lelf

test-libnuma:
	$(BUILD) -lnuma

test-libunwind:
	$(BUILD) $(LIBUNWIND_LIBS) -lelf

test-libaudit:
	$(BUILD) -laudit

test-libslang:
	$(BUILD) -I/usr/include/slang -lslang

test-gtk2:
	$(BUILD) $(shell pkg-config --libs --cflags gtk+-2.0 2>/dev/null)

test-gtk2-infobar:
	$(BUILD) $(shell pkg-config --libs --cflags gtk+-2.0 2>/dev/null)

grep-libs  = $(filter -l%,$(1))
strip-libs = $(filter-out -l%,$(1))

PERL_EMBED_LDOPTS = $(shell perl -MExtUtils::Embed -e ldopts 2>/dev/null)
PERL_EMBED_LDFLAGS = $(call strip-libs,$(PERL_EMBED_LDOPTS))
PERL_EMBED_LIBADD = $(call grep-libs,$(PERL_EMBED_LDOPTS))
PERL_EMBED_CCOPTS = `perl -MExtUtils::Embed -e ccopts 2>/dev/null`
FLAGS_PERL_EMBED=$(PERL_EMBED_CCOPTS) $(PERL_EMBED_LDOPTS)

test-libperl:
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

test-libpython:
	$(BUILD) $(FLAGS_PYTHON_EMBED)

test-libpython-version:
	$(BUILD) $(FLAGS_PYTHON_EMBED)

test-libbfd:
	$(BUILD) -DPACKAGE='"perf"' -lbfd -ldl

test-liberty:
	$(CC) -o $(OUTPUT)$@ test-libbfd.c -DPACKAGE='"perf"' -lbfd -ldl -liberty

test-liberty-z:
	$(CC) -o $(OUTPUT)$@ test-libbfd.c -DPACKAGE='"perf"' -lbfd -ldl -liberty -lz

test-cplus-demangle:
	$(BUILD) -liberty

test-on-exit:
	$(BUILD)

test-backtrace:
	$(BUILD)

test-timerfd:
	$(BUILD)

-include *.d

###############################

clean:
	rm -f $(FILES) *.d
