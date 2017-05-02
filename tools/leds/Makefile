# Makefile for LEDs tools

CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Wextra -g -I../../include/uapi

all: uledmon led_hw_brightness_mon
%: %.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	$(RM) uledmon led_hw_brightness_mon

.PHONY: all clean
