/* sms4.h */
#define ENCRYPT  0
#define DECRYPT  1
void SMS4Crypt(unsigned char *Input, unsigned char *Output, unsigned int *rk);
void SMS4KeyExt(unsigned char *Key, unsigned int *rk, unsigned int CryptFlag);
