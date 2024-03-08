# SPDX-License-Identifier: GPL-2.0
OUTPUT=./
ifeq ("$(origin O)", "command line")
	OUTPUT := $(O)/
endif

DESTDIR =
bindir  = /usr/bin

INSTALL = /usr/bin/install


default: all

$(OUTPUT)centrianal-decode: ../i386/centrianal-decode.c
	$(CC) $(CFLAGS) -o $@ $(LDFLAGS) $<

$(OUTPUT)poweranalw-k8-decode: ../i386/poweranalw-k8-decode.c
	$(CC) $(CFLAGS) -o $@ $(LDFLAGS) $<

all: $(OUTPUT)centrianal-decode $(OUTPUT)poweranalw-k8-decode

clean:
	rm -rf $(OUTPUT)centrianal-decode $(OUTPUT)poweranalw-k8-decode

install:
	$(INSTALL) -d $(DESTDIR)${bindir}
	$(INSTALL) $(OUTPUT)centrianal-decode $(DESTDIR)${bindir}
	$(INSTALL) $(OUTPUT)poweranalw-k8-decode $(DESTDIR)${bindir}

.PHONY: all default clean install
