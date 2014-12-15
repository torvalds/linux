#ifndef  ERR_CODE_H_
#define ERR_CODE_H_

#define   TRUE		1
#define	FALSE		0
#define	SUCCESS	0
#define	FAIL		1


#define	LOGO_PARA_UNPARSED	0x30001
//parser

#define    ENOPARSER				0x10001	
#define    PARSER_FOUND			0x10002
#define    PARSER_UNFOUND		0x10003	
#define 	PARSER_DECODE_FAIL	0x10004

//dev
#define  	EDEV_NO_TRANSFER_NEED 	0x20001
#define    OUTPUT_DEV_FOUND			0x20002
#define 	OUTPUT_DEV_UNFOUND	 	0x20003
#define	OUTPUT_DEV_SETUP_FAIL		0x20004
#define	OUTPUT_DEV_GE2D_SETUP_FAIL	0x20005
#endif

