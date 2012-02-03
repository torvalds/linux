ifndef  SRCBASE
        SRCBASE = ../../..
endif

include ../../../Makerules
include ../Makefile.inc

PROGS = ca_cli tc_cli 

all: ${PROGS}

ca_cli: ca_cli.o 
	${CC} ${CFLAGS} -o $@ ca_cli.o ${CALIBS}

tc_cli: tc_cli.o
	${CC} ${CFLAGS} -o $@ tc_cli.o ${CALIBS}

clean:
	rm -f ${PROGS} ${CLEANFILES}
