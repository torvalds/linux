/*-
 * Copyright (c) 1998, Michael Smith
 * Copyright (c) 1996, Sujal M. Patel
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Machine-independant ISA PnP enumerator implementing a subset of the
 * ISA PnP specification.
 */
#include <stand.h>
#include <string.h>
#include <bootstrap.h>
#include <isapnp.h>

#define	inb(x)		(archsw.arch_isainb((x)))
#define	outb(x,y)	(archsw.arch_isaoutb((x),(y)))

static void	isapnp_write(int d, int r);
static void	isapnp_send_Initiation_LFSR(void);
static int	isapnp_get_serial(uint8_t *p);
static int	isapnp_isolation_protocol(void);
static void	isapnp_enumerate(void);

/* PnP read data port */
int		isapnp_readport = 0;

#define	_PNP_ID_LEN	9

struct pnphandler isapnphandler =
{
    "ISA bus",
    isapnp_enumerate
};

static void
isapnp_write(int d, int r)
{
    outb (_PNP_ADDRESS, d);
    outb (_PNP_WRITE_DATA, r);
}

/*
 * Send Initiation LFSR as described in "Plug and Play ISA Specification",
 * Intel May 94.
 */
static void
isapnp_send_Initiation_LFSR(void)
{
    int cur, i;

    /* Reset the LSFR */
    outb(_PNP_ADDRESS, 0);
    outb(_PNP_ADDRESS, 0); /* yes, we do need it twice! */

    cur = 0x6a;
    outb(_PNP_ADDRESS, cur);

    for (i = 1; i < 32; i++) {
	cur = (cur >> 1) | (((cur ^ (cur >> 1)) << 7) & 0xff);
	outb(_PNP_ADDRESS, cur);
    }
}

/*
 * Get the device's serial number.  Returns 1 if the serial is valid.
 */
static int
isapnp_get_serial(uint8_t *data)
{
    int		i, bit, valid = 0, sum = 0x6a;

    bzero(data, _PNP_ID_LEN);
    outb(_PNP_ADDRESS, SERIAL_ISOLATION);
    for (i = 0; i < 72; i++) {
	bit = inb(isapnp_readport) == 0x55;
	delay(250);	/* Delay 250 usec */

	/* Can't Short Circuit the next evaluation, so 'and' is last */
	bit = (inb(isapnp_readport) == 0xaa) && bit;
	delay(250);	/* Delay 250 usec */

	valid = valid || bit;

	if (i < 64)
	    sum = (sum >> 1) |
		(((sum ^ (sum >> 1) ^ bit) << 7) & 0xff);

	data[i / 8] = (data[i / 8] >> 1) | (bit ? 0x80 : 0);
    }

    valid = valid && (data[8] == sum);

    return valid;
}

/*
 * Fills the buffer with resource info from the device.
 * Returns nonzero if the device fails to report
 */
static int
isapnp_get_resource_info(uint8_t *buffer, int len)
{
    int		i, j;
    u_char	temp;

    for (i = 0; i < len; i++) {
	outb(_PNP_ADDRESS, STATUS);
	for (j = 0; j < 100; j++) {
	    if ((inb(isapnp_readport)) & 0x1)
		break;
	    delay(1);
	}
	if (j == 100) {
	    printf("PnP device failed to report resource data\n");
	    return(1);
	}
	outb(_PNP_ADDRESS, RESOURCE_DATA);
	temp = inb(isapnp_readport);
	if (buffer != NULL)
	    buffer[i] = temp;
    }
    return(0);
}

/*
 * Scan Resource Data for useful information.
 *
 * We scan the resource data for compatible device IDs and
 * identifier strings; we only take the first identifier string
 * and assume it's for the card as a whole.
 *
 * Returns 0 if the scan completed OK, nonzero on error.
 */
