/*
 * device_id.h -- PCMCIA driver matching helpers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * (C) 2003 - 2004	David Woodhouse
 * (C) 2003 - 2004	Dominik Brodowski
 */

#ifndef _LINUX_PCMCIA_DEVICE_ID_H
#define _LINUX_PCMCIA_DEVICE_ID_H

#ifdef __KERNEL__

#define PCMCIA_DEVICE_MANF_CARD(manf, card) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_MANF_ID| \
			PCMCIA_DEV_ID_MATCH_CARD_ID, \
	.manf_id = (manf), \
	.card_id = (card), }

#define PCMCIA_DEVICE_FUNC_ID(func) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_FUNC_ID, \
	.func_id = (func), }

#define PCMCIA_DEVICE_PROD_ID1(v1, vh1) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_PROD_ID1, \
	.prod_id = { (v1), NULL, NULL, NULL }, \
	.prod_id_hash = { (vh1), 0, 0, 0 }, }

#define PCMCIA_DEVICE_PROD_ID2(v2, vh2) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_PROD_ID2, \
	.prod_id = { NULL, (v2), NULL, NULL },  \
	.prod_id_hash = { 0, (vh2), 0, 0 }, }

#define PCMCIA_DEVICE_PROD_ID12(v1, v2, vh1, vh2) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_PROD_ID1| \
			PCMCIA_DEV_ID_MATCH_PROD_ID2, \
	.prod_id = { (v1), (v2), NULL, NULL }, \
	.prod_id_hash = { (vh1), (vh2), 0, 0 }, }

#define PCMCIA_DEVICE_PROD_ID13(v1, v3, vh1, vh3) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_PROD_ID1| \
			PCMCIA_DEV_ID_MATCH_PROD_ID3, \
	.prod_id = { (v1), NULL, (v3), NULL }, \
	.prod_id_hash = { (vh1), 0, (vh3), 0 }, }

#define PCMCIA_DEVICE_PROD_ID14(v1, v4, vh1, vh4) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_PROD_ID1| \
			PCMCIA_DEV_ID_MATCH_PROD_ID4, \
	.prod_id = { (v1), NULL, NULL, (v4) }, \
	.prod_id_hash = { (vh1), 0, 0, (vh4) }, }

#define PCMCIA_DEVICE_PROD_ID123(v1, v2, v3, vh1, vh2, vh3) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_PROD_ID1| \
			PCMCIA_DEV_ID_MATCH_PROD_ID2| \
			PCMCIA_DEV_ID_MATCH_PROD_ID3, \
	.prod_id = { (v1), (v2), (v3), NULL },\
	.prod_id_hash = { (vh1), (vh2), (vh3), 0 }, }

#define PCMCIA_DEVICE_PROD_ID124(v1, v2, v4, vh1, vh2, vh4) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_PROD_ID1| \
			PCMCIA_DEV_ID_MATCH_PROD_ID2| \
			PCMCIA_DEV_ID_MATCH_PROD_ID4, \
	.prod_id = { (v1), (v2), NULL, (v4) }, \
	.prod_id_hash = { (vh1), (vh2), 0, (vh4) }, }

#define PCMCIA_DEVICE_PROD_ID134(v1, v3, v4, vh1, vh3, vh4) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_PROD_ID1| \
			PCMCIA_DEV_ID_MATCH_PROD_ID3| \
			PCMCIA_DEV_ID_MATCH_PROD_ID4, \
	.prod_id = { (v1), NULL, (v3), (v4) }, \
	.prod_id_hash = { (vh1), 0, (vh3), (vh4) }, }

#define PCMCIA_DEVICE_PROD_ID1234(v1, v2, v3, v4, vh1, vh2, vh3, vh4) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_PROD_ID1| \
			PCMCIA_DEV_ID_MATCH_PROD_ID2| \
			PCMCIA_DEV_ID_MATCH_PROD_ID3| \
			PCMCIA_DEV_ID_MATCH_PROD_ID4, \
	.prod_id = { (v1), (v2), (v3), (v4) }, \
	.prod_id_hash = { (vh1), (vh2), (vh3), (vh4) }, }

#define PCMCIA_DEVICE_MANF_CARD_PROD_ID1(manf, card, v1, vh1) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_MANF_ID| \
			PCMCIA_DEV_ID_MATCH_CARD_ID| \
			PCMCIA_DEV_ID_MATCH_PROD_ID1, \
	.manf_id = (manf), \
	.card_id = (card), \
	.prod_id = { (v1), NULL, NULL, NULL }, \
	.prod_id_hash = { (vh1), 0, 0, 0 }, }


/* multi-function devices */

#define PCMCIA_MFC_DEVICE_MANF_CARD(mfc, manf, card) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_MANF_ID| \
			PCMCIA_DEV_ID_MATCH_CARD_ID| \
			PCMCIA_DEV_ID_MATCH_FUNCTION, \
	.manf_id = (manf), \
	.card_id = (card), \
	.function = (mfc), }

