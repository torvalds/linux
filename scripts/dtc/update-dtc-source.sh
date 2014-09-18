#!/bin/sh
# Simple script to update the version of DTC carried by the Linux kernel
#
# This script assumes that the dtc and the linux git trees are in the
# same directory. After building dtc in the dtc directory, it copies the
# source files and generated source files into the scripts/dtc directory
# in the kernel and creates a git commit updating them to the new
# version.
#
# Usage: from the top level Linux source tree, run:
# $ ./scripts/dtc/update-dtc-source.sh
#
# The script will change into the dtc tree, build and test dtc, copy the
# relevant files into the kernel tree and create a git commit. The commit
# message will need to be modified to reflect the version of DTC being
# imported
#
# TODO:
# This script is pretty basic, but it is seldom used so a few manual tasks
# aren't a big deal. If anyone is interested in making it more robust, the
# the following would be nice:
# * Actually fail to complete if any testcase fails.
#   - The dtc "make check" target needs to return a failure
# * Extract the version number from the dtc repo for the commit message
# * Build dtc in the kernel tree
# * run 'make check" on dtc built from the kernel tree

set -ev

DTC_UPSTREAM_PATH=`pwd`/../dtc
DTC_LINUX_PATH=`pwd`/scripts/dtc

DTC_SOURCE="checks.c data.c dtc.c dtc.h flattree.c fstree.c livetree.c srcpos.c \
		srcpos.h treesource.c util.c util.h version_gen.h Makefile.dtc \
		dtc-lexer.l dtc-parser.y"
DTC_GENERATED="dtc-lexer.lex.c dtc-parser.tab.c dtc-parser.tab.h"

# Build DTC
cd $DTC_UPSTREAM_PATH
make clean
make check

# Copy the files into the Linux tree
cd $DTC_LINUX_PATH
for f in $DTC_SOURCE; do
	cp ${DTC_UPSTREAM_PATH}/${f} ${f}
	git add ${f}
done
for f in $DTC_GENERATED; do
	cp ${DTC_UPSTREAM_PATH}/$f ${f}_shipped
	git add ${f}_shipped
done

git commit -e -v -m "scripts/dtc: Update to upstream version [CHANGEME]"
