
/* Various types of HFS files stored on Unix systems */
#define TYPE_NONE	0	/* Unknown file type (ordinary Unix file) */
#define TYPE_CAP	1	/* AUFS CAP */
#define TYPE_NETA	2	/* Netatalk */
#define TYPE_DBL	3	/* AppleDouble */
#define TYPE_ESH	4	/* Helios EtherShare */
#define TYPE_FEU	5	/* PC Exchange (Upper case) */
#define TYPE_FEL	6	/* PC Exchange (Lower case) */
#define TYPE_SGI	7	/* SGI */
#define TYPE_MBIN	8	/* MacBinary */
#define TYPE_SGL	9	/* AppleSingle */

/* above encoded in a bit map */
#define DO_NONE		(1 << TYPE_NONE)
#define DO_CAP		(1 << TYPE_CAP)
#define DO_NETA		(1 << TYPE_NETA)
#define DO_DBL		(1 << TYPE_DBL)
#define DO_ESH		(1 << TYPE_ESH)
#define DO_FEU		(1 << TYPE_FEU)
#define DO_FEL		(1 << TYPE_FEL)
#define DO_SGI		(1 << TYPE_SGI)
#define DO_MBIN		(1 << TYPE_MBIN)
#define DO_SGL		(1 << TYPE_SGL)

