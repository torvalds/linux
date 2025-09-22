/*	$OpenBSD: ascvar.h,v 1.9 2002/05/02 22:56:06 miod Exp $	*/
/*	$NetBSD: ascvar.h,v 1.7 2000/10/31 15:16:26 simonb Exp $	*/

/*
 * State kept for each active SCSI host interface (53C94).
 */

struct asc_softc {
	struct ncr53c9x_softc sc_ncr53c9x;	/* glue to MI code */
	bus_space_tag_t sc_bst;			/* bus space tag */
	bus_space_handle_t sc_bsh;		/* ASC register handle */
	bus_dma_tag_t sc_dmat;			/* bus dma tag */
	bus_dmamap_t sc_dmamap;			/* bus dmamap */
	caddr_t *sc_dmaaddr;
	size_t *sc_dmalen;
	size_t sc_dmasize;
	unsigned sc_flags;
#define ASC_ISPULLUP		0x01
#define ASC_DMAACTIVE		0x02
#define ASC_MAPLOADED		0x04
};

u_char	asc_read_reg(struct ncr53c9x_softc *, int);
void	asc_write_reg(struct ncr53c9x_softc *, int, u_char);
