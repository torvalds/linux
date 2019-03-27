/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1996, by Steve Passe
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the developer may NOT be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

/*
 * mptable.c
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

/*
 * this will cause the raw mp table to be dumped to /tmp/mpdump
 *
#define RAW_DUMP
 */

#define MP_SIG			0x5f504d5f	/* _MP_ */
#define EXTENDED_PROCESSING_READY
#define OEM_PROCESSING_READY_NOT

#include <sys/param.h>
#include <sys/mman.h>
#include <x86/mptable.h>
#include <err.h>
#include <fcntl.h>
#include <paths.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SEP_LINE \
"\n-------------------------------------------------------------------------------\n"

#define SEP_LINE2 \
"\n===============================================================================\n"

/* EBDA is @ 40:0e in real-mode terms */
#define EBDA_POINTER		0x040e		/* location of EBDA pointer */

/* CMOS 'top of mem' is @ 40:13 in real-mode terms */
#define TOPOFMEM_POINTER	0x0413		/* BIOS: base memory size */

#define DEFAULT_TOPOFMEM	0xa0000

#define BIOS_BASE		0xf0000
#define BIOS_BASE2		0xe0000
#define BIOS_SIZE		0x10000
#define ONE_KBYTE		1024

#define GROPE_AREA1		0x80000
#define GROPE_AREA2		0x90000
#define GROPE_SIZE		0x10000

#define MAXPNSTR		132

typedef struct BUSTYPENAME {
    u_char	type;
    char	name[ 7 ];
} busTypeName;

static const busTypeName busTypeTable[] =
{
    { CBUS,		"CBUS"   },
    { CBUSII,		"CBUSII" },
    { EISA,		"EISA"   },
    { UNKNOWN_BUSTYPE,	"---"    },
    { UNKNOWN_BUSTYPE,	"---"    },
    { ISA,		"ISA"    },
    { UNKNOWN_BUSTYPE,	"---"    },
    { UNKNOWN_BUSTYPE,	"---"    },
    { UNKNOWN_BUSTYPE,	"---"    },
    { UNKNOWN_BUSTYPE,	"---"    },
    { UNKNOWN_BUSTYPE,	"---"    },
    { UNKNOWN_BUSTYPE,	"---"    },
    { PCI,		"PCI"    },
    { UNKNOWN_BUSTYPE,	"---"    },
    { UNKNOWN_BUSTYPE,	"---"    },
    { UNKNOWN_BUSTYPE,	"---"    },
    { UNKNOWN_BUSTYPE,	"---"    },
    { UNKNOWN_BUSTYPE,	"---"    },
    { UNKNOWN_BUSTYPE,	"---"    }
};

static const char *whereStrings[] = {
    "Extended BIOS Data Area",
    "BIOS top of memory",
    "Default top of memory",
    "BIOS",
    "Extended BIOS",
    "GROPE AREA #1",
    "GROPE AREA #2"
};

static void apic_probe( u_int32_t* paddr, int* where );

static void MPConfigDefault( int featureByte );

static void MPFloatingPointer( u_int32_t paddr, int where, mpfps_t* mpfpsp );
static void MPConfigTableHeader( u_int32_t pap );

static void seekEntry( u_int32_t addr );
static void readEntry( void* entry, int size );
static void *mapEntry( u_int32_t addr, int size );

static void processorEntry( proc_entry_ptr entry );
static void busEntry( bus_entry_ptr entry );
static void ioApicEntry( io_apic_entry_ptr entry );
static void intEntry( int_entry_ptr entry );

static void sasEntry( sas_entry_ptr entry );
static void bhdEntry( bhd_entry_ptr entry );
static void cbasmEntry( cbasm_entry_ptr entry );

static void doDmesg( void );
static void pnstr( char* s, int c );

/* global data */
static int	pfd;		/* physical /dev/mem fd */

static int	busses[256];
static int	apics[256];

