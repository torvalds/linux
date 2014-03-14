/*
 * "$Id: mxml-file.c 438 2011-03-24 05:47:51Z mike $"
 *
 * File loading code for Mini-XML, a small XML-like file parsing library.
 *
 * Copyright 2003-2011 by Michael R Sweet.
 *
 * These coded instructions, statements, and computer programs are the
 * property of Michael R Sweet and are protected by Federal copyright
 * law.  Distribution and use rights are outlined in the file "COPYING"
 * which should have been included with this file.  If this file is
 * missing or damaged, see the license at:
 *
 *     http://www.minixml.org/
 *
 * Contents:
 *
 *   mxmlLoadFd()            - Load a file descriptor into an XML node tree.
 *   mxmlLoadFile()          - Load a file into an XML node tree.
 *   mxmlLoadString()        - Load a string into an XML node tree.
 *   mxmlSaveAllocString()   - Save an XML tree to an allocated string.
 *   mxmlSaveFd()            - Save an XML tree to a file descriptor.
 *   mxmlSaveFile()          - Save an XML tree to a file.
 *   mxmlSaveString()        - Save an XML node tree to a string.
 *   mxmlSAXLoadFd()         - Load a file descriptor into an XML node tree
 *                             using a SAX callback.
 *   mxmlSAXLoadFile()       - Load a file into an XML node tree
 *                             using a SAX callback.
 *   mxmlSAXLoadString()     - Load a string into an XML node tree
 *                             using a SAX callback.
 *   mxmlSetCustomHandlers() - Set the handling functions for custom data.
 *   mxmlSetErrorCallback()  - Set the error message callback.
 *   mxmlSetWrapMargin()     - Set the wrap margin when saving XML data.
 *   mxml_add_char()         - Add a character to a buffer, expanding as needed.
 *   mxml_fd_getc()          - Read a character from a file descriptor.
 *   mxml_fd_putc()          - Write a character to a file descriptor.
 *   mxml_fd_read()          - Read a buffer of data from a file descriptor.
 *   mxml_fd_write()         - Write a buffer of data to a file descriptor.
 *   mxml_file_getc()        - Get a character from a file.
 *   mxml_file_putc()        - Write a character to a file.
 *   mxml_get_entity()       - Get the character corresponding to an entity...
 *   mxml_load_data()        - Load data into an XML node tree.
 *   mxml_parse_element()    - Parse an element for any attributes...
 *   mxml_string_getc()      - Get a character from a string.
 *   mxml_string_putc()      - Write a character to a string.
 *   mxml_write_name()       - Write a name string.
 *   mxml_write_node()       - Save an XML node to a file.
 *   mxml_write_string()     - Write a string, escaping & and < as needed.
 *   mxml_write_ws()         - Do whitespace callback...
 */

/*
 * Include necessary headers...
 */

#ifndef WIN32
#  include <unistd.h>
#endif /* !WIN32 */
#include "mxml-private.h"


/*
 * Character encoding...
 */

#define ENCODE_UTF8	0		/* UTF-8 */
#define ENCODE_UTF16BE	1		/* UTF-16 Big-Endian */
#define ENCODE_UTF16LE	2		/* UTF-16 Little-Endian */


/*
 * Macro to test for a bad XML character...
 */

#define mxml_bad_char(ch) ((ch) < ' ' && (ch) != '\n' && (ch) != '\r' && (ch) != '\t')


/*
 * Types and structures...
 */

typedef int (*_mxml_getc_cb_t)(void *, int *);
typedef int (*_mxml_putc_cb_t)(int, void *);

typedef struct _mxml_fdbuf_s		/**** File descriptor buffer ****/
{
  int		fd;			/* File descriptor */
  unsigned char	*current,		/* Current position in buffer */
		*end,			/* End of buffer */
		buffer[8192];		/* Character buffer */
} _mxml_fdbuf_t;


/*
 * Local functions...
 */

static int		mxml_add_char(int ch, char **ptr, char **buffer,
			              int *bufsize);
static int		mxml_fd_getc(void *p, int *encoding);
static int		mxml_fd_putc(int ch, void *p);
static int		mxml_fd_read(_mxml_fdbuf_t *buf);
static int		mxml_fd_write(_mxml_fdbuf_t *buf);
static int		mxml_file_getc(void *p, int *encoding);
static int		mxml_file_putc(int ch, void *p);
static int		mxml_get_entity(mxml_node_t *parent, void *p,
			                int *encoding,
					_mxml_getc_cb_t getc_cb);
static inline int	mxml_isspace(int ch)
			{
			  return (ch == ' ' || ch == '\t' || ch == '\r' ||
			          ch == '\n');
			}
static mxml_node_t	*mxml_load_data(mxml_node_t *top, void *p,
			                mxml_load_cb_t cb,
			                _mxml_getc_cb_t getc_cb,
                                        mxml_sax_cb_t sax_cb, void *sax_data);
static int		mxml_parse_element(mxml_node_t *node, void *p,
			                   int *encoding,
					   _mxml_getc_cb_t getc_cb);
static int		mxml_string_getc(void *p, int *encoding);
static int		mxml_string_putc(int ch, void *p);
static int		mxml_write_name(const char *s, void *p,
					_mxml_putc_cb_t putc_cb);
static int		mxml_write_node(mxml_node_t *node, void *p,
			                mxml_save_cb_t cb, int col,
					_mxml_putc_cb_t putc_cb,
					_mxml_global_t *global);
static int		mxml_write_string(const char *s, void *p,
					  _mxml_putc_cb_t putc_cb);
static int		mxml_write_ws(mxml_node_t *node, void *p, 
			              mxml_save_cb_t cb, int ws,
				      int col, _mxml_putc_cb_t putc_cb);


/*
 * 'mxmlLoadFd()' - Load a file descriptor into an XML node tree.
 *
 * The nodes in the specified file are added to the specified top node.
 * If no top node is provided, the XML file MUST be well-formed with a
 * single parent node like <?xml> for the entire file. The callback
 * function returns the value type that should be used for child nodes.
 * If MXML_NO_CALLBACK is specified then all child nodes will be either
 * MXML_ELEMENT or MXML_TEXT nodes.
 *
 * The constants MXML_INTEGER_CALLBACK, MXML_OPAQUE_CALLBACK,
 * MXML_REAL_CALLBACK, and MXML_TEXT_CALLBACK are defined for loading
 * child nodes of the specified type.
 */

mxml_node_t *				/* O - First node or NULL if the file could not be read. */
mxmlLoadFd(mxml_node_t    *top,		/* I - Top node */
           int            fd,		/* I - File descriptor to read from */
           mxml_load_cb_t cb)		/* I - Callback function or MXML_NO_CALLBACK */
{
  _mxml_fdbuf_t	buf;			/* File descriptor buffer */


 /*
  * Initialize the file descriptor buffer...
  */

  buf.fd      = fd;
  buf.current = buf.buffer;
  buf.end     = buf.buffer;

 /*
  * Read the XML data...
  */

  return (mxml_load_data(top, &buf, cb, mxml_fd_getc, MXML_NO_CALLBACK, NULL));
}


/*
 * 'mxmlLoadFile()' - Load a file into an XML node tree.
 *
 * The nodes in the specified file are added to the specified top node.
 * If no top node is provided, the XML file MUST be well-formed with a
 * single parent node like <?xml> for the entire file. The callback
 * function returns the value type that should be used for child nodes.
 * If MXML_NO_CALLBACK is specified then all child nodes will be either
 * MXML_ELEMENT or MXML_TEXT nodes.
 *
 * The constants MXML_INTEGER_CALLBACK, MXML_OPAQUE_CALLBACK,
 * MXML_REAL_CALLBACK, and MXML_TEXT_CALLBACK are defined for loading
 * child nodes of the specified type.
 */

mxml_node_t *				/* O - First node or NULL if the file could not be read. */
mxmlLoadFile(mxml_node_t    *top,	/* I - Top node */
             FILE           *fp,	/* I - File to read from */
             mxml_load_cb_t cb)		/* I - Callback function or MXML_NO_CALLBACK */
{
 /*
  * Read the XML data...
  */

  return (mxml_load_data(top, fp, cb, mxml_file_getc, MXML_NO_CALLBACK, NULL));
}


/*
 * 'mxmlLoadString()' - Load a string into an XML node tree.
 *
 * The nodes in the specified string are added to the specified top node.
 * If no top node is provided, the XML string MUST be well-formed with a
 * single parent node like <?xml> for the entire string. The callback
 * function returns the value type that should be used for child nodes.
 * If MXML_NO_CALLBACK is specified then all child nodes will be either
 * MXML_ELEMENT or MXML_TEXT nodes.
 *
 * The constants MXML_INTEGER_CALLBACK, MXML_OPAQUE_CALLBACK,
 * MXML_REAL_CALLBACK, and MXML_TEXT_CALLBACK are defined for loading
 * child nodes of the specified type.
 */

mxml_node_t *				/* O - First node or NULL if the string has errors. */
mxmlLoadString(mxml_node_t    *top,	/* I - Top node */
               const char     *s,	/* I - String to load */
               mxml_load_cb_t cb)	/* I - Callback function or MXML_NO_CALLBACK */
{
 /*
  * Read the XML data...
  */

  return (mxml_load_data(top, (void *)&s, cb, mxml_string_getc, MXML_NO_CALLBACK,
                         NULL));
}


/*
 * 'mxmlSaveAllocString()' - Save an XML tree to an allocated string.
 *
 * This function returns a pointer to a string containing the textual
 * representation of the XML node tree.  The string should be freed
 * using the free() function when you are done with it.  NULL is returned
 * if the node would produce an empty string or if the string cannot be
 * allocated.
 *
 * The callback argument specifies a function that returns a whitespace
 * string or NULL before and after each element. If MXML_NO_CALLBACK
 * is specified, whitespace will only be added before MXML_TEXT nodes
 * with leading whitespace and before attribute names inside opening
 * element tags.
 */

