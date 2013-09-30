
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

-include *.d */*.d

###############################

clean:
	rm -f $(FILES) *.d
