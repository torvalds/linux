#!/bin/sh

# This script generates the dummy HFS filesystem used for the PowerPC boot
# blocks. It uses hfsutils (emulators/hfsutils) to generate a template
# filesystem with the relevant interesting files. These are then found by
# grep, and the offsets written to a Makefile snippet.
#
# Because of licensing concerns, and because it is overkill, we do not
# distribute hfsutils as a build tool. If you need to regenerate the HFS
# template (e.g. because the boot block or the CHRP script have grown),
# you must install it from ports.

# $FreeBSD$

HFS_SIZE=400		#Size in 2048-byte blocks of the produced image
LOADER_SIZE=300k

# Generate 800K HFS image
OUTPUT_FILE=hfs-boot

dd if=/dev/zero of=$OUTPUT_FILE bs=2048 count=$HFS_SIZE
hformat -l "FreeBSD Install" $OUTPUT_FILE
hmount $OUTPUT_FILE

# Create and bless a directory for the boot loader
hmkdir ppc
hattrib -b ppc
hcd ppc

# Make the CHRP boot script, which gets loader from the ISO9660 partition
cat > bootinfo.txt << EOF
<CHRP-BOOT>
<DESCRIPTION>FreeBSD/powerpc bootloader</DESCRIPTION>
<OS-NAME>FreeBSD</OS-NAME>
<VERSION> $FreeBSD: head/stand/powerpc/boot1.chrp/bootinfo.txt 184490 2008-10
-31 00:52:31Z nwhitehorn $ </VERSION>

<COMPATIBLE>
MacRISC MacRISC3 MacRISC4
</COMPATIBLE>
<BOOT-SCRIPT>
" screen" output
boot &device;:,\ppc\loader &device;:0
</BOOT-SCRIPT>
</CHRP-BOOT>
EOF
echo 'Loader START' | dd of=loader.tmp cbs=$LOADER_SIZE count=1 conv=block

hcopy bootinfo.txt :bootinfo.txt
hcopy loader.tmp :loader
hattrib -c chrp -t tbxi bootinfo.txt
humount

rm bootinfo.txt
rm loader.tmp

bzip2 $OUTPUT_FILE
echo 'HFS boot filesystem created by generate-hfs.sh' > $OUTPUT_FILE.bz2.uu
echo 'DO NOT EDIT' >> $OUTPUT_FILE.bz2.uu
echo '$FreeBSD$' >> $OUTPUT_FILE.bz2.uu

uuencode $OUTPUT_FILE.bz2 $OUTPUT_FILE.bz2 >> $OUTPUT_FILE.bz2.uu
rm $OUTPUT_FILE.bz2

