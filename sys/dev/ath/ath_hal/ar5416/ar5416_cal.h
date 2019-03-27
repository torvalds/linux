/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2008 Atheros Communications, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $FreeBSD$
 */
#ifndef _ATH_AR5416_CAL_H_
#define _ATH_AR5416_CAL_H_
 
typedef enum {
	ADC_DC_INIT_CAL	= 0x1,
	ADC_GAIN_CAL	= 0x2,
	ADC_DC_CAL	= 0x4,
	IQ_MISMATCH_CAL	= 0x8
} HAL_CAL_TYPE;

/* Calibrate state */
typedef enum {
	CAL_INACTIVE,
	CAL_WAITING,
	CAL_RUNNING,
	CAL_DONE
} HAL_CAL_STATE;

typedef union {
	uint32_t	u;
	int32_t		s;
} HAL_CAL_SAMPLE;

#define	MIN_CAL_SAMPLES     1
#define	MAX_CAL_SAMPLES    64
#define	INIT_LOG_COUNT      5
#define	PER_MIN_LOG_COUNT   2
#define	PER_MAX_LOG_COUNT  10

/* Per Calibration data structure */
typedef struct per_cal_data {
	const char	*calName;		/* for diagnostics */
	HAL_CAL_TYPE	calType;		/* Type of calibration */
	uint32_t	calNumSamples;		/* # SW samples to collect */
	uint32_t	calCountMax;		/* # HW samples to collect */
	void (*calCollect)(struct ath_hal *);	/* Accumulator function */
						/* Post-processing function */
	void (*calPostProc)(struct ath_hal *, uint8_t);
} HAL_PERCAL_DATA;

/* List structure for calibration data */
typedef struct cal_list {
	struct cal_list		*calNext;
	HAL_CAL_STATE		calState;
	const HAL_PERCAL_DATA	*calData;
} HAL_CAL_LIST;

struct ar5416PerCal {
	/*
	 * Periodic calibration state.
	 */
	HAL_CAL_TYPE	suppCals;
	HAL_CAL_LIST	iqCalData;
	HAL_CAL_LIST	adcGainCalData;
	HAL_CAL_LIST	adcDcCalInitData;
	HAL_CAL_LIST	adcDcCalData;
	HAL_CAL_LIST	*cal_list;
	HAL_CAL_LIST	*cal_last;
	HAL_CAL_LIST	*cal_curr;
#define AR5416_MAX_CHAINS            	3	/* XXX dup's eeprom def */
	HAL_CAL_SAMPLE	caldata[4][AR5416_MAX_CHAINS];
	int		calSamples;
	/*
	 * Noise floor cal histogram support.
	 * XXX be nice to re-use space in ar5212
	 */
#define	AR5416_NUM_NF_READINGS		6	/* (3 chains * (ctl + ext) */
	struct ar5212NfCalHist nfCalHist[AR5416_NUM_NF_READINGS];
};

#define INIT_CAL(_perCal) do {						\
	(_perCal)->calState = CAL_WAITING;				\
	(_perCal)->calNext = AH_NULL;					\
} while (0)

#define INSERT_CAL(_cal, _perCal) do {					\
	if ((_cal)->cal_last == AH_NULL) {				\
		(_cal)->cal_list = (_cal)->cal_last = (_perCal);	\
		((_cal)->cal_last)->calNext = (_perCal);		\
	} else {							\
		((_cal)->cal_last)->calNext = (_perCal);		\
		(_cal)->cal_last = (_perCal);				\
		(_perCal)->calNext = (_cal)->cal_list;			\
	}								\
} while (0)

HAL_BOOL	ar5416InitCalHardware(struct ath_hal *ah, const struct ieee80211_channel *chan);
HAL_BOOL ar5416InitCal(struct ath_hal *, const struct ieee80211_channel *);
HAL_BOOL ar5416PerCalibration(struct ath_hal *,  struct ieee80211_channel *,
	    HAL_BOOL *isIQdone);
HAL_BOOL ar5416PerCalibrationN(struct ath_hal *, struct ieee80211_channel *,
	    u_int chainMask, HAL_BOOL longCal, HAL_BOOL *isCalDone);
HAL_BOOL ar5416ResetCalValid(struct ath_hal *,
	    const struct ieee80211_channel *);

void	ar5416IQCalCollect(struct ath_hal *ah);
void	ar5416IQCalibration(struct ath_hal *ah, uint8_t numChains);
void	ar5416AdcGainCalCollect(struct ath_hal *ah);
void	ar5416AdcGainCalibration(struct ath_hal *ah, uint8_t numChains);
void	ar5416AdcDcCalCollect(struct ath_hal *ah);
void	ar5416AdcDcCalibration(struct ath_hal *ah, uint8_t numChains);
void	ar5416InitNfHistBuff(struct ar5212NfCalHist *h);
#endif /* _ATH_AR5416_CAL_H_ */
