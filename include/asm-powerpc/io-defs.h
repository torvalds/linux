/* This file is meant to be include multiple times by other headers */

DEF_PCI_AC_RET(readb, u8, (const PCI_IO_ADDR addr), (addr))
DEF_PCI_AC_RET(readw, u16, (const PCI_IO_ADDR addr), (addr))
DEF_PCI_AC_RET(readl, u32, (const PCI_IO_ADDR addr), (addr))
DEF_PCI_AC_RET(readq, u64, (const PCI_IO_ADDR addr), (addr))
DEF_PCI_AC_RET(readw_be, u16, (const PCI_IO_ADDR addr), (addr))
DEF_PCI_AC_RET(readl_be, u32, (const PCI_IO_ADDR addr), (addr))
DEF_PCI_AC_RET(readq_be, u64, (const PCI_IO_ADDR addr), (addr))
DEF_PCI_AC_NORET(writeb, (u8 val, PCI_IO_ADDR addr), (val, addr))
DEF_PCI_AC_NORET(writew, (u16 val, PCI_IO_ADDR addr), (val, addr))
DEF_PCI_AC_NORET(writel, (u32 val, PCI_IO_ADDR addr), (val, addr))
DEF_PCI_AC_NORET(writeq, (u64 val, PCI_IO_ADDR addr), (val, addr))
DEF_PCI_AC_NORET(writew_be, (u16 val, PCI_IO_ADDR addr), (val, addr))
DEF_PCI_AC_NORET(writel_be, (u32 val, PCI_IO_ADDR addr), (val, addr))
DEF_PCI_AC_NORET(writeq_be, (u64 val, PCI_IO_ADDR addr), (val, addr))

DEF_PCI_AC_RET(inb, u8, (unsigned long port), (port))
DEF_PCI_AC_RET(inw, u16, (unsigned long port), (port))
DEF_PCI_AC_RET(inl, u32, (unsigned long port), (port))
DEF_PCI_AC_NORET(outb, (u8 val, unsigned long port), (val, port))
DEF_PCI_AC_NORET(outw, (u16 val, unsigned long port), (val, port))
DEF_PCI_AC_NORET(outl, (u32 val, unsigned long port), (val, port))

DEF_PCI_AC_NORET(readsb, (const PCI_IO_ADDR a, void *b, unsigned long c), \
		 (a, b, c))
DEF_PCI_AC_NORET(readsw, (const PCI_IO_ADDR a, void *b, unsigned long c), \
		 (a, b, c))
DEF_PCI_AC_NORET(readsl, (const PCI_IO_ADDR a, void *b, unsigned long c), \
		 (a, b, c))
DEF_PCI_AC_NORET(writesb, (PCI_IO_ADDR a, const void *b, unsigned long c), \
		 (a, b, c))
DEF_PCI_AC_NORET(writesw, (PCI_IO_ADDR a, const void *b, unsigned long c), \
		 (a, b, c))
DEF_PCI_AC_NORET(writesl, (PCI_IO_ADDR a, const void *b, unsigned long c), \
		 (a, b, c))

DEF_PCI_AC_NORET(insb, (unsigned long p, void *b, unsigned long c), \
		 (p, b, c))
DEF_PCI_AC_NORET(insw, (unsigned long p, void *b, unsigned long c), \
		 (p, b, c))
DEF_PCI_AC_NORET(insl, (unsigned long p, void *b, unsigned long c), \
		 (p, b, c))
DEF_PCI_AC_NORET(outsb, (unsigned long p, const void *b, unsigned long c), \
		 (p, b, c))
DEF_PCI_AC_NORET(outsw, (unsigned long p, const void *b, unsigned long c), \
		 (p, b, c))
DEF_PCI_AC_NORET(outsl, (unsigned long p, const void *b, unsigned long c), \
		 (p, b, c))

DEF_PCI_AC_NORET(memset_io, (PCI_IO_ADDR a, int c, unsigned long n),	   \
		 (a, c, n))
DEF_PCI_AC_NORET(memcpy_fromio,(void *d,const PCI_IO_ADDR s,unsigned long n), \
		 (d, s, n))
DEF_PCI_AC_NORET(memcpy_toio,(PCI_IO_ADDR d,const void *s,unsigned long n),   \
		 (d, s, n))
