/*
  initramfs_data includes the compressed binary that is the
  filesystem used for early user space.
  Note: Older versions of "as" (prior to binutils 2.11.90.0.23
  released on 2001-07-14) dit not support .incbin.
  If you are forced to use older binutils than that then the
  following trick can be applied to create the resulting binary:


  ld -m elf_i386  --format binary --oformat elf32-i386 -r \
  -T initramfs_data.scr initramfs_data.cpio.gz -o initramfs_data.o
   ld -m elf_i386  -r -o built-in.o initramfs_data.o

  initramfs_data.scr looks like this:
SECTIONS
{
       .init.ramfs : { *(.data) }
}

  The above example is for i386 - the parameters vary from architectures.
  Eventually look up LDFLAGS_BLOB in an older version of the
  arch/$(ARCH)/Makefile to see the flags used before .incbin was introduced.

  Using .incbin has the advantage over ld that the correct flags are set
  in the ELF header, as required by certain architectures.
*/

.section .init.ramfs,"a"
.incbin "usr/initramfs_data.cpio.lzo"
