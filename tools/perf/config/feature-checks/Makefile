
FILES=test-hello

all: $(FILES)

BUILD = $(CC) -o $(OUTPUT)$@ $@.c

###############################

test-hello: test-hello.c
	$(BUILD)

###############################

clean:
	rm -f $(FILES)
