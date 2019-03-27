/* *INDENT-OFF* */ /* THIS FILE IS GENERATED */

/* This file was created with the aid of ``regdat.sh'' and ``../../../../contrib/gdb/gdb/regformats/reg-i386.dat''.  */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "regdef.h"
#include "regcache.h"

struct reg regs_i386[] = {
  { "eax", 0, 32 },
  { "ecx", 32, 32 },
  { "edx", 64, 32 },
  { "ebx", 96, 32 },
  { "esp", 128, 32 },
  { "ebp", 160, 32 },
  { "esi", 192, 32 },
  { "edi", 224, 32 },
  { "eip", 256, 32 },
  { "eflags", 288, 32 },
  { "cs", 320, 32 },
  { "ss", 352, 32 },
  { "ds", 384, 32 },
  { "es", 416, 32 },
  { "fs", 448, 32 },
  { "gs", 480, 32 },
  { "st0", 512, 80 },
  { "st1", 592, 80 },
  { "st2", 672, 80 },
  { "st3", 752, 80 },
  { "st4", 832, 80 },
  { "st5", 912, 80 },
  { "st6", 992, 80 },
  { "st7", 1072, 80 },
  { "fctrl", 1152, 32 },
  { "fstat", 1184, 32 },
  { "ftag", 1216, 32 },
  { "fiseg", 1248, 32 },
  { "fioff", 1280, 32 },
  { "foseg", 1312, 32 },
  { "fooff", 1344, 32 },
  { "fop", 1376, 32 },
  { "xmm0", 1408, 128 },
  { "xmm1", 1536, 128 },
  { "xmm2", 1664, 128 },
  { "xmm3", 1792, 128 },
  { "xmm4", 1920, 128 },
  { "xmm5", 2048, 128 },
  { "xmm6", 2176, 128 },
  { "xmm7", 2304, 128 },
  { "mxcsr", 2432, 32 },
};

const char *expedite_regs_i386[] = { "ebp", "esp", "eip", 0 };

void
init_registers ()
{
    set_register_cache (regs_i386,
			sizeof (regs_i386) / sizeof (regs_i386[0]));
    gdbserver_expedite_regs = expedite_regs_i386;
}