char *					/* O - Allocated string or NULL */
mxmlSaveAllocString(
    mxml_node_t    *node,		/* I - Node to write */
    mxml_save_cb_t cb)			/* I - Whitespace callback or MXML_NO_CALLBACK */
{
  int	bytes;				/* Required bytes */
  char	buffer[8192];			/* Temporary buffer */
  char	*s;				/* Allocated string */


 /*
  * Write the node to the temporary buffer...
  */

  bytes = mxmlSaveString(node, buffer, sizeof(buffer), cb);

  if (bytes <= 0)
    return (NULL);

  if (bytes < (int)(sizeof(buffer) - 1))
  {
   /*
    * Node fit inside the buffer, so just duplicate that string and
    * return...
    */

    return (strdup(buffer));
  }

 /*
  * Allocate a buffer of the required size and save the node to the
  * new buffer...
  */

  if ((s = malloc(bytes + 1)) == NULL)
    return (NULL);

  mxmlSaveString(node, s, bytes + 1, cb);

 /*
  * Return the allocated string...
  */

  return (s);
}


/*
 * 'mxmlSaveFd()' - Save an XML tree to a file descriptor.
 *
 * The callback argument specifies a function that returns a whitespace
 * string or NULL before and after each element. If MXML_NO_CALLBACK
 * is specified, whitespace will only be added before MXML_TEXT nodes
 * with leading whitespace and before attribute names inside opening
 * element tags.
 */

int					/* O - 0 on success, -1 on error. */
mxmlSaveFd(mxml_node_t    *node,	/* I - Node to write */
           int            fd,		/* I - File descriptor to write to */
	   mxml_save_cb_t cb)		/* I - Whitespace callback or MXML_NO_CALLBACK */
{
  int		col;			/* Final column */
  _mxml_fdbuf_t	buf;			/* File descriptor buffer */
  _mxml_global_t *global = _mxml_global();
					/* Global data */


 /*
  * Initialize the file descriptor buffer...
  */

  buf.fd      = fd;
  buf.current = buf.buffer;
  buf.end     = buf.buffer + sizeof(buf.buffer);

 /*
  * Write the node...
  */

  if ((col = mxml_write_node(node, &buf, cb, 0, mxml_fd_putc, global)) < 0)
    return (-1);

  if (col > 0)
    if (mxml_fd_putc('\n', &buf) < 0)
      return (-1);

 /*
  * Flush and return...
  */

  return (mxml_fd_write(&buf));
}


/*
 * 'mxmlSaveFile()' - Save an XML tree to a file.
 *
 * The callback argument specifies a function that returns a whitespace
 * string or NULL before and after each element. If MXML_NO_CALLBACK
 * is specified, whitespace will only be added before MXML_TEXT nodes
 * with leading whitespace and before attribute names inside opening
 * element tags.
 */

int					/* O - 0 on success, -1 on error. */
mxmlSaveFile(mxml_node_t    *node,	/* I - Node to write */
             FILE           *fp,	/* I - File to write to */
	     mxml_save_cb_t cb)		/* I - Whitespace callback or MXML_NO_CALLBACK */
{
  int	col;				/* Final column */
  _mxml_global_t *global = _mxml_global();
					/* Global data */


 /*
  * Write the node...
  */

  if ((col = mxml_write_node(node, fp, cb, 0, mxml_file_putc, global)) < 0)
    return (-1);

  if (col > 0)
    if (putc('\n', fp) < 0)
      return (-1);

 /*
  * Return 0 (success)...
  */

  return (0);
}


/*
 * 'mxmlSaveString()' - Save an XML node tree to a string.
 *
 * This function returns the total number of bytes that would be
 * required for the string but only copies (bufsize - 1) characters
 * into the specified buffer.
 *
 * The callback argument specifies a function that returns a whitespace
 * string or NULL before and after each element. If MXML_NO_CALLBACK
 * is specified, whitespace will only be added before MXML_TEXT nodes
 * with leading whitespace and before attribute names inside opening
 * element tags.
 */

int					/* O - Size of string */
mxmlSaveString(mxml_node_t    *node,	/* I - Node to write */
               char           *buffer,	/* I - String buffer */
               int            bufsize,	/* I - Size of string buffer */
               mxml_save_cb_t cb)	/* I - Whitespace callback or MXML_NO_CALLBACK */
{
  int	col;				/* Final column */
  char	*ptr[2];			/* Pointers for putc_cb */
  _mxml_global_t *global = _mxml_global();
					/* Global data */


 /*
  * Write the node...
  */

  ptr[0] = buffer;
  ptr[1] = buffer + bufsize;

  if ((col = mxml_write_node(node, ptr, cb, 0, mxml_string_putc, global)) < 0)
    return (-1);

  if (col > 0)
    mxml_string_putc('\n', ptr);

 /*
  * Nul-terminate the buffer...
  */

  if (ptr[0] >= ptr[1])
    buffer[bufsize - 1] = '\0';
  else
    ptr[0][0] = '\0';

 /*
  * Return the number of characters...
  */

  return (ptr[0] - buffer);
}


/*
 * 'mxmlSAXLoadFd()' - Load a file descriptor into an XML node tree
 *                     using a SAX callback.
 *
 * The nodes in the specified file are added to the specified top node.
 * If no top node is provided, the XML file MUST be well-formed with a
 * single parent node like <?xml> for the entire file. The callback
 * function returns the value type that should be used for child nodes.
 * If MXML_NO_CALLBACK is specified then all child nodes will be either
 * MXML_ELEMENT or MXML_TEXT nodes.
 *
 * The constants MXML_INTEGER_CALLBACK, MXML_OPAQUE_CALLBACK,
 * MXML_REAL_CALLBACK, and MXML_TEXT_CALLBACK are defined for loading
 * child nodes of the specified type.
 *
 * The SAX callback must call mxmlRetain() for any nodes that need to
 * be kept for later use. Otherwise, nodes are deleted when the parent
 * node is closed or after each data, comment, CDATA, or directive node.
 *
 * @since Mini-XML 2.3@
 */

mxml_node_t *				/* O - First node or NULL if the file could not be read. */
mxmlSAXLoadFd(mxml_node_t    *top,	/* I - Top node */
              int            fd,	/* I - File descriptor to read from */
              mxml_load_cb_t cb,	/* I - Callback function or MXML_NO_CALLBACK */
              mxml_sax_cb_t  sax_cb,	/* I - SAX callback or MXML_NO_CALLBACK */
              void           *sax_data)	/* I - SAX user data */
{
  _mxml_fdbuf_t	buf;			/* File descriptor buffer */


 /*
  * Initialize the file descriptor buffer...
  */

  buf.fd      = fd;
  buf.current = buf.buffer;
  buf.end     = buf.buffer;

 /*
  * Read the XML data...
  */

  return (mxml_load_data(top, &buf, cb, mxml_fd_getc, sax_cb, sax_data));
}


/*
 * 'mxmlSAXLoadFile()' - Load a file into an XML node tree
 *                       using a SAX callback.
 *
 * The nodes in the specified file are added to the specified top node.
 * If no top node is provided, the XML file MUST be well-formed with a
 * single parent node like <?xml> for the entire file. The callback
 * function returns the value type that should be used for child nodes.
 * If MXML_NO_CALLBACK is specified then all child nodes will be either
 * MXML_ELEMENT or MXML_TEXT nodes.
 *
 * The constants MXML_INTEGER_CALLBACK, MXML_OPAQUE_CALLBACK,
 * MXML_REAL_CALLBACK, and MXML_TEXT_CALLBACK are defined for loading
 * child nodes of the specified type.
 *
 * The SAX callback must call mxmlRetain() for any nodes that need to
 * be kept for later use. Otherwise, nodes are deleted when the parent
 * node is closed or after each data, comment, CDATA, or directive node.
 *
 * @since Mini-XML 2.3@
 */

mxml_node_t *				/* O - First node or NULL if the file could not be read. */
mxmlSAXLoadFile(
    mxml_node_t    *top,		/* I - Top node */
    FILE           *fp,			/* I - File to read from */
    mxml_load_cb_t cb,			/* I - Callback function or MXML_NO_CALLBACK */
    mxml_sax_cb_t  sax_cb,		/* I - SAX callback or MXML_NO_CALLBACK */
    void           *sax_data)		/* I - SAX user data */
{
 /*
  * Read the XML data...
  */

  return (mxml_load_data(top, fp, cb, mxml_file_getc, sax_cb, sax_data));
}


/*
 * 'mxmlSAXLoadString()' - Load a string into an XML node tree
 *                         using a SAX callback.
 *
 * The nodes in the specified string are added to the specified top node.
 * If no top node is provided, the XML string MUST be well-formed with a
 * single parent node like <?xml> for the entire string. The callback
 * function returns the value type that should be used for child nodes.
 * If MXML_NO_CALLBACK is specified then all child nodes will be either
 * MXML_ELEMENT or MXML_TEXT nodes.
 *
 * The constants MXML_INTEGER_CALLBACK, MXML_OPAQUE_CALLBACK,
 * MXML_REAL_CALLBACK, and MXML_TEXT_CALLBACK are defined for loading
 * child nodes of the specified type.
 *
 * The SAX callback must call mxmlRetain() for any nodes that need to
 * be kept for later use. Otherwise, nodes are deleted when the parent
 * node is closed or after each data, comment, CDATA, or directive node.
 *
 * @since Mini-XML 2.3@
 */

mxml_node_t *				/* O - First node or NULL if the string has errors. */
mxmlSAXLoadString(
    mxml_node_t    *top,		/* I - Top node */
    const char     *s,			/* I - String to load */
    mxml_load_cb_t cb,			/* I - Callback function or MXML_NO_CALLBACK */
    mxml_sax_cb_t  sax_cb,		/* I - SAX callback or MXML_NO_CALLBACK */
    void           *sax_data)		/* I - SAX user data */
{
 /*
  * Read the XML data...
  */

  return (mxml_load_data(top, (void *)&s, cb, mxml_string_getc, sax_cb, sax_data));
}


