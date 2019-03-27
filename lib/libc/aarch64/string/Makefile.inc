# $FreeBSD$
#
# String handling from the Cortex Strings library
# https://git.linaro.org/toolchain/cortex-strings.git
#

.PATH: ${SRCTOP}/contrib/cortex-strings/src/aarch64

MDSRCS+= \
	memchr.S \
	memcmp.S \
	memcpy.S \
	memmove.S \
	memset.S \
	strchr.S \
	strcmp.S \
	strcpy.S \
	strlen.S \
	strncmp.S \
	strnlen.S