static int
isapnp_scan_resdata(struct pnpinfo *pi)
{
    u_char	tag, resinfo[8];
    u_int	limit;
    size_t	large_len;
    u_char	*str;

    limit = 1000;
    while ((limit-- > 0) && !isapnp_get_resource_info(&tag, 1)) {
	if (PNP_RES_TYPE(tag) == 0) {
	    /* Small resource */
	    switch (PNP_SRES_NUM(tag)) {

		case COMP_DEVICE_ID:
		    /* Got a compatible device id resource */
		    if (isapnp_get_resource_info(resinfo, PNP_SRES_LEN(tag)))
			return(1);
		    pnp_addident(pi, pnp_eisaformat(resinfo));

		case END_TAG:
		    return(0);
		    break;

		default:
		    /* Skip this resource */
		    if (isapnp_get_resource_info(NULL, PNP_SRES_LEN(tag)))
			return(1);
		    break;
	    }
	} else {
	    /* Large resource */
	    if (isapnp_get_resource_info(resinfo, 2))
		return(1);

	    large_len = resinfo[1];
	    large_len = (large_len << 8) + resinfo[0];

	    switch(PNP_LRES_NUM(tag)) {

	    case ID_STRING_ANSI:
		str = malloc(large_len + 1);
		if (isapnp_get_resource_info(str, (ssize_t)large_len)) {
		    free(str);
		    return(1);
		}
		str[large_len] = 0;
		if (pi->pi_desc == NULL) {
		    pi->pi_desc = (char *)str;
		} else {
		    free(str);
		}
		break;
		
	    default:
		/* Large resource, skip it */
		if (isapnp_get_resource_info(NULL, (ssize_t)large_len))
		    return(1);
	    }
	}
    }
    return(1);
}

/*
 * Run the isolation protocol. Upon exiting, all cards are aware that
 * they should use isapnp_readport as the READ_DATA port.
 */
static int
isapnp_isolation_protocol(void)
{
    int			csn;
    struct pnpinfo	*pi;
    uint8_t		cardid[_PNP_ID_LEN];
    int			ndevs;

    isapnp_send_Initiation_LFSR();
    ndevs = 0;
    
    isapnp_write(CONFIG_CONTROL, 0x04);	/* Reset CSN for All Cards */

    for (csn = 1; ; csn++) {
	/* Wake up cards without a CSN (ie. all of them) */
	isapnp_write(WAKE, 0);
	isapnp_write(SET_RD_DATA, (isapnp_readport >> 2));
	outb(_PNP_ADDRESS, SERIAL_ISOLATION);
	delay(1000);	/* Delay 1 msec */

	if (isapnp_get_serial(cardid)) {
	    isapnp_write(SET_CSN, csn);
	    pi = pnp_allocinfo();
	    ndevs++;
	    pnp_addident(pi, pnp_eisaformat(cardid));
	    /* scan the card obtaining all the identifiers it holds */
	    if (isapnp_scan_resdata(pi)) {
		pnp_freeinfo(pi);	/* error getting data, ignore */
	    } else {
		pnp_addinfo(pi);
	    }
	} else {
	    break;
	}
    }
    /* Move all cards to wait-for-key state */
    while (--csn > 0) {
	isapnp_send_Initiation_LFSR();
	isapnp_write(WAKE, csn);
	isapnp_write(CONFIG_CONTROL, 0x02);
	delay(1000); /* XXX is it really necessary ? */
	csn--;
    }
    return(ndevs);
}

/*
 * Locate ISA-PnP devices and populate the supplied list.
 */
static void
isapnp_enumerate(void) 
{
    int		pnp_rd_port;
    
    /* Check for I/O port access */
    if ((archsw.arch_isainb == NULL) || (archsw.arch_isaoutb == NULL))
	return;

    /* 
     * Validate a possibly-suggested read port value.  If the autoscan failed
     * last time, this will return us to autoscan mode again.
     */
    if ((isapnp_readport > 0) &&
	(((isapnp_readport < 0x203) ||
	  (isapnp_readport > 0x3ff) ||
	  (isapnp_readport & 0x3) != 0x3)))
	 /* invalid, go look for ourselves */
	isapnp_readport = 0;

    if (isapnp_readport < 0) {
	/* someone is telling us there is no ISA in the system */
	return;

    } else if (isapnp_readport > 0) {
	/* Someone has told us where the port is/should be, or we found one last time */
	isapnp_isolation_protocol();

    } else {
	/* No clues, look for it ourselves */
	for (pnp_rd_port = 0x80; pnp_rd_port < 0xff; pnp_rd_port += 0x10) {
	    /* Look for something, quit when we find it */
	    isapnp_readport = (pnp_rd_port << 2) | 0x3;
	    if (isapnp_isolation_protocol() > 0)
		break;
	}
    }
}
