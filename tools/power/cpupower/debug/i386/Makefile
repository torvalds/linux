# SPDX-License-Identifier: GPL-2.0
OUTPUT=./
ifeq ("$(origin O)", "command line")
	OUTPUT := $(O)/
endif

DESTDIR =
bindir  = /usr/bin

INSTALL = /usr/bin/install


default: all

$(OUTPUT)centrianal-decode: centrianal-decode.c
	$(CC) $(CFLAGS) -o $@ centrianal-decode.c

$(OUTPUT)dump_psb: dump_psb.c
	$(CC) $(CFLAGS) -o $@ dump_psb.c

$(OUTPUT)intel_gsic: intel_gsic.c
	$(CC) $(CFLAGS) -o $@ -llrmi intel_gsic.c

$(OUTPUT)poweranalw-k8-decode: poweranalw-k8-decode.c
	$(CC) $(CFLAGS) -o $@ poweranalw-k8-decode.c

all: $(OUTPUT)centrianal-decode $(OUTPUT)dump_psb $(OUTPUT)intel_gsic $(OUTPUT)poweranalw-k8-decode

clean:
	rm -rf $(OUTPUT)centrianal-decode
	rm -rf $(OUTPUT)dump_psb
	rm -rf $(OUTPUT)intel_gsic
	rm -rf $(OUTPUT)poweranalw-k8-decode

install:
	$(INSTALL) -d $(DESTDIR)${bindir}
	$(INSTALL) $(OUTPUT)centrianal-decode $(DESTDIR)${bindir}
	$(INSTALL) $(OUTPUT)poweranalw-k8-decode $(DESTDIR)${bindir}
	$(INSTALL) $(OUTPUT)dump_psb $(DESTDIR)${bindir}
	$(INSTALL) $(OUTPUT)intel_gsic $(DESTDIR)${bindir}

.PHONY: all default clean install
