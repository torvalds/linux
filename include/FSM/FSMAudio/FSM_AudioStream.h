/*!
\file
\brief FSM Аудиопотоки
\authors Гусенков.С.В
\version 0.0.1_rc1
\date 30.12.2015
*/
#ifndef FCM_AUDIO_STREAM_H
#define FCM_AUDIO_STREAM_H
typedef void (*FSM_StreamProcessUser)(char*, short, int id);
typedef void (*FSM_StreamProcessProcess)(char*, short);
typedef void (*FSM_StreamProcessSend)(unsigned short, char*, short);
typedef unsigned int (*FSM_ADSendEthPack)(void* data, int len, int id);

struct FSM_AudioStream
{
    char reg;
    unsigned short iddev;
    FSM_StreamProcessUser ToUser;
    FSM_StreamProcessProcess ToProcess;
    int TransportDevice;
    char TransportDeviceType;
    unsigned short IDConnection;
    char typcon;
    void* Data;
};

struct FSM_FIFOAS
{
    char reg;
    unsigned short streamid;
    char outBuffer[160];
    char inBuffer[1024];
    unsigned short in_readptr;
    unsigned short in_writeptr;
    unsigned short in_count;
    unsigned short out_count;
};

int FSM_AudioStreamRegistr(struct FSM_AudioStream fsmas);
void FSM_AudioStreamUnRegistr(int id);
void FSM_AudioStreamToUser(int id, char* Data, short len);
void FSM_AudioStreamToProcess(int id, char* Data, short len);
void* FSM_AudioStreamData(int id);
unsigned short FSM_AudioStreamGETIDConnect(int id);
void FSM_AudioStreamSetIDConnect(int id, unsigned short idcon, char type);
char FSM_AudioStreamGETTypeConnect(int id);
int FSM_AudioStreamGetFIFODevice(int id);
void FSM_FIFOAudioStreamTobuffer(char* Data, short len, int id);
int FSM_FIFOAudioStreamRegistr(struct FSM_AudioStream fsmas, unsigned short* idfifo);
void FSM_FIFOAudioStreamWrite(char* Data, short len, unsigned short idfifo);
int FSM_FIFOAudioStreamRead(char* Data, unsigned short count, unsigned short idfifo);
void FSM_AudioStreamSetToProcess(int id, FSM_StreamProcessProcess fsmtu);
unsigned short FSM_FIFOAudioStreamGetAS(unsigned short idfifo);
void FSM_AudioStreamSetFIFODevice(int id, int edev);

#endif