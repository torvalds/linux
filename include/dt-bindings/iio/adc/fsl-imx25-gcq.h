/*
 * This header provides constants for configuring the I.MX25 ADC
 */

#ifndef _DT_BINDINGS_IIO_ADC_FS_IMX25_GCQ_H
#define _DT_BINDINGS_IIO_ADC_FS_IMX25_GCQ_H

#define MX25_ADC_REFP_YP	0 /* YP voltage reference */
#define MX25_ADC_REFP_XP	1 /* XP voltage reference */
#define MX25_ADC_REFP_EXT	2 /* External voltage reference */
#define MX25_ADC_REFP_INT	3 /* Internal voltage reference */

#define MX25_ADC_REFN_XN	0 /* XN ground reference */
#define MX25_ADC_REFN_YN	1 /* YN ground reference */
#define MX25_ADC_REFN_NGND	2 /* Internal ground reference */
#define MX25_ADC_REFN_NGND2	3 /* External ground reference */

#endif