#define PCMCIA_MFC_DEVICE_PROD_ID1(mfc, v1, vh1) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_PROD_ID1| \
			PCMCIA_DEV_ID_MATCH_FUNCTION, \
	.prod_id = { (v1), NULL, NULL, NULL }, \
	.prod_id_hash = { (vh1), 0, 0, 0 }, \
	.function = (mfc), }

#define PCMCIA_MFC_DEVICE_PROD_ID2(mfc, v2, vh2) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_PROD_ID2| \
			PCMCIA_DEV_ID_MATCH_FUNCTION, \
	.prod_id = { NULL, (v2), NULL, NULL },  \
	.prod_id_hash = { 0, (vh2), 0, 0 }, \
	.function = (mfc), }

#define PCMCIA_MFC_DEVICE_PROD_ID12(mfc, v1, v2, vh1, vh2) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_PROD_ID1| \
			PCMCIA_DEV_ID_MATCH_PROD_ID2| \
			PCMCIA_DEV_ID_MATCH_FUNCTION, \
	.prod_id = { (v1), (v2), NULL, NULL }, \
	.prod_id_hash = { (vh1), (vh2), 0, 0 }, \
	.function = (mfc), }

#define PCMCIA_MFC_DEVICE_PROD_ID13(mfc, v1, v3, vh1, vh3) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_PROD_ID1| \
			PCMCIA_DEV_ID_MATCH_PROD_ID3| \
			PCMCIA_DEV_ID_MATCH_FUNCTION, \
	.prod_id = { (v1), NULL, (v3), NULL }, \
	.prod_id_hash = { (vh1), 0, (vh3), 0 }, \
	.function = (mfc), }

#define PCMCIA_MFC_DEVICE_PROD_ID123(mfc, v1, v2, v3, vh1, vh2, vh3) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_PROD_ID1| \
			PCMCIA_DEV_ID_MATCH_PROD_ID2| \
			PCMCIA_DEV_ID_MATCH_PROD_ID3| \
			PCMCIA_DEV_ID_MATCH_FUNCTION, \
	.prod_id = { (v1), (v2), (v3), NULL },\
	.prod_id_hash = { (vh1), (vh2), (vh3), 0 }, \
	.function = (mfc), }

/* pseudo multi-function devices */

#define PCMCIA_PFC_DEVICE_MANF_CARD(mfc, manf, card) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_MANF_ID| \
			PCMCIA_DEV_ID_MATCH_CARD_ID| \
			PCMCIA_DEV_ID_MATCH_DEVICE_NO, \
	.manf_id = (manf), \
	.card_id = (card), \
	.device_no = (mfc), }

#define PCMCIA_PFC_DEVICE_PROD_ID1(mfc, v1, vh1) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_PROD_ID1| \
			PCMCIA_DEV_ID_MATCH_DEVICE_NO, \
	.prod_id = { (v1), NULL, NULL, NULL }, \
	.prod_id_hash = { (vh1), 0, 0, 0 }, \
	.device_no = (mfc), }

#define PCMCIA_PFC_DEVICE_PROD_ID2(mfc, v2, vh2) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_PROD_ID2| \
			PCMCIA_DEV_ID_MATCH_DEVICE_NO, \
	.prod_id = { NULL, (v2), NULL, NULL },  \
	.prod_id_hash = { 0, (vh2), 0, 0 }, \
	.device_no = (mfc), }

#define PCMCIA_PFC_DEVICE_PROD_ID12(mfc, v1, v2, vh1, vh2) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_PROD_ID1| \
			PCMCIA_DEV_ID_MATCH_PROD_ID2| \
			PCMCIA_DEV_ID_MATCH_DEVICE_NO, \
	.prod_id = { (v1), (v2), NULL, NULL }, \
	.prod_id_hash = { (vh1), (vh2), 0, 0 }, \
	.device_no = (mfc), }

#define PCMCIA_PFC_DEVICE_PROD_ID13(mfc, v1, v3, vh1, vh3) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_PROD_ID1| \
			PCMCIA_DEV_ID_MATCH_PROD_ID3| \
			PCMCIA_DEV_ID_MATCH_DEVICE_NO, \
	.prod_id = { (v1), NULL, (v3), NULL }, \
	.prod_id_hash = { (vh1), 0, (vh3), 0 }, \
	.device_no = (mfc), }

#define PCMCIA_PFC_DEVICE_PROD_ID123(mfc, v1, v2, v3, vh1, vh2, vh3) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_PROD_ID1| \
			PCMCIA_DEV_ID_MATCH_PROD_ID2| \
			PCMCIA_DEV_ID_MATCH_PROD_ID3| \
			PCMCIA_DEV_ID_MATCH_DEVICE_NO, \
	.prod_id = { (v1), (v2), (v3), NULL },\
	.prod_id_hash = { (vh1), (vh2), (vh3), 0 }, \
	.device_no = (mfc), }

