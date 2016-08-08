/*
 * internal.h - printk internal definitions
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/percpu.h>

typedef __printf(2, 0) int (*printk_func_t)(int level, const char *fmt,
					    va_list args);

__printf(2, 0)
int vprintk_default(int level, const char *fmt, va_list args);

#ifdef CONFIG_PRINTK_NMI

extern raw_spinlock_t logbuf_lock;

/*
 * printk() could not take logbuf_lock in NMI context. Instead,
 * it temporary stores the strings into a per-CPU buffer.
 * The alternative implementation is chosen transparently
 * via per-CPU variable.
 */
DECLARE_PER_CPU(printk_func_t, printk_func);
__printf(2, 0)
static inline int vprintk_func(int level, const char *fmt, va_list args)
{
	return this_cpu_read(printk_func)(level, fmt, args);
}

extern atomic_t nmi_message_lost;
static inline int get_nmi_message_lost(void)
{
	return atomic_xchg(&nmi_message_lost, 0);
}

#else /* CONFIG_PRINTK_NMI */

__printf(2, 0)
static inline int vprintk_func(int level, const char *fmt, va_list args)
{
	return vprintk_default(level, fmt, args);
}

static inline int get_nmi_message_lost(void)
{
	return 0;
}

#endif /* CONFIG_PRINTK_NMI */
