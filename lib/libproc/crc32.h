/*-
 *  COPYRIGHT (C) 1986 Gary S. Brown.  You may use this program, or
 *  code or tables extracted from it, as desired without restriction.
 *
 * $FreeBSD$
 */

#ifndef _CRC32_H_
#define	_CRC32_H_

#include <stdint.h>	/* uint32_t */
#include <stdlib.h>	/* size_t */

extern uint32_t crc32_tab[];

static __inline uint32_t
crc32(const void *buf, size_t size)
{
	const uint8_t *p = buf;
	uint32_t crc;

	crc = ~0U;
	while (size--)
		crc = crc32_tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
	return (crc ^ ~0U);
}

#endif	/* !_CRC32_H_ */
