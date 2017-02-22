# Makefile for LEDs tools

CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Wextra -g -I../../include/uapi

all: uledmon
%: %.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	$(RM) uledmon

.PHONY: all clean