/*
 * 'mxmlSetCustomHandlers()' - Set the handling functions for custom data.
 *
 * The load function accepts a node pointer and a data string and must
 * return 0 on success and non-zero on error.
 *
 * The save function accepts a node pointer and must return a malloc'd
 * string on success and NULL on error.
 * 
 */

void
mxmlSetCustomHandlers(
    mxml_custom_load_cb_t load,		/* I - Load function */
    mxml_custom_save_cb_t save)		/* I - Save function */
{
  _mxml_global_t *global = _mxml_global();
					/* Global data */


  global->custom_load_cb = load;
  global->custom_save_cb = save;
}


/*
 * 'mxmlSetErrorCallback()' - Set the error message callback.
 */

void
mxmlSetErrorCallback(mxml_error_cb_t cb)/* I - Error callback function */
{
  _mxml_global_t *global = _mxml_global();
					/* Global data */


  global->error_cb = cb;
}


/*
 * 'mxmlSetWrapMargin()' - Set the wrap margin when saving XML data.
 *
 * Wrapping is disabled when "column" is 0.
 *
 * @since Mini-XML 2.3@
 */

void
mxmlSetWrapMargin(int column)		/* I - Column for wrapping, 0 to disable wrapping */
{
  _mxml_global_t *global = _mxml_global();
					/* Global data */


  global->wrap = column;
}


/*
 * 'mxml_add_char()' - Add a character to a buffer, expanding as needed.
 */

static int				/* O  - 0 on success, -1 on error */
mxml_add_char(int  ch,			/* I  - Character to add */
              char **bufptr,		/* IO - Current position in buffer */
	      char **buffer,		/* IO - Current buffer */
	      int  *bufsize)		/* IO - Current buffer size */
{
  char	*newbuffer;			/* New buffer value */


  if (*bufptr >= (*buffer + *bufsize - 4))
  {
   /*
    * Increase the size of the buffer...
    */

    if (*bufsize < 1024)
      (*bufsize) *= 2;
    else
      (*bufsize) += 1024;

    if ((newbuffer = realloc(*buffer, *bufsize)) == NULL)
    {
      free(*buffer);

      mxml_error("Unable to expand string buffer to %d bytes!", *bufsize);

      return (-1);
    }

    *bufptr = newbuffer + (*bufptr - *buffer);
    *buffer = newbuffer;
  }

  if (ch < 0x80)
  {
   /*
    * Single byte ASCII...
    */

    *(*bufptr)++ = ch;
  }
  else if (ch < 0x800)
  {
   /*
    * Two-byte UTF-8...
    */

    *(*bufptr)++ = 0xc0 | (ch >> 6);
    *(*bufptr)++ = 0x80 | (ch & 0x3f);
  }
  else if (ch < 0x10000)
  {
   /*
    * Three-byte UTF-8...
    */

    *(*bufptr)++ = 0xe0 | (ch >> 12);
    *(*bufptr)++ = 0x80 | ((ch >> 6) & 0x3f);
    *(*bufptr)++ = 0x80 | (ch & 0x3f);
  }
  else
  {
   /*
    * Four-byte UTF-8...
    */

    *(*bufptr)++ = 0xf0 | (ch >> 18);
    *(*bufptr)++ = 0x80 | ((ch >> 12) & 0x3f);
    *(*bufptr)++ = 0x80 | ((ch >> 6) & 0x3f);
    *(*bufptr)++ = 0x80 | (ch & 0x3f);
  }

  return (0);
}


/*
 * 'mxml_fd_getc()' - Read a character from a file descriptor.
 */

static int				/* O  - Character or EOF */
mxml_fd_getc(void *p,			/* I  - File descriptor buffer */
             int  *encoding)		/* IO - Encoding */
{
  _mxml_fdbuf_t	*buf;			/* File descriptor buffer */
  int		ch,			/* Current character */
		temp;			/* Temporary character */


 /*
  * Grab the next character in the buffer...
  */

  buf = (_mxml_fdbuf_t *)p;

  if (buf->current >= buf->end)
    if (mxml_fd_read(buf) < 0)
      return (EOF);

  ch = *(buf->current)++;

  switch (*encoding)
  {
    case ENCODE_UTF8 :
       /*
	* Got a UTF-8 character; convert UTF-8 to Unicode and return...
	*/

	if (!(ch & 0x80))
	{
#if DEBUG > 1
          printf("mxml_fd_getc: %c (0x%04x)\n", ch < ' ' ? '.' : ch, ch);
#endif /* DEBUG > 1 */

	  if (mxml_bad_char(ch))
	  {
	    mxml_error("Bad control character 0x%02x not allowed by XML standard!",
        	       ch);
	    return (EOF);
	  }

	  return (ch);
        }
	else if (ch == 0xfe)
	{
	 /*
	  * UTF-16 big-endian BOM?
	  */

	  if (buf->current >= buf->end)
	    if (mxml_fd_read(buf) < 0)
	      return (EOF);

	  ch = *(buf->current)++;
          
	  if (ch != 0xff)
	    return (EOF);

	  *encoding = ENCODE_UTF16BE;

	  return (mxml_fd_getc(p, encoding));
	}
	else if (ch == 0xff)
	{
	 /*
	  * UTF-16 little-endian BOM?
	  */

	  if (buf->current >= buf->end)
	    if (mxml_fd_read(buf) < 0)
	      return (EOF);

	  ch = *(buf->current)++;
          
	  if (ch != 0xfe)
	    return (EOF);

	  *encoding = ENCODE_UTF16LE;

	  return (mxml_fd_getc(p, encoding));
	}
	else if ((ch & 0xe0) == 0xc0)
	{
	 /*
	  * Two-byte value...
	  */

	  if (buf->current >= buf->end)
	    if (mxml_fd_read(buf) < 0)
	      return (EOF);

	  temp = *(buf->current)++;

	  if ((temp & 0xc0) != 0x80)
	    return (EOF);

	  ch = ((ch & 0x1f) << 6) | (temp & 0x3f);

	  if (ch < 0x80)
	  {
	    mxml_error("Invalid UTF-8 sequence for character 0x%04x!", ch);
	    return (EOF);
	  }
	}
	else if ((ch & 0xf0) == 0xe0)
	{
	 /*
	  * Three-byte value...
	  */

	  if (buf->current >= buf->end)
	    if (mxml_fd_read(buf) < 0)
	      return (EOF);

	  temp = *(buf->current)++;

	  if ((temp & 0xc0) != 0x80)
	    return (EOF);

	  ch = ((ch & 0x0f) << 6) | (temp & 0x3f);

	  if (buf->current >= buf->end)
	    if (mxml_fd_read(buf) < 0)
	      return (EOF);

	  temp = *(buf->current)++;

	  if ((temp & 0xc0) != 0x80)
	    return (EOF);

	  ch = (ch << 6) | (temp & 0x3f);

	  if (ch < 0x800)
	  {
	    mxml_error("Invalid UTF-8 sequence for character 0x%04x!", ch);
	    return (EOF);
	  }

         /*
	  * Ignore (strip) Byte Order Mark (BOM)...
	  */

	  if (ch == 0xfeff)
	    return (mxml_fd_getc(p, encoding));
	}
	else if ((ch & 0xf8) == 0xf0)
	{
	 /*
	  * Four-byte value...
	  */

	  if (buf->current >= buf->end)
	    if (mxml_fd_read(buf) < 0)
	      return (EOF);

	  temp = *(buf->current)++;

	  if ((temp & 0xc0) != 0x80)
	    return (EOF);

	  ch = ((ch & 0x07) << 6) | (temp & 0x3f);

	  if (buf->current >= buf->end)
	    if (mxml_fd_read(buf) < 0)
	      return (EOF);

	  temp = *(buf->current)++;

	  if ((temp & 0xc0) != 0x80)
	    return (EOF);

	  ch = (ch << 6) | (temp & 0x3f);

	  if (buf->current >= buf->end)
	    if (mxml_fd_read(buf) < 0)
	      return (EOF);

	  temp = *(buf->current)++;

	  if ((temp & 0xc0) != 0x80)
	    return (EOF);

	  ch = (ch << 6) | (temp & 0x3f);

	  if (ch < 0x10000)
	  {
	    mxml_error("Invalid UTF-8 sequence for character 0x%04x!", ch);
	    return (EOF);
	  }
	}
	else
	  return (EOF);
	break;

    case ENCODE_UTF16BE :
       /*
        * Read UTF-16 big-endian char...
	*/

	if (buf->current >= buf->end)
	  if (mxml_fd_read(buf) < 0)
	    return (EOF);

	temp = *(buf->current)++;

	ch = (ch << 8) | temp;

	if (mxml_bad_char(ch))
	{
	  mxml_error("Bad control character 0x%02x not allowed by XML standard!",
        	     ch);
	  return (EOF);
	}
        else if (ch >= 0xd800 && ch <= 0xdbff)
	{
	 /*
	  * Multi-word UTF-16 char...
	  */

          int lch;

	  if (buf->current >= buf->end)
	    if (mxml_fd_read(buf) < 0)
	      return (EOF);

	  lch = *(buf->current)++;

	  if (buf->current >= buf->end)
	    if (mxml_fd_read(buf) < 0)
	      return (EOF);

	  temp = *(buf->current)++;

	  lch = (lch << 8) | temp;

          if (lch < 0xdc00 || lch >= 0xdfff)
	    return (EOF);

          ch = (((ch & 0x3ff) << 10) | (lch & 0x3ff)) + 0x10000;
	}
	break;

    case ENCODE_UTF16LE :
       /*
        * Read UTF-16 little-endian char...
	*/

	if (buf->current >= buf->end)
	  if (mxml_fd_read(buf) < 0)
	    return (EOF);

	temp = *(buf->current)++;

	ch |= (temp << 8);

        if (mxml_bad_char(ch))
	{
	  mxml_error("Bad control character 0x%02x not allowed by XML standard!",
        	     ch);
	  return (EOF);
	}
        else if (ch >= 0xd800 && ch <= 0xdbff)
	{
	 /*
	  * Multi-word UTF-16 char...
	  */

          int lch;

	  if (buf->current >= buf->end)
	    if (mxml_fd_read(buf) < 0)
	      return (EOF);

	  lch = *(buf->current)++;

	  if (buf->current >= buf->end)
	    if (mxml_fd_read(buf) < 0)
	      return (EOF);

	  temp = *(buf->current)++;

	  lch |= (temp << 8);

          if (lch < 0xdc00 || lch >= 0xdfff)
	    return (EOF);

          ch = (((ch & 0x3ff) << 10) | (lch & 0x3ff)) + 0x10000;
	}
	break;
  }

#if DEBUG > 1
  printf("mxml_fd_getc: %c (0x%04x)\n", ch < ' ' ? '.' : ch, ch);
#endif /* DEBUG > 1 */

  return (ch);
}


