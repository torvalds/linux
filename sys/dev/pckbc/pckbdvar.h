/* $OpenBSD: pckbdvar.h,v 1.4 2010/12/03 18:29:56 shadchin Exp $ */
/* $NetBSD: pckbdvar.h,v 1.3 2000/03/10 06:10:35 thorpej Exp $ */

int	pckbd_cnattach(pckbc_tag_t);
void	pckbd_hookup_bell(void (*fn)(void *, u_int, u_int, u_int, int),
	    void *);
