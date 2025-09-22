/*	$OpenBSD: cpufunc.h,v 1.11 2023/01/25 09:53:53 kettenis Exp $	*/

/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _MACHINE_CPUFUNC_H_
#define _MACHINE_CPUFUNC_H_

static inline void
eieio(void)
{
	__asm volatile ("eieio" ::: "memory");
}

static inline void
isync(void)
{
	__asm volatile ("isync" ::: "memory");
}

static inline void
ptesync(void)
{
	__asm volatile ("ptesync" ::: "memory");
}

static inline void
sync(void)
{
	__asm volatile ("sync" ::: "memory");
}

static inline void
slbia(void)
{
	__asm volatile ("slbia");
}

static inline void
slbie(uint64_t esid)
{
	__asm volatile ("slbie %0" :: "r"(esid));
}

static inline uint64_t
slbmfee(uint64_t entry)
{
	uint64_t value;
	__asm volatile ("slbmfee %0, %1" : "=r"(value) : "r"(entry));
	return value;
}

static inline void
slbmte(uint64_t slbv, uint64_t slbe)
{
	__asm volatile ("slbmte %0, %1" :: "r"(slbv), "r"(slbe));
}

static inline void
tlbie(uint64_t ava)
{
	__asm volatile ("tlbie %0, %1" :: "r"(ava), "r"(0));
}

static inline void
tlbiel(uint64_t ava)
{
	__asm volatile ("tlbiel %0" :: "r"(ava));
}

static inline void
tlbsync(void)
{
	__asm volatile ("tlbsync" ::: "memory");
}

static inline uint64_t
mfmsr(void)
{
	uint64_t value;
	__asm volatile ("mfmsr %0" : "=r"(value));
	return value;
}

static inline void
mtmsr(uint64_t value)
{
	__asm volatile ("mtmsr %0" :: "r"(value));
}

static inline uint64_t
mftb(void)
{
	uint64_t value;
	__asm volatile ("mftb %0" : "=r"(value));
	return value;
}

static inline uint32_t
mfdsisr(void)
{
	uint32_t value;
	__asm volatile ("mfdsisr %0" : "=r"(value));
	return value;
}

static inline uint64_t
mfdar(void)
{
	uint64_t value;
	__asm volatile ("mfdar %0" : "=r"(value));
	return value;
}

static inline void
mtdec(uint32_t value)
{
	__asm volatile ("mtdec %0" :: "r"(value));
}

static inline void
mtsdr1(uint64_t value)
{
	__asm volatile ("mtsdr1 %0" :: "r"(value));
}

static inline void
mtamr(uint64_t value)
{
	__asm volatile ("mtspr 29, %0" :: "r"(value));
}

static inline void
mtfscr(uint64_t value)
{
	__asm volatile ("mtspr 153, %0" :: "r"(value));
}

static inline void
mtuamor(uint64_t value)
{
	__asm volatile ("mtspr 157, %0" :: "r"(value));
}

static inline uint32_t
mfpvr(void)
{
	uint32_t value;
	__asm volatile ("mfspr %0, 287" : "=r"(value));
	return value;
}

static inline uint64_t
mflpcr(void)
{
	uint64_t value;
	__asm volatile ("mfspr %0, 318" : "=r"(value));
	return value;
}

static inline void
mtlpcr(uint64_t value)
{
	__asm volatile ("mtspr 318, %0" :: "r"(value));
}

#define LPCR_PECE	0x000040000001f000UL
#define LPCR_LPES	0x0000000000000008UL
#define LPCR_HVICE	0x0000000000000002UL

static inline void
mtamor(uint64_t value)
{
	__asm volatile ("mtspr 349, %0" :: "r"(value));
}

static inline void
mtptcr(uint64_t value)
{
	__asm volatile ("mtspr 464, %0" :: "r"(value));
}

static inline uint64_t
mfpmsr(void)
{
	uint64_t value;
	__asm volatile ("mfspr %0, 853" : "=r"(value));
	return value;
}

static inline void
mtpmcr(uint64_t value)
{
	__asm volatile ("mtspr 884, %0" :: "r"(value));
}

static inline uint32_t
mfpir(void)
{
	uint32_t value;
	__asm volatile ("mfspr %0, 1023" : "=r"(value));
	return value;
}

extern int cacheline_size;

void	__syncicache(void *, size_t);

#endif /* _MACHINE_CPUFUNC_H_ */
