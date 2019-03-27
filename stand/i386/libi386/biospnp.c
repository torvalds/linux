/*-
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
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
 * PnP BIOS enumerator.
 */

#include <stand.h>
#include <machine/stdarg.h>
#include <bootstrap.h>
#include <isapnp.h>
#include <btxv86.h>


static int	biospnp_init(void);
static void	biospnp_enumerate(void);

struct pnphandler biospnphandler =
{
    "PnP BIOS",
    biospnp_enumerate
};

struct pnp_ICstructure
{
    uint8_t	pnp_signature[4];
    uint8_t	pnp_version;
    uint8_t	pnp_length;
    uint16_t	pnp_BIOScontrol;
    uint8_t	pnp_checksum;
    uint32_t	pnp_eventflag;
    uint16_t	pnp_rmip;
    uint16_t	pnp_rmcs;
    uint16_t	pnp_pmip;
    uint32_t	pnp_pmcs;
    uint8_t	pnp_OEMdev[4];
    uint16_t	pnp_rmds;
    uint32_t	pnp_pmds;
} __packed;

struct pnp_devNode 
{
    uint16_t	dn_size;
    uint8_t	dn_handle;
    uint8_t	dn_id[4];
    uint8_t	dn_type[3];
    uint16_t	dn_attrib;
    uint8_t	dn_data[1];
} __packed;

struct pnp_isaConfiguration
{
    uint8_t	ic_revision;
    uint8_t	ic_nCSN;
    uint16_t	ic_rdport;
    uint16_t	ic_reserved;
} __packed;

static struct pnp_ICstructure	*pnp_Icheck = NULL;
static uint16_t			pnp_NumNodes;
static uint16_t			pnp_NodeSize;

static void	biospnp_scanresdata(struct pnpinfo *pi, struct pnp_devNode *dn);
static int	biospnp_call(int func, const char *fmt, ...);

#define vsegofs(vptr)	(((uint32_t)VTOPSEG(vptr) << 16) + VTOPOFF(vptr))

typedef void    v86bios_t(uint32_t, uint32_t, uint32_t, uint32_t);
v86bios_t	*v86bios = (v86bios_t *)v86int;

#define	biospnp_f00(NumNodes, NodeSize)			biospnp_call(0x00, "ll", NumNodes, NodeSize)
#define biospnp_f01(Node, devNodeBuffer, Control)	biospnp_call(0x01, "llw", Node, devNodeBuffer, Control)
#define biospnp_f40(Configuration)			biospnp_call(0x40, "l", Configuration)

/* PnP BIOS return codes */
#define PNP_SUCCESS			0x00
#define PNP_FUNCTION_NOT_SUPPORTED	0x80

/*
 * Initialisation: locate the PnP BIOS, test that we can call it.
 * Returns nonzero if the PnP BIOS is not usable on this system.
 */
static int
biospnp_init(void)
{
    struct pnp_isaConfiguration	icfg;
    char			*sigptr;
    int				result;
    
    /* Search for the $PnP signature */
    pnp_Icheck = NULL;
    for (sigptr = PTOV(0xf0000); sigptr < PTOV(0xfffff); sigptr += 16)
	if (!bcmp(sigptr, "$PnP", 4)) {
	    pnp_Icheck = (struct pnp_ICstructure *)sigptr;
	    break;
	}
	
    /* No signature, no BIOS */
    if (pnp_Icheck == NULL)
	return(1);

    /*
     * Fetch the system table parameters as a test of the BIOS
     */
    result = biospnp_f00(vsegofs(&pnp_NumNodes), vsegofs(&pnp_NodeSize));
    if (result != PNP_SUCCESS) {
	return(1);
    }

    /*
     * Look for the PnP ISA configuration table 
     */
    result = biospnp_f40(vsegofs(&icfg));
    switch (result) {
    case PNP_SUCCESS:
	/* If the BIOS found some PnP devices, take its hint for the read port */
	if ((icfg.ic_revision == 1) && (icfg.ic_nCSN > 0))
	    isapnp_readport = icfg.ic_rdport;
	break;
    case PNP_FUNCTION_NOT_SUPPORTED:
	/* The BIOS says there is no ISA bus (should we trust that this works?) */
	printf("PnP BIOS claims no ISA bus\n");
	isapnp_readport = -1;
	break;
    }
    return(0);
}