static int	ncpu;
static int	nbus;
static int	napic;
static int	nintr;

static int	dmesg;
static int	grope;
static int	verbose;

static void
usage( void )
{
    fprintf( stderr, "usage: mptable [-dmesg] [-verbose] [-grope] [-help]\n" );
    exit( 0 );
}

/*
 * 
 */
int
main( int argc, char *argv[] )
{
    u_int32_t	paddr;
    int		where;
    mpfps_t	mpfps;
    int		defaultConfig;

    int		ch;

    /* announce ourselves */
    puts( SEP_LINE2 );

    printf( "MPTable\n" );

    while ((ch = getopt(argc, argv, "d:g:h:v:")) != -1) {
	switch(ch) {
	case 'd':
	    if ( strcmp( optarg, "mesg") == 0 )
	        dmesg = 1;
	    else
	        dmesg = 0;
	    break;
	case 'h':
	    if ( strcmp( optarg, "elp") == 0 )
	        usage();
	    break;
	case 'g':
	    if ( strcmp( optarg, "rope") == 0 )
	        grope = 1;
	    break;
	case 'v':
	    if ( strcmp( optarg, "erbose") == 0 )
	        verbose = 1;
	    break;
	default:
	    usage();
	}
	argc -= optind;
	argv += optind;
	optreset = 1;
	optind = 0;
    }

    /* open physical memory for access to MP structures */
    if ( (pfd = open( _PATH_MEM, O_RDONLY )) < 0 )
        err( 1, "mem open" );

    /* probe for MP structures */
    apic_probe( &paddr, &where );
    if ( where <= 0 ) {
        fprintf( stderr, "\n MP FPS NOT found,\n" );
        if (!grope)
            fprintf( stderr, " suggest trying -grope option!!!\n\n" );
        return 1;
    }

    if ( verbose )
        printf( "\n MP FPS found in %s @ physical addr: 0x%08x\n",
	      whereStrings[ where - 1 ], paddr );

    puts( SEP_LINE );

    /* analyze the MP Floating Pointer Structure */
    MPFloatingPointer( paddr, where, &mpfps );

    puts( SEP_LINE );

    /* check whether an MP config table exists */
    if ( (defaultConfig = mpfps->config_type) )
        MPConfigDefault( defaultConfig );
    else
	MPConfigTableHeader( mpfps->pap );

    /* do a dmesg output */
    if ( dmesg )
        doDmesg();

    puts( SEP_LINE2 );

    return 0;
}


/*
 * set PHYSICAL address of MP floating pointer structure
 */
