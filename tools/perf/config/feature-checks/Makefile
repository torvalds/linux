
FILES=					\
	test-hello			\
	test-stackprotector-all		\
	test-stackprotector		\
	test-volatile-register-var	\
	test-fortify-source		\
	test-bionic			\
	test-libelf			\
	test-glibc			\
	test-dwarf			\
	test-libelf-mmap		\
	test-libelf-getphdrnum		\
	test-libunwind			\
	test-libaudit			\
	test-libslang			\
	test-gtk2			\
	test-gtk2-infobar		\
	test-libperl			\
	test-libpython			\
	test-libpython-version		\
	test-strlcpy			\
	test-libbfd			\
	test-on-exit			\
	test-backtrace			\
	test-libnuma

CC := $(CC) -MD

all: $(FILES)

BUILD = $(CC) -o $(OUTPUT)$@ $@.c

###############################

test-hello:
	$(BUILD)

test-stackprotector-all:
	$(BUILD) -Werror -fstack-protector-all

test-stackprotector:
	$(BUILD) -Werror -fstack-protector

test-volatile-register-var:
	$(BUILD) -Werror -Wvolatile-register-var

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
	$(BUILD) -lunwind -lunwind-x86_64 -lelf

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

test-strlcpy:
	$(BUILD)

test-libbfd:
	$(BUILD) -DPACKAGE='perf' -DPACKAGE=perf -lbfd -ldl

test-on-exit:
	$(BUILD)

test-backtrace:
	$(BUILD)

-include *.d */*.d

###############################

clean:
	rm -f $(FILES) *.d