/*
 * 'mxml_fd_putc()' - Write a character to a file descriptor.
 */

static int				/* O - 0 on success, -1 on error */
mxml_fd_putc(int  ch,			/* I - Character */
             void *p)			/* I - File descriptor buffer */
{
  _mxml_fdbuf_t	*buf;			/* File descriptor buffer */


 /*
  * Flush the write buffer as needed...
  */

  buf = (_mxml_fdbuf_t *)p;

  if (buf->current >= buf->end)
    if (mxml_fd_write(buf) < 0)
      return (-1);

  *(buf->current)++ = ch;

 /*
  * Return successfully...
  */

  return (0);
}


/*
 * 'mxml_fd_read()' - Read a buffer of data from a file descriptor.
 */

static int				/* O - 0 on success, -1 on error */
mxml_fd_read(_mxml_fdbuf_t *buf)		/* I - File descriptor buffer */
{
  int	bytes;				/* Bytes read... */


 /*
  * Range check input...
  */

  if (!buf)
    return (-1);

 /*
  * Read from the file descriptor...
  */

  while ((bytes = read(buf->fd, buf->buffer, sizeof(buf->buffer))) < 0)
#ifdef EINTR
    if (errno != EAGAIN && errno != EINTR)
#else
    if (errno != EAGAIN)
#endif /* EINTR */
      return (-1);

  if (bytes == 0)
    return (-1);

 /*
  * Update the pointers and return success...
  */

  buf->current = buf->buffer;
  buf->end     = buf->buffer + bytes;

  return (0);
}


/*
 * 'mxml_fd_write()' - Write a buffer of data to a file descriptor.
 */

static int				/* O - 0 on success, -1 on error */
mxml_fd_write(_mxml_fdbuf_t *buf)	/* I - File descriptor buffer */
{
  int		bytes;			/* Bytes written */
  unsigned char	*ptr;			/* Pointer into buffer */


 /*
  * Range check...
  */

  if (!buf)
    return (-1);

 /*
  * Return 0 if there is nothing to write...
  */

  if (buf->current == buf->buffer)
    return (0);

 /*
  * Loop until we have written everything...
  */

  for (ptr = buf->buffer; ptr < buf->current; ptr += bytes)
    if ((bytes = write(buf->fd, ptr, buf->current - ptr)) < 0)
      return (-1);

 /*
  * All done, reset pointers and return success...
  */

  buf->current = buf->buffer;

  return (0);
}


/*
 * 'mxml_file_getc()' - Get a character from a file.
 */

static int				/* O  - Character or EOF */
mxml_file_getc(void *p,			/* I  - Pointer to file */
               int  *encoding)		/* IO - Encoding */
{
  int	ch,				/* Character from file */
	temp;				/* Temporary character */
  FILE	*fp;				/* Pointer to file */


 /*
  * Read a character from the file and see if it is EOF or ASCII...
  */

  fp = (FILE *)p;
  ch = getc(fp);

  if (ch == EOF)
    return (EOF);

  switch (*encoding)
  {
    case ENCODE_UTF8 :
       /*
	* Got a UTF-8 character; convert UTF-8 to Unicode and return...
	*/

	if (!(ch & 0x80))
	{
	  if (mxml_bad_char(ch))
	  {
	    mxml_error("Bad control character 0x%02x not allowed by XML standard!",
        	       ch);
	    return (EOF);
	  }

#if DEBUG > 1
          printf("mxml_file_getc: %c (0x%04x)\n", ch < ' ' ? '.' : ch, ch);
#endif /* DEBUG > 1 */

	  return (ch);
        }
	else if (ch == 0xfe)
	{
	 /*
	  * UTF-16 big-endian BOM?
	  */

          ch = getc(fp);
	  if (ch != 0xff)
	    return (EOF);

	  *encoding = ENCODE_UTF16BE;

	  return (mxml_file_getc(p, encoding));
	}
	else if (ch == 0xff)
	{
	 /*
	  * UTF-16 little-endian BOM?
	  */

          ch = getc(fp);
	  if (ch != 0xfe)
	    return (EOF);

	  *encoding = ENCODE_UTF16LE;

	  return (mxml_file_getc(p, encoding));
	}
	else if ((ch & 0xe0) == 0xc0)
	{
	 /*
	  * Two-byte value...
	  */

	  if ((temp = getc(fp)) == EOF || (temp & 0xc0) != 0x80)
	    return (EOF);

	  ch = ((ch & 0x1f) << 6) | (temp & 0x3f);

	  if (ch < 0x80)
	  {
	    mxml_error("Invalid UTF-8 sequence for character 0x%04x!", ch);
	    return (EOF);
	  }
	}
	else if ((ch & 0xf0) == 0xe0)
	{
	 /*
	  * Three-byte value...
	  */

	  if ((temp = getc(fp)) == EOF || (temp & 0xc0) != 0x80)
	    return (EOF);

	  ch = ((ch & 0x0f) << 6) | (temp & 0x3f);

	  if ((temp = getc(fp)) == EOF || (temp & 0xc0) != 0x80)
	    return (EOF);

	  ch = (ch << 6) | (temp & 0x3f);

	  if (ch < 0x800)
	  {
	    mxml_error("Invalid UTF-8 sequence for character 0x%04x!", ch);
	    return (EOF);
	  }

         /*
	  * Ignore (strip) Byte Order Mark (BOM)...
	  */

	  if (ch == 0xfeff)
	    return (mxml_file_getc(p, encoding));
	}
	else if ((ch & 0xf8) == 0xf0)
	{
	 /*
	  * Four-byte value...
	  */

	  if ((temp = getc(fp)) == EOF || (temp & 0xc0) != 0x80)
	    return (EOF);

	  ch = ((ch & 0x07) << 6) | (temp & 0x3f);

	  if ((temp = getc(fp)) == EOF || (temp & 0xc0) != 0x80)
	    return (EOF);

	  ch = (ch << 6) | (temp & 0x3f);

	  if ((temp = getc(fp)) == EOF || (temp & 0xc0) != 0x80)
	    return (EOF);

	  ch = (ch << 6) | (temp & 0x3f);

	  if (ch < 0x10000)
	  {
	    mxml_error("Invalid UTF-8 sequence for character 0x%04x!", ch);
	    return (EOF);
	  }
	}
	else
	  return (EOF);
	break;

    case ENCODE_UTF16BE :
       /*
        * Read UTF-16 big-endian char...
	*/

	ch = (ch << 8) | getc(fp);

	if (mxml_bad_char(ch))
	{
	  mxml_error("Bad control character 0x%02x not allowed by XML standard!",
        	     ch);
	  return (EOF);
	}
        else if (ch >= 0xd800 && ch <= 0xdbff)
	{
	 /*
	  * Multi-word UTF-16 char...
	  */

          int lch = (getc(fp) << 8);
          lch |= getc(fp);

          if (lch < 0xdc00 || lch >= 0xdfff)
	    return (EOF);

          ch = (((ch & 0x3ff) << 10) | (lch & 0x3ff)) + 0x10000;
	}
	break;

    case ENCODE_UTF16LE :
       /*
        * Read UTF-16 little-endian char...
	*/

	ch |= (getc(fp) << 8);

        if (mxml_bad_char(ch))
	{
	  mxml_error("Bad control character 0x%02x not allowed by XML standard!",
        	     ch);
	  return (EOF);
	}
        else if (ch >= 0xd800 && ch <= 0xdbff)
	{
	 /*
	  * Multi-word UTF-16 char...
	  */

          int lch = getc(fp);
		  lch |= (getc(fp) << 8);

          if (lch < 0xdc00 || lch >= 0xdfff)
	    return (EOF);

          ch = (((ch & 0x3ff) << 10) | (lch & 0x3ff)) + 0x10000;
	}
	break;
  }

#if DEBUG > 1
  printf("mxml_file_getc: %c (0x%04x)\n", ch < ' ' ? '.' : ch, ch);
#endif /* DEBUG > 1 */

  return (ch);
}


/*
 * 'mxml_file_putc()' - Write a character to a file.
 */

static int				/* O - 0 on success, -1 on failure */
mxml_file_putc(int  ch,			/* I - Character to write */
               void *p)			/* I - Pointer to file */
{
  return (putc(ch, (FILE *)p) == EOF ? -1 : 0);
}


/*
 * 'mxml_get_entity()' - Get the character corresponding to an entity...
 */