#define NEXT(X)		((X) += 4)
static void
apic_probe( u_int32_t* paddr, int* where )
{
    /*
     * c rewrite of apic_probe() by Jack F. Vogel
     */

    int		x;
    u_short	segment;
    u_int32_t	target;
    u_int	buffer[ BIOS_SIZE / sizeof( int ) ];

    if ( verbose )
        printf( "\n" );

    /* search Extended Bios Data Area, if present */
    if ( verbose )
        printf( " looking for EBDA pointer @ 0x%04x, ", EBDA_POINTER );
    seekEntry( (u_int32_t)EBDA_POINTER );
    readEntry( &segment, 2 );
    if ( segment ) {		    /* search EBDA */
        target = (u_int32_t)segment << 4;
	if ( verbose )
	    printf( "found, searching EBDA @ 0x%08x\n", target );
        seekEntry( target );
        readEntry( buffer, ONE_KBYTE );

        for ( x = 0; x < ONE_KBYTE / (int)sizeof ( unsigned int ); NEXT(x) ) {
            if ( buffer[ x ] == MP_SIG ) {
                *where = 1;
                *paddr = (x * sizeof( unsigned int )) + target;
                return;
            }
        }
    }
    else {
	if ( verbose )
	    printf( "NOT found\n" );
    }

    /* read CMOS for real top of mem */
    seekEntry( (u_int32_t)TOPOFMEM_POINTER );
    readEntry( &segment, 2 );
    --segment;						/* less ONE_KBYTE */
    target = segment * 1024;
    if ( verbose )
        printf( " searching CMOS 'top of mem' @ 0x%08x (%dK)\n",
	        target, segment );
    seekEntry( target );
    readEntry( buffer, ONE_KBYTE );

    for ( x = 0; x < ONE_KBYTE / (int)sizeof ( unsigned int ); NEXT(x) ) {
        if ( buffer[ x ] == MP_SIG ) {
            *where = 2;
            *paddr = (x * sizeof( unsigned int )) + target;
            return;
        }
    }

    /* we don't necessarily believe CMOS, check base of the last 1K of 640K */
    if ( target != (DEFAULT_TOPOFMEM - 1024)) {
	target = (DEFAULT_TOPOFMEM - 1024);
	if ( verbose )
	    printf( " searching default 'top of mem' @ 0x%08x (%dK)\n",
		    target, (target / 1024) );
	seekEntry( target );
	readEntry( buffer, ONE_KBYTE );

	for ( x = 0; x < ONE_KBYTE / (int)sizeof ( unsigned int ); NEXT(x) ) {
	    if ( buffer[ x ] == MP_SIG ) {
		*where = 3;
		*paddr = (x * sizeof( unsigned int )) + target;
		return;
	    }
	}
    }

    /* search the BIOS */
    if ( verbose )
        printf( " searching BIOS @ 0x%08x\n", BIOS_BASE );
    seekEntry( BIOS_BASE );
    readEntry( buffer, BIOS_SIZE );

    for ( x = 0; x < BIOS_SIZE / (int)sizeof( unsigned int ); NEXT(x) ) {
        if ( buffer[ x ] == MP_SIG ) {
            *where = 4;
            *paddr = (x * sizeof( unsigned int )) + BIOS_BASE;
            return;
        }
    }

    /* search the extended BIOS */
    if ( verbose )
        printf( " searching extended BIOS @ 0x%08x\n", BIOS_BASE2 );
    seekEntry( BIOS_BASE2 );
    readEntry( buffer, BIOS_SIZE );

    for ( x = 0; x < BIOS_SIZE / (int)sizeof( unsigned int ); NEXT(x) ) {
        if ( buffer[ x ] == MP_SIG ) {
            *where = 5;
            *paddr = (x * sizeof( unsigned int )) + BIOS_BASE2;
            return;
        }
    }

    if ( grope ) {
	/* search additional memory */
	target = GROPE_AREA1;
	if ( verbose )
	    printf( " groping memory @ 0x%08x\n", target );
	seekEntry( target );
	readEntry( buffer, GROPE_SIZE );

	for ( x = 0; x < GROPE_SIZE / (int)sizeof( unsigned int ); NEXT(x) ) {
	    if ( buffer[ x ] == MP_SIG ) {
		*where = 6;
		*paddr = (x * sizeof( unsigned int )) + GROPE_AREA1;
		return;
	    }
	}

	target = GROPE_AREA2;
	if ( verbose )
	    printf( " groping memory @ 0x%08x\n", target );
	seekEntry( target );
	readEntry( buffer, GROPE_SIZE );

	for ( x = 0; x < GROPE_SIZE / (int)sizeof( unsigned int ); NEXT(x) ) {
	    if ( buffer[ x ] == MP_SIG ) {
		*where = 7;
		*paddr = (x * sizeof( unsigned int )) + GROPE_AREA2;
		return;
	    }
	}
    }

    *where = 0;
    *paddr = (u_int32_t)0;
}


/*
 * 
 */
