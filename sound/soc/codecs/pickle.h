/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PICKLE_H_
#define  PICKLE_H_

void *pickle_pjt(cfw_project *p, int *n);
cfw_project *unpickle_pjt(void *p, int n);

#endif
