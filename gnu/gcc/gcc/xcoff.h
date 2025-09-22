/* Storage classes in XCOFF object file format designed for DBX's use.
   This info is from the `Files Reference' manual for IBM's AIX version 3
   for the RS6000.  */

#define C_GSYM		0x80
#define C_LSYM		0x81
#define C_PSYM		0x82
#define C_RSYM		0x83
#define C_RPSYM		0x84
#define C_STSYM		0x85

#define C_BCOMM		0x87
#define C_ECOML		0x88
#define C_ECOMM		0x89
#define C_DECL		0x8c
#define C_ENTRY		0x8d
#define C_FUN		0x8e