static void
MPFloatingPointer( u_int32_t paddr, int where, mpfps_t* mpfpsp )
{
    mpfps_t mpfps;
	
    /* map in mpfps structure*/
    *mpfpsp = mpfps = mapEntry( paddr, sizeof( *mpfps ) );

    /* show its contents */
    printf( "MP Floating Pointer Structure:\n\n" );

    printf( "  location:\t\t\t" );
    switch ( where )
    {
    case 1:
	printf( "EBDA\n" );
	break;
    case 2:
	printf( "BIOS base memory\n" );
	break;
    case 3:
	printf( "DEFAULT base memory (639K)\n" );
	break;
    case 4:
	printf( "BIOS\n" );
	break;
    case 5:
	printf( "Extended BIOS\n" );
	break;

    case 0:
	printf( "NOT found!\n" );
	exit( 1 );
    default:
	printf( "BOGUS!\n" );
	exit( 1 );
    }
    printf( "  physical address:\t\t0x%08x\n", paddr );

    printf( "  signature:\t\t\t'" );
    pnstr( mpfps->signature, 4 );
    printf( "'\n" );

    printf( "  length:\t\t\t%d bytes\n", mpfps->length * 16 );
    printf( "  version:\t\t\t1.%1d\n", mpfps->spec_rev );
    printf( "  checksum:\t\t\t0x%02x\n", mpfps->checksum );

    /* bits 0:6 are RESERVED */
    if ( mpfps->mpfb2 & 0x7f ) {
        printf( " warning, MP feature byte 2: 0x%02x\n", mpfps->mpfb2 );
    }

    /* bit 7 is IMCRP */
    printf( "  mode:\t\t\t\t%s\n", (mpfps->mpfb2 & MPFB2_IMCR_PRESENT) ?
            "PIC" : "Virtual Wire" );

    /* MP feature bytes 3-5 are expected to be ZERO */
    if ( mpfps->mpfb3 )
        printf( " warning, MP feature byte 3 NONZERO!\n" );
    if ( mpfps->mpfb4 )
        printf( " warning, MP feature byte 4 NONZERO!\n" );
    if ( mpfps->mpfb5 )
        printf( " warning, MP feature byte 5 NONZERO!\n" );
}


/*
 * 
 */
static void
MPConfigDefault( int featureByte )
{
    printf( "  MP default config type: %d\n\n", featureByte );
    switch ( featureByte ) {
    case 1:
	printf( "   bus: ISA, APIC: 82489DX\n" );
	break;
    case 2:
	printf( "   bus: EISA, APIC: 82489DX\n" );
	break;
    case 3:
	printf( "   bus: EISA, APIC: 82489DX\n" );
	break;
    case 4:
	printf( "   bus: MCA, APIC: 82489DX\n" );
	break;
    case 5:
	printf( "   bus: ISA+PCI, APIC: Integrated\n" );
	break;
    case 6:
	printf( "   bus: EISA+PCI, APIC: Integrated\n" );
	break;
    case 7:
	printf( "   bus: MCA+PCI, APIC: Integrated\n" );
	break;
    default:
	printf( "   future type\n" );
	break;
    }

    switch ( featureByte ) {
    case 1:
    case 2:
    case 3:
    case 4:
	nbus = 1;
	break;
    case 5:
    case 6:
    case 7:
	nbus = 2;
	break;
    default:
	printf( "   future type\n" );
	break;
    }

    ncpu = 2;
    napic = 1;
    nintr = 16;
}


/*
 * 
 */
