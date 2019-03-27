/*
 * mjs copyright
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * "Plug and Play" functionality.
 *
 * We use the PnP enumerators to obtain identifiers for installed hardware,
 * and the contents of a database to determine modules to be loaded to support
 * such hardware.
 */

#include <stand.h>
#include <string.h>
#include <bootstrap.h>

static struct pnpinfo_stql pnp_devices;
static int		pnp_devices_initted = 0;

static void		pnp_discard(void);

/*
 * Perform complete enumeration sweep
 */

COMMAND_SET(pnpscan, "pnpscan", "scan for PnP devices", pnp_scan);

static int
pnp_scan(int argc, char *argv[]) 
{
    struct pnpinfo	*pi;
    int			hdlr;
    int			verbose;
    int			ch;

    if (pnp_devices_initted == 0) {
	STAILQ_INIT(&pnp_devices);
	pnp_devices_initted = 1;
    }

    verbose = 0;
    optind = 1;
    optreset = 1;
    while ((ch = getopt(argc, argv, "v")) != -1) {
	switch(ch) {
	case 'v':
	    verbose = 1;
	    break;
	case '?':
	default:
	    /* getopt has already reported an error */
	    return(CMD_OK);
	}
    }

    /* forget anything we think we knew */
    pnp_discard();

    /* iterate over all of the handlers */
    for (hdlr = 0; pnphandlers[hdlr] != NULL; hdlr++) {
	if (verbose)
	    printf("Probing %s...\n", pnphandlers[hdlr]->pp_name);
	pnphandlers[hdlr]->pp_enumerate();
    }
    if (verbose) {
	pager_open();
	if (pager_output("PNP scan summary:\n"))
		goto out;
	STAILQ_FOREACH(pi, &pnp_devices, pi_link) {
	    pager_output(STAILQ_FIRST(&pi->pi_ident)->id_ident);	/* first ident should be canonical */
	    if (pi->pi_desc != NULL) {
		pager_output(" : ");
		pager_output(pi->pi_desc);
	    }
	    if (pager_output("\n"))
		    break;
	}
out:
	pager_close();
    }
    return(CMD_OK);
}

/*
 * Throw away anything we think we know about PnP devices.
 */
static void
pnp_discard(void)
{
    struct pnpinfo	*pi;

    while (STAILQ_FIRST(&pnp_devices) != NULL) {
	pi = STAILQ_FIRST(&pnp_devices);
	STAILQ_REMOVE_HEAD(&pnp_devices, pi_link);
	pnp_freeinfo(pi);
    }
}

/*
 * Add a unique identifier to (pi)
 */
void
pnp_addident(struct pnpinfo *pi, char *ident)
{
    struct pnpident	*id;

    STAILQ_FOREACH(id, &pi->pi_ident, id_link)
	if (!strcmp(id->id_ident, ident))
	    return;			/* already have this one */

    id = malloc(sizeof(struct pnpident));
    id->id_ident = strdup(ident);
    STAILQ_INSERT_TAIL(&pi->pi_ident, id, id_link);
}

/*
 * Allocate a new pnpinfo struct
 */
struct pnpinfo *
pnp_allocinfo(void)
{
    struct pnpinfo	*pi;
    
    pi = malloc(sizeof(struct pnpinfo));
    bzero(pi, sizeof(struct pnpinfo));
    STAILQ_INIT(&pi->pi_ident);
    return(pi);
}

/*
 * Release storage held by a pnpinfo struct
 */
void
pnp_freeinfo(struct pnpinfo *pi)
{
    struct pnpident	*id;

    while (!STAILQ_EMPTY(&pi->pi_ident)) {
	id = STAILQ_FIRST(&pi->pi_ident);
	STAILQ_REMOVE_HEAD(&pi->pi_ident, id_link);
	free(id->id_ident);
	free(id);
    }
    if (pi->pi_desc)
	free(pi->pi_desc);
    if (pi->pi_module)
	free(pi->pi_module);
    if (pi->pi_argv)
	free(pi->pi_argv);
    free(pi);
}

/*
 * Add a new pnpinfo struct to the list.
 */
void
pnp_addinfo(struct pnpinfo *pi)
{
    STAILQ_INSERT_TAIL(&pnp_devices, pi, pi_link);
}


/*
 * Format an EISA id as a string in standard ISA PnP format, AAAIIRR
 * where 'AAA' is the EISA vendor ID, II is the product ID and RR the revision ID.
 */
char *
pnp_eisaformat(uint8_t *data)
{
    static char	idbuf[8];
    const char	hextoascii[] = "0123456789abcdef";

    idbuf[0] = '@' + ((data[0] & 0x7c) >> 2);
    idbuf[1] = '@' + (((data[0] & 0x3) << 3) + ((data[1] & 0xe0) >> 5));
    idbuf[2] = '@' + (data[1] & 0x1f);
    idbuf[3] = hextoascii[(data[2] >> 4)];
    idbuf[4] = hextoascii[(data[2] & 0xf)];
    idbuf[5] = hextoascii[(data[3] >> 4)];
    idbuf[6] = hextoascii[(data[3] & 0xf)];
    idbuf[7] = 0;
    return(idbuf);
}
