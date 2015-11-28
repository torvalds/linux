#!/bin/sh

arm-unknown-linux-gnueabihf-gcc -static -mfloat-abi=hard -mlittle-endian -mcpu=mpcore -march=armv6k -o init init.c
