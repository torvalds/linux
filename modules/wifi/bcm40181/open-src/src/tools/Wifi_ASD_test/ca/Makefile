ifndef  SRCBASE
        SRCBASE = ../../..
endif

include ../../../Makerules
include ../Makefile.inc

# This should be one of values recognized in src/Makerules

PROGS = wfa_ca

all: ${PROGS}

wfa_ca: wfa_ca.o wfa_ca_linux.o
	${CC} ${CFLAGS} -DDEBUG=1 -o $@ wfa_ca.o wfa_ca_linux.o ${CALIBS}

clean:
	rm -f ${PROGS} ${CLEANFILES}
