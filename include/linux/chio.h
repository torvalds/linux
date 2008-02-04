/*
 * ioctl interface for the scsi media changer driver
 */

/* changer element types */
#define CHET_MT   0	/* media transport element (robot) */
#define CHET_ST   1	/* storage element (media slots) */
#define CHET_IE   2	/* import/export element */
#define CHET_DT   3	/* data transfer element (tape/cdrom/whatever) */
#define CHET_V1   4	/* vendor specific #1 */
#define CHET_V2   5	/* vendor specific #2 */
#define CHET_V3   6	/* vendor specific #3 */
#define CHET_V4   7	/* vendor specific #4 */


/*
 * CHIOGPARAMS
 *    query changer properties
 *
 * CHIOVGPARAMS
 *    query vendor-specific element types
 *
 *    accessing elements works by specifing type and unit of the element.
 *    for eample, storage elements are addressed with type = CHET_ST and
 *    unit = 0 .. cp_nslots-1
 *
 */
struct changer_params {
	int cp_curpicker;  /* current transport element */
	int cp_npickers;   /* number of transport elements      (CHET_MT) */
	int cp_nslots;     /* number of storage elements        (CHET_ST) */
	int cp_nportals;   /* number of import/export elements  (CHET_IE) */
	int cp_ndrives;    /* number of data transfer elements  (CHET_DT) */
};
struct changer_vendor_params {
	int  cvp_n1;       /* number of vendor specific elems   (CHET_V1) */
	char cvp_label1[16];
	int  cvp_n2;       /* number of vendor specific elems   (CHET_V2) */
	char cvp_label2[16];
	int  cvp_n3;       /* number of vendor specific elems   (CHET_V3) */
	char cvp_label3[16];
	int  cvp_n4;       /* number of vendor specific elems   (CHET_V4) */
	char cvp_label4[16];
	int  reserved[8];
};


/*
 * CHIOMOVE
 *    move a medium from one element to another
 */
struct changer_move {
	int cm_fromtype;	/* type/unit of source element */
	int cm_fromunit;	
	int cm_totype;	/* type/unit of destination element */
	int cm_tounit;
	int cm_flags;
};
#define CM_INVERT   1   /* flag: rotate media (for double-sided like MOD) */


/*
 * CHIOEXCHANGE
 *    move one medium from element #1 to element #2,
 *    and another one from element #2 to element #3.
 *    element #1 and #3 are allowed to be identical.
 */
struct changer_exchange {
	int ce_srctype;	    /* type/unit of element #1 */
	int ce_srcunit;
	int ce_fdsttype;    /* type/unit of element #2 */
	int ce_fdstunit;
	int ce_sdsttype;    /* type/unit of element #3 */
	int ce_sdstunit;
	int ce_flags;
};
#define CE_INVERT1   1
#define CE_INVERT2   2


/*
 * CHIOPOSITION
 *    move the transport element (robot arm) to a specific element.
 */
struct changer_position {
	int cp_type;
	int cp_unit;
	int cp_flags;
};
#define CP_INVERT   1


/*
 * CHIOGSTATUS
 *    get element status for all elements of a specific type
 */
struct changer_element_status {
	int             ces_type;
	unsigned char   __user *ces_data;
};
#define CESTATUS_FULL     0x01 /* full */
#define CESTATUS_IMPEXP   0x02	/* media was imported (inserted by sysop) */
#define CESTATUS_EXCEPT   0x04	/* error condition */
#define CESTATUS_ACCESS   0x08	/* access allowed */
#define CESTATUS_EXENAB   0x10	/* element can export media */
#define CESTATUS_INENAB   0x20	/* element can import media */


/*
 * CHIOGELEM
 *    get more detailed status information for a single element
 */
struct changer_get_element {
	int	cge_type;	 /* type/unit */
	int	cge_unit;
	int	cge_status;      /* status */
	int     cge_errno;       /* errno */
	int     cge_srctype;     /* source element of the last move/exchange */
	int     cge_srcunit;
	int     cge_id;          /* scsi id  (for data transfer elements) */
	int     cge_lun;         /* scsi lun (for data transfer elements) */
	char    cge_pvoltag[36]; /* primary volume tag */
	char    cge_avoltag[36]; /* alternate volume tag */
	int     cge_flags;
};
/* flags */
#define CGE_ERRNO     0x01       /* errno available       */
#define CGE_INVERT    0x02       /* media inverted        */
#define CGE_SRC       0x04       /* media src available   */
#define CGE_IDLUN     0x08       /* ID+LUN available      */
#define CGE_PVOLTAG   0x10       /* primary volume tag available */
#define CGE_AVOLTAG   0x20       /* alternate volume tag available */


/*
 * CHIOSVOLTAG
 *    set volume tag
 */
struct changer_set_voltag {
	int	csv_type;	 /* type/unit */
	int	csv_unit;
	char    csv_voltag[36];  /* volume tag */
	int     csv_flags;
};
#define CSV_PVOLTAG   0x01       /* primary volume tag */
#define CSV_AVOLTAG   0x02       /* alternate volume tag */
#define CSV_CLEARTAG  0x04       /* clear volume tag */

/* ioctls */
#define CHIOMOVE       _IOW('c', 1,struct changer_move)
#define CHIOEXCHANGE   _IOW('c', 2,struct changer_exchange)
#define CHIOPOSITION   _IOW('c', 3,struct changer_position)
#define CHIOGPICKER    _IOR('c', 4,int)                        /* not impl. */
#define CHIOSPICKER    _IOW('c', 5,int)                        /* not impl. */
#define CHIOGPARAMS    _IOR('c', 6,struct changer_params)
#define CHIOGSTATUS    _IOW('c', 8,struct changer_element_status)
#define CHIOGELEM      _IOW('c',16,struct changer_get_element)
#define CHIOINITELEM   _IO('c',17)
#define CHIOSVOLTAG    _IOW('c',18,struct changer_set_voltag)
#define CHIOGVPARAMS   _IOR('c',19,struct changer_vendor_params)

/* ---------------------------------------------------------------------- */

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
