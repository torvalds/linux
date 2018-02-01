PREFIX ?= /usr
SBINDIR ?= sbin
INSTALL ?= install
CFLAGS += -D__EXPORTED_HEADERS__ -I../../include/uapi -I../../include
CC = $(CROSS_COMPILE)gcc

TARGET = dell-smbios-example

all: $(TARGET)

%: %.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	$(RM) $(TARGET)

install: dell-smbios-example
	$(INSTALL) -D -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/$(SBINDIR)/$(TARGET)
