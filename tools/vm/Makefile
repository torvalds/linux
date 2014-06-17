# Makefile for vm tools
#
TARGETS=page-types slabinfo

LIB_DIR = ../lib/api
LIBS = $(LIB_DIR)/libapikfs.a

CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Wextra -I../lib/
LDFLAGS = $(LIBS)

$(TARGETS): $(LIBS)

$(LIBS):
	make -C $(LIB_DIR)

%: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	$(RM) page-types slabinfo
	make -C $(LIB_DIR) clean
