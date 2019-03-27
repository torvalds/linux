/*
 * Various routines from the OSTA 2.01 specs.  Copyrights are included with
 * each code segment.  Slight whitespace modifications have been made for
 * formatting purposes.  Typos/bugs have been fixed.
 *
 * $FreeBSD$
 */

#include <fs/udf/osta.h>

/*****************************************************************************/
/*-
 **********************************************************************
 * OSTA compliant Unicode compression, uncompression routines.
 * Copyright 1995 Micro Design International, Inc.
 * Written by Jason M. Rinn.
 * Micro Design International gives permission for the free use of the
 * following source code.
 */

/***********************************************************************
 * Takes an OSTA CS0 compressed unicode name, and converts
 * it to Unicode.
 * The Unicode output will be in the byte order
 * that the local compiler uses for 16-bit values.
 * NOTE: This routine only performs error checking on the compID.
 * It is up to the user to ensure that the unicode buffer is large
 * enough, and that the compressed unicode name is correct.
 *
 * RETURN VALUE
 *
 * The number of unicode characters which were uncompressed.
 * A -1 is returned if the compression ID is invalid.
 */
int
udf_UncompressUnicode(
	int numberOfBytes,	/* (Input) number of bytes read from media. */
	byte *UDFCompressed,	/* (Input) bytes read from media. */
	unicode_t *unicode)	/* (Output) uncompressed unicode characters. */
{
	unsigned int compID;
	int returnValue, unicodeIndex, byteIndex;

	/* Use UDFCompressed to store current byte being read. */
	compID = UDFCompressed[0];

	/* First check for valid compID. */
	if (compID != 8 && compID != 16) {
		returnValue = -1;
	} else {
		unicodeIndex = 0;
		byteIndex = 1;

		/* Loop through all the bytes. */
		while (byteIndex < numberOfBytes) {
			if (compID == 16) {
				/* Move the first byte to the high bits of the
				 * unicode char.
				 */
				unicode[unicodeIndex] =
				    UDFCompressed[byteIndex++] << 8;
			} else {
				unicode[unicodeIndex] = 0;
			}
			if (byteIndex < numberOfBytes) {
				/*Then the next byte to the low bits. */
				unicode[unicodeIndex] |=
				    UDFCompressed[byteIndex++];
			}
			unicodeIndex++;
		}
		returnValue = unicodeIndex;
	}
	return(returnValue);
}

/*
 * Almost same as udf_UncompressUnicode(). The difference is that
 * it keeps byte order of unicode string.
 */
int
udf_UncompressUnicodeByte(
	int numberOfBytes,	/* (Input) number of bytes read from media. */
	byte *UDFCompressed,	/* (Input) bytes read from media. */
	byte *unicode)		/* (Output) uncompressed unicode characters. */
{
	unsigned int compID;
	int returnValue, unicodeIndex, byteIndex;

	/* Use UDFCompressed to store current byte being read. */
	compID = UDFCompressed[0];

	/* First check for valid compID. */
	if (compID != 8 && compID != 16) {
		returnValue = -1;
	} else {
		unicodeIndex = 0;
		byteIndex = 1;

		/* Loop through all the bytes. */
		while (byteIndex < numberOfBytes) {
			if (compID == 16) {
				/* Move the first byte to the high bits of the
				 * unicode char.
				 */
				unicode[unicodeIndex++] =
				    UDFCompressed[byteIndex++];
			} else {
				unicode[unicodeIndex++] = 0;
			}
			if (byteIndex < numberOfBytes) {
				/*Then the next byte to the low bits. */
				unicode[unicodeIndex++] =
				    UDFCompressed[byteIndex++];
			}
		}
		returnValue = unicodeIndex;
	}
	return(returnValue);
}

/***********************************************************************
 * DESCRIPTION:
 * Takes a string of unicode wide characters and returns an OSTA CS0
 * compressed unicode string. The unicode MUST be in the byte order of
 * the compiler in order to obtain correct results. Returns an error
 * if the compression ID is invalid.
 *
 * NOTE: This routine assumes the implementation already knows, by
 * the local environment, how many bits are appropriate and
 * therefore does no checking to test if the input characters fit
 * into that number of bits or not.
 *
 * RETURN VALUE
 *
 * The total number of bytes in the compressed OSTA CS0 string,
 * including the compression ID.
 * A -1 is returned if the compression ID is invalid.
 */
int
udf_CompressUnicode(
	int numberOfChars,	/* (Input) number of unicode characters. */
	int compID,		/* (Input) compression ID to be used. */
	unicode_t *unicode,	/* (Input) unicode characters to compress. */
	byte *UDFCompressed)	/* (Output) compressed string, as bytes. */
{
	int byteIndex, unicodeIndex;

	if (compID != 8 && compID != 16) {
		byteIndex = -1; /* Unsupported compression ID ! */
	} else {
		/* Place compression code in first byte. */
		UDFCompressed[0] = compID;

		byteIndex = 1;
		unicodeIndex = 0;
		while (unicodeIndex < numberOfChars) {
			if (compID == 16) {
				/* First, place the high bits of the char
				 * into the byte stream.
				 */
				UDFCompressed[byteIndex++] =
				    (unicode[unicodeIndex] & 0xFF00) >> 8;
			}
			/*Then place the low bits into the stream. */
			UDFCompressed[byteIndex++] =
			    unicode[unicodeIndex] & 0x00FF;
			unicodeIndex++;
		}
	}
	return(byteIndex);
}

