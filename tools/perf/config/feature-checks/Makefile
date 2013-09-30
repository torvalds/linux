
FILES=test-hello

CC := $(CC) -MD

all: $(FILES)

BUILD = $(CC) -o $(OUTPUT)$@ $@.c

###############################

test-hello:
	$(BUILD)

-include *.d */*.d

###############################

clean:
	rm -f $(FILES) *.d
