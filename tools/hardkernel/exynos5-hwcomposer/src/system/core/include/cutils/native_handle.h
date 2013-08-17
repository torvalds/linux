/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef NATIVE_HANDLE_H_
#define NATIVE_HANDLE_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct native_handle
{
    int version;        /* sizeof(native_handle_t) */
    int numFds;         /* number of file-descriptors at &data[0] */
    int numInts;        /* number of ints at &data[numFds] */
    int data[0];        /* numFds + numInts ints */
} native_handle_t;

/*
 * native_handle_close
 * 
 * closes the file descriptors contained in this native_handle_t
 * 
 * return 0 on success, or a negative error code on failure
 * 
 */
int native_handle_close(const native_handle_t* h);


/*
 * native_handle_create
 * 
 * creates a native_handle_t and initializes it. must be destroyed with
 * native_handle_delete().
 * 
 */
native_handle_t* native_handle_create(int numFds, int numInts);

/*
 * native_handle_delete
 * 
 * frees a native_handle_t allocated with native_handle_create().
 * This ONLY frees the memory allocated for the native_handle_t, but doesn't
 * close the file descriptors; which can be achieved with native_handle_close().
 * 
 * return 0 on success, or a negative error code on failure
 * 
 */
int native_handle_delete(native_handle_t* h);


#ifdef __cplusplus
}
#endif

#endif /* NATIVE_HANDLE_H_ */
