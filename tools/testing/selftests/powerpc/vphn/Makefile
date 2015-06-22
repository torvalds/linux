PROG := test-vphn

CFLAGS += -m64

all: $(PROG)

$(PROG): ../harness.c

run_tests: all
	./$(PROG)

clean:
	rm -f $(PROG)

.PHONY: all run_tests clean
