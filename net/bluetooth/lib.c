/*
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated

   Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS,
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS
   SOFTWARE IS DISCLAIMED.
*/

/* Bluetooth kernel library. */

#define pr_fmt(fmt) "Bluetooth: " fmt

#include <linux/export.h>

#include <net/bluetooth/bluetooth.h>

/**
 * baswap() - Swaps the order of a bd address
 * @dst: Pointer to a bdaddr_t struct that will store the swapped
 * 		 bd address.
 * @src: Pointer to the bdaddr_t struct to be swapped.
 *
 * This function reverses the byte order of a Bluetooth device
 * address.
 */
void baswap(bdaddr_t *dst, const bdaddr_t *src)
{
	const unsigned char *s = (const unsigned char *)src;
	unsigned char *d = (unsigned char *)dst;
	unsigned int i;

	for (i = 0; i < 6; i++)
		d[i] = s[5 - i];
}
EXPORT_SYMBOL(baswap);

/**
 * bt_to_errno() - Bluetooth error codes to standard errno
 * @code: Bluetooth error code to be converted
 *
 * This function takes a Bluetooth error code as input and converts
 * it to an equivalent Unix/standard errno value.
 *
 * Return:
 *
 * If the bt error code is known, an equivalent Unix errno value
 * is returned.
 * If the given bt error code is not known, ENOSYS is returned.
 */
int bt_to_errno(__u16 code)
{
	switch (code) {
	case 0:
		return 0;

	case 0x01:
		return EBADRQC;

	case 0x02:
		return ENOTCONN;

	case 0x03:
		return EIO;

	case 0x04:
	case 0x3c:
		return EHOSTDOWN;

	case 0x05:
		return EACCES;

	case 0x06:
		return EBADE;

	case 0x07:
		return ENOMEM;

	case 0x08:
		return ETIMEDOUT;

	case 0x09:
		return EMLINK;

	case 0x0a:
		return EMLINK;

	case 0x0b:
		return EALREADY;

	case 0x0c:
		return EBUSY;

	case 0x0d:
	case 0x0e:
	case 0x0f:
		return ECONNREFUSED;

	case 0x10:
		return ETIMEDOUT;

	case 0x11:
	case 0x27:
	case 0x29:
	case 0x20:
		return EOPNOTSUPP;

	case 0x12:
		return EINVAL;

	case 0x13:
	case 0x14:
	case 0x15:
		return ECONNRESET;

	case 0x16:
		return ECONNABORTED;

	case 0x17:
		return ELOOP;

	case 0x18:
		return EACCES;

	case 0x1a:
		return EPROTONOSUPPORT;

	case 0x1b:
		return ECONNREFUSED;

	case 0x19:
	case 0x1e:
	case 0x23:
	case 0x24:
	case 0x25:
		return EPROTO;

	default:
		return ENOSYS;
	}
}
EXPORT_SYMBOL(bt_to_errno);

/**
 * bt_status() - Standard errno value to Bluetooth error code
 * @err: Unix/standard errno value to be converted
 *
 * This function converts a standard/Unix errno value to an
 * equivalent Bluetooth error code.
 *
 * Return: Bluetooth error code.
 *
 * If the given errno is not found, 0x1f is returned by default
 * which indicates an unspecified error.
 * For err >= 0, no conversion is performed, and the same value
 * is immediately returned.
 */
__u8 bt_status(int err)
{
	if (err >= 0)
		return err;

	switch (err) {
	case -EBADRQC:
		return 0x01;

	case -ENOTCONN:
		return 0x02;

	case -EIO:
		return 0x03;

	case -EHOSTDOWN:
		return 0x04;

	case -EACCES:
		return 0x05;

	case -EBADE:
		return 0x06;

	case -ENOMEM:
		return 0x07;

	case -ETIMEDOUT:
		return 0x08;

	case -EMLINK:
		return 0x09;

	case -EALREADY:
		return 0x0b;

	case -EBUSY:
		return 0x0c;

	case -ECONNREFUSED:
		return 0x0d;

	case -EOPNOTSUPP:
		return 0x11;

	case -EINVAL:
		return 0x12;

	case -ECONNRESET:
		return 0x13;

	case -ECONNABORTED:
		return 0x16;

	case -ELOOP:
		return 0x17;

	case -EPROTONOSUPPORT:
		return 0x1a;

	case -EPROTO:
		return 0x19;

	default:
		return 0x1f;
	}
}
EXPORT_SYMBOL(bt_status);

/**
 * bt_info() - Log Bluetooth information message
 * @format: Message's format string
 */
void bt_info(const char *format, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, format);

	vaf.fmt = format;
	vaf.va = &args;

	pr_info("%pV", &vaf);

	va_end(args);
}
EXPORT_SYMBOL(bt_info);

/**
 * bt_warn() - Log Bluetooth warning message
 * @format: Message's format string
 */
void bt_warn(const char *format, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, format);

	vaf.fmt = format;
	vaf.va = &args;

	pr_warn("%pV", &vaf);

	va_end(args);
}
EXPORT_SYMBOL(bt_warn);

/**
 * bt_err() - Log Bluetooth error message
 * @format: Message's format string
 */
void bt_err(const char *format, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, format);

	vaf.fmt = format;
	vaf.va = &args;

	pr_err("%pV", &vaf);

	va_end(args);
}
EXPORT_SYMBOL(bt_err);

#ifdef CONFIG_BT_FEATURE_DEBUG
static bool debug_enable;

void bt_dbg_set(bool enable)
{
	debug_enable = enable;
}

bool bt_dbg_get(void)
{
	return debug_enable;
}

/**
 * bt_dbg() - Log Bluetooth debugging message
 * @format: Message's format string
 */
void bt_dbg(const char *format, ...)
{
	struct va_format vaf;
	va_list args;

	if (likely(!debug_enable))
		return;

	va_start(args, format);

	vaf.fmt = format;
	vaf.va = &args;

	printk(KERN_DEBUG pr_fmt("%pV"), &vaf);

	va_end(args);
}
EXPORT_SYMBOL(bt_dbg);
#endif

/**
 * bt_warn_ratelimited() - Log rate-limited Bluetooth warning message
 * @format: Message's format string
 *
 * This functions works like bt_warn, but it uses rate limiting
 * to prevent the message from being logged too often.
 */
void bt_warn_ratelimited(const char *format, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, format);

	vaf.fmt = format;
	vaf.va = &args;

	pr_warn_ratelimited("%pV", &vaf);

	va_end(args);
}
EXPORT_SYMBOL(bt_warn_ratelimited);

/**
 * bt_err_ratelimited() - Log rate-limited Bluetooth error message
 * @format: Message's format string
 *
 * This functions works like bt_err, but it uses rate limiting
 * to prevent the message from being logged too often.
 */
void bt_err_ratelimited(const char *format, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, format);

	vaf.fmt = format;
	vaf.va = &args;

	pr_err_ratelimited("%pV", &vaf);

	va_end(args);
}
EXPORT_SYMBOL(bt_err_ratelimited);
