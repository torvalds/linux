/*-
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD$
 *
 */
#ifndef	_SYS_INFLATE_H_
#define	_SYS_INFLATE_H_

#if defined(_KERNEL) || defined(KZIP)

#define GZ_EOF -1

#define GZ_WSIZE 0x8000

/*
 * Global variables used by inflate and friends.
 * This structure is used in order to make inflate() reentrant.
 */
struct inflate {
	/* Public part */

	/* This pointer is passed along to the two functions below */
	void           *gz_private;

	/* Fetch next character to be uncompressed */
	int             (*gz_input)(void *);

	/* Dispose of uncompressed characters */
	int             (*gz_output)(void *, u_char *, u_long);

	/* Private part */
	u_long          gz_bb;	/* bit buffer */
	unsigned        gz_bk;	/* bits in bit buffer */
	unsigned        gz_hufts;	/* track memory usage */
	struct huft    *gz_fixed_tl;	/* must init to NULL !! */
	struct huft    *gz_fixed_td;
	int             gz_fixed_bl;
	int             gz_fixed_bd;
	u_char         *gz_slide;
	unsigned        gz_wp;
};

int inflate(struct inflate *);

#endif	/* _KERNEL || KZIP */

#endif	/* ! _SYS_INFLATE_H_ */
