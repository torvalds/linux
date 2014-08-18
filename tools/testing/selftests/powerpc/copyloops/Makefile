# The loops are all 64-bit code
CFLAGS += -m64
CFLAGS += -I$(CURDIR)
CFLAGS += -D SELFTEST

# Use our CFLAGS for the implicit .S rule
ASFLAGS = $(CFLAGS)

PROGS := copyuser_64 copyuser_power7 memcpy_64 memcpy_power7
EXTRA_SOURCES := validate.c ../harness.c

all: $(PROGS)

copyuser_64:     CPPFLAGS += -D COPY_LOOP=test___copy_tofrom_user_base
copyuser_power7: CPPFLAGS += -D COPY_LOOP=test___copy_tofrom_user_power7
memcpy_64:       CPPFLAGS += -D COPY_LOOP=test_memcpy
memcpy_power7:   CPPFLAGS += -D COPY_LOOP=test_memcpy_power7

$(PROGS): $(EXTRA_SOURCES)

run_tests: all
	@-for PROG in $(PROGS); do \
		./$$PROG; \
	done;

clean:
	rm -f $(PROGS) *.o

.PHONY: all run_tests clean
