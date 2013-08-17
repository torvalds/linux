/*
 * Copyright (C) 2012 Samsung Electronics Co., LTD
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

#ifndef __CONTENT_PROTECT_H__
#define __CONTENT_PROTECT_H__

__BEGIN_DECLS

typedef enum {
	CP_SUCCESS = 0,
	CP_ERROR_ENABLE_PATH_PROTECTION_FAILED,
	CP_ERROR_DISABLE_PATH_PROTECTION_FAILED,
} cpResult_t;


/**
 * protection IP
 */
#define CP_PROTECT_MFC		(1 << 0)
#define CP_PROTECT_GSC0		(1 << 1)
#define CP_PROTECT_GSC3		(1 << 2)
#define CP_PROTECT_FIMD		(1 << 3)
#define CP_PROTECT_MIXER	(1 << 4)

#define CP_PROTECT_MFC1		(1 << 5)
#define CP_PROTECT_GSC1		(1 << 6)
#define CP_PROTECT_GSC2		(1 << 7)
#define CP_PROTECT_HDMI		(1 << 8)


cpResult_t CP_Enable_Path_Protection(uint32_t);
cpResult_t CP_Disable_Path_Protection(uint32_t);

__END_DECLS

#endif