static void
MPConfigTableHeader( u_int32_t pap )
{
    mpcth_t	cth;
    int		x;
    int		totalSize;
    int		c;
    int		oldtype, entrytype;
    u_int8_t	*entry;

    if ( pap == 0 ) {
	printf( "MP Configuration Table Header MISSING!\n" );
        exit( 1 );
    }

    /* map in cth structure */
    cth = mapEntry( pap, sizeof( *cth ) );

    printf( "MP Config Table Header:\n\n" );

    printf( "  physical address:\t\t0x%08x\n", pap );

    printf( "  signature:\t\t\t'" );
    pnstr( cth->signature, 4 );
    printf( "'\n" );

    printf( "  base table length:\t\t%d\n", cth->base_table_length );

    printf( "  version:\t\t\t1.%1d\n", cth->spec_rev );
    printf( "  checksum:\t\t\t0x%02x\n", cth->checksum );

    printf( "  OEM ID:\t\t\t'" );
    pnstr( cth->oem_id, 8 );
    printf( "'\n" );

    printf( "  Product ID:\t\t\t'" );
    pnstr( cth->product_id, 12 );
    printf( "'\n" );

    printf( "  OEM table pointer:\t\t0x%08x\n", cth->oem_table_pointer );
    printf( "  OEM table size:\t\t%d\n", cth->oem_table_size );

    printf( "  entry count:\t\t\t%d\n", cth->entry_count );

    printf( "  local APIC address:\t\t0x%08x\n", cth->apic_address );

    printf( "  extended table length:\t%d\n", cth->extended_table_length );
    printf( "  extended table checksum:\t%d\n", cth->extended_table_checksum );

    totalSize = cth->base_table_length - sizeof( struct MPCTH );

    puts( SEP_LINE );

    printf( "MP Config Base Table Entries:\n\n" );

    /* initialize tables */
    for (x = 0; x < (int)nitems(busses); x++)
	busses[x] = 0xff;

    for (x = 0; x < (int)nitems(apics); x++)
	apics[x] = 0xff;

    ncpu = 0;
    nbus = 0;
    napic = 0;
    nintr = 0;

    oldtype = -1;
    entry = mapEntry(pap + sizeof(*cth), cth->base_table_length);
    for (c = cth->entry_count; c; c--) {
	entrytype = *entry;
	if (entrytype != oldtype)
	    printf("--\n");
	if (entrytype < oldtype)
	    printf("MPTABLE OUT OF ORDER!\n");
	switch (entrytype) {
	case MPCT_ENTRY_PROCESSOR:
	    if (oldtype != MPCT_ENTRY_PROCESSOR)
		printf( "Processors:\tAPIC ID\tVersion\tState"
			"\t\tFamily\tModel\tStep\tFlags\n" );
	    processorEntry((proc_entry_ptr)entry);
	    entry += sizeof(struct PROCENTRY);
	    break;

	case MPCT_ENTRY_BUS:
	    if (oldtype != MPCT_ENTRY_BUS)
		printf( "Bus:\t\tBus ID\tType\n" );
	    busEntry((bus_entry_ptr)entry);
	    entry += sizeof(struct BUSENTRY);
	    break;

	case MPCT_ENTRY_IOAPIC:
	    if (oldtype != MPCT_ENTRY_IOAPIC)
		printf( "I/O APICs:\tAPIC ID\tVersion\tState\t\tAddress\n" );
	    ioApicEntry((io_apic_entry_ptr)entry);
	    entry += sizeof(struct IOAPICENTRY);
	    break;

	case MPCT_ENTRY_INT:
	    if (oldtype != MPCT_ENTRY_INT)
		printf( "I/O Ints:\tType\tPolarity    Trigger\tBus ID\t IRQ\tAPIC ID\tPIN#\n" );
	    intEntry((int_entry_ptr)entry);
	    entry += sizeof(struct INTENTRY);
	    break;

	case MPCT_ENTRY_LOCAL_INT:
	    if (oldtype != MPCT_ENTRY_LOCAL_INT)
		printf( "Local Ints:\tType\tPolarity    Trigger\tBus ID\t IRQ\tAPIC ID\tPIN#\n" );
	    intEntry((int_entry_ptr)entry);
	    entry += sizeof(struct INTENTRY);
	    break;

	default:
	    printf("MPTABLE HOSED! record type = %d\n", entrytype);
	    exit(1);
	}
	oldtype = entrytype;
    }


#if defined( EXTENDED_PROCESSING_READY )
    /* process any extended data */
    if ( cth->extended_table_length ) {
	ext_entry_ptr ext_entry, end;

	puts( SEP_LINE );

        printf( "MP Config Extended Table Entries:\n\n" );

	ext_entry = mapEntry(pap + cth->base_table_length,
	    cth->extended_table_length);
	end = (ext_entry_ptr)((char *)ext_entry + cth->extended_table_length);
	while (ext_entry < end) {
	    switch (ext_entry->type) {
            case MPCT_EXTENTRY_SAS:
		sasEntry((sas_entry_ptr)ext_entry);
		break;
            case MPCT_EXTENTRY_BHD:
		bhdEntry((bhd_entry_ptr)ext_entry);
		break;
            case MPCT_EXTENTRY_CBASM:
		cbasmEntry((cbasm_entry_ptr)ext_entry);
		break;
            default:
                printf( "Extended Table HOSED!\n" );
                exit( 1 );
            }

	    ext_entry = (ext_entry_ptr)((char *)ext_entry + ext_entry->length);
        }
    }
#endif  /* EXTENDED_PROCESSING_READY */

    /* process any OEM data */
    if ( cth->oem_table_pointer && (cth->oem_table_size > 0) ) {
#if defined( OEM_PROCESSING_READY )
# error your on your own here!
        /* map in oem table structure */
	oemdata = mapEntry( cth->oem_table_pointer, cth->oem_table_size);

        /** process it */
#else
        printf( "\nyou need to modify the source to handle OEM data!\n\n" );
#endif  /* OEM_PROCESSING_READY */
    }

    fflush( stdout );

#if defined( RAW_DUMP )
{
    int		ofd;
    void	*dumpbuf;

    ofd = open( "/tmp/mpdump", O_CREAT | O_RDWR, 0666 );
    
    dumpbuf = mapEntry( paddr, 1024 );
    write( ofd, dumpbuf, 1024 );
    close( ofd );
}
#endif /* RAW_DUMP */
}


