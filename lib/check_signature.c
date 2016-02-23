#include <linux/io.h>
#include <linux/export.h>

/**
 *	check_signature		-	find BIOS signatures
 *	@io_addr: mmio address to check
 *	@signature:  signature block
 *	@length: length of signature
 *
 *	Perform a signature comparison with the mmio address io_addr. This
 *	address should have been obtained by ioremap.
 *	Returns 1 on a match.
 */

int check_signature(const volatile void __iomem *io_addr,
			const unsigned char *signature, int length)
{
	while (length--) {
		if (readb(io_addr) != *signature)
			return 0;
		io_addr++;
		signature++;
	}
	return 1;
}
EXPORT_SYMBOL(check_signature);
