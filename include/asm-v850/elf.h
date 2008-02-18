#ifndef __V850_ELF_H__
#define __V850_ELF_H__

/*
 * ELF register definitions..
 */

#include <asm/ptrace.h>
#include <asm/user.h>
#include <asm/byteorder.h>

typedef unsigned long elf_greg_t;

#define ELF_NGREG (sizeof (struct pt_regs) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef struct user_fpu_struct elf_fpregset_t;

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x)  \
  ((x)->e_machine == EM_V850 || (x)->e_machine == EM_CYGNUS_V850)


/* v850 relocation types.  */
#define R_V850_NONE		0
#define R_V850_9_PCREL		1
#define R_V850_22_PCREL		2
#define R_V850_HI16_S		3
#define R_V850_HI16		4
#define R_V850_LO16		5
#define R_V850_32		6
#define R_V850_16		7
#define R_V850_8		8
#define R_V850_SDA_16_16_OFFSET	9	/* For ld.b, st.b, set1, clr1,
					   not1, tst1, movea, movhi */
#define R_V850_SDA_15_16_OFFSET	10	/* For ld.w, ld.h, ld.hu, st.w, st.h */
#define R_V850_ZDA_16_16_OFFSET	11	/* For ld.b, st.b, set1, clr1,
					   not1, tst1, movea, movhi */
#define R_V850_ZDA_15_16_OFFSET	12	/* For ld.w, ld.h, ld.hu, st.w, st.h */
#define R_V850_TDA_6_8_OFFSET	13	/* For sst.w, sld.w */
#define R_V850_TDA_7_8_OFFSET	14	/* For sst.h, sld.h */
#define R_V850_TDA_7_7_OFFSET	15	/* For sst.b, sld.b */
#define R_V850_TDA_16_16_OFFSET	16	/* For set1, clr1, not1, tst1,
					   movea, movhi */
#define R_V850_NUM		17


/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS32
#ifdef __LITTLE_ENDIAN__
#define ELF_DATA	ELFDATA2LSB
#else
#define ELF_DATA	ELFDATA2MSB
#endif
#define ELF_ARCH	EM_V850

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	4096


#define ELF_CORE_COPY_REGS(_dest,_regs)				\
	memcpy((char *) &_dest, (char *) _regs,			\
	       sizeof(struct pt_regs));

/* This yields a mask that user programs can use to figure out what
   instruction set this CPU supports.  This could be done in user space,
   but it's not easy, and we've already done it here.  */

#define ELF_HWCAP	(0)

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo.

   For the moment, we have only optimizations for the Intel generations,
   but that could change... */

#define ELF_PLATFORM  (NULL)

#define ELF_PLAT_INIT(_r, load_addr)					      \
  do {									      \
	 _r->gpr[0] =  _r->gpr[1] =  _r->gpr[2] =  _r->gpr[3] =		      \
	 _r->gpr[4] =  _r->gpr[5] =  _r->gpr[6] =  _r->gpr[7] =		      \
	 _r->gpr[8] =  _r->gpr[9] = _r->gpr[10] = _r->gpr[11] =		      \
	_r->gpr[12] = _r->gpr[13] = _r->gpr[14] = _r->gpr[15] =		      \
	_r->gpr[16] = _r->gpr[17] = _r->gpr[18] = _r->gpr[19] =		      \
	_r->gpr[20] = _r->gpr[21] = _r->gpr[22] = _r->gpr[23] =		      \
	_r->gpr[24] = _r->gpr[25] = _r->gpr[26] = _r->gpr[27] =		      \
	_r->gpr[28] = _r->gpr[29] = _r->gpr[30] = _r->gpr[31] =		      \
	0;								      \
  } while (0)

#define SET_PERSONALITY(ex, ibcs2) set_personality(PER_LINUX_32BIT)

#endif /* __V850_ELF_H__ */
