PROG= acpidump
SRCS=	acpidump.c
KERNEL_INCLUDE := ../../../include
CFLAGS += -Wall -Wstrict-prototypes -Wdeclaration-after-statement -Os -s -D_LINUX -DDEFINE_ALTERNATE_TYPES -I$(KERNEL_INCLUDE) 

all: acpidump
$(PROG) : $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(PROG)

CLEANFILES= $(PROG)

clean : 
	rm -f $(CLEANFILES) $(patsubst %.c,%.o, $(SRCS)) *~

install :
	install acpidump /usr/sbin/acpidump
	install acpidump.8 /usr/share/man/man8

