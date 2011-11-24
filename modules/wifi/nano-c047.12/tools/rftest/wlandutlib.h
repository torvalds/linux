//
// DO NOT CHANGE THIS FILE
//

#ifndef _WLANDUTLIB_H
#define _WLANDUTLIB_H

#ifdef __cplusplus
extern "C" {
#endif 


typedef enum _DataRate {
    DATA_RATE_UNSUPPORT, //for index 0
    DATA_RATE_1M,   
    DATA_RATE_2M,
    DATA_RATE_5_5M,
    DATA_RATE_6M,   
    DATA_RATE_9M,
    DATA_RATE_11M,
    DATA_RATE_12M,
    DATA_RATE_18M,
    DATA_RATE_22M,
    DATA_RATE_24M,
    DATA_RATE_33M,
    DATA_RATE_36M,
    DATA_RATE_48M,
    DATA_RATE_54M,
    DATA_RATE_MCS0,
    DATA_RATE_MCS1,
    DATA_RATE_MCS2,
    DATA_RATE_MCS3,
    DATA_RATE_MCS4,
    DATA_RATE_MCS5,
    DATA_RATE_MCS6,
    DATA_RATE_MCS7,
} DataRate;


/**
 * Set 'Device under Test(DUT)' mode up
 *
 * @return 0 on success, < 0 on failure.
 */
int OpenDUT(void);  


/**
 * Close DUT. 
 *
 * @return 0 on success, < 0 on failure.
 */
int CloseDUT(void); 


/**
 * SetChannel sets the frequency of channel.
 *
 * @param channel is the channel number which will be set (1 ~ 14).
 *
 * @return 0 on success, < 0 on failure.
 */
int SetChannel(int channel);              


/**
 * SetDataRate sets the data rate of 802.11b,g,n mode.
 *
 * @param rate should be the member of enum 'DataRate'. 
 *
 * @return 0 on success, < 0 on failure.
 */
int SetDataRate(DataRate rate);                


/**
 * SetLongPreamble() determines the type of preamble.
 *
 * @param enable should be 1 if long preamble, 0 if short preamble.
 *
 * @return 0 on success, < 0 on failure.
 */
int SetLongPreamble(int enable);                 


/**
 * SetShortGuardInterval() determines guard interval by 'ns' unit.
 *
 * @param enable should be 1 if 400ns GI, 0 if 800ns GI.
 *
 * @return 0 on success, < 0 on failure.
 */
int SetShortGuardInterval(int enable);


/**
 * TxGain() controls the power level for tx signal.
 *
 * @param txpwr is tx level (dBm unit).
 *
 * @return 0 on success, < 0 on failure.
 */
int TxGain(int txpwr);        


/**
 * SetBurstInterval() sets the burst interval.
 *
 * @param burstinterval
 *
 * @return 0 on success or unsupport, < 0 on failure.
 */
int SetBurstInterval(int burstinterval);   


/**
 * SetPayload() sets the size of payload.
 * For TxStart without this option,
 * 1024 should be set internally as a default value.
 *
 * @param size means payload length(Bytes).
 *
 * @return 0 on success, < 0 on failure.
 */
int SetPayload(int size);                 


/**
 * SetBand() sets the band with one of 2.4GHz and 5GHz.
 * For TxStart or RxStart without this option,
 * 2.4GHz should be set internally as a default value.
 *
 * @param band should be 1 if 2.4GHz (b/g/n), 2 if 5GHz (a/n).
 *
 * @return 0 on success, < 0 on failure.
 */
int SetBand(int band);      


/**
 * SetBandWidth() sets the bandwidth per channel, one of 20MHz and 40MHz
 * For TxStart or RxStart without this option,
 * 20MHz should be set internally as a default value.
 *
 * @param enable should be 1 if 20MHz BW, 2 if 40MHz BW
 *
 * @return 0 on success, < 0 on failure.
 */
int SetBandWidth(int width);      


/**
 * Tx test with modulation begins when TxStartWithMod() is called.
 *
 * @return 0 on success, < 0 on failure.
 */
int TxStartWithMod();                     


/**
 * Tx test without modulation begins when TxStartWoMod() is called.
 *
 * @return 0 on success, < 0 on failure.
 */
int TxStartWoMod();         


/**
 * TxStop() makes Tx test stop.
 *
 * @return 0 on success, < 0 on failure.
 */
int TxStop();     


/**
 * Rx test begins when RxStart() is called. 
 *
 * @return 0 on success, < 0 on failure.
 */
int RxStart();                        


/**
 * RxStop() makes Rx test stop.
 *
 * @return 0 on success, < 0 on failure.
 */
int RxStop();                    


/**
 * GoodFrame() gets the number of good frames after the Rx test stopped.
 * Good frames include some error frames which can be fixed.
 *
 * @return : the number of good frames.
 */
int GetGoodFrame();     


/**
 * ErrorFrame() gets the number of error frames after the Rx test stopped.
 *
 * @return : the number of error frames.
 */
int GetErrorFrame();      


/**
 * GetErrorString gets the detailed error result when the above methods return failure.
 */
const char *GetErrorString();



#ifdef __cplusplus
} /* extern "C" */
#endif 

#endif 
