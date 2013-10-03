# Makefile for vm tools
#
TARGETS=page-types slabinfo

LK_DIR = ../lib/lk
LIBLK = $(LK_DIR)/liblk.a

CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Wextra -I../lib/
LDFLAGS = $(LIBLK)

$(TARGETS): liblk

liblk:
	make -C $(LK_DIR)

%: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	$(RM) page-types slabinfo
	make -C ../lib/lk clean