static int				/* O  - Character value or EOF on error */
mxml_get_entity(mxml_node_t *parent,	/* I  - Parent node */
		void        *p,		/* I  - Pointer to source */
		int         *encoding,	/* IO - Character encoding */
                int         (*getc_cb)(void *, int *))
					/* I  - Get character function */
{
  int	ch;				/* Current character */
  char	entity[64],			/* Entity string */
	*entptr;			/* Pointer into entity */


  entptr = entity;

  while ((ch = (*getc_cb)(p, encoding)) != EOF)
    if (ch > 126 || (!isalnum(ch) && ch != '#'))
      break;
    else if (entptr < (entity + sizeof(entity) - 1))
      *entptr++ = ch;
    else
    {
      mxml_error("Entity name too long under parent <%s>!",
	         parent ? parent->value.element.name : "null");
      break;
    }

  *entptr = '\0';

  if (ch != ';')
  {
    mxml_error("Character entity \"%s\" not terminated under parent <%s>!",
	       entity, parent ? parent->value.element.name : "null");
    return (EOF);
  }

  if (entity[0] == '#')
  {
    if (entity[1] == 'x')
      ch = strtol(entity + 2, NULL, 16);
    else
      ch = strtol(entity + 1, NULL, 10);
  }
  else if ((ch = mxmlEntityGetValue(entity)) < 0)
    mxml_error("Entity name \"%s;\" not supported under parent <%s>!",
	       entity, parent ? parent->value.element.name : "null");

  if (mxml_bad_char(ch))
  {
    mxml_error("Bad control character 0x%02x under parent <%s> not allowed by XML standard!",
               ch, parent ? parent->value.element.name : "null");
    return (EOF);
  }

  return (ch);
}


/*
 * 'mxml_load_data()' - Load data into an XML node tree.
 */

static mxml_node_t *			/* O - First node or NULL if the file could not be read. */
mxml_load_data(
    mxml_node_t     *top,		/* I - Top node */
    void            *p,			/* I - Pointer to data */
    mxml_load_cb_t  cb,			/* I - Callback function or MXML_NO_CALLBACK */
    _mxml_getc_cb_t getc_cb,		/* I - Read function */
    mxml_sax_cb_t   sax_cb,		/* I - SAX callback or MXML_NO_CALLBACK */
    void            *sax_data)		/* I - SAX user data */
{
  mxml_node_t	*node,			/* Current node */
		*first,			/* First node added */
		*parent;		/* Current parent node */
  int		ch,			/* Character from file */
		whitespace;		/* Non-zero if whitespace seen */
  char		*buffer,		/* String buffer */
		*bufptr;		/* Pointer into buffer */
  int		bufsize;		/* Size of buffer */
  mxml_type_t	type;			/* Current node type */
  int		encoding;		/* Character encoding */
  _mxml_global_t *global = _mxml_global();
					/* Global data */
  static const char * const types[] =	/* Type strings... */
		{
		  "MXML_ELEMENT",	/* XML element with attributes */
		  "MXML_INTEGER",	/* Integer value */
		  "MXML_OPAQUE",	/* Opaque string */
		  "MXML_REAL",		/* Real value */
		  "MXML_TEXT",		/* Text fragment */
		  "MXML_CUSTOM"		/* Custom data */
		};


 /*
  * Read elements and other nodes from the file...
  */

  if ((buffer = malloc(64)) == NULL)
  {
    mxml_error("Unable to allocate string buffer!");
    return (NULL);
  }

  bufsize    = 64;
  bufptr     = buffer;
  parent     = top;
  first      = NULL;
  whitespace = 0;
  encoding   = ENCODE_UTF8;

  if (cb && parent)
    type = (*cb)(parent);
  else
    type = MXML_TEXT;

  while ((ch = (*getc_cb)(p, &encoding)) != EOF)
  {
    if ((ch == '<' ||
         (mxml_isspace(ch) && type != MXML_OPAQUE && type != MXML_CUSTOM)) &&
        bufptr > buffer)
    {
     /*
      * Add a new value node...
      */

      *bufptr = '\0';

      switch (type)
      {
	case MXML_INTEGER :
            node = mxmlNewInteger(parent, strtol(buffer, &bufptr, 0));
	    break;

	case MXML_OPAQUE :
            node = mxmlNewOpaque(parent, buffer);
	    break;

	case MXML_REAL :
            node = mxmlNewReal(parent, strtod(buffer, &bufptr));
	    break;

	case MXML_TEXT :
            node = mxmlNewText(parent, whitespace, buffer);
	    break;

	case MXML_CUSTOM :
	    if (global->custom_load_cb)
	    {
	     /*
	      * Use the callback to fill in the custom data...
	      */

              node = mxmlNewCustom(parent, NULL, NULL);

	      if ((*global->custom_load_cb)(node, buffer))
	      {
	        mxml_error("Bad custom value '%s' in parent <%s>!",
		           buffer, parent ? parent->value.element.name : "null");
		mxmlDelete(node);
		node = NULL;
	      }
	      break;
	    }

        default : /* Ignore... */
	    node = NULL;
	    break;
      }	  

      if (*bufptr)
      {
       /*
        * Bad integer/real number value...
	*/

        mxml_error("Bad %s value '%s' in parent <%s>!",
	           type == MXML_INTEGER ? "integer" : "real", buffer,
		   parent ? parent->value.element.name : "null");
	break;
      }

      bufptr     = buffer;
      whitespace = mxml_isspace(ch) && type == MXML_TEXT;

      if (!node && type != MXML_IGNORE)
      {
       /*
	* Print error and return...
	*/

	mxml_error("Unable to add value node of type %s to parent <%s>!",
	           types[type], parent ? parent->value.element.name : "null");
	goto error;
      }

      if (sax_cb)
      {
        (*sax_cb)(node, MXML_SAX_DATA, sax_data);

        if (!mxmlRelease(node))
          node = NULL;
      }

      if (!first && node)
        first = node;
    }
    else if (mxml_isspace(ch) && type == MXML_TEXT)
      whitespace = 1;

   /*
    * Add lone whitespace node if we have an element and existing
    * whitespace...
    */

    if (ch == '<' && whitespace && type == MXML_TEXT)
    {
      if (parent)
      {
	node = mxmlNewText(parent, whitespace, "");

	if (sax_cb)
	{
	  (*sax_cb)(node, MXML_SAX_DATA, sax_data);

	  if (!mxmlRelease(node))
	    node = NULL;
	}

	if (!first && node)
	  first = node;
      }

      whitespace = 0;
    }

    if (ch == '<')
    {
     /*
      * Start of open/close tag...
      */

      bufptr = buffer;

      while ((ch = (*getc_cb)(p, &encoding)) != EOF)
        if (mxml_isspace(ch) || ch == '>' || (ch == '/' && bufptr > buffer))
	  break;
	else if (ch == '<')
	{
	  mxml_error("Bare < in element!");
	  goto error;
	}
	else if (ch == '&')
	{
	  if ((ch = mxml_get_entity(parent, p, &encoding, getc_cb)) == EOF)
	    goto error;

	  if (mxml_add_char(ch, &bufptr, &buffer, &bufsize))
	    goto error;
	}
	else if (mxml_add_char(ch, &bufptr, &buffer, &bufsize))
	  goto error;
	else if (((bufptr - buffer) == 1 && buffer[0] == '?') ||
	         ((bufptr - buffer) == 3 && !strncmp(buffer, "!--", 3)) ||
	         ((bufptr - buffer) == 8 && !strncmp(buffer, "![CDATA[", 8)))
	  break;

      *bufptr = '\0';

      if (!strcmp(buffer, "!--"))
      {
       /*
        * Gather rest of comment...
	*/

	while ((ch = (*getc_cb)(p, &encoding)) != EOF)
	{
	  if (ch == '>' && bufptr > (buffer + 4) &&
	      bufptr[-3] != '-' && bufptr[-2] == '-' && bufptr[-1] == '-')
	    break;
	  else if (mxml_add_char(ch, &bufptr, &buffer, &bufsize))
	    goto error;
	}

       /*
        * Error out if we didn't get the whole comment...
	*/

        if (ch != '>')
	{
	 /*
	  * Print error and return...
	  */

	  mxml_error("Early EOF in comment node!");
	  goto error;
	}


       /*
        * Otherwise add this as an element under the current parent...
	*/

	*bufptr = '\0';

        if (!parent && first)
	{
	 /*
	  * There can only be one root element!
	  */

	  mxml_error("<%s> cannot be a second root node after <%s>", 
	             buffer, first->value.element.name);
          goto error; 		     
	}

	if ((node = mxmlNewElement(parent, buffer)) == NULL)
	{
	 /*
	  * Just print error for now...
	  */

	  mxml_error("Unable to add comment node to parent <%s>!",
	             parent ? parent->value.element.name : "null");
	  break;
	}

        if (sax_cb)
        {
          (*sax_cb)(node, MXML_SAX_COMMENT, sax_data);

          if (!mxmlRelease(node))
            node = NULL;
        }

	if (node && !first)
	  first = node;
      }
      else if (!strcmp(buffer, "![CDATA["))
      {
       /*
        * Gather CDATA section...
	*/

	while ((ch = (*getc_cb)(p, &encoding)) != EOF)
	{
	  if (ch == '>' && !strncmp(bufptr - 2, "]]", 2))
	    break;
	  else if (mxml_add_char(ch, &bufptr, &buffer, &bufsize))
	    goto error;
	}

       /*
        * Error out if we didn't get the whole comment...
	*/

        if (ch != '>')
	{
	 /*
	  * Print error and return...
	  */

	  mxml_error("Early EOF in CDATA node!");
	  goto error;
	}


       /*
        * Otherwise add this as an element under the current parent...
	*/

	*bufptr = '\0';

        if (!parent && first)
	{
	 /*
	  * There can only be one root element!
	  */

	  mxml_error("<%s> cannot be a second root node after <%s>", 
	             buffer, first->value.element.name);
          goto error; 		     
	}

	if ((node = mxmlNewElement(parent, buffer)) == NULL)
	{
	 /*
	  * Print error and return...
	  */

	  mxml_error("Unable to add CDATA node to parent <%s>!",
	             parent ? parent->value.element.name : "null");
	  goto error;
	}

        if (sax_cb)
        {
          (*sax_cb)(node, MXML_SAX_CDATA, sax_data);

          if (!mxmlRelease(node))
            node = NULL;
        }

	if (node && !first)
	  first = node;
      }
      else if (buffer[0] == '?')
      {
       /*
        * Gather rest of processing instruction...
	*/

	while ((ch = (*getc_cb)(p, &encoding)) != EOF)
	{
	  if (ch == '>' && bufptr > buffer && bufptr[-1] == '?')
	    break;
	  else if (mxml_add_char(ch, &bufptr, &buffer, &bufsize))
	    goto error;
	}

       /*
        * Error out if we didn't get the whole processing instruction...
	*/

        if (ch != '>')
	{
	 /*
	  * Print error and return...
	  */

	  mxml_error("Early EOF in processing instruction node!");
	  goto error;
	}

       /*
        * Otherwise add this as an element under the current parent...
	*/

	*bufptr = '\0';

        if (!parent && first)
	{
	 /*
	  * There can only be one root element!
	  */

	  mxml_error("<%s> cannot be a second root node after <%s>", 
	             buffer, first->value.element.name);
          goto error; 		     
	}

	if ((node = mxmlNewElement(parent, buffer)) == NULL)
	{
	 /*
	  * Print error and return...
	  */

	  mxml_error("Unable to add processing instruction node to parent <%s>!",
	             parent ? parent->value.element.name : "null");
	  goto error;
	}

        if (sax_cb)
        {
          (*sax_cb)(node, MXML_SAX_DIRECTIVE, sax_data);

          if (!mxmlRelease(node))
            node = NULL;
        }

        if (node)
	{
	  if (!first)
            first = node;

	  if (!parent)
	  {
	    parent = node;

	    if (cb)
	      type = (*cb)(parent);
	  }
	}
      }
      else if (buffer[0] == '!')
      {
       /*
        * Gather rest of declaration...
	*/

	do
	{
	  if (ch == '>')
	    break;
	  else
	  {
            if (ch == '&')
	      if ((ch = mxml_get_entity(parent, p, &encoding, getc_cb)) == EOF)
		goto error;

	    if (mxml_add_char(ch, &bufptr, &buffer, &bufsize))
	      goto error;
	  }
	}
        while ((ch = (*getc_cb)(p, &encoding)) != EOF);

       /*
        * Error out if we didn't get the whole declaration...
	*/

        if (ch != '>')
	{
	 /*
	  * Print error and return...
	  */

	  mxml_error("Early EOF in declaration node!");
	  goto error;
	}

       /*
        * Otherwise add this as an element under the current parent...
	*/

	*bufptr = '\0';

        if (!parent && first)
	{
	 /*
	  * There can only be one root element!
	  */

	  mxml_error("<%s> cannot be a second root node after <%s>", 
	             buffer, first->value.element.name);
          goto error; 		     
	}

	if ((node = mxmlNewElement(parent, buffer)) == NULL)
	{
	 /*
	  * Print error and return...
	  */

	  mxml_error("Unable to add declaration node to parent <%s>!",
	             parent ? parent->value.element.name : "null");
	  goto error;
	}

        if (sax_cb)
        {
          (*sax_cb)(node, MXML_SAX_DIRECTIVE, sax_data);

          if (!mxmlRelease(node))
            node = NULL;
        }

        if (node)
	{
	  if (!first)
            first = node;

	  if (!parent)
	  {
	    parent = node;

	    if (cb)
	      type = (*cb)(parent);
	  }
	}
      }
      else if (buffer[0] == '/')
      {
       /*
        * Handle close tag...
	*/

        if (!parent || strcmp(buffer + 1, parent->value.element.name))
	{
	 /*
	  * Close tag doesn't match tree; print an error for now...
	  */

	  mxml_error("Mismatched close tag <%s> under parent <%s>!",
	             buffer, parent ? parent->value.element.name : "(null)");
          goto error;
	}

       /*
        * Keep reading until we see >...
	*/

        while (ch != '>' && ch != EOF)
	  ch = (*getc_cb)(p, &encoding);

        node   = parent;
        parent = parent->parent;

        if (sax_cb)
        {
          (*sax_cb)(node, MXML_SAX_ELEMENT_CLOSE, sax_data);

          if (!mxmlRelease(node) && first == node)
	    first = NULL;
        }

       /*
	* Ascend into the parent and set the value type as needed...
	*/

	if (cb && parent)
	  type = (*cb)(parent);
      }
      else
      {
       /*
        * Handle open tag...
	*/

        if (!parent && first)
	{
	 /*
	  * There can only be one root element!
	  */

	  mxml_error("<%s> cannot be a second root node after <%s>", 
	             buffer, first->value.element.name);
          goto error; 		     
	}

        if ((node = mxmlNewElement(parent, buffer)) == NULL)
	{
	 /*
	  * Just print error for now...
	  */

	  mxml_error("Unable to add element node to parent <%s>!",
	             parent ? parent->value.element.name : "null");
	  goto error;
	}

        if (mxml_isspace(ch))
        {
	  if ((ch = mxml_parse_element(node, p, &encoding, getc_cb)) == EOF)
	    goto error;
        }
        else if (ch == '/')
	{
	  if ((ch = (*getc_cb)(p, &encoding)) != '>')
	  {
	    mxml_error("Expected > but got '%c' instead for element <%s/>!",
	               ch, buffer);
            mxmlDelete(node);
            goto error;
	  }

	  ch = '/';
	}

        if (sax_cb)
          (*sax_cb)(node, MXML_SAX_ELEMENT_OPEN, sax_data);

        if (!first)
	  first = node;

	if (ch == EOF)
	  break;

        if (ch != '/')
	{
	 /*
	  * Descend into this node, setting the value type as needed...
	  */

	  parent = node;

	  if (cb && parent)
	    type = (*cb)(parent);
	}
        else if (sax_cb)
        {
          (*sax_cb)(node, MXML_SAX_ELEMENT_CLOSE, sax_data);

          if (!mxmlRelease(node) && first == node)
            first = NULL;
        }
      }

      bufptr  = buffer;
    }
    else if (ch == '&')
    {
     /*
      * Add character entity to current buffer...
      */

      if ((ch = mxml_get_entity(parent, p, &encoding, getc_cb)) == EOF)
	goto error;

      if (mxml_add_char(ch, &bufptr, &buffer, &bufsize))
	goto error;
    }
    else if (type == MXML_OPAQUE || type == MXML_CUSTOM || !mxml_isspace(ch))
    {
     /*
      * Add character to current buffer...
      */

      if (mxml_add_char(ch, &bufptr, &buffer, &bufsize))
	goto error;
    }
  }

 /*
  * Free the string buffer - we don't need it anymore...
  */

  free(buffer);

 /*
  * Find the top element and return it...
  */

  if (parent)
  {
    node = parent;

    while (parent->parent != top && parent->parent)
      parent = parent->parent;

    if (node != parent)
    {
      mxml_error("Missing close tag </%s> under parent <%s>!",
	         node->value.element.name,
		 node->parent ? node->parent->value.element.name : "(null)");

      mxmlDelete(first);

      return (NULL);
    }
  }

  if (parent)
    return (parent);
  else
    return (first);

 /*
  * Common error return...
  */

error:

  mxmlDelete(first);

  free(buffer);

  return (NULL);
}