/*
 * 
 */
static void
seekEntry( u_int32_t addr )
{
    if ( lseek( pfd, (off_t)addr, SEEK_SET ) < 0 )
        err( 1, "%s seek", _PATH_MEM );
}


/*
 * 
 */
static void
readEntry( void* entry, int size )
{
    if ( read( pfd, entry, size ) != size )
        err( 1, "readEntry" );
}

static void *
mapEntry( u_int32_t addr, int size )
{
    void	*p;

    p = mmap( NULL, size, PROT_READ, MAP_SHARED, pfd, addr );
    if (p == MAP_FAILED)
	err( 1, "mapEntry" );
    return (p);
}

static void
processorEntry( proc_entry_ptr entry )
{

    /* count it */
    ++ncpu;

    printf( "\t\t%2d", entry->apic_id );
    printf( "\t 0x%2x", entry->apic_version );

    printf( "\t %s, %s",
            (entry->cpu_flags & PROCENTRY_FLAG_BP) ? "BSP" : "AP",
            (entry->cpu_flags & PROCENTRY_FLAG_EN) ? "usable" : "unusable" );

    printf( "\t %d\t %d\t %d",
            (entry->cpu_signature >> 8) & 0x0f,
            (entry->cpu_signature >> 4) & 0x0f,
            entry->cpu_signature & 0x0f );

    printf( "\t 0x%04x\n", entry->feature_flags );
}


/*
 * 
 */
static int
lookupBusType( char* name )
{
    int x;

    for ( x = 0; x < MAX_BUSTYPE; ++x )
	if ( strcmp( busTypeTable[ x ].name, name ) == 0 )
	    return busTypeTable[ x ].type;

    return UNKNOWN_BUSTYPE;
}


static void
busEntry( bus_entry_ptr entry )
{
    int		x;
    char	name[ 8 ];
    char	c;

    /* count it */
    ++nbus;

    printf( "\t\t%2d", entry->bus_id );
    printf( "\t " ); pnstr( entry->bus_type, 6 ); printf( "\n" );

    for ( x = 0; x < 6; ++x ) {
	if ( (c = entry->bus_type[ x ]) == ' ' )
	    break;
	name[ x ] = c;
    }
    name[ x ] = '\0';
    busses[ entry->bus_id ] = lookupBusType( name );
}


