/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1993 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Functions to provide access to special i386 instructions.
 * This in included in sys/systm.h, and that file should be
 * used in preference to this.
 */

#ifndef _MACHINE_CPUFUNC_H_
#define	_MACHINE_CPUFUNC_H_

#ifndef _SYS_CDEFS_H_
#error this file needs sys/cdefs.h as a prerequisite
#endif

struct region_descriptor;

#define readb(va)	(*(volatile uint8_t *) (va))
#define readw(va)	(*(volatile uint16_t *) (va))
#define readl(va)	(*(volatile uint32_t *) (va))

#define writeb(va, d)	(*(volatile uint8_t *) (va) = (d))
#define writew(va, d)	(*(volatile uint16_t *) (va) = (d))
#define writel(va, d)	(*(volatile uint32_t *) (va) = (d))

#if defined(__GNUCLIKE_ASM) && defined(__CC_SUPPORTS___INLINE)

static __inline void
breakpoint(void)
{
	__asm __volatile("int $3");
}

static __inline __pure2 u_int
bsfl(u_int mask)
{
	u_int	result;

	__asm("bsfl %1,%0" : "=r" (result) : "rm" (mask) : "cc");
	return (result);
}

static __inline __pure2 u_int
bsrl(u_int mask)
{
	u_int	result;

	__asm("bsrl %1,%0" : "=r" (result) : "rm" (mask) : "cc");
	return (result);
}

static __inline void
clflush(u_long addr)
{

	__asm __volatile("clflush %0" : : "m" (*(char *)addr));
}

static __inline void
clflushopt(u_long addr)
{

	__asm __volatile(".byte 0x66;clflush %0" : : "m" (*(char *)addr));
}

static __inline void
clts(void)
{

	__asm __volatile("clts");
}

static __inline void
disable_intr(void)
{

	__asm __volatile("cli" : : : "memory");
}

#ifdef _KERNEL
static __inline void
do_cpuid(u_int ax, u_int *p)
{
	__asm __volatile("cpuid"
	    : "=a" (p[0]), "=b" (p[1]), "=c" (p[2]), "=d" (p[3])
	    :  "0" (ax));
}

static __inline void
cpuid_count(u_int ax, u_int cx, u_int *p)
{
	__asm __volatile("cpuid"
	    : "=a" (p[0]), "=b" (p[1]), "=c" (p[2]), "=d" (p[3])
	    :  "0" (ax), "c" (cx));
}
#else
static __inline void
do_cpuid(u_int ax, u_int *p)
{
	__asm __volatile(
	    "pushl\t%%ebx\n\t"
	    "cpuid\n\t"
	    "movl\t%%ebx,%1\n\t"
	    "popl\t%%ebx"
	    : "=a" (p[0]), "=DS" (p[1]), "=c" (p[2]), "=d" (p[3])
	    :  "0" (ax));
}

static __inline void
cpuid_count(u_int ax, u_int cx, u_int *p)
{
	__asm __volatile(
	    "pushl\t%%ebx\n\t"
	    "cpuid\n\t"
	    "movl\t%%ebx,%1\n\t"
	    "popl\t%%ebx"
	    : "=a" (p[0]), "=DS" (p[1]), "=c" (p[2]), "=d" (p[3])
	    :  "0" (ax), "c" (cx));
}
#endif

static __inline void
enable_intr(void)
{

	__asm __volatile("sti");
}

static __inline void
cpu_monitor(const void *addr, u_long extensions, u_int hints)
{

	__asm __volatile("monitor"
	    : : "a" (addr), "c" (extensions), "d" (hints));
}

static __inline void
cpu_mwait(u_long extensions, u_int hints)
{

	__asm __volatile("mwait" : : "a" (hints), "c" (extensions));
}

static __inline void
lfence(void)
{

	__asm __volatile("lfence" : : : "memory");
}

static __inline void
mfence(void)
{

	__asm __volatile("mfence" : : : "memory");
}

static __inline void
sfence(void)
{

	__asm __volatile("sfence" : : : "memory");
}

#ifdef _KERNEL

#define	HAVE_INLINE_FFS

static __inline __pure2 int
ffs(int mask)
{
	/*
	 * Note that gcc-2's builtin ffs would be used if we didn't declare
	 * this inline or turn off the builtin.  The builtin is faster but
	 * broken in gcc-2.4.5 and slower but working in gcc-2.5 and later
	 * versions.
	 */
	 return (mask == 0 ? mask : (int)bsfl((u_int)mask) + 1);
}

#define	HAVE_INLINE_FFSL

static __inline __pure2 int
ffsl(long mask)
{
	return (ffs((int)mask));
}

