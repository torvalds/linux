#include <linux/kernel.h>
#include <linux/gcd.h>
#include <linux/module.h>

/* Greatest common divisor */
unsigned long gcd(unsigned long a, unsigned long b)
{
	unsigned long r;

	if (a < b)
		swap(a, b);

	if (!b)
		return a;
	while ((r = a % b) != 0) {
		a = b;
		b = r;
	}
	return b;
}
EXPORT_SYMBOL_GPL(gcd);
