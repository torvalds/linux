#!/bin/sh

CROSS_PATH="/opt/x-tools/arm-unknown-linux-gnueabi/bin/"

export PATH=$PATH:$CROSS_PATH
arm-unknown-linux-gnueabi-gcc init.c -o init