/*
 * 'mxml_parse_element()' - Parse an element for any attributes...
 */

static int				/* O  - Terminating character */
mxml_parse_element(
    mxml_node_t     *node,		/* I  - Element node */
    void            *p,			/* I  - Data to read from */
    int             *encoding,		/* IO - Encoding */
    _mxml_getc_cb_t getc_cb)		/* I  - Data callback */
{
  int	ch,				/* Current character in file */
	quote;				/* Quoting character */
  char	*name,				/* Attribute name */
	*value,				/* Attribute value */
	*ptr;				/* Pointer into name/value */
  int	namesize,			/* Size of name string */
	valsize;			/* Size of value string */


 /*
  * Initialize the name and value buffers...
  */

  if ((name = malloc(64)) == NULL)
  {
    mxml_error("Unable to allocate memory for name!");
    return (EOF);
  }

  namesize = 64;

  if ((value = malloc(64)) == NULL)
  {
    free(name);
    mxml_error("Unable to allocate memory for value!");
    return (EOF);
  }

  valsize = 64;

 /*
  * Loop until we hit a >, /, ?, or EOF...
  */

  while ((ch = (*getc_cb)(p, encoding)) != EOF)
  {
#if DEBUG > 1
    fprintf(stderr, "parse_element: ch='%c'\n", ch);
#endif /* DEBUG > 1 */

   /*
    * Skip leading whitespace...
    */

    if (mxml_isspace(ch))
      continue;

   /*
    * Stop at /, ?, or >...
    */

    if (ch == '/' || ch == '?')
    {
     /*
      * Grab the > character and print an error if it isn't there...
      */

      quote = (*getc_cb)(p, encoding);

      if (quote != '>')
      {
        mxml_error("Expected '>' after '%c' for element %s, but got '%c'!",
	           ch, node->value.element.name, quote);
        goto error;
      }

      break;
    }
    else if (ch == '<')
    {
      mxml_error("Bare < in element %s!", node->value.element.name);
      goto error;
    }
    else if (ch == '>')
      break;

   /*
    * Read the attribute name...
    */

    name[0] = ch;
    ptr     = name + 1;

    if (ch == '\"' || ch == '\'')
    {
     /*
      * Name is in quotes, so get a quoted string...
      */

      quote = ch;

      while ((ch = (*getc_cb)(p, encoding)) != EOF)
      {
        if (ch == '&')
	  if ((ch = mxml_get_entity(node, p, encoding, getc_cb)) == EOF)
	    goto error;

	if (mxml_add_char(ch, &ptr, &name, &namesize))
	  goto error;

	if (ch == quote)
          break;
      }
    }
    else
    {
     /*
      * Grab an normal, non-quoted name...
      */

      while ((ch = (*getc_cb)(p, encoding)) != EOF)
	if (mxml_isspace(ch) || ch == '=' || ch == '/' || ch == '>' ||
	    ch == '?')
          break;
	else
	{
          if (ch == '&')
	    if ((ch = mxml_get_entity(node, p, encoding, getc_cb)) == EOF)
	      goto error;

	  if (mxml_add_char(ch, &ptr, &name, &namesize))
	    goto error;
	}
    }

    *ptr = '\0';

    if (mxmlElementGetAttr(node, name))
      goto error;

    while (ch != EOF && mxml_isspace(ch))
      ch = (*getc_cb)(p, encoding);

    if (ch == '=')
    {
     /*
      * Read the attribute value...
      */

      while ((ch = (*getc_cb)(p, encoding)) != EOF && mxml_isspace(ch));

      if (ch == EOF)
      {
        mxml_error("Missing value for attribute '%s' in element %s!",
	           name, node->value.element.name);
        goto error;
      }

      if (ch == '\'' || ch == '\"')
      {
       /*
        * Read quoted value...
	*/

        quote = ch;
	ptr   = value;

        while ((ch = (*getc_cb)(p, encoding)) != EOF)
	  if (ch == quote)
	    break;
	  else
	  {
	    if (ch == '&')
	      if ((ch = mxml_get_entity(node, p, encoding, getc_cb)) == EOF)
	        goto error;
	      
	    if (mxml_add_char(ch, &ptr, &value, &valsize))
	      goto error;
	  }

        *ptr = '\0';
      }
      else
      {
       /*
        * Read unquoted value...
	*/

	value[0] = ch;
	ptr      = value + 1;

	while ((ch = (*getc_cb)(p, encoding)) != EOF)
	  if (mxml_isspace(ch) || ch == '=' || ch == '/' || ch == '>')
            break;
	  else
	  {
	    if (ch == '&')
	      if ((ch = mxml_get_entity(node, p, encoding, getc_cb)) == EOF)
	        goto error;
	      
	    if (mxml_add_char(ch, &ptr, &value, &valsize))
	      goto error;
	  }

        *ptr = '\0';
      }

     /*
      * Set the attribute with the given string value...
      */

      mxmlElementSetAttr(node, name, value);
    }
    else
    {
      mxml_error("Missing value for attribute '%s' in element %s!",
	         name, node->value.element.name);
      goto error;
    }

   /*
    * Check the end character...
    */

    if (ch == '/' || ch == '?')
    {
     /*
      * Grab the > character and print an error if it isn't there...
      */

      quote = (*getc_cb)(p, encoding);

      if (quote != '>')
      {
        mxml_error("Expected '>' after '%c' for element %s, but got '%c'!",
	           ch, node->value.element.name, quote);
        ch = EOF;
      }

      break;
    }
    else if (ch == '>')
      break;
  }

 /*
  * Free the name and value buffers and return...
  */

  free(name);
  free(value);

  return (ch);

 /*
  * Common error return point...
  */

error:

  free(name);
  free(value);

  return (EOF);
}


