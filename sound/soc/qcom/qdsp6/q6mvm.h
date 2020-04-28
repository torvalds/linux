/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _Q6_MVM_H
#define _Q6_MVM_H

#include "q6voice.h"

struct q6voice_session;

struct q6voice_session *q6mvm_session_create(enum q6voice_path_type path);

int q6mvm_attach(struct q6voice_session *mvm, struct q6voice_session *cvp,
		 bool state);
int q6mvm_start(struct q6voice_session *mvm, bool state);

#endif /*_Q6_MVM_H */
