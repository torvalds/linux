/*
 *  arch/s390/kernel/cpcmd.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Christian Borntraeger (cborntra@de.ibm.com),
 */

#ifndef __CPCMD__
#define __CPCMD__

/*
 * the caller of __cpcmd has to ensure that the response buffer is below 2 GB
 */
extern void __cpcmd(char *cmd, char *response, int rlen);

#ifndef __s390x__
#define cpcmd __cpcmd
#else
extern void cpcmd(char *cmd, char *response, int rlen);
#endif /*__s390x__*/

#endif
