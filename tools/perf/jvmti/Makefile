ARCH=$(shell uname -m)

ifeq ($(ARCH), x86_64)
JARCH=amd64
endif
ifeq ($(ARCH), armv7l)
JARCH=armhf
endif
ifeq ($(ARCH), armv6l)
JARCH=armhf
endif
ifeq ($(ARCH), aarch64)
JARCH=aarch64
endif
ifeq ($(ARCH), ppc64)
JARCH=powerpc
endif
ifeq ($(ARCH), ppc64le)
JARCH=powerpc
endif

DESTDIR=/usr/local

VERSION=1
REVISION=0
AGE=0

LN=ln -sf
RM=rm

SLIBJVMTI=libjvmti.so.$(VERSION).$(REVISION).$(AGE)
VLIBJVMTI=libjvmti.so.$(VERSION)
SLDFLAGS=-shared -Wl,-soname -Wl,$(VLIBJVMTI)
SOLIBEXT=so

# The following works at least on fedora 23, you may need the next
# line for other distros.
ifneq (,$(wildcard /usr/sbin/update-java-alternatives))
JDIR=$(shell /usr/sbin/update-java-alternatives -l | head -1 | cut -d ' ' -f 3)
else
  ifneq (,$(wildcard /usr/sbin/alternatives))
    JDIR=$(shell alternatives --display java | tail -1 | cut -d' ' -f 5 | sed 's%/jre/bin/java.%%g')
  endif
endif
ifndef JDIR
$(error Could not find alternatives command, you need to set JDIR= to point to the root of your Java directory)
else
  ifeq (,$(wildcard $(JDIR)/include/jvmti.h))
  $(error the openjdk development package appears to me missing, install and try again)
  endif
endif
$(info Using Java from $(JDIR))
# -lrt required in 32-bit mode for clock_gettime()
LIBS=-lelf -lrt
INCDIR=-I $(JDIR)/include -I $(JDIR)/include/linux

TARGETS=$(SLIBJVMTI)

SRCS=libjvmti.c jvmti_agent.c
OBJS=$(SRCS:.c=.o)
SOBJS=$(OBJS:.o=.lo)
OPT=-O2 -g -Werror -Wall

CFLAGS=$(INCDIR) $(OPT)

all: $(TARGETS)

.c.o:
	$(CC) $(CFLAGS) -c $*.c
.c.lo:
	$(CC) -fPIC -DPIC $(CFLAGS) -c $*.c -o $*.lo

$(OBJS) $(SOBJS): Makefile jvmti_agent.h ../util/jitdump.h

$(SLIBJVMTI):  $(SOBJS)
	$(CC) $(CFLAGS) $(SLDFLAGS)  -o $@ $(SOBJS) $(LIBS)
	$(LN) $@ libjvmti.$(SOLIBEXT)

clean:
	$(RM) -f *.o *.so.* *.so *.lo

install:
	-mkdir -p $(DESTDIR)/lib
	install -m 755 $(SLIBJVMTI) $(DESTDIR)/lib/
	(cd $(DESTDIR)/lib; $(LN) $(SLIBJVMTI) $(VLIBJVMTI))
	(cd $(DESTDIR)/lib; $(LN) $(SLIBJVMTI) libjvmti.$(SOLIBEXT))
	ldconfig

.SUFFIXES: .c .S .o .lo