#define	HAVE_INLINE_FLS

static __inline __pure2 int
fls(int mask)
{
	return (mask == 0 ? mask : (int)bsrl((u_int)mask) + 1);
}

#define	HAVE_INLINE_FLSL

static __inline __pure2 int
flsl(long mask)
{
	return (fls((int)mask));
}

#endif /* _KERNEL */

static __inline void
halt(void)
{
	__asm __volatile("hlt");
}

static __inline u_char
inb(u_int port)
{
	u_char	data;

	__asm __volatile("inb %w1, %0" : "=a" (data) : "Nd" (port));
	return (data);
}

static __inline u_int
inl(u_int port)
{
	u_int	data;

	__asm __volatile("inl %w1, %0" : "=a" (data) : "Nd" (port));
	return (data);
}

static __inline void
insb(u_int port, void *addr, size_t count)
{
	__asm __volatile("cld; rep; insb"
			 : "+D" (addr), "+c" (count)
			 : "d" (port)
			 : "memory");
}

static __inline void
insw(u_int port, void *addr, size_t count)
{
	__asm __volatile("cld; rep; insw"
			 : "+D" (addr), "+c" (count)
			 : "d" (port)
			 : "memory");
}

static __inline void
insl(u_int port, void *addr, size_t count)
{
	__asm __volatile("cld; rep; insl"
			 : "+D" (addr), "+c" (count)
			 : "d" (port)
			 : "memory");
}

static __inline void
invd(void)
{
	__asm __volatile("invd");
}

static __inline u_short
inw(u_int port)
{
	u_short	data;

	__asm __volatile("inw %w1, %0" : "=a" (data) : "Nd" (port));
	return (data);
}

static __inline void
outb(u_int port, u_char data)
{
	__asm __volatile("outb %0, %w1" : : "a" (data), "Nd" (port));
}

static __inline void
outl(u_int port, u_int data)
{
	__asm __volatile("outl %0, %w1" : : "a" (data), "Nd" (port));
}

static __inline void
outsb(u_int port, const void *addr, size_t count)
{
	__asm __volatile("cld; rep; outsb"
			 : "+S" (addr), "+c" (count)
			 : "d" (port));
}

static __inline void
outsw(u_int port, const void *addr, size_t count)
{
	__asm __volatile("cld; rep; outsw"
			 : "+S" (addr), "+c" (count)
			 : "d" (port));
}

static __inline void
outsl(u_int port, const void *addr, size_t count)
{
	__asm __volatile("cld; rep; outsl"
			 : "+S" (addr), "+c" (count)
			 : "d" (port));
}

static __inline void
outw(u_int port, u_short data)
{
	__asm __volatile("outw %0, %w1" : : "a" (data), "Nd" (port));
}

static __inline void
ia32_pause(void)
{
	__asm __volatile("pause");
}

static __inline u_int
read_eflags(void)
{
	u_int	ef;

	__asm __volatile("pushfl; popl %0" : "=r" (ef));
	return (ef);
}

static __inline uint64_t
rdmsr(u_int msr)
{
	uint64_t rv;

	__asm __volatile("rdmsr" : "=A" (rv) : "c" (msr));
	return (rv);
}

static __inline uint32_t
rdmsr32(u_int msr)
{
	uint32_t low;

	__asm __volatile("rdmsr" : "=a" (low) : "c" (msr) : "edx");
	return (low);
}

static __inline uint64_t
rdpmc(u_int pmc)
{
	uint64_t rv;

	__asm __volatile("rdpmc" : "=A" (rv) : "c" (pmc));
	return (rv);
}

static __inline uint64_t
rdtsc(void)
{
	uint64_t rv;

	__asm __volatile("rdtsc" : "=A" (rv));
	return (rv);
}

static __inline uint64_t
rdtscp(void)
{
	uint64_t rv;

	__asm __volatile("rdtscp" : "=A" (rv) : : "ecx");
	return (rv);
}

static __inline uint32_t
rdtsc32(void)
{
	uint32_t rv;

	__asm __volatile("rdtsc" : "=a" (rv) : : "edx");
	return (rv);
}

static __inline void
wbinvd(void)
{
	__asm __volatile("wbinvd");
}

static __inline void
write_eflags(u_int ef)
{
	__asm __volatile("pushl %0; popfl" : : "r" (ef));
}

static __inline void
wrmsr(u_int msr, uint64_t newval)
{
	__asm __volatile("wrmsr" : : "A" (newval), "c" (msr));
}

static __inline void
load_cr0(u_int data)
{

	__asm __volatile("movl %0,%%cr0" : : "r" (data));
}

