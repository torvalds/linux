# $OpenBSD: Makefile.inc,v 1.17 2025/06/28 12:51:08 jsing Exp $

# aarch64-specific libcrypto build rules

SRCS += crypto_cpu_caps.c

SRCS += sha1_aarch64.c
SRCS += sha1_aarch64_ce.S
SRCS += sha256_aarch64.c
SRCS += sha256_aarch64_ce.S
SRCS += sha512_aarch64.c
SRCS += sha512_aarch64_ce.S

AFLAGS+= -mmark-bti-property