static void
ioApicEntry( io_apic_entry_ptr entry )
{

    /* count it */
    ++napic;

    printf( "\t\t%2d", entry->apic_id );
    printf( "\t 0x%02x", entry->apic_version );
    printf( "\t %s",
            (entry->apic_flags & IOAPICENTRY_FLAG_EN) ? "usable" : "unusable" );
    printf( "\t\t 0x%x\n", entry->apic_address );

    apics[ entry->apic_id ] = entry->apic_id;
}


static const char *intTypes[] = {
    "INT", "NMI", "SMI", "ExtINT"
};

static const char *polarityMode[] = {
    "conforms", "active-hi", "reserved", "active-lo"
};
static const char *triggerMode[] = {
    "conforms", "edge", "reserved", "level"
};

static void
intEntry( int_entry_ptr entry )
{

    /* count it */
    if ( entry->type == MPCT_ENTRY_INT )
	++nintr;

    printf( "\t\t%s", intTypes[ entry->int_type ] );

    printf( "\t%9s", polarityMode[ entry->int_flags & INTENTRY_FLAGS_POLARITY ] );
    printf( "%12s", triggerMode[ (entry->int_flags & INTENTRY_FLAGS_TRIGGER) >> 2 ] );

    printf( "\t %5d", entry->src_bus_id );
    if ( busses[ entry->src_bus_id ] == PCI )
	printf( "\t%2d:%c", 
	        (entry->src_bus_irq >> 2) & 0x1f,
	        (entry->src_bus_irq & 0x03) + 'A' );
    else
	printf( "\t %3d", entry->src_bus_irq );
    printf( "\t %6d", entry->dst_apic_id );
    printf( "\t %3d\n", entry->dst_apic_int );
}


static void
sasEntry( sas_entry_ptr entry )
{

    printf( "--\nSystem Address Space\n");
    printf( " bus ID: %d", entry->bus_id );
    printf( " address type: " );
    switch ( entry->address_type ) {
    case SASENTRY_TYPE_IO:
	printf( "I/O address\n" );
	break;
    case SASENTRY_TYPE_MEMORY:
	printf( "memory address\n" );
	break;
    case SASENTRY_TYPE_PREFETCH:
	printf( "prefetch address\n" );
	break;
    default:
	printf( "UNKNOWN type\n" );
	break;
    }

    printf( " address base: 0x%jx\n", (uintmax_t)entry->address_base );
    printf( " address range: 0x%jx\n", (uintmax_t)entry->address_length );
}


static void
bhdEntry( bhd_entry_ptr entry )
{

    printf( "--\nBus Hierarchy\n" );
    printf( " bus ID: %d", entry->bus_id );
    printf( " bus info: 0x%02x", entry->bus_info );
    printf( " parent bus ID: %d\n", entry->parent_bus );
}


static void
cbasmEntry( cbasm_entry_ptr entry )
{

    printf( "--\nCompatibility Bus Address\n" );
    printf( " bus ID: %d", entry->bus_id );
    printf( " address modifier: %s\n",
	(entry->address_mod & CBASMENTRY_ADDRESS_MOD_SUBTRACT) ?
	"subtract" : "add" );
    printf( " predefined range: 0x%08x\n", entry->predefined_range );
}


/*
 * do a dmesg output
 */
static void
doDmesg( void )
{
    puts( SEP_LINE );

    printf( "dmesg output:\n\n" );
    fflush( stdout );
    system( "dmesg" );
}


/*
 * 
 */
static void
pnstr( char* s, int c )
{
    char string[ MAXPNSTR + 1 ];

    if ( c > MAXPNSTR )
        c = MAXPNSTR;
    strncpy( string, s, c );
    string[ c ] = '\0';
    printf( "%s", string );
}
