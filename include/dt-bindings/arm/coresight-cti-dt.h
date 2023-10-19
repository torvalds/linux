/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This header provides constants for the defined trigger signal
 * types on CoreSight CTI.
 */

#ifndef _DT_BINDINGS_ARM_CORESIGHT_CTI_DT_H
#define _DT_BINDINGS_ARM_CORESIGHT_CTI_DT_H

#define GEN_IO		0
#define GEN_INTREQ	1
#define GEN_INTACK	2
#define GEN_HALTREQ	3
#define GEN_RESTARTREQ	4
#define PE_EDBGREQ	5
#define PE_DBGRESTART	6
#define PE_CTIIRQ	7
#define PE_PMUIRQ	8
#define PE_DBGTRIGGER	9
#define ETM_EXTOUT	10
#define ETM_EXTIN	11
#define SNK_FULL	12
#define SNK_ACQCOMP	13
#define SNK_FLUSHCOMP	14
#define SNK_FLUSHIN	15
#define SNK_TRIGIN	16
#define STM_ASYNCOUT	17
#define STM_TOUT_SPTE	18
#define STM_TOUT_SW	19
#define STM_TOUT_HETE	20
#define STM_HWEVENT	21
#define ELA_TSTART	22
#define ELA_TSTOP	23
#define ELA_DBGREQ	24
#define CTI_TRIG_MAX	25

#endif /*_DT_BINDINGS_ARM_CORESIGHT_CTI_DT_H */
