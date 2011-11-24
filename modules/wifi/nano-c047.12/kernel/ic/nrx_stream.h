/* Copyright (C) 2007 Nanoradio AB */
/* $Id: nrx_stream.h 9119 2008-06-09 11:50:52Z joda $ */

#ifndef __nrx_stream_h__
#define __nrx_stream_h__

struct nrx_stream;

/*!
  * @brief Closes a previously opened stream.
  *
  * @param stream References the stream.
  *
  * @return Zero on success, or a negative errno number.
  */
int
nrx_stream_close (struct nrx_stream *stream);

/*!
  * @brief Flushes written data to medium.
  *
  * @param stream References the stream.
  *
  * @return Zero on success, or a negative errno number.
  */
int
nrx_stream_flush (struct nrx_stream *stream);

/*!
  * @brief Seeks to a specified position on a stream.
  *
  * @param stream References the stream.
  * @param offset Offset to seek to relative to position given by whence.
  * @param whence Position to seek from, 
  *   0 = start of stream, 
  *   1 = current position, 
  *   2 = end of stream.
  *
  * @return Position in stream, or a negative errno number.
  */
loff_t
nrx_stream_lseek (
	struct nrx_stream *stream,
	loff_t offset,
	int whence);

/*!
  * @brief Opens a fixed size buffer as a stream.
  *
  * @param buf Points to the buffer.
  * @param len Size of buf in bytes.
  * @param stream Returns a stream object.
  *
  * @return Zero on success, or -ENOMEM if there was a memory error.
  */
int
nrx_stream_open_buf (
	void *buf,
	size_t len,
	struct nrx_stream **stream);

/*!
  * @brief Opens a file as a stream.
  *
  * @param filename The path to the file to open.
  * @param flags Flags to pass to open (O_RDONLY etc).
  * @param mode Mode of the file to open, only meaningful 
  *             if flags include O_CREAT.
  * @param stream Returns a stream object.
  *
  * @return Zero on success, or a negative errno number.
  */
int
nrx_stream_open_file (
	const char *filename,
	int flags,
	mode_t mode,
	struct nrx_stream **stream);

/*!
  * @brief Reads a specified number of bytes from current position in stream.
  *
  * @param stream References the stream.
  * @param buf The buffer to receive the read data.
  * @param len Number of bytes to read.
  *
  * @return Number of bytes read, or a negative errno number.
  */
ssize_t
nrx_stream_read (
	struct nrx_stream *stream,
	void *buf,
	size_t len);

/*!
  * @brief Writes a specified number of bytes to current position in stream.
  *
  * @param stream References the stream.
  * @param buf The buffer that holds the data to write.
  * @param len Size of buf.
  *
  * @return Number of bytes written, or a negative errno number.
  */
ssize_t
nrx_stream_write (
	struct nrx_stream *stream,
	const void *buf,
	size_t len);

#endif /* __nrx_stream_h__ */
