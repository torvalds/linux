#!/bin/sh

# $FreeBSD$

#
# Full list of all arches, but we only build a subset. All different mips add any
# value, and there's a few others we just don't support.
#
#	mips/mipsel mips/mips mips/mips64el mips/mips64 mips/mipsn32 \
#	mips/mipselhf mips/mipshf mips/mips64elhf mips/mips64hf \
#	powerpc/powerpc powerpc/powerpc64 powerpc/powerpcspe \
#	riscv/riscv64 riscv/riscv64sf
#
# This script is expected to be run in stand (though you could run it anywhere
# in the tree). It does a full clean build. For stand you can do all the archs in
# about a minute or two on a fast machine. It's also possible that you need a full
# make universe for this to work completely.
#
# Output is put into _.boot.$TARGET_ARCH.log in sys.boot.
#

dobuild()
{
    local ta=$1
    local lf=$2
    local opt=$3

    echo -n "Building $ta ${opt} ... "
    objdir=$(make buildenv TARGET_ARCH=$ta BUILDENV_SHELL="make -V .OBJDIR" | tail -1)
    rm -rf ${objdir}
    if ! make buildenv TARGET_ARCH=$ta BUILDENV_SHELL="make clean cleandepend cleandir obj depend"  \
	 > $lf 2>&1; then
	echo "Fail (cleanup)"
	continue
    fi
    if ! make buildenv TARGET_ARCH=$ta BUILDENV_SHELL="make ${opt} -j 20 all"  \
	 >> $lf 2>&1; then
	echo "Fail (build)"
	continue
    fi
    echo "Success"
}

top=$(make -V SRCTOP)
cd $top/stand

# Build without forth
for i in \
	amd64/amd64 \
	i386/i386 \
	; do
    ta=${i##*/}
    dobuild $ta _.boot.${ta}.no_forth.log "WITHOUT_FORTH=yes"
done

# Build without GELI
for i in \
	amd64/amd64 \
	i386/i386 \
	; do
    ta=${i##*/}
    dobuild $ta _.boot.${ta}.no_geli.log "WITHOUT_LOADER_GEIL=yes"
done

# Default build for a goodly selection of architectures
for i in \
	amd64/amd64 \
	arm/arm arm/armv7 \
	arm64/aarch64 \
	i386/i386 \
	mips/mips mips/mips64 \
	powerpc/powerpc powerpc/powerpc64 \
	sparc64/sparc64 \
	; do
    ta=${i##*/}
    dobuild $ta _.boot.${ta}.log ""
done

# Default build for a goodly selection of architectures with Lua
for i in \
	amd64/amd64 \
	arm/arm arm/armv7 \
	arm64/aarch64 \
	i386/i386 \
	mips/mips mips/mips64 \
	powerpc/powerpc powerpc/powerpc64 \
	sparc64/sparc64 \
	; do
    ta=${i##*/}
    dobuild $ta _.boot.${ta}.lua.log "MK_LOADEDER_LUA=yes MK_FORTH=no"
done

# Build w/o ZFS
for i in \
	amd64/amd64 \
	i386/i386 \
	sparc64/sparc64 \
	; do
    ta=${i##*/}
    dobuild $ta _.boot.${ta}.no_zfs.log "MK_ZFS=no"
done

# Build with firewire
for i in \
	amd64/amd64 \
	i386/i386 \
	; do
    ta=${i##*/}
    dobuild $ta _.boot.${ta}.firewire.log "MK_LOADER_FIREWIRE=yes"
done

# Build with LOADER_DEBUG, only sparc64 does this.
for i in \
	sparc64/sparc64 \
	; do
    ta=${i##*/}
    dobuild $ta _.boot.${ta}.debug.log "LOADER_DEBUG=yes"
done