static __inline u_int
rcr0(void)
{
	u_int	data;

	__asm __volatile("movl %%cr0,%0" : "=r" (data));
	return (data);
}

static __inline u_int
rcr2(void)
{
	u_int	data;

	__asm __volatile("movl %%cr2,%0" : "=r" (data));
	return (data);
}

static __inline void
load_cr3(u_int data)
{

	__asm __volatile("movl %0,%%cr3" : : "r" (data) : "memory");
}

static __inline u_int
rcr3(void)
{
	u_int	data;

	__asm __volatile("movl %%cr3,%0" : "=r" (data));
	return (data);
}

static __inline void
load_cr4(u_int data)
{
	__asm __volatile("movl %0,%%cr4" : : "r" (data));
}

static __inline u_int
rcr4(void)
{
	u_int	data;

	__asm __volatile("movl %%cr4,%0" : "=r" (data));
	return (data);
}

static __inline uint64_t
rxcr(u_int reg)
{
	u_int low, high;

	__asm __volatile("xgetbv" : "=a" (low), "=d" (high) : "c" (reg));
	return (low | ((uint64_t)high << 32));
}

static __inline void
load_xcr(u_int reg, uint64_t val)
{
	u_int low, high;

	low = val;
	high = val >> 32;
	__asm __volatile("xsetbv" : : "c" (reg), "a" (low), "d" (high));
}

/*
 * Global TLB flush (except for thise for pages marked PG_G)
 */
static __inline void
invltlb(void)
{

	load_cr3(rcr3());
}

/*
 * TLB flush for an individual page (even if it has PG_G).
 * Only works on 486+ CPUs (i386 does not have PG_G).
 */
static __inline void
invlpg(u_int addr)
{

	__asm __volatile("invlpg %0" : : "m" (*(char *)addr) : "memory");
}

static __inline u_short
rfs(void)
{
	u_short sel;
	__asm __volatile("movw %%fs,%0" : "=rm" (sel));
	return (sel);
}

static __inline uint64_t
rgdt(void)
{
	uint64_t gdtr;
	__asm __volatile("sgdt %0" : "=m" (gdtr));
	return (gdtr);
}

static __inline u_short
rgs(void)
{
	u_short sel;
	__asm __volatile("movw %%gs,%0" : "=rm" (sel));
	return (sel);
}

static __inline uint64_t
ridt(void)
{
	uint64_t idtr;
	__asm __volatile("sidt %0" : "=m" (idtr));
	return (idtr);
}

static __inline u_short
rldt(void)
{
	u_short ldtr;
	__asm __volatile("sldt %0" : "=g" (ldtr));
	return (ldtr);
}

static __inline u_short
rss(void)
{
	u_short sel;
	__asm __volatile("movw %%ss,%0" : "=rm" (sel));
	return (sel);
}

static __inline u_short
rtr(void)
{
	u_short tr;
	__asm __volatile("str %0" : "=g" (tr));
	return (tr);
}

static __inline void
load_fs(u_short sel)
{
	__asm __volatile("movw %0,%%fs" : : "rm" (sel));
}

static __inline void
load_gs(u_short sel)
{
	__asm __volatile("movw %0,%%gs" : : "rm" (sel));
}

static __inline void
lidt(struct region_descriptor *addr)
{
	__asm __volatile("lidt (%0)" : : "r" (addr));
}

static __inline void
lldt(u_short sel)
{
	__asm __volatile("lldt %0" : : "r" (sel));
}

static __inline void
ltr(u_short sel)
{
	__asm __volatile("ltr %0" : : "r" (sel));
}

static __inline u_int
rdr0(void)
{
	u_int	data;
	__asm __volatile("movl %%dr0,%0" : "=r" (data));
	return (data);
}

static __inline void
load_dr0(u_int dr0)
{
	__asm __volatile("movl %0,%%dr0" : : "r" (dr0));
}

static __inline u_int
rdr1(void)
{
	u_int	data;
	__asm __volatile("movl %%dr1,%0" : "=r" (data));
	return (data);
}

static __inline void
load_dr1(u_int dr1)
{
	__asm __volatile("movl %0,%%dr1" : : "r" (dr1));
}

static __inline u_int
rdr2(void)
{
	u_int	data;
	__asm __volatile("movl %%dr2,%0" : "=r" (data));
	return (data);
}

static __inline void
load_dr2(u_int dr2)
{
	__asm __volatile("movl %0,%%dr2" : : "r" (dr2));
}

static __inline u_int
rdr3(void)
{
	u_int	data;
	__asm __volatile("movl %%dr3,%0" : "=r" (data));
	return (data);
}

