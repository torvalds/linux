
FILES=					\
	test-hello			\
	test-stackprotector-all		\
	test-stackprotector		\
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

test-libnuma:
	$(BUILD) -lnuma

-include *.d */*.d

###############################

clean:
	rm -f $(FILES) *.d
