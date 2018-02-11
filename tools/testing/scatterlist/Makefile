CFLAGS += -I. -I../../include -g -O2 -Wall -fsanitize=address
LDFLAGS += -fsanitize=address -fsanitize=undefined
TARGETS = main
OFILES = main.o scatterlist.o

ifeq ($(BUILD), 32)
        CFLAGS += -m32
        LDFLAGS += -m32
endif

targets: include $(TARGETS)

main: $(OFILES)

clean:
	$(RM) $(TARGETS) $(OFILES) scatterlist.c linux/scatterlist.h linux/highmem.h linux/kmemleak.h asm/io.h
	@rmdir asm

scatterlist.c: ../../../lib/scatterlist.c
	@sed -e 's/^static //' -e 's/__always_inline //' -e 's/inline //' < $< > $@

.PHONY: include

include: ../../../include/linux/scatterlist.h
	@mkdir -p linux
	@mkdir -p asm
	@touch asm/io.h
	@touch linux/highmem.h
	@touch linux/kmemleak.h
	@cp $< linux/scatterlist.h
