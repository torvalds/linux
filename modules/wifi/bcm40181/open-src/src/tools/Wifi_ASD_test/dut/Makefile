ifndef  SRCBASE
        SRCBASE = ../../../
endif

include ../../../Makerules
include ../Makefile.inc

DUT_OBJ= ./$(TARGETARCH)/wfa_dut.o ./$(TARGETARCH)/wfa_dut_init.o
all: ${DUT_OBJ}

$(TARGETARCH)/%.o: %.c
	@mkdir -p $(TARGETARCH)
	$(CC) -c $(CFLAGS) -o $@ $^

clean:
	rm -f ${PROGS} ${CLEANFILES}