static __inline void
load_dr3(u_int dr3)
{
	__asm __volatile("movl %0,%%dr3" : : "r" (dr3));
}

static __inline u_int
rdr6(void)
{
	u_int	data;
	__asm __volatile("movl %%dr6,%0" : "=r" (data));
	return (data);
}

static __inline void
load_dr6(u_int dr6)
{
	__asm __volatile("movl %0,%%dr6" : : "r" (dr6));
}

static __inline u_int
rdr7(void)
{
	u_int	data;
	__asm __volatile("movl %%dr7,%0" : "=r" (data));
	return (data);
}

static __inline void
load_dr7(u_int dr7)
{
	__asm __volatile("movl %0,%%dr7" : : "r" (dr7));
}

static __inline u_char
read_cyrix_reg(u_char reg)
{
	outb(0x22, reg);
	return inb(0x23);
}

static __inline void
write_cyrix_reg(u_char reg, u_char data)
{
	outb(0x22, reg);
	outb(0x23, data);
}

static __inline register_t
intr_disable(void)
{
	register_t eflags;

	eflags = read_eflags();
	disable_intr();
	return (eflags);
}

static __inline void
intr_restore(register_t eflags)
{
	write_eflags(eflags);
}

static __inline uint32_t
rdpkru(void)
{
	uint32_t res;

	__asm __volatile("rdpkru" :  "=a" (res) : "c" (0) : "edx");
	return (res);
}

static __inline void
wrpkru(uint32_t mask)
{

	__asm __volatile("wrpkru" :  : "a" (mask),  "c" (0), "d" (0));
}

#else /* !(__GNUCLIKE_ASM && __CC_SUPPORTS___INLINE) */

int	breakpoint(void);
u_int	bsfl(u_int mask);
u_int	bsrl(u_int mask);
void	clflush(u_long addr);
void	clts(void);
void	cpuid_count(u_int ax, u_int cx, u_int *p);
void	disable_intr(void);
void	do_cpuid(u_int ax, u_int *p);
void	enable_intr(void);
void	halt(void);
void	ia32_pause(void);
u_char	inb(u_int port);
u_int	inl(u_int port);
void	insb(u_int port, void *addr, size_t count);
void	insl(u_int port, void *addr, size_t count);
void	insw(u_int port, void *addr, size_t count);
register_t	intr_disable(void);
void	intr_restore(register_t ef);
void	invd(void);
void	invlpg(u_int addr);
void	invltlb(void);
u_short	inw(u_int port);
void	lidt(struct region_descriptor *addr);
void	lldt(u_short sel);
void	load_cr0(u_int cr0);
void	load_cr3(u_int cr3);
void	load_cr4(u_int cr4);
void	load_dr0(u_int dr0);
void	load_dr1(u_int dr1);
void	load_dr2(u_int dr2);
void	load_dr3(u_int dr3);
void	load_dr6(u_int dr6);
void	load_dr7(u_int dr7);
void	load_fs(u_short sel);
void	load_gs(u_short sel);
void	ltr(u_short sel);
void	outb(u_int port, u_char data);
void	outl(u_int port, u_int data);
void	outsb(u_int port, const void *addr, size_t count);
void	outsl(u_int port, const void *addr, size_t count);
void	outsw(u_int port, const void *addr, size_t count);
void	outw(u_int port, u_short data);
u_int	rcr0(void);
u_int	rcr2(void);
u_int	rcr3(void);
u_int	rcr4(void);
uint64_t rdmsr(u_int msr);
uint64_t rdpmc(u_int pmc);
u_int	rdr0(void);
u_int	rdr1(void);
u_int	rdr2(void);
u_int	rdr3(void);
u_int	rdr6(void);
u_int	rdr7(void);
uint64_t rdtsc(void);
u_char	read_cyrix_reg(u_char reg);
u_int	read_eflags(void);
u_int	rfs(void);
uint64_t rgdt(void);
u_int	rgs(void);
uint64_t ridt(void);
u_short	rldt(void);
u_short	rtr(void);
void	wbinvd(void);
void	write_cyrix_reg(u_char reg, u_char data);
void	write_eflags(u_int ef);
void	wrmsr(u_int msr, uint64_t newval);

#endif	/* __GNUCLIKE_ASM && __CC_SUPPORTS___INLINE */

void    reset_dbregs(void);

#ifdef _KERNEL
int	rdmsr_safe(u_int msr, uint64_t *val);
int	wrmsr_safe(u_int msr, uint64_t newval);
#endif

#endif /* !_MACHINE_CPUFUNC_H_ */
