#!/bin/sh

arm-unknown-linux-gnueabihf-gcc -mfloat-abi=hard -mlittle-endian -mcpu=mpcore -march=armv6k -o init init.c