/*
 * 'mxml_string_getc()' - Get a character from a string.
 */

static int				/* O  - Character or EOF */
mxml_string_getc(void *p,		/* I  - Pointer to file */
                 int  *encoding)	/* IO - Encoding */
{
  int		ch;			/* Character */
  const char	**s;			/* Pointer to string pointer */


  s = (const char **)p;

  if ((ch = (*s)[0] & 255) != 0 || *encoding == ENCODE_UTF16LE)
  {
   /*
    * Got character; convert UTF-8 to integer and return...
    */

    (*s)++;

    switch (*encoding)
    {
      case ENCODE_UTF8 :
	  if (!(ch & 0x80))
	  {
#if DEBUG > 1
            printf("mxml_string_getc: %c (0x%04x)\n", ch < ' ' ? '.' : ch, ch);
#endif /* DEBUG > 1 */

	    if (mxml_bad_char(ch))
	    {
	      mxml_error("Bad control character 0x%02x not allowed by XML standard!",
        		 ch);
	      return (EOF);
	    }

	    return (ch);
          }
	  else if (ch == 0xfe)
	  {
	   /*
	    * UTF-16 big-endian BOM?
	    */

            if (((*s)[0] & 255) != 0xff)
	      return (EOF);

	    *encoding = ENCODE_UTF16BE;
	    (*s)++;

	    return (mxml_string_getc(p, encoding));
	  }
	  else if (ch == 0xff)
	  {
	   /*
	    * UTF-16 little-endian BOM?
	    */

            if (((*s)[0] & 255) != 0xfe)
	      return (EOF);

	    *encoding = ENCODE_UTF16LE;
	    (*s)++;

	    return (mxml_string_getc(p, encoding));
	  }
	  else if ((ch & 0xe0) == 0xc0)
	  {
	   /*
	    * Two-byte value...
	    */

	    if (((*s)[0] & 0xc0) != 0x80)
              return (EOF);

	    ch = ((ch & 0x1f) << 6) | ((*s)[0] & 0x3f);

	    (*s)++;

	    if (ch < 0x80)
	    {
	      mxml_error("Invalid UTF-8 sequence for character 0x%04x!", ch);
	      return (EOF);
	    }

#if DEBUG > 1
            printf("mxml_string_getc: %c (0x%04x)\n", ch < ' ' ? '.' : ch, ch);
#endif /* DEBUG > 1 */

	    return (ch);
	  }
	  else if ((ch & 0xf0) == 0xe0)
	  {
	   /*
	    * Three-byte value...
	    */

	    if (((*s)[0] & 0xc0) != 0x80 ||
        	((*s)[1] & 0xc0) != 0x80)
              return (EOF);

	    ch = ((((ch & 0x0f) << 6) | ((*s)[0] & 0x3f)) << 6) | ((*s)[1] & 0x3f);

	    (*s) += 2;

	    if (ch < 0x800)
	    {
	      mxml_error("Invalid UTF-8 sequence for character 0x%04x!", ch);
	      return (EOF);
	    }

	   /*
	    * Ignore (strip) Byte Order Mark (BOM)...
	    */

	    if (ch == 0xfeff)
	      return (mxml_string_getc(p, encoding));

#if DEBUG > 1
            printf("mxml_string_getc: %c (0x%04x)\n", ch < ' ' ? '.' : ch, ch);
#endif /* DEBUG > 1 */

	    return (ch);
	  }
	  else if ((ch & 0xf8) == 0xf0)
	  {
	   /*
	    * Four-byte value...
	    */

	    if (((*s)[0] & 0xc0) != 0x80 ||
        	((*s)[1] & 0xc0) != 0x80 ||
        	((*s)[2] & 0xc0) != 0x80)
              return (EOF);

	    ch = ((((((ch & 0x07) << 6) | ((*s)[0] & 0x3f)) << 6) |
        	   ((*s)[1] & 0x3f)) << 6) | ((*s)[2] & 0x3f);

	    (*s) += 3;

	    if (ch < 0x10000)
	    {
	      mxml_error("Invalid UTF-8 sequence for character 0x%04x!", ch);
	      return (EOF);
	    }

#if DEBUG > 1
            printf("mxml_string_getc: %c (0x%04x)\n", ch < ' ' ? '.' : ch, ch);
#endif /* DEBUG > 1 */

	    return (ch);
	  }
	  else
	    return (EOF);

      case ENCODE_UTF16BE :
	 /*
          * Read UTF-16 big-endian char...
	  */

	  ch = (ch << 8) | ((*s)[0] & 255);
	  (*s) ++;

          if (mxml_bad_char(ch))
	  {
	    mxml_error("Bad control character 0x%02x not allowed by XML standard!",
        	       ch);
	    return (EOF);
	  }
          else if (ch >= 0xd800 && ch <= 0xdbff)
	  {
	   /*
	    * Multi-word UTF-16 char...
	    */

            int lch;			/* Lower word */


            if (!(*s)[0])
	      return (EOF);

            lch = (((*s)[0] & 255) << 8) | ((*s)[1] & 255);
	    (*s) += 2;

            if (lch < 0xdc00 || lch >= 0xdfff)
	      return (EOF);

            ch = (((ch & 0x3ff) << 10) | (lch & 0x3ff)) + 0x10000;
	  }

#if DEBUG > 1
          printf("mxml_string_getc: %c (0x%04x)\n", ch < ' ' ? '.' : ch, ch);
#endif /* DEBUG > 1 */

	  return (ch);

      case ENCODE_UTF16LE :
	 /*
          * Read UTF-16 little-endian char...
	  */

	  ch = ch | (((*s)[0] & 255) << 8);

	  if (!ch)
	  {
	    (*s) --;
	    return (EOF);
	  }

	  (*s) ++;

          if (mxml_bad_char(ch))
	  {
	    mxml_error("Bad control character 0x%02x not allowed by XML standard!",
        	       ch);
	    return (EOF);
	  }
          else if (ch >= 0xd800 && ch <= 0xdbff)
	  {
	   /*
	    * Multi-word UTF-16 char...
	    */

            int lch;			/* Lower word */


            if (!(*s)[1])
	      return (EOF);

            lch = (((*s)[1] & 255) << 8) | ((*s)[0] & 255);
	    (*s) += 2;

            if (lch < 0xdc00 || lch >= 0xdfff)
	      return (EOF);

            ch = (((ch & 0x3ff) << 10) | (lch & 0x3ff)) + 0x10000;
	  }

#if DEBUG > 1
          printf("mxml_string_getc: %c (0x%04x)\n", ch < ' ' ? '.' : ch, ch);
#endif /* DEBUG > 1 */

	  return (ch);
    }
  }

  return (EOF);
}


/*
 * 'mxml_string_putc()' - Write a character to a string.
 */

static int				/* O - 0 on success, -1 on failure */
mxml_string_putc(int  ch,		/* I - Character to write */
                 void *p)		/* I - Pointer to string pointers */
{
  char	**pp;				/* Pointer to string pointers */


  pp = (char **)p;

  if (pp[0] < pp[1])
    pp[0][0] = ch;

  pp[0] ++;

  return (0);
}


/*
 * 'mxml_write_name()' - Write a name string.
 */

