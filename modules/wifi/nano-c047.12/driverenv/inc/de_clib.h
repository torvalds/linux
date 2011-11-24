/* $Id: $ */

/*!
  * @file de_clib.h
  *
  * Adaptation layer for C-library routines
  *
  */

#ifndef DE_CLIB_H
#define DE_CLIB_H

/** @defgroup c_lib_defines LIBC function wrappers
 *  @{
 */

/*!
 * \brief Round up size to alignment  
 * 
 * @param size       Size in bytes
 * @param alignment  Alignment  
 * 
 * @return Rounded up size.
 */
#define DE_SZ_ALIGN(_size, _alignment) ((((uint32_t)_size) + (_alignment) - 1) & ~(((uint32_t)_alignment) - 1))

/*!
 * \brief Copy string. 
 * 
 * Copies the content pointed by src to dest stopping after the terminating null-character is copied.
 * dest should have enough memory space allocated to contain src string.
 * @param dest Destination string. Should be enough long to contain src
 * @param src Null-terminated string to copy. 
 * 
 * @return dest is returned.
 */
#define  DE_STRCPY(dest,src)    strcpy((char*)dest,(char*)src)

/*!
 * \brief Copy characters from one string to another. 
 * 
 * Copies the first num characters of src to dest.
 * No null-character is implicitly appended to dest after copying process.
 * So dest may not be null-terminated if no null-caracters are copied from src.
 * If num is greater than the length of src, dest is padded with zeros until num.
 * @param dest Destination string. Space allocated should be at least num characters long.
 * @param src Null-terminated string. 
 * @param num  Null-terminated string to copy. 
 * 
 * @return dest is returned.
 */
#define  DE_STRNCPY(dest,src,num) strncpy((char*)dest,(char*)src,num)

/*!
 * \brief Compare some characters of two strings. 
 * 
 * Compares the first num characters of string1 to the first num characters of string2.
 * The comparison is performed character by character. If a character that is not equal in
 * both strings is found the function ends and returns a value that determines which of them was greater.
 * @param string1 Null-terminated string to compare.
 * @param string2 Null-terminated string to compare. 
 * @param num Maximum number of characters to compare. 
 * 
 * @return Returns a value indicating the lexicographical relation between the strings:
 *         <0    string1 is less than string2
 *          0    string1 is the same as string2
 *         >0    string1 is greater than string2
 */
#define  DE_STRNCMP(string1,string2,num) strncmp((char*)string1,(char*)string2,num)

#define DE_STRCMP(S1, S2) strcmp((S1), (S2))

/*!
 * \brief Return string length. 
 * 
 * Returns the number of characters in string before the terminating null-character.
 * @param string Null-terminated string.
 * 
 * @return The length of string.
 */
#define  DE_STRLEN(string)      strlen((char*)string)

/*!
 * \brief Find character in string. 
 * 
 * Returns the first occurrence of c in string.
 * The null-terminating character is included as part of the string and can also be searched.
 *
 * @param string Null-terminated string scanned in the search.
 * @param c Character to be found.
 * 
 * @return If character is found, a pointer to the first occurrence of c in string is returned.
 * If not, NULL is returned.
 */
#define  DE_STRCHR(string,c)    strchr((char*)string,c)

/*!
 * \brief Convert a string to unsigned long
 * 
 * ANSI C function strtoul().
 *
 */
#define  DE_STRTOUL(a,b,c) simple_strtoul((a), (b), (c))

/*!
 * \brief Convert a string to long
 * 
 * ANSI C function strtol().
 *
 */
#define  DE_STRTOL               simple_strtol

/*!
 * \brief Copy bytes to buffer from buffer. 
 * 
 * Copies num bytes from src buffer to memory location pointed by dest.
 *
 * @param dest Destination buffer where data is copied.
 * @param src Source buffer to copy from.
 * @param num Number of bytes to copy.
 * 
 * @return dest is returned.
 */
#define  DE_MEMCPY(dest,src,num)  memcpy((char*)dest,(char*)src,num)

/*!
 * \brief Move bytes to buffer from buffer. 
 * 
 * ANSI C function memmove().
 *
 * @param dest Destination buffer where data is copied.
 * @param src Source buffer to copy from.
 * @param num Number of bytes to copy.
 * 
 * @return dest is returned.
 */
#define  DE_MEMMOVE(dest,src,num) memmove(dest, src, num)

/*!
 * \brief Fill buffer with specified character. 
 * 
 * ANSI C function memset().
 * Sets the first num bytes pointed by buffer to the value specified by c parameter.
 *
 * @param buffer Pointer to block of data to be filled with c.
 * @param c character value to set.
 * @param num Number of bytes to copy.
 * 
 * @return buffer is returned..
 */
#define  DE_MEMSET(buffer,c,num)  memset((char*)buffer,c,num)

/*!
 * \brief Compare two buffers. 
 * 
 * ANSI C function memcmp().
 * Compares the fisrt num bytes of two memory blocks pointed by buffer1 and buffer2.
 *
 * @param buffer1 Pointer to buffer.
 * @param buffer2 Pointer to buffer.
 * @param num Number of bytes to compare.
 * 
 * @return buffer is returned.
 */
#define  DE_MEMCMP(buffer1,buffer2,num)  memcmp((char*)buffer1,buffer2,num)

/*!
 * \brief Print formatted data to a string. 
 * 
 * Writes a sequence of arguments to the given buffer formatted as the format argument
 * specifies.
 * 
 * @return On success, the total number of characters printed is returned.
 * On error, a negative number is returned.
 */
#define  DE_SPRINTF sprintf

/*!
 * \brief Print formatted data to a string. 
 * 
 * ANSI C function snprintf().
 *
 */
#define  DE_SNPRINTF snprintf

/*!
 * \brief Convert string to integer. 
 * 
 * Parses string interpreting its content as a number and returns an int value.
 *
 * @param string String representing an integer number. The number is considered until a
 * non-numeric character is found (digits, and signs are considered valid numeric characters
 * for this parameter as specified in format).
 * 
 * @return The converted integer value of the input string.
 * On overflow the result is undefined.
 * If an error occurs 0 is returned.
 */
#define  DE_ATOI(string)        (int)simple_strtol((string), NULL, 10)

#define DE_MIN(_a, _b) min(_a, _b)
#define DE_MAX(_a, _b) max(_a, _b)

/* Casting macros for buffer-to-type conversions */
#define AS_UINT8(x) (*((uint8_t *)x))
#define AS_UINT16(x) (*((uint16_t *)x))

/* @brief Evaluates to the number of elements in ARRAY */
#undef DE_ARRAY_SIZE
#define DE_ARRAY_SIZE(ARRAY) ARRAY_SIZE(ARRAY)

/** @} */ /* End of c_lib_defines group */

#endif /* DE_CLIB_H */
