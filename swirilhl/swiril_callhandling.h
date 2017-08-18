/*
** This source code is "Not a Contribution" under Apache license
**
** Sierra Wireless RIL
**
** Based on reference-ril by The Android Open Source Project
** and U300 RIL by ST-Ericsson.
** Modified by Sierra Wireless, Inc.
**
** Copyright (C) 2010 Sierra Wireless, Inc.
** Copyright (C) ST-Ericsson AB 2008-2009
** Copyright 2006, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
**
** Based on reference-ril by The Android Open Source Project.
**
** Heavily modified for ST-Ericsson U300 modems.
** Author: Christian Bejram <christian.bejram@stericsson.com>
*/

#ifndef U300_RIL_CALLHANDLING_H
#define U300_RIL_CALLHANDLING_H 1

void requestUDUB(void *data, size_t datalen, RIL_Token t);
void requestSetMute(void *data, size_t datalen, RIL_Token t);
void requestGetMute(void *data, size_t datalen, RIL_Token t);
void requestLastCallFailCause(void *data, size_t datalen, RIL_Token t);
void requestGetCurrentCalls(void *data, size_t datalen, RIL_Token t);
void requestDial(void *data, size_t datalen, RIL_Token t);
void requestAnswer(void *data, size_t datalen, RIL_Token t);
void requestHangup(void *data, size_t datalen, RIL_Token t);
void requestHangupWaitingOrBackground(void *data, size_t datalen,
                                      RIL_Token t);
void requestHangupForegroundResumeBackground(void *data, size_t datalen,
                                             RIL_Token t);
void requestSwitchWaitingOrHoldingAndActive(void *data, size_t datalen,
                                            RIL_Token t);
void requestConference(void *data, size_t datalen, RIL_Token t);
void requestSeparateConnection(void *data, size_t datalen, RIL_Token t);
void requestExplicitCallTransfer(void *data, size_t datalen, RIL_Token t);

void setCurrentMuteValue( int mute );
void pollCallState( void * );
void startCallStatePolling(void);

#endif
