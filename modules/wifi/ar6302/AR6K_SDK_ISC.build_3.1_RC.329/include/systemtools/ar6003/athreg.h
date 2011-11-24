
#ifndef	__INCathregh
#define	__INCathregh

#define _ASSERT_STR(z) _ASSERT_TMP(z)
#define _ASSERT_TMP(z) #z


#ifndef LINUX
//#if defined(__STDC__) || defined(__cplusplus)
#ifndef AR6K
extern void __assert (const char *msg);
#else
#define __assert A_ASSERT
#endif
//#else
//extern void __assert ();
//#endif
#endif

#ifdef BUILD_AP
extern void apAssFail(const char *str);
#define art_assert(test) ((void) \
		      ((test) ? ((void) 0) : \
		       apAssFail("Assertion failed: "#test", file " 	\
                                __FILE__ ", line "_ASSERT_STR(__LINE__)"\n")))
#else
#define art_assert(test) ((void) 0)
/*
//#define art_assert(test) ((void) \
//		      ((test) ? ((void) 0) : \
//		       __assert("Assertion failed: "#test", file " 	\
//                                __FILE__ ", line "_ASSERT_STR(__LINE__)"\n")))
*/
#endif


#define MAX_FILE_WIDTH  250     //max number of characters we will reading from file
#define MAX_NAME_LENGTH  70     //max number of character a register or field name can have
#define MAX_VALUE_LENGTH 30
#define MAX_EAR_LOCATIONS	1024
#define MAX_PCI_ENTRIES	1024
#define AR6000_MAX_PCI_ENTRIES_PER_CMD	256

#if defined(AR6K) || defined(AR6002)
#define MAX_PCI_ENTRIES_PER_CMD	AR6000_MAX_PCI_ENTRIES_PER_CMD
#else
#define MAX_PCI_ENTRIES_PER_CMD	MAX_PCI_ENTRIES
#endif
#define PHOENIX_MAX_PCI_ENTRIES_PER_CMD 30

#define PHOENIX_SUBSYSTEM_ID_OFFSET  0x0
#define PHOENIX_SUBSYSTEM_ID_S  0x8
#define PHOENIX_SUBSYSTEM_ID_M  0xff00

//definitions for the text file columns
#define FIELD_NAME      1
#define BASE_VALUE      2
#define TURBO_VALUE     3
#define REG_NAME        4
#define REG_OFFSET      5
#define BIT_RANGE       6
#define VALUE_SIGNED   7
#define REG_WRITABLE    8
#define REG_READABLE    9
#define SW_CONTROLLED   10
#define EEPROM_VALUE    11
#define PUBLIC_NAME     12
// #define NUM_RF_BANKS	9
// #define ALL_BANKS		0x1ff         // Added ADDAC BANK
#define NUM_RF_BANKS	8
#define ALL_BANKS		0xff

#define BASE_11A_VALUE	2
#define TURBO_11A_VALUE	3
#define BASE_11B_VALUE	4
#define BASE_11G_VALUE	5
#define TURBO_11G_VALUE 6

#define HOFFMAN_DONOT_WRITE 0
#define HOFFMAN_MODAL       1
#define HOFFMAN_COMMON      2
#define HOFFMAN_BANK0       3
#define HOFFMAN_BB_GAIN     4
#define HOFFMAN_BANK1       5
#define HOFFMAN_BANK2       6
#define HOFFMAN_BANK3       7
#define HOFFMAN_BANK6       8
#define HOFFMAN_BANK7       9

