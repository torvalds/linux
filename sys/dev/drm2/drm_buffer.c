/**************************************************************************
 *
 * Copyright 2010 Pauli Nieminen.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 **************************************************************************/
/*
 * Multipart buffer for coping data which is larger than the page size.
 *
 * Authors:
 * Pauli Nieminen <suokkos-at-gmail-dot-com>
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/drm2/drm_buffer.h>

/**
 * Allocate the drm buffer object.
 *
 *   buf: Pointer to a pointer where the object is stored.
 *   size: The number of bytes to allocate.
 */
int drm_buffer_alloc(struct drm_buffer **buf, int size)
{
	int nr_pages = size / PAGE_SIZE + 1;
	int idx;

	/* Allocating pointer table to end of structure makes drm_buffer
	 * variable sized */
	*buf = malloc(sizeof(struct drm_buffer) + nr_pages*sizeof(char *),
			DRM_MEM_DRIVER, M_ZERO | M_WAITOK);

	if (*buf == NULL) {
		DRM_ERROR("Failed to allocate drm buffer object to hold"
				" %d bytes in %d pages.\n",
				size, nr_pages);
		return -ENOMEM;
	}

	(*buf)->size = size;

	for (idx = 0; idx < nr_pages; ++idx) {

		(*buf)->data[idx] =
			malloc(min(PAGE_SIZE, size - idx * PAGE_SIZE),
				DRM_MEM_DRIVER, M_WAITOK);


		if ((*buf)->data[idx] == NULL) {
			DRM_ERROR("Failed to allocate %dth page for drm"
					" buffer with %d bytes and %d pages.\n",
					idx + 1, size, nr_pages);
			goto error_out;
		}

	}

	return 0;

error_out:

	/* Only last element can be null pointer so check for it first. */
	if ((*buf)->data[idx])
		free((*buf)->data[idx], DRM_MEM_DRIVER);

	for (--idx; idx >= 0; --idx)
		free((*buf)->data[idx], DRM_MEM_DRIVER);

	free(*buf, DRM_MEM_DRIVER);
	return -ENOMEM;
}
EXPORT_SYMBOL(drm_buffer_alloc);

/**
 * Copy the user data to the begin of the buffer and reset the processing
 * iterator.
 *
 *   user_data: A pointer the data that is copied to the buffer.
 *   size: The Number of bytes to copy.
 */
int drm_buffer_copy_from_user(struct drm_buffer *buf,
			      void __user *user_data, int size)
{
	int nr_pages = size / PAGE_SIZE + 1;
	int idx;

	if (size > buf->size) {
		DRM_ERROR("Requesting to copy %d bytes to a drm buffer with"
				" %d bytes space\n",
				size, buf->size);
		return -EFAULT;
	}

	for (idx = 0; idx < nr_pages; ++idx) {

		if (DRM_COPY_FROM_USER(buf->data[idx],
			(char *)user_data + idx * PAGE_SIZE,
			min(PAGE_SIZE, size - idx * PAGE_SIZE))) {
			DRM_ERROR("Failed to copy user data (%p) to drm buffer"
					" (%p) %dth page.\n",
					user_data, buf, idx);
			return -EFAULT;

		}
	}
	buf->iterator = 0;
	return 0;
}
EXPORT_SYMBOL(drm_buffer_copy_from_user);

/**
 * Free the drm buffer object
 */
void drm_buffer_free(struct drm_buffer *buf)
{

	if (buf != NULL) {

		int nr_pages = buf->size / PAGE_SIZE + 1;
		int idx;
		for (idx = 0; idx < nr_pages; ++idx)
			free(buf->data[idx], DRM_MEM_DRIVER);

		free(buf, DRM_MEM_DRIVER);
	}
}
EXPORT_SYMBOL(drm_buffer_free);

/**
 * Read an object from buffer that may be split to multiple parts. If object
 * is not split function just returns the pointer to object in buffer. But in
 * case of split object data is copied to given stack object that is suplied
 * by caller.
 *
 * The processing location of the buffer is also advanced to the next byte
 * after the object.
 *
 *   objsize: The size of the objet in bytes.
 *   stack_obj: A pointer to a memory location where object can be copied.
 */
void *drm_buffer_read_object(struct drm_buffer *buf,
		int objsize, void *stack_obj)
{
	int idx = drm_buffer_index(buf);
	int page = drm_buffer_page(buf);
	void *obj = NULL;

	if (idx + objsize <= PAGE_SIZE) {
		obj = &buf->data[page][idx];
	} else {
		/* The object is split which forces copy to temporary object.*/
		int beginsz = PAGE_SIZE - idx;
		memcpy(stack_obj, &buf->data[page][idx], beginsz);

		memcpy((char *)stack_obj + beginsz, &buf->data[page + 1][0],
				objsize - beginsz);

		obj = stack_obj;
	}

	drm_buffer_advance(buf, objsize);
	return obj;
}
EXPORT_SYMBOL(drm_buffer_read_object);
