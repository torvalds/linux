PROG=	acpidump
SRCS=	acpidump.c
KERNEL_INCLUDE := ../../../include
CFLAGS += -Wall -Wstrict-prototypes -Os -s -D_LINUX -DDEFINE_ALTERNATE_TYPES -I$(KERNEL_INCLUDE) 

all: acpidump

acpidump : $(SRCS) 
	$(CC) $(CFLAGS) $(SRCS) -o $(PROG)

CLEANFILES= $(PROG)

clean : 
	rm -f $(CLEANFILES) $(patsubst %.c,%.o, $(SRCS)) *~

