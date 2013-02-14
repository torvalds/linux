CC		= $(CROSS_COMPILE)gcc
BUILD_OUTPUT	:= $(PWD)
PREFIX		:= /usr
DESTDIR		:=

turbostat : turbostat.c
CFLAGS +=	-Wall
CFLAGS +=	-I../../../../arch/x86/include/uapi/

%: %.c
	@mkdir -p $(BUILD_OUTPUT)
	$(CC) $(CFLAGS) $< -o $(BUILD_OUTPUT)/$@

.PHONY : clean
clean :
	@rm -f $(BUILD_OUTPUT)/turbostat

install : turbostat
	install -d  $(DESTDIR)$(PREFIX)/bin
	install $(BUILD_OUTPUT)/turbostat $(DESTDIR)$(PREFIX)/bin/turbostat
	install -d  $(DESTDIR)$(PREFIX)/share/man/man8
	install turbostat.8 $(DESTDIR)$(PREFIX)/share/man/man8
