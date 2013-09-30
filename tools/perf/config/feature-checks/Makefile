
FILES=					\
	test-hello			\
	test-libnuma

CC := $(CC) -MD

all: $(FILES)

BUILD = $(CC) -o $(OUTPUT)$@ $@.c

###############################

test-hello:
	$(BUILD)

test-libnuma:
	$(BUILD) -lnuma

-include *.d */*.d

###############################

clean:
	rm -f $(FILES) *.d