/*****************************************************************************/
/*
 * CRC 010041
 */
static unsigned short crc_table[256] = {
	0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
	0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
	0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
	0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
	0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
	0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
	0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
	0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
	0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
	0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
	0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
	0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
	0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
	0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
	0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
	0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
	0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
	0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
	0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
	0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
	0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
	0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
	0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
	0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
	0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
	0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
	0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
	0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
	0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
	0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
	0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
	0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

unsigned short
udf_cksum(s, n)
	unsigned char *s;
	int n;
{
	unsigned short crc=0;

	while (n-- > 0)
		crc = crc_table[(crc>>8 ^ *s++) & 0xff] ^ (crc<<8);
	return crc;
}

/* UNICODE Checksum */
unsigned short
udf_unicode_cksum(s, n)
	unsigned short *s;
	int n;
{
	unsigned short crc=0;

	while (n-- > 0) {
		/* Take high order byte first--corresponds to a big endian
		 * byte stream.
		 */
		crc = crc_table[(crc>>8 ^ (*s>>8)) & 0xff] ^ (crc<<8);
		crc = crc_table[(crc>>8 ^ (*s++ & 0xff)) & 0xff] ^ (crc<<8);
	}
	return crc;
}

#ifdef MAIN
unsigned char bytes[] = { 0x70, 0x6A, 0x77 };

main()
{
	unsigned short x;
	x = cksum(bytes, sizeof bytes);
	printf("checksum: calculated=%4.4x, correct=%4.4x\en", x, 0x3299);
	exit(0);
}
#endif

/*****************************************************************************/
#ifdef NEEDS_ISPRINT
/*-
 **********************************************************************
 * OSTA UDF compliant file name translation routine for OS/2,
 * Windows 95, Windows NT, Macintosh and UNIX.
 * Copyright 1995 Micro Design International, Inc.
 * Written by Jason M. Rinn.
 * Micro Design International gives permission for the free use of the
 * following source code.
 */

/***********************************************************************
 * To use these routines with different operating systems.
 *
 * OS/2
 * Define OS2
 * Define MAXLEN = 254
 *
 * Windows 95
 * Define WIN_95
 * Define MAXLEN = 255
 *
 * Windows NT
 * Define WIN_NT
 * Define MAXLEN = 255
 *
 * Macintosh:
 * Define APPLE_MAC.
 * Define MAXLEN = 31.
 *
 * UNIX
 * Define UNIX.
 * Define MAXLEN as specified by unix version.
 */

#define	ILLEGAL_CHAR_MARK	0x005F
#define	CRC_MARK	0x0023
#define	EXT_SIZE	5
#define	TRUE	1
#define	FALSE	0
#define	PERIOD	0x002E
#define	SPACE	0x0020

/*** PROTOTYPES ***/
int IsIllegal(unicode_t ch);

/* Define a function or macro which determines if a Unicode character is
 * printable under your implementation.
 */
int UnicodeIsPrint(unicode_t);

/***********************************************************************
 * Translates a long file name to one using a MAXLEN and an illegal
 * char set in accord with the OSTA requirements. Assumes the name has
 * already been translated to Unicode.
 *
 * RETURN VALUE
 *
 * Number of unicode characters in translated name.
 */
int UDFTransName(
	unicode_t *newName,	/* (Output)Translated name. Must be of length
				 * MAXLEN */
	unicode_t *udfName,	/* (Input) Name from UDF volume.*/
	int udfLen)		/* (Input) Length of UDF Name. */
{
	int index, newIndex = 0, needsCRC = FALSE;
	int extIndex = 0, newExtIndex = 0, hasExt = FALSE;
#if defined OS2 || defined WIN_95 || defined WIN_NT
	int trailIndex = 0;
#endif
	unsigned short valueCRC;
	unicode_t current;
	const char hexChar[] = "0123456789ABCDEF";

	for (index = 0; index < udfLen; index++) {
		current = udfName[index];

		if (IsIllegal(current) || !UnicodeIsPrint(current)) {
			needsCRC = TRUE;
			/* Replace Illegal and non-displayable chars with
			 * underscore.
			 */
			current = ILLEGAL_CHAR_MARK;
			/* Skip any other illegal or non-displayable
			 * characters.
			 */
			while(index+1 < udfLen && (IsIllegal(udfName[index+1])
			    || !UnicodeIsPrint(udfName[index+1]))) {
				index++;
			}
		}

		/* Record position of extension, if one is found. */
		if (current == PERIOD && (udfLen - index -1) <= EXT_SIZE) {
			if (udfLen == index + 1) {
				/* A trailing period is NOT an extension. */
				hasExt = FALSE;
			} else {
				hasExt = TRUE;
				extIndex = index;
				newExtIndex = newIndex;
			}
		}

#if defined OS2 || defined WIN_95 || defined WIN_NT
		/* Record position of last char which is NOT period or space. */
		else if (current != PERIOD && current != SPACE) {
			trailIndex = newIndex;
		}
#endif

		if (newIndex < MAXLEN) {
			newName[newIndex++] = current;
		} else {
			needsCRC = TRUE;
		}
	}

#if defined OS2 || defined WIN_95 || defined WIN_NT
	/* For OS2, 95 & NT, truncate any trailing periods and\or spaces. */
	if (trailIndex != newIndex - 1) {
		newIndex = trailIndex + 1;
		needsCRC = TRUE;
		hasExt = FALSE; /* Trailing period does not make an
				 * extension. */
	}
#endif

	if (needsCRC) {
		unicode_t ext[EXT_SIZE];
		int localExtIndex = 0;
		if (hasExt) {
			int maxFilenameLen;
			/* Translate extension, and store it in ext. */
			for(index = 0; index<EXT_SIZE &&
			    extIndex + index +1 < udfLen; index++ ) {
				current = udfName[extIndex + index + 1];
				if (IsIllegal(current) ||
				    !UnicodeIsPrint(current)) {
					needsCRC = 1;
					/* Replace Illegal and non-displayable
					 * chars with underscore.
					 */
					current = ILLEGAL_CHAR_MARK;
					/* Skip any other illegal or
					 * non-displayable characters.
					 */
					while(index + 1 < EXT_SIZE
					    && (IsIllegal(udfName[extIndex +
					    index + 2]) ||
					    !isprint(udfName[extIndex +
					    index + 2]))) {
						index++;
					}
				}
				ext[localExtIndex++] = current;
			}

			/* Truncate filename to leave room for extension and
			 * CRC.
			 */
			maxFilenameLen = ((MAXLEN - 5) - localExtIndex - 1);
			if (newIndex > maxFilenameLen) {
				newIndex = maxFilenameLen;
			} else {
				newIndex = newExtIndex;
			}
		} else if (newIndex > MAXLEN - 5) {
			/*If no extension, make sure to leave room for CRC. */
			newIndex = MAXLEN - 5;
		}
		newName[newIndex++] = CRC_MARK; /* Add mark for CRC. */

		/*Calculate CRC from original filename from FileIdentifier. */
		valueCRC = udf_unicode_cksum(udfName, udfLen);
		/* Convert 16-bits of CRC to hex characters. */
		newName[newIndex++] = hexChar[(valueCRC & 0xf000) >> 12];
		newName[newIndex++] = hexChar[(valueCRC & 0x0f00) >> 8];
		newName[newIndex++] = hexChar[(valueCRC & 0x00f0) >> 4];
		newName[newIndex++] = hexChar[(valueCRC & 0x000f)];

		/* Place a translated extension at end, if found. */
		if (hasExt) {
			newName[newIndex++] = PERIOD;
			for (index = 0;index < localExtIndex ;index++ ) {
				newName[newIndex++] = ext[index];
			}
		}
	}
	return(newIndex);
}

#if defined OS2 || defined WIN_95 || defined WIN_NT
/***********************************************************************
 * Decides if a Unicode character matches one of a list
 * of ASCII characters.
 * Used by OS2 version of IsIllegal for readability, since all of the
 * illegal characters above 0x0020 are in the ASCII subset of Unicode.
 * Works very similarly to the standard C function strchr().
 *
 * RETURN VALUE
 *
 * Non-zero if the Unicode character is in the given ASCII string.
 */
int UnicodeInString(
	unsigned char *string,	/* (Input) String to search through. */
	unicode_t ch)		/* (Input) Unicode char to search for. */
{
	int found = FALSE;
	while (*string != '\0' && found == FALSE) {
		/* These types should compare, since both are unsigned
		 * numbers. */
		if (*string == ch) {
			found = TRUE;
		}
		string++;
	}
	return(found);
}
#endif /* OS2 */

/***********************************************************************
 * Decides whether the given character is illegal for a given OS.
 *
 * RETURN VALUE
 *
 * Non-zero if char is illegal.
 */
int IsIllegal(unicode_t ch)
{
#ifdef APPLE_MAC
	/* Only illegal character on the MAC is the colon. */
	if (ch == 0x003A) {
		return(1);
	} else {
		return(0);
	}

#elif defined UNIX
	/* Illegal UNIX characters are NULL and slash. */
	if (ch == 0x0000 || ch == 0x002F) {
		return(1);
	} else {
		return(0);
	}

#elif defined OS2 || defined WIN_95 || defined WIN_NT
	/* Illegal char's for OS/2 according to WARP toolkit. */
	if (ch < 0x0020 || UnicodeInString("\\/:*?\"<>|", ch)) {
		return(1);
	} else {
		return(0);
	}
#endif
}
#endif