//atheros register file struct used by parser, contains some extra fields 
//to help manipulation of members during parsing
typedef struct parseAtherosRegFile {
    A_CHAR      fieldName[MAX_NAME_LENGTH];
    A_CHAR      regName[MAX_NAME_LENGTH];
    A_UINT32    fieldBaseValue;
    A_UINT32    fieldTurboValue;
    A_UINT32    regOffset;
    A_UINT16    fieldStartBitPos;
    A_UINT16    fieldSize;
    A_BOOL      writable;
    A_BOOL      readable;
    A_UINT32    softwareControlled;
    A_BOOL      existsInEepromOrMode;
    A_BOOL      publicText;
    A_BOOL      radioRegister;
	A_BOOL		valueSigned;
    A_UCHAR     rfRegNumber;
    A_UINT32    maxValue;	//also acts as the mask
    A_UINT32    indexToModeSection;   
	A_BOOL		existsInModeSection;
	A_BOOL		dontReverseField;
} PARSE_ATHEROS_REG_FILE;

//structure used to create and read in dk ini file
typedef struct atherosRegFile {
    A_CHAR      fieldName[MAX_NAME_LENGTH];
    A_CHAR      regName[MAX_NAME_LENGTH];
    A_UINT32    fieldBaseValue;
    A_UINT32    fieldTurboValue;
    A_UINT32    regOffset;
    A_UINT16    fieldStartBitPos;
    A_UINT16    fieldSize;
    A_BOOL      writable;
    A_BOOL      readable;
    A_UINT32    softwareControlled;
    A_BOOL      existsInEepromOrMode;
    A_BOOL      publicText;
    A_BOOL      radioRegister;
	A_BOOL		valueSigned;
    A_UCHAR     rfRegNumber;
    A_UINT32    maxValue;   //also acts as the mask
	A_BOOL		dontReverse;
} ATHEROS_REG_FILE;

typedef struct parseFieldInfo {
    A_CHAR      fieldName[MAX_NAME_LENGTH];
	A_CHAR		valueString[MAX_VALUE_LENGTH];
} PARSE_FIELD_INFO;


typedef struct parseModeInfo {
    A_CHAR      fieldName[MAX_NAME_LENGTH];
	A_CHAR		value11aStr[MAX_VALUE_LENGTH];
	A_CHAR		value11aTurboStr[MAX_VALUE_LENGTH];
	A_CHAR		value11bStr[MAX_VALUE_LENGTH];
	A_CHAR		value11gStr[MAX_VALUE_LENGTH];
	A_CHAR      value11gTurboStr[MAX_VALUE_LENGTH];
} PARSE_MODE_INFO;


typedef struct modeInfo {
    A_CHAR      fieldName[MAX_NAME_LENGTH];		//put this back in so that can check when overwrite with an external file
	A_UINT32	value11a;
	A_UINT32	value11aTurbo;
	A_UINT32	value11b;
	A_UINT32	value11g;
    A_UINT32    value11gTurbo;
	A_UINT32	indexToMainArray;
} MODE_INFO;

typedef struct pciRegValues {
    A_UINT32    offset;
    A_UINT32    baseValue;
    A_UINT32    turboValue;
} PCI_REG_VALUES;

typedef struct pciValues {
    A_UINT32    offset;
    A_UINT32    baseValue;
} PCI_VALUES;

typedef struct rfRegInfo {
	A_UINT32			bankNum;
	ATHEROS_REG_FILE	*pRegFields;
	A_UINT32			numRegFields;
	PCI_REG_VALUES		*pPciValues;
	A_UINT16			numPciRegs;
	A_BOOL				writeBank;
} RF_REG_INFO;

void updateField(A_UINT32 devNum, ATHEROS_REG_FILE *fieldDetails, A_UINT32 newValue, A_BOOL immediate);
void sendPciWrites(A_UINT32 devNum, PCI_VALUES *pciValues, A_UINT32 nRegs);
void writeMultipleFieldsInRfBank(A_UINT32 devNum, PARSE_FIELD_INFO *pFieldsToChange, A_UINT32 numFields, A_UINT32 bankIndex);
void replaceMutipleFields(A_UINT32 devNum, ATHEROS_REG_FILE *pInitRegs, A_UINT16 regSize, MODE_INFO *pModeValues, A_UINT16 modeSize);
#endif // __INCathregh
