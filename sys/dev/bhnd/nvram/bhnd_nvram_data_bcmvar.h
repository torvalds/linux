/*-
 * Copyright (c) 2015-2016 Landon Fuller <landonf@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */

#ifndef _BHND_NVRAM_BHND_NVRAM_BCMVAR_H_
#define _BHND_NVRAM_BHND_NVRAM_BCMVAR_H_

#define	BCM_NVRAM_ENCODE_OPT_VERSION	"bcm_version"

/**
 * BCM NVRAM header value data.
 */
union bhnd_nvram_bcm_hvar_value {
	uint16_t	u16;
	uint32_t	u32;
};

/**
 * Internal representation of BCM NVRAM values that mirror (and must be
 * vended as) NVRAM variables.
 */
struct bhnd_nvram_bcm_hvar {
	const char	*name;	/**< variable name */
	bhnd_nvram_type	 type;	/**< value type */
	size_t		 nelem;	/**< value element count */
	size_t		 len;	/**< value length */
	const char	*envp;	/**< Pointer to the NVRAM variable mirroring
				     this header value, or NULL. */
	bool		 stale;	/**< header value does not match
				     mirrored NVRAM value */

	/** variable data */
	union bhnd_nvram_bcm_hvar_value value;
};
	
/** BCM NVRAM header */
struct bhnd_nvram_bcmhdr {
	uint32_t magic;
	uint32_t size;
	uint32_t cfg0;		/**< crc:8, version:8, sdram_init:16 */
	uint32_t cfg1;		/**< sdram_config:16, sdram_refresh:16 */
	uint32_t sdram_ncdl;	/**< sdram_ncdl */
} __packed;

int	bhnd_nvram_bcm_getvar_direct_common(struct bhnd_nvram_io *io,
	    const char *name, void *outp, size_t *olen, bhnd_nvram_type otype,
	    bool have_header);

#endif /* _BHND_NVRAM_BHND_NVRAM_BCMVAR_H_ */