/* cards needing a CIS override */

#define PCMCIA_DEVICE_CIS_MANF_CARD(manf, card, _cisfile) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_FAKE_CIS | \
			PCMCIA_DEV_ID_MATCH_MANF_ID| \
			PCMCIA_DEV_ID_MATCH_CARD_ID, \
	.manf_id = (manf), \
	.card_id = (card), \
	.cisfile = (_cisfile)}

#define PCMCIA_DEVICE_CIS_PROD_ID12(v1, v2, vh1, vh2, _cisfile) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_FAKE_CIS | \
			PCMCIA_DEV_ID_MATCH_PROD_ID1| \
			PCMCIA_DEV_ID_MATCH_PROD_ID2, \
	.prod_id = { (v1), (v2), NULL, NULL }, \
	.prod_id_hash = { (vh1), (vh2), 0, 0 }, \
	.cisfile = (_cisfile)}

#define PCMCIA_DEVICE_CIS_PROD_ID123(v1, v2, v3, vh1, vh2, vh3, _cisfile) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_FAKE_CIS | \
			PCMCIA_DEV_ID_MATCH_PROD_ID1| \
			PCMCIA_DEV_ID_MATCH_PROD_ID2| \
			PCMCIA_DEV_ID_MATCH_PROD_ID3, \
	.prod_id = { (v1), (v2), (v3), NULL },\
	.prod_id_hash = { (vh1), (vh2), (vh3), 0 }, \
	.cisfile = (_cisfile)}


#define PCMCIA_DEVICE_CIS_PROD_ID2(v2, vh2, _cisfile) { \
	.match_flags =  PCMCIA_DEV_ID_MATCH_FAKE_CIS | \
			PCMCIA_DEV_ID_MATCH_PROD_ID2, \
	.prod_id = { NULL, (v2), NULL, NULL },  \
	.prod_id_hash = { 0, (vh2), 0, 0 }, \
	.cisfile = (_cisfile)}

#define PCMCIA_PFC_DEVICE_CIS_PROD_ID12(mfc, v1, v2, vh1, vh2, _cisfile) { \
	.match_flags =  PCMCIA_DEV_ID_MATCH_FAKE_CIS | \
			PCMCIA_DEV_ID_MATCH_PROD_ID1| \
			PCMCIA_DEV_ID_MATCH_PROD_ID2| \
			PCMCIA_DEV_ID_MATCH_DEVICE_NO, \
	.prod_id = { (v1), (v2), NULL, NULL }, \
	.prod_id_hash = { (vh1), (vh2), 0, 0 },\
	.device_no = (mfc), \
	.cisfile = (_cisfile)}

#define PCMCIA_MFC_DEVICE_CIS_MANF_CARD(mfc, manf, card, _cisfile) { \
	.match_flags =  PCMCIA_DEV_ID_MATCH_FAKE_CIS | \
			PCMCIA_DEV_ID_MATCH_MANF_ID| \
			PCMCIA_DEV_ID_MATCH_CARD_ID| \
			PCMCIA_DEV_ID_MATCH_FUNCTION, \
	.manf_id = (manf), \
	.card_id = (card), \
	.function = (mfc), \
	.cisfile = (_cisfile)}

#define PCMCIA_MFC_DEVICE_CIS_PROD_ID12(mfc, v1, v2, vh1, vh2, _cisfile) { \
	.match_flags =  PCMCIA_DEV_ID_MATCH_FAKE_CIS | \
			PCMCIA_DEV_ID_MATCH_PROD_ID1| \
			PCMCIA_DEV_ID_MATCH_PROD_ID2| \
			PCMCIA_DEV_ID_MATCH_FUNCTION, \
	.prod_id = { (v1), (v2), NULL, NULL }, \
	.prod_id_hash = { (vh1), (vh2), 0, 0 }, \
	.function = (mfc), \
	.cisfile = (_cisfile)}

#define PCMCIA_MFC_DEVICE_CIS_PROD_ID4(mfc, v4, vh4, _cisfile) { \
	.match_flags =  PCMCIA_DEV_ID_MATCH_FAKE_CIS | \
			PCMCIA_DEV_ID_MATCH_PROD_ID4| \
			PCMCIA_DEV_ID_MATCH_FUNCTION, \
	.prod_id = { NULL, NULL, NULL, (v4) }, \
	.prod_id_hash = { 0, 0, 0, (vh4) }, \
	.function = (mfc), \
	.cisfile = (_cisfile)}


#define PCMCIA_DEVICE_NULL { .match_flags = 0, }

#endif /* __KERNEL__ */
#endif /* _LINUX_PCMCIA_DEVICE_ID_H */
