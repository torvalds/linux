ec_access: ec_access.o
	$(ECHO) "  LD      " $@
	$(QUIET) $(LD) $(CFLAGS) $(LDFLAGS) $< -o $@
	$(QUIET) $(STRIPCMD) $@

%.o: %.c
	$(ECHO) "  CC      " $@
	$(QUIET) $(CC) -c $(CFLAGS) -o $@ $<

all: ec_access

install:
	$(INSTALL) -d $(DESTDIR)${sbindir}
	$(INSTALL_PROGRAM) ec_access $(DESTDIR)${sbindir}

uninstall:
	- rm -f $(DESTDIR)${sbindir}/ec_access

clean:
	-rm -f $(OUTPUT)ec_access

.PHONY: all install uninstall