static void
biospnp_enumerate(void)
{
    uint8_t		Node;
    struct pnp_devNode	*devNodeBuffer;
    int			result;
    struct pnpinfo	*pi;
    int			count;

    /* Init/check state */
    if (biospnp_init())
	return;

    devNodeBuffer = (struct pnp_devNode *)alloca(pnp_NodeSize);
    Node = 0;
    count = 1000;
    while((Node != 0xff) && (count-- > 0)) {
	result = biospnp_f01(vsegofs(&Node), vsegofs(devNodeBuffer), 0x1);
	if (result != PNP_SUCCESS) {
	    printf("PnP BIOS node %d: error 0x%x\n", Node, result);
	} else {
	    pi = pnp_allocinfo();
	    pnp_addident(pi, pnp_eisaformat(devNodeBuffer->dn_id));
	    biospnp_scanresdata(pi, devNodeBuffer);
	    pnp_addinfo(pi);
	}
    }
}

/*
 * Scan the resource data in the node's data area for compatible device IDs
 * and descriptions.
 */
static void
biospnp_scanresdata(struct pnpinfo *pi, struct pnp_devNode *dn)
{
    u_int	tag, i, rlen, dlen;
    uint8_t	*p;
    char	*str;

    p = dn->dn_data;			/* point to resource data */
    dlen = dn->dn_size - (p - (uint8_t *)dn);	/* length of resource data */

    for (i = 0; i < dlen; i+= rlen) {
	tag = p[i];
	i++;
	if (PNP_RES_TYPE(tag) == 0) {
	    rlen = PNP_SRES_LEN(tag);
	    /* small resource */
	    switch (PNP_SRES_NUM(tag)) {

	    case COMP_DEVICE_ID:
		/* got a compatible device ID */
		pnp_addident(pi, pnp_eisaformat(p + i));
		break;
		
	    case END_TAG:
		return;
	    }
	} else {
	    /* large resource */
	    rlen = *(uint16_t *)(p + i);
	    i += sizeof(uint16_t);
	    
	    switch(PNP_LRES_NUM(tag)) {

	    case ID_STRING_ANSI:
		str = malloc(rlen + 1);
		bcopy(p + i, str, rlen);
		str[rlen] = 0;
		if (pi->pi_desc == NULL) {
		    pi->pi_desc = str;
		} else {
		    free(str);
		}
		break;
	    }
	}
    }
}


/*
 * Make a 16-bit realmode PnP BIOS call.
 *
 * The first argument passed is the function number, the last is the
 * BIOS data segment selector.  Intermediate arguments may be 16 or 
 * 32 bytes in length, and are described by the format string.
 *
 * Arguments to the BIOS functions must be packed on the stack, hence
 * this evil.
 */
static int
biospnp_call(int func, const char *fmt, ...)
{
    va_list	ap;
    const char	*p;
    uint8_t	*argp;
    uint32_t	args[4];
    uint32_t	i;

    /* function number first */
    argp = (uint8_t *)args;
    *(uint16_t *)argp = func;
    argp += sizeof(uint16_t);

    /* take args according to format */
    va_start(ap, fmt);
    for (p = fmt; *p != 0; p++) {
	switch(*p) {

	case 'w':
	    i = va_arg(ap, u_int);
	    *(uint16_t *)argp = i;
	    argp += sizeof(uint16_t);
	    break;
	    
	case 'l':
	    i = va_arg(ap, uint32_t);
	    *(uint32_t *)argp = i;
	    argp += sizeof(uint32_t);
	    break;
	}
    }
    va_end(ap);

    /* BIOS segment last */
    *(uint16_t *)argp = pnp_Icheck->pnp_rmds;
    argp += sizeof(uint16_t);

    /* prepare for call */
    v86.ctl = V86_ADDR | V86_CALLF; 
    v86.addr = ((uint32_t)pnp_Icheck->pnp_rmcs << 16) + pnp_Icheck->pnp_rmip;
    
    /* call with packed stack and return */
    v86bios(args[0], args[1], args[2], args[3]);
    return(v86.eax & 0xffff);
}