static int				/* O - 0 on success, -1 on failure */
mxml_write_name(const char *s,		/* I - Name to write */
                void       *p,		/* I - Write pointer */
		int        (*putc_cb)(int, void *))
					/* I - Write callback */
{
  char		quote;			/* Quote character */
  const char	*name;			/* Entity name */


  if (*s == '\"' || *s == '\'')
  {
   /*
    * Write a quoted name string...
    */

    if ((*putc_cb)(*s, p) < 0)
      return (-1);

    quote = *s++;

    while (*s && *s != quote)
    {
      if ((name = mxmlEntityGetName(*s)) != NULL)
      {
	if ((*putc_cb)('&', p) < 0)
          return (-1);

        while (*name)
	{
	  if ((*putc_cb)(*name, p) < 0)
            return (-1);

          name ++;
	}

	if ((*putc_cb)(';', p) < 0)
          return (-1);
      }
      else if ((*putc_cb)(*s, p) < 0)
	return (-1);

      s ++;
    }

   /*
    * Write the end quote...
    */

    if ((*putc_cb)(quote, p) < 0)
      return (-1);
  }
  else
  {
   /*
    * Write a non-quoted name string...
    */

    while (*s)
    {
      if ((*putc_cb)(*s, p) < 0)
	return (-1);

      s ++;
    }
  }

  return (0);
}


/*
 * 'mxml_write_node()' - Save an XML node to a file.
 */

static int				/* O - Column or -1 on error */
mxml_write_node(mxml_node_t     *node,	/* I - Node to write */
                void            *p,	/* I - File to write to */
	        mxml_save_cb_t  cb,	/* I - Whitespace callback */
		int             col,	/* I - Current column */
		_mxml_putc_cb_t putc_cb,/* I - Output callback */
		_mxml_global_t  *global)/* I - Global data */
{
  int		i,			/* Looping var */
		width;			/* Width of attr + value */
  mxml_attr_t	*attr;			/* Current attribute */
  char		s[255];			/* Temporary string */


 /*
  * Print the node value...
  */

  switch (node->type)
  {
    case MXML_ELEMENT :
	col = mxml_write_ws(node, p, cb, MXML_WS_BEFORE_OPEN, col, putc_cb);

	if ((*putc_cb)('<', p) < 0)
	  return (-1);
	if (node->value.element.name[0] == '?' ||
	    !strncmp(node->value.element.name, "!--", 3) ||
	    !strncmp(node->value.element.name, "![CDATA[", 8))
	{
	 /*
	  * Comments, CDATA, and processing instructions do not
	  * use character entities.
	  */

	  const char	*ptr;		/* Pointer into name */


	  for (ptr = node->value.element.name; *ptr; ptr ++)
	    if ((*putc_cb)(*ptr, p) < 0)
	      return (-1);
	}
	else if (mxml_write_name(node->value.element.name, p, putc_cb) < 0)
	  return (-1);

	col += strlen(node->value.element.name) + 1;

	for (i = node->value.element.num_attrs, attr = node->value.element.attrs;
	     i > 0;
	     i --, attr ++)
	{
	  width = strlen(attr->name);

	  if (attr->value)
	    width += strlen(attr->value) + 3;

	  if (global->wrap > 0 && (col + width) > global->wrap)
	  {
	    if ((*putc_cb)('\n', p) < 0)
	      return (-1);

	    col = 0;
	  }
	  else
	  {
	    if ((*putc_cb)(' ', p) < 0)
	      return (-1);

	    col ++;
	  }

	  if (mxml_write_name(attr->name, p, putc_cb) < 0)
	    return (-1);

	  if (attr->value)
	  {
	    if ((*putc_cb)('=', p) < 0)
	      return (-1);
	    if ((*putc_cb)('\"', p) < 0)
	      return (-1);
	    if (mxml_write_string(attr->value, p, putc_cb) < 0)
	      return (-1);
	    if ((*putc_cb)('\"', p) < 0)
	      return (-1);
	  }

	  col += width;
	}

	if (node->child)
	{
	 /*
	  * Write children...
	  */

	  mxml_node_t *child;		/* Current child */


	  if ((*putc_cb)('>', p) < 0)
	    return (-1);
	  else
	    col ++;

	  col = mxml_write_ws(node, p, cb, MXML_WS_AFTER_OPEN, col, putc_cb);

          for (child = node->child; child; child = child->next)
	  {
	    if ((col = mxml_write_node(child, p, cb, col, putc_cb, global)) < 0)
	      return (-1);
	  }

	 /*
	  * The ? and ! elements are special-cases and have no end tags...
	  */

	  if (node->value.element.name[0] != '!' &&
	      node->value.element.name[0] != '?')
	  {
	    col = mxml_write_ws(node, p, cb, MXML_WS_BEFORE_CLOSE, col, putc_cb);

	    if ((*putc_cb)('<', p) < 0)
	      return (-1);
	    if ((*putc_cb)('/', p) < 0)
	      return (-1);
	    if (mxml_write_string(node->value.element.name, p, putc_cb) < 0)
	      return (-1);
	    if ((*putc_cb)('>', p) < 0)
	      return (-1);

	    col += strlen(node->value.element.name) + 3;

	    col = mxml_write_ws(node, p, cb, MXML_WS_AFTER_CLOSE, col, putc_cb);
	  }
	}
	else if (node->value.element.name[0] == '!' ||
		 node->value.element.name[0] == '?')
	{
	 /*
	  * The ? and ! elements are special-cases...
	  */

	  if ((*putc_cb)('>', p) < 0)
	    return (-1);
	  else
	    col ++;

	  col = mxml_write_ws(node, p, cb, MXML_WS_AFTER_OPEN, col, putc_cb);
	}
	else
	{
	  if ((*putc_cb)(' ', p) < 0)
	    return (-1);
	  if ((*putc_cb)('/', p) < 0)
	    return (-1);
	  if ((*putc_cb)('>', p) < 0)
	    return (-1);

	  col += 3;

	  col = mxml_write_ws(node, p, cb, MXML_WS_AFTER_OPEN, col, putc_cb);
	}
	break;

    case MXML_INTEGER :
	if (node->prev)
	{
	  if (global->wrap > 0 && col > global->wrap)
	  {
	    if ((*putc_cb)('\n', p) < 0)
	      return (-1);

	    col = 0;
	  }
	  else if ((*putc_cb)(' ', p) < 0)
	    return (-1);
	  else
	    col ++;
	}

	sprintf(s, "%d", node->value.integer);
	if (mxml_write_string(s, p, putc_cb) < 0)
	  return (-1);

	col += strlen(s);
	break;

    case MXML_OPAQUE :
	if (mxml_write_string(node->value.opaque, p, putc_cb) < 0)
	  return (-1);

	col += strlen(node->value.opaque);
	break;

    case MXML_REAL :
	if (node->prev)
	{
	  if (global->wrap > 0 && col > global->wrap)
	  {
	    if ((*putc_cb)('\n', p) < 0)
	      return (-1);

	    col = 0;
	  }
	  else if ((*putc_cb)(' ', p) < 0)
	    return (-1);
	  else
	    col ++;
	}

	sprintf(s, "%f", node->value.real);
	if (mxml_write_string(s, p, putc_cb) < 0)
	  return (-1);

	col += strlen(s);
	break;

    case MXML_TEXT :
	if (node->value.text.whitespace && col > 0)
	{
	  if (global->wrap > 0 && col > global->wrap)
	  {
	    if ((*putc_cb)('\n', p) < 0)
	      return (-1);

	    col = 0;
	  }
	  else if ((*putc_cb)(' ', p) < 0)
	    return (-1);
	  else
	    col ++;
	}

	if (mxml_write_string(node->value.text.string, p, putc_cb) < 0)
	  return (-1);

	col += strlen(node->value.text.string);
	break;

    case MXML_CUSTOM :
	if (global->custom_save_cb)
	{
	  char	*data;		/* Custom data string */
	  const char	*newline;	/* Last newline in string */


	  if ((data = (*global->custom_save_cb)(node)) == NULL)
	    return (-1);

	  if (mxml_write_string(data, p, putc_cb) < 0)
	    return (-1);

	  if ((newline = strrchr(data, '\n')) == NULL)
	    col += strlen(data);
	  else
	    col = strlen(newline);

	  free(data);
	  break;
	}

    default : /* Should never happen */
	return (-1);
  }

  return (col);
}


/*
 * 'mxml_write_string()' - Write a string, escaping & and < as needed.
 */

static int				/* O - 0 on success, -1 on failure */
mxml_write_string(
    const char      *s,			/* I - String to write */
    void            *p,			/* I - Write pointer */
    _mxml_putc_cb_t putc_cb)		/* I - Write callback */
{
  const char	*name;			/* Entity name, if any */


  while (*s)
  {
    if ((name = mxmlEntityGetName(*s)) != NULL)
    {
      if ((*putc_cb)('&', p) < 0)
        return (-1);

      while (*name)
      {
	if ((*putc_cb)(*name, p) < 0)
          return (-1);
        name ++;
      }

      if ((*putc_cb)(';', p) < 0)
        return (-1);
    }
    else if ((*putc_cb)(*s, p) < 0)
      return (-1);

    s ++;
  }

  return (0);
}


/*
 * 'mxml_write_ws()' - Do whitespace callback...
 */

static int				/* O - New column */
mxml_write_ws(mxml_node_t     *node,	/* I - Current node */
              void            *p,	/* I - Write pointer */
              mxml_save_cb_t  cb,	/* I - Callback function */
	      int             ws,	/* I - Where value */
	      int             col,	/* I - Current column */
              _mxml_putc_cb_t putc_cb)	/* I - Write callback */
{
  const char	*s;			/* Whitespace string */


  if (cb && (s = (*cb)(node, ws)) != NULL)
  {
    while (*s)
    {
      if ((*putc_cb)(*s, p) < 0)
	return (-1);
      else if (*s == '\n')
	col = 0;
      else if (*s == '\t')
      {
	col += MXML_TAB;
	col = col - (col % MXML_TAB);
      }
      else
	col ++;

      s ++;
    }
  }

  return (col);
}


/*
 * End of "$Id: mxml-file.c 438 2011-03-24 05:47:51Z mike $".
 */
