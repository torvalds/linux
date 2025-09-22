/* Debug allocators for the Expat test suite
                            __  __            _
                         ___\ \/ /_ __   __ _| |_
                        / _ \\  /| '_ \ / _` | __|
                       |  __//  \| |_) | (_| | |_
                        \___/_/\_\ .__/ \__,_|\__|
                                 |_| XML parser

   Copyright (c) 2017      Rhodri James <rhodri@wildebeest.org.uk>
   Copyright (c) 2017-2023 Sebastian Pipping <sebastian@pipping.org>
   Copyright (c) 2022      Sean McBride <sean@rogue-research.com>
   Licensed under the MIT license:

   Permission is  hereby granted,  free of charge,  to any  person obtaining
   a  copy  of  this  software   and  associated  documentation  files  (the
   "Software"),  to  deal in  the  Software  without restriction,  including
   without  limitation the  rights  to use,  copy,  modify, merge,  publish,
   distribute, sublicense, and/or sell copies of the Software, and to permit
   persons  to whom  the Software  is  furnished to  do so,  subject to  the
   following conditions:

   The above copyright  notice and this permission notice  shall be included
   in all copies or substantial portions of the Software.

   THE  SOFTWARE  IS  PROVIDED  "AS  IS",  WITHOUT  WARRANTY  OF  ANY  KIND,
   EXPRESS  OR IMPLIED,  INCLUDING  BUT  NOT LIMITED  TO  THE WARRANTIES  OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
   NO EVENT SHALL THE AUTHORS OR  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR  OTHER LIABILITY, WHETHER  IN AN  ACTION OF CONTRACT,  TORT OR
   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
   USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include "memcheck.h"

/* Structures to keep track of what has been allocated.  Speed isn't a
 * big issue for the tests this is required for, so we will use a
 * doubly-linked list to make deletion easier.
 */

typedef struct allocation_entry {
  struct allocation_entry *next;
  struct allocation_entry *prev;
  void *allocation;
  size_t num_bytes;
} AllocationEntry;

static AllocationEntry *alloc_head = NULL;
static AllocationEntry *alloc_tail = NULL;

static AllocationEntry *find_allocation(const void *ptr);

/* Allocate some memory and keep track of it. */
void *
tracking_malloc(size_t size) {
  AllocationEntry *const entry
      = (AllocationEntry *)malloc(sizeof(AllocationEntry));

  if (entry == NULL) {
    printf("Allocator failure\n");
    return NULL;
  }
  entry->num_bytes = size;
  entry->allocation = malloc(size);
  if (entry->allocation == NULL) {
    free(entry);
    return NULL;
  }
  entry->next = NULL;

  /* Add to the list of allocations */
  if (alloc_head == NULL) {
    entry->prev = NULL;
    alloc_head = alloc_tail = entry;
  } else {
    entry->prev = alloc_tail;
    alloc_tail->next = entry;
    alloc_tail = entry;
  }

  return entry->allocation;
}

static AllocationEntry *
find_allocation(const void *ptr) {
  AllocationEntry *entry;

  for (entry = alloc_head; entry != NULL; entry = entry->next) {
    if (entry->allocation == ptr) {
      return entry;
    }
  }
  return NULL;
}

/* Free some memory and remove the tracking for it */
void
tracking_free(void *ptr) {
  AllocationEntry *entry;

  if (ptr == NULL) {
    /* There won't be an entry for this */
    return;
  }

  entry = find_allocation(ptr);
  if (entry != NULL) {
    /* This is the relevant allocation.  Unlink it */
    if (entry->prev != NULL)
      entry->prev->next = entry->next;
    else
      alloc_head = entry->next;
    if (entry->next != NULL)
      entry->next->prev = entry->prev;
    else
      alloc_tail = entry->next;
    free(entry);
  } else {
    printf("Attempting to free unallocated memory at %p\n", ptr);
  }
  free(ptr);
}

/* Reallocate some memory and keep track of it */
void *
tracking_realloc(void *ptr, size_t size) {
  AllocationEntry *entry;

  if (ptr == NULL) {
    /* By definition, this is equivalent to malloc(size) */
    return tracking_malloc(size);
  }
  if (size == 0) {
    /* By definition, this is equivalent to free(ptr) */
    tracking_free(ptr);
    return NULL;
  }

  /* Find the allocation entry for this memory */
  entry = find_allocation(ptr);
  if (entry == NULL) {
    printf("Attempting to realloc unallocated memory at %p\n", ptr);
    entry = (AllocationEntry *)malloc(sizeof(AllocationEntry));
    if (entry == NULL) {
      printf("Reallocator failure\n");
      return NULL;
    }
    entry->allocation = realloc(ptr, size);
    if (entry->allocation == NULL) {
      free(entry);
      return NULL;
    }

    /* Add to the list of allocations */
    entry->next = NULL;
    if (alloc_head == NULL) {
      entry->prev = NULL;
      alloc_head = alloc_tail = entry;
    } else {
      entry->prev = alloc_tail;
      alloc_tail->next = entry;
      alloc_tail = entry;
    }
  } else {
    void *const reallocated = realloc(ptr, size);
    if (reallocated == NULL) {
      return NULL;
    }
    entry->allocation = reallocated;
  }

  entry->num_bytes = size;
  return entry->allocation;
}

int
tracking_report(void) {
  AllocationEntry *entry;

  if (alloc_head == NULL)
    return 1;

  /* Otherwise we have allocations that haven't been freed */
  for (entry = alloc_head; entry != NULL; entry = entry->next) {
    printf("Allocated %lu bytes at %p\n", (long unsigned)entry->num_bytes,
           entry->allocation);
  }
  return 0;
}
