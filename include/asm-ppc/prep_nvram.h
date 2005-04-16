/*
 * PreP compliant NVRAM access
 */

/* Corey Minyard (minyard@acm.org) - Stolen from PReP book.   Per the
   license I must say:
     (C) Copyright (Corey Minyard), (1998).  All rights reserved
 */

/* Structure map for NVRAM on PowerPC Reference Platform */
/* All fields are either character/byte strings which are valid either
  endian or they are big-endian numbers.

  There are a number of Date and Time fields which are in RTC format,
  big-endian. These are stored in UT (GMT).

  For enum's: if given in hex then they are bit significant, i.e. only
  one bit is on for each enum.
*/
#ifdef __KERNEL__
#ifndef _PPC_PREP_NVRAM_H
#define _PPC_PREP_NVRAM_H

#define MAX_PREP_NVRAM 0x8000
#define PREP_NVRAM_AS0	0x74
#define PREP_NVRAM_AS1	0x75
#define PREP_NVRAM_DATA	0x77

#define NVSIZE 4096	/* size of NVRAM */
#define OSAREASIZE 512	/* size of OSArea space */
#define CONFSIZE 1024	/* guess at size of Configuration space */

typedef struct _SECURITY {
  unsigned long BootErrCnt;	    /* Count of boot password errors */
  unsigned long ConfigErrCnt;	    /* Count of config password errors */
  unsigned long BootErrorDT[2];	    /* Date&Time from RTC of last error in pw */
  unsigned long ConfigErrorDT[2];   /* Date&Time from RTC of last error in pw */
  unsigned long BootCorrectDT[2];   /* Date&Time from RTC of last correct pw */
  unsigned long ConfigCorrectDT[2]; /* Date&Time from RTC of last correct pw */
  unsigned long BootSetDT[2];	    /* Date&Time from RTC of last set of pw */
  unsigned long ConfigSetDT[2];	    /* Date&Time from RTC of last set of pw */
  unsigned char Serial[16];	    /* Box serial number */
} SECURITY;

typedef enum _OS_ID {
  Unknown = 0,
  Firmware = 1,
  AIX = 2,
  NT = 3,
  MKOS2 = 4,
  MKAIX = 5,
  Taligent = 6,
  Solaris = 7,
  MK = 12
} OS_ID;

typedef struct _ERROR_LOG {
  unsigned char ErrorLogEntry[40]; /* To be architected */
} ERROR_LOG;

typedef enum _BOOT_STATUS {
  BootStarted = 0x01,
  BootFinished = 0x02,
  RestartStarted = 0x04,
  RestartFinished = 0x08,
  PowerFailStarted = 0x10,
  PowerFailFinished = 0x20,
  ProcessorReady = 0x40,
  ProcessorRunning = 0x80,
  ProcessorStart = 0x0100
} BOOT_STATUS;

typedef struct _RESTART_BLOCK {
  unsigned short Version;
  unsigned short Revision;
  unsigned long ResumeReserve1[2];
  volatile unsigned long BootStatus;
  unsigned long CheckSum; /* Checksum of RESTART_BLOCK */
  void * RestartAddress;
  void * SaveAreaAddr;
  unsigned long SaveAreaLength;
} RESTART_BLOCK;

typedef enum _OSAREA_USAGE {
  Empty = 0,
  Used = 1
} OSAREA_USAGE;

typedef enum _PM_MODE {
  Suspend = 0x80, /* Part of state is in memory */
  Normal = 0x00   /* No power management in effect */
} PMMODE;

typedef struct _HEADER {
  unsigned short Size;       /* NVRAM size in K(1024) */
  unsigned char Version;     /* Structure map different */
  unsigned char Revision;    /* Structure map the same -may
                                be new values in old fields
                                in other words old code still works */
  unsigned short Crc1;       /* check sum from beginning of nvram to OSArea */
  unsigned short Crc2;       /* check sum of config */
  unsigned char LastOS;      /* OS_ID */
  unsigned char Endian;      /* B if big endian, L if little endian */
  unsigned char OSAreaUsage; /* OSAREA_USAGE */
  unsigned char PMMode;      /* Shutdown mode */
  RESTART_BLOCK RestartBlock;
  SECURITY Security;
  ERROR_LOG ErrorLog[2];

  /* Global Environment information */
  void * GEAddress;
  unsigned long GELength;

  /* Date&Time from RTC of last change to Global Environment */
  unsigned long GELastWriteDT[2];

  /* Configuration information */
  void * ConfigAddress;
  unsigned long ConfigLength;

  /* Date&Time from RTC of last change to Configuration */
  unsigned long ConfigLastWriteDT[2];
  unsigned long ConfigCount; /* Count of entries in Configuration */

  /* OS dependent temp area */
  void * OSAreaAddress;
  unsigned long OSAreaLength;

  /* Date&Time from RTC of last change to OSAreaArea */
  unsigned long OSAreaLastWriteDT[2];
} HEADER;

/* Here is the whole map of the NVRAM */
typedef struct _NVRAM_MAP {
  HEADER Header;
  unsigned char GEArea[NVSIZE-CONFSIZE-OSAREASIZE-sizeof(HEADER)];
  unsigned char OSArea[OSAREASIZE];
  unsigned char ConfigArea[CONFSIZE];
} NVRAM_MAP;

/* Routines to manipulate the NVRAM */
void init_prep_nvram(void);
char *prep_nvram_get_var(const char *name);
char *prep_nvram_first_var(void);
char *prep_nvram_next_var(char *name);

/* Routines to read and write directly to the NVRAM */
unsigned char prep_nvram_read_val(int addr);
void prep_nvram_write_val(int           addr,
			  unsigned char val);

#endif /* _PPC_PREP_NVRAM_H */
#endif /* __KERNEL__ */
