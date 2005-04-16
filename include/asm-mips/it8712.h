
#ifndef __IT8712_H__
#define __IT8712_H__

#define LPC_BASE_ADDR	0x14000000

// MB PnP configuration register
#define LPC_KEY_ADDR	0x1400002E
#define LPC_DATA_ADDR	0x1400002F

// Device LDN
#define LDN_SERIAL1	0x01
#define LDN_SERIAL2	0x02
#define LDN_PARALLEL	0x03
#define LDN_KEYBOARD	0x05
#define LDN_MOUSE	0x06

#define IT8712_UART1_PORT      0x3F8
#define IT8712_UART2_PORT      0x2F8

#ifndef ASM_ONLY

void LPCSetConfig(char LdnNumber, char Index, char data);
char LPCGetConfig(char LdnNumber, char Index);

#endif

#endif
