/* 
** This source code is "Not a Contribution" under Apache license
**
** Sierra Wireless RIL
**
** Based on reference-ril by The Android Open Source Project
** and U300 RIL by ST-Ericsson.
** Modified by Sierra Wireless, Inc.
**
** Copyright (C) 2012 Sierra Wireless, Inc.
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

#include <stdio.h>
#include <string.h>
#include <telephony/ril.h>
#include <telephony/librilutils.h>
#include <assert.h>
#include <stdbool.h>
#include <cutils/properties.h>
#include <time.h>
#include "at_channel.h"
#include "at_tok.h"
#include "at_misc.h"
#include "swiril_main.h"
#include "swiril_misc.h"
#include "swiril_cache.h"
#include "swiril_network.h"
#include "swiril_network_at.h"
#include "swiril_pdp.h"
#include "swiril_pdp_common.h"

#define LOG_TAG "RIL"
#include "swiril_log.h"

#define REPOLL_OPERATOR_SELECTED 30     /* 30 * 2 = 1M = ok? */
#define MINUTES_OF_DAY  1440
#define MINUTES_OF_HOUR 60

/* Index into +CIND response for the signal strength.  Also used 
   with the +CIEV unsolicited AT response.
 */
#define AT_CIND_INDEX_SIGNAL 1
#define HL_CIND_INDEX_SIGNAL 2
#define NW_REGISTERED 1 /* value of <stat> field = 1 from +CREG: indication indicates
                         * device registered successfully to the network */
#define NW_REGISTERED_ROAMING 5 /* value of <stat> field = 5 from +CREG: indication
                                 * indicates device successfully registered to the
                                 * network under roaming */

typedef enum {
    RIL_RAT_WCDMA_PREPERRED            = 0, /* GSM/WCDMA (WCDMA preferred) */
    RIL_RAT_GSM_ONLY                   = 1, /* GSM only */
    RIL_RAT_WCDMA_ONLY                 = 2, /* WCDMA only */
    RIL_RAT_GSM_WCDMA_AUTO             = 3, /* GSM/WCDMA (auto mode, according to PRL) */
    RIL_RAT_CDMA_EVDO_AUTO             = 4, /* CDMA and EvDo (auto mode, according to PRL) */
    RIL_RAT_CDMA_ONLY                  = 5, /* CDMA only */
    RIL_RAT_EVDO_ONLY                  = 6, /* EVDO only */
    RIL_RAT_GSM_WCDMA_CDMA_EVDO_AUTO   = 7,  /* GSM/WCDMA, CDMA, and EvDo (auto mode, according to PRL) */
    RIL_RAT_LTE_GSM_WCDMA              = 9,
    RIL_RAT_LTE_ONLY                   = 11, /* LTE only */
    RIL_RAT_LTE_WCDMA                  = 12  /* LTE/WCDMA */
} RIL_RatSelection;

typedef enum {
    SWI_RAT_GSM_WCDMA_AUTO             = 0, /* GSM/WCDMA (auto mode) */
    SWI_RAT_WCDMA_ONLY                 = 1, /* WCDMA only */
    SWI_RAT_GSM_ONLY                   = 2, /* GSM only */
    SWI_RAT_WCDMA_PREPERRED            = 3, /* GSM/WCDMA (WCDMA preferred) */
    SWI_RAT_GSM_PREPERRED              = 4  /* GSM 2G preferred */
} SWI_RatSelection;

typedef enum {
    HL8_RAT_GSM_ONLY                   = 1, /* GSM only */
    HL8_RAT_WCDMA_ONLY                 = 2, /* WCDMA only */
    HL7_RAT_WCDMA_ONLY                 = 2, /* WCDMA only */
    HL8_RAT_GSM_PREPERRED              = 3, /* GSM 2G preferred */
    HL8_RAT_WCDMA_PREPERRED            = 4, /* GSM/WCDMA (WCDMA preferred) */
    HL7_RAT_LTE_ONLY                   = 5, /* LTE only */
    HL7_RAT_WCDMA_PREPERRED            = 6, /* WCDMA preferred */
    HL7_RAT_LTE_PREPERRED              = 7, /* LTE preferred */
} HL_RatSelection;



typedef enum {
    RIL_AUTOMATIC                      = 0, /* "unspecified" (selected by baseband automatically) */
    RIL_EURO_GSM_900_1800_WCDMA_2000   = 1, /* "EURO band" (GSM-900 / DCS-1800 / WCDMA-IMT-2000) */
    RIL_US_WCDMA_850_1900_GSM_850_1900 = 2, /* "US band" (GSM-850 / PCS-1900 / WCDMA-850 / WCDMA-PCS-1900) */
    RIL_JPN_WCDMA_800_2000             = 3, /* "JPN band" (WCDMA-800 / WCDMA-IMT-2000) */
    RIL_AUS_GSM_900_1800_WCDMA_850_2000= 4, /* "AUS band" (GSM-900 / DCS-1800 / WCDMA-850 / WCDMA-IMT-2000) */
    RIL_AUS2_GSM_900_1800_WCDMA_850    = 5, /* "AUS band 2" (GSM-900 / DCS-1800 / WCDMA-850) */
    RIL_CELLULAR_800                   = 6, /* "Cellular (800-MHz Band)" */
    RIL_PCS_1900                       = 7, /* "PCS (1900-MHz Band)" */
    RIL_BAND_CLASS_3                   = 8, /* "Band Class 3 (JTACS Band)" */    
    RIL_BAND_CLASS_4                   = 9, /* "Band Class 4 (Korean PCS Band)" */
    RIL_BAND_CLASS_5                   = 10,/* "Band Class 5 (450-MHz Band)" */
    RIL_BAND_CLASS_6                   = 11,/* "Band Class 6 (2-GMHz IMT2000 Band)" */
    RIL_BAND_CLASS_7                   = 12,/* "Band Class 7 (Upper 700-MHz Band)" */
    RIL_BAND_CLASS_8                   = 13,/* "Band Class 8 (1800-MHz Band)" */
    RIL_BAND_CLASS_9                   = 14,/* "Band Class 9 (900-MHz Band)" */
    RIL_BAND_CLASS_10                  = 15,/* "Band Class 10 (Secondary 800-MHz Band)" */
    RIL_BAND_CLASS_11                  = 16,/* "Band Class 11 (400-MHz European PAMR Band)" */
    RIL_BAND_CLASS_15                  = 17,/* "Band Class 15 (AWS Band)" */
    RIL_BAND_CLASS_16                  = 18,/* "Band Class 16 (US 2.5-GHz Band)" */
} RIL_BandSelection;

/* MSM6290 MSM6246 BAND PREFERENCES FOR PRL REGION 2/5 (AMERICAS) */
typedef enum {
    SWI_ALL_BANDS                      = 0, /* All bands */
    SWI_WCDMA_2100                     = 1, /* WCDMA 2100 */
    SWI_WCDMA_850_1900                 = 2, /* WCDMA 850/1900 */
    SWI_GSM_900_1800                   = 3, /* GSM 900/1800 */
    SWI_GSM_850_1900                   = 4, /* GSM 850/1900 */
    SWI_GSM_ALL                        = 5, /* GSM ALL */
    SWI_WCDMA_2100_GSM_900_1800        = 6, /* WCDMA 2100 GSM 900/1800 */
    SWI_WCDMA_850_1900_GSM_850_1900    = 7, /* WCDMA 850/1900 GSM 850/1900 */
    SWI_WCDMA_ALL                      = 8, /* WCDMA ALL */    
    SWI_WCDMA_850_2100                 = 9, /* WCDMA 850/2100 */
    SWI_WCDMA_800_2100                 = 0x0A, /* WCDMA 800/2100 */
    SWI_WCDMA_850_2100_GSM_900_1800    = 0x0B, /* WCDMA 850/2100 GSM 900/1800 */
    SWI_WCDMA_850_GSM_900_1800         = 0x0C, /* WCDMA 850 GSM 900/1800 */
    SWI_WCDMA_850                      = 0x0D, /* WCDMA 850 */
    SWI_WCDMA_900                      = 0x0E, /* WCDMA 900 */
    SWI_WCDMA_900_2100_GSM_900_1800    = 0x0F, /* WCDMA 900/2100 GSM 900/1800 */
    SWI_WCDMA_900_2100                 = 0x10, /* WCDMA 900/2100 */
    SWI_WCDMA_1900                     = 0x11, /* WCDMA 1900 */
    SWI_BAND_MAX                       = 0x12  /* Max */
} SWI_BandSelection;

typedef enum {
    SWI_UNKNOWN_MODE                   = 0, /* Modem runs in unknown mode */
    SWI_GSM_MODE                       = 1, /* Modem runs in GSM mode */
    SWI_WCDMA_MODE                     = 2, /* Modem runs in WCDMA mode */
} SWI_SystemMode;

typedef enum {
    RIL_CELL_GSM_SERVING            = 0,
    RIL_CELL_GSM_NEIGHBOR           = 1,
    RIL_CELL_UMTS_SERVING           = 2, 
    RIL_CELL_UMTS_NEIGHBOR          = 3,
    RIL_CELL_UMTS_DETECTED          = 4,
    RIL_CELL_LTE_SERVING            = 5,
    RIL_CELL_LTE_NEIGHBOR           = 6
} RIL_CellType;

typedef struct rat_umts_lookup
{
    char *ratStr;               /**< *CNTI response radio access technology string */
    RIL_RAT ril_rat;            /**< RIL radio access technology */
} STRUCT_swi_rat_umts_lookup_type;

/** 
 * GSM/UMTS/LTE network technology lookup table
 */
static const STRUCT_swi_rat_umts_lookup_type swi_rat_umts_table[] =
{
    {"GSM",                 RIL_RAT_GPRS},
    {"GPRS",                RIL_RAT_GPRS},
    {"EDGE",                RIL_RAT_EDGE},
    {"UMTS",                RIL_RAT_UMTS},
    {"HSDPA",               RIL_RAT_HSDPA},
    {"HSUPA",               RIL_RAT_HSUPA},
    {"HSDPA/HSUPA",         RIL_RAT_HSPA},
    {"LTE",                 RIL_RAT_LTE},
    {"HSPA+",               RIL_RAT_HSPAP},
    {NULL,                  RIL_RAT_UNKNOWN}
};

static const struct timeval TIMEVAL_30S = { 30, 0 };
static const struct timeval TIMEVAL_2S = { 2, 0 };
static const struct timeval TIMEVAL_OPERATOR_SELECT_POLL = { 2, 0 };
static const struct timeval TIMEVAL_CURRENT_RAT_POLL = { 60, 0 };
static bool s_RatPoll = false;
static void pollOperatorSelected(void *params);
int net_sel_mode = -1;
bool g_query_in_progress = false;

struct operatorPollParams {
    RIL_Token t;
    int loopcount;
};

typedef struct ril_rat_umts_lookup
{
    RIL_RAT ril_rat;            /**< RIL radio access technology */
    int order;                  /**< the order we prefer to select */
} STRUCT_ril_rat_umts_lookup_type;

/** 
 * GSM/UMTS/LTE network technology lookup table
 */
static const STRUCT_ril_rat_umts_lookup_type ril_rat_umts_order_table[] =
{
    {RIL_RAT_UNKNOWN,                   0},
    {RIL_RAT_GPRS,                      1},
    {RIL_RAT_EDGE,                      2},
    {RIL_RAT_UMTS,                      3},
    {RIL_RAT_IS95A,                     0},
    {RIL_RAT_IS95B,                     0},
    {RIL_RAT_1xRTT,                     0},
    {RIL_RAT_EvDo_REV0,                 0},
    {RIL_RAT_EvDo_REVA,                 0},
    {RIL_RAT_HSDPA,                     4},
    {RIL_RAT_HSUPA,                     5},
    {RIL_RAT_HSPA,                      6},
    {RIL_RAT_EvDo_REVB,                 0},
    {RIL_RAT_LTE,                       8},
    {RIL_RAT_EHRPD,                     0},
    {RIL_RAT_HSPAP,                     7}
};

extern void setPowerOff(void);

#if defined(SWI_RIL_VERSION_12)
/**
 * RIL_REQUEST_SIGNAL_STRENGTH / RIL_UNSOL_SIGNAL_STRENGTH
 * Requests current or unsolicited signal strength
 *
 * @param[in] data 
 *     Pointer to the request data
 * @param datalen 
 *     request data length
 * @param t 
 *     RIL token
 *
 * @return
 *     none
 *
 * @par Abort:<br> 
 *     none
 *
 * @note
 *     Must succeed if radio is on.
 */
void requestSignalStrength_r12(void *data, size_t datalen, RIL_Token t)
{
    ATResponse *atresponse = NULL;
    int err;
    RIL_SignalStrength_v10 signalStrength;
    char *line;

    memset(&signalStrength, 0, sizeof(signalStrength));

    /*initialize first, Java layer cares about some fields */
    signalStrength.GW_SignalStrength.signalStrength = RIL_GSM_SIGNAL_STRENGTH_DEFAULT;
    signalStrength.GW_SignalStrength.bitErrorRate = RIL_GSM_SIGNAL_BIT_ERROR_RATE_DEFAULT;
    /* set all LTE fields to -1 to let Java layer use GSM value */
    signalStrength.LTE_SignalStrength.signalStrength = RIL_LTE_SIGNAL_STRENGTH_UNUSED;
    signalStrength.LTE_SignalStrength.rsrp = RIL_LTE_SIGNAL_STRENGTH_UNUSED;
    signalStrength.LTE_SignalStrength.rsrq = RIL_LTE_SIGNAL_STRENGTH_UNUSED;
    signalStrength.LTE_SignalStrength.rssnr = RIL_LTE_SIGNAL_STRENGTH_UNUSED;
    signalStrength.LTE_SignalStrength.cqi = RIL_LTE_SIGNAL_STRENGTH_UNUSED;
    signalStrength.LTE_SignalStrength.timingAdvance = RIL_LTE_SIGNAL_STRENGTH_UNUSED;
    /* set TD SCDMA RSCP as pe rril.h */
    signalStrength.TD_SCDMA_SignalStrength.rscp = INT_MAX;

    err = at_send_command_singleline("AT+CSQ", "+CSQ:", &atresponse);

    if (err < 0 || atresponse->success == 0)
        goto error;

    line = atresponse->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0)
        goto error;

    err = at_tok_nextint(&line,
                         &(signalStrength.GW_SignalStrength.signalStrength));
    if (err < 0)
        goto error;

    err = at_tok_nextint(&line,
                         &(signalStrength.GW_SignalStrength.bitErrorRate));
    if (err < 0)
        goto error;

    /* If we got here due to a solicited command, then the token will be valid,
       otherwise, we are handling an unsolicited response.  In either case, the
       value is cached for next time.
     */
    swiril_cache_set(RIL_REQUEST_SIGNAL_STRENGTH,
                    &signalStrength,
                    sizeof(signalStrength));

    if (t != NULL) {
        RIL_onRequestComplete(t, RIL_E_SUCCESS,
                              &signalStrength,
                              sizeof(signalStrength));
    }
    else {
        RIL_onUnsolicitedResponse(RIL_UNSOL_SIGNAL_STRENGTH,
                                  &signalStrength,
                                  sizeof(signalStrength));
    }

finally:
    at_response_free(atresponse);
    return;

error:
    /* If we got here due to a solicited command, then the token will be valid,
       otherwise, we are handling an unsolicited response.  If we get an error
       for an unsolicited response, then just discard it.  Solicited commands
       always require a response.
     */
    if (t != NULL) {
        /* SWI_TBD 
           According to ril.h, RIL_E_GENERIC_FAILURE is not a valid error for
           this command, so send whatever values out.
         */
        LOGE("%s must never return an error when radio is on", __func__);
        RIL_onRequestComplete(t, RIL_E_SUCCESS,
                              &signalStrength,
                              sizeof(signalStrength));
    } else {
        LOGE("%s: error reading unsolicited signal strength", __func__);
    }
    goto finally;
}

#elif defined(SWI_RIL_VERSION_6)
/**
 * RIL_REQUEST_SIGNAL_STRENGTH / RIL_UNSOL_SIGNAL_STRENGTH
 * Requests current or unsolicited signal strength
 *
 * @param[in] data 
 *     Pointer to the request data
 * @param datalen 
 *     request data length
 * @param t 
 *     RIL token
 *
 * @return
 *     none
 *
 * @par Abort:<br> 
 *     none
 *
 * @note
 *     Must succeed if radio is on.
 */
void requestSignalStrength_r6(void *data, size_t datalen, RIL_Token t)
{
    ATResponse *atresponse = NULL;
    int err;
    RIL_SignalStrength_v6 signalStrength;
    char *line;

    memset(&signalStrength, 0, sizeof(signalStrength));    

    /*initialize first, Java layer cares about some fields */
    signalStrength.GW_SignalStrength.signalStrength = RIL_GSM_SIGNAL_STRENGTH_DEFAULT;
    signalStrength.GW_SignalStrength.bitErrorRate = RIL_GSM_SIGNAL_BIT_ERROR_RATE_DEFAULT;
    /* set all LTE fields to -1 to let Java layer use GSM value */
    signalStrength.LTE_SignalStrength.signalStrength = RIL_LTE_SIGNAL_STRENGTH_UNUSED;
    signalStrength.LTE_SignalStrength.rsrp = RIL_LTE_SIGNAL_STRENGTH_UNUSED;
    signalStrength.LTE_SignalStrength.rsrq = RIL_LTE_SIGNAL_STRENGTH_UNUSED;
    signalStrength.LTE_SignalStrength.rssnr = RIL_LTE_SIGNAL_STRENGTH_UNUSED;
    signalStrength.LTE_SignalStrength.cqi = RIL_LTE_SIGNAL_STRENGTH_UNUSED;

    err = at_send_command_singleline("AT+CSQ", "+CSQ:", &atresponse);

    if (err < 0 || atresponse->success == 0)
        goto error;

    line = atresponse->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0)
        goto error;

    err = at_tok_nextint(&line,
                         &(signalStrength.GW_SignalStrength.signalStrength));
    if (err < 0)
        goto error;

    err = at_tok_nextint(&line,
                         &(signalStrength.GW_SignalStrength.bitErrorRate));
    if (err < 0)
        goto error;

    /* If we got here due to a solicited command, then the token will be valid,
       otherwise, we are handling an unsolicited response.  In either case, the
       value is cached for next time.
     */
    swiril_cache_set(RIL_REQUEST_SIGNAL_STRENGTH,
                    &signalStrength,
                    sizeof(signalStrength));

    if (t != NULL) {
        RIL_onRequestComplete(t, RIL_E_SUCCESS,
                              &signalStrength,
                              sizeof(signalStrength));
    }
    else {
        RIL_onUnsolicitedResponse(RIL_UNSOL_SIGNAL_STRENGTH,
                                  &signalStrength,
                                  sizeof(signalStrength));
    }

finally:
    at_response_free(atresponse);
    return;

error:
    /* If we got here due to a solicited command, then the token will be valid,
       otherwise, we are handling an unsolicited response.  If we get an error
       for an unsolicited response, then just discard it.  Solicited commands
       always require a response.
     */
    if (t != NULL) {
        /* SWI_TBD 
           According to ril.h, RIL_E_GENERIC_FAILURE is not a valid error for
           this command, so send whatever values out.
         */
        LOGE("%s must never return an error when radio is on", __func__);
        RIL_onRequestComplete(t, RIL_E_SUCCESS,
                              &signalStrength,
                              sizeof(signalStrength));
    } else {
        LOGE("%s: error reading unsolicited signal strength", __func__);
    }
    goto finally;
}

#else

/**
 * RIL_REQUEST_SIGNAL_STRENGTH / RIL_UNSOL_SIGNAL_STRENGTH
 * Requests current or unsolicited signal strength
 *
 * @param[in] data 
 *     Pointer to the request data
 * @param datalen 
 *     request data length
 * @param t 
 *     RIL token
 *
 * @return
 *     none
 *
 * @par Abort:<br> 
 *     none
 *
 * @note
 *     Must succeed if radio is on.
 */
void requestSignalStrength(void *data, size_t datalen, RIL_Token t)
{
    ATResponse *atresponse = NULL;
    int err;
    RIL_SignalStrength signalStrength;
    char *line;

    memset(&signalStrength, 0, sizeof(signalStrength));

    /*initialize first, Java layer cares about some fields */
    signalStrength.GW_SignalStrength.signalStrength = RIL_GSM_SIGNAL_STRENGTH_DEFAULT;
    signalStrength.GW_SignalStrength.bitErrorRate = RIL_GSM_SIGNAL_BIT_ERROR_RATE_DEFAULT;

    err = at_send_command_singleline("AT+CSQ", "+CSQ:", &atresponse);

    if (err < 0 || atresponse->success == 0)
        goto error;

    line = atresponse->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0)
        goto error;

    err = at_tok_nextint(&line,
                         &(signalStrength.GW_SignalStrength.signalStrength));
    if (err < 0)
        goto error;

    err = at_tok_nextint(&line,
                         &(signalStrength.GW_SignalStrength.bitErrorRate));
    if (err < 0)
        goto error;

    /* If we got here due to a solicited command, then the token will be valid,
       otherwise, we are handling an unsolicited response.  In either case, the
       value is cached for next time.
     */
    swiril_cache_set(RIL_REQUEST_SIGNAL_STRENGTH,
                    &signalStrength,
                    sizeof(signalStrength));

    if (t != NULL) {
        RIL_onRequestComplete(t, RIL_E_SUCCESS,
                              &signalStrength,
                              sizeof(signalStrength));
    }
    else {
        RIL_onUnsolicitedResponse(RIL_UNSOL_SIGNAL_STRENGTH,
                                  &signalStrength,
                                  sizeof(signalStrength));
    }

finally:
    at_response_free(atresponse);
    return;

error:
    /* If we got here due to a solicited command, then the token will be valid,
       otherwise, we are handling an unsolicited response.  If we get an error
       for an unsolicited response, then just discard it.  Solicited commands
       always require a response.
     */
    if (t != NULL) {
        /* SWI_TBD 
           According to ril.h, RIL_E_GENERIC_FAILURE is not a valid error for
           this command, so send whatever values out.
         */
        LOGE("%s must never return an error when radio is on", __func__);
        RIL_onRequestComplete(t, RIL_E_SUCCESS,
                              &signalStrength,
                              sizeof(signalStrength));
    } else {
        LOGE("%s: error reading unsolicited signal strength", __func__);
    }
    goto finally;
}
#endif
    
/**
 *
 * Request current registration state.
 *
 * @param[in] data 
 *     Pointer to the request data
 * @param datalen 
 *     request data length
 * @param t 
 *     RIL token
 *
 * @return
 *     none
 *
 * @par Abort:<br> 
 *     none
 *
 * @note
 *     No CDMA support
 *     +CREG/+CGREG uses 27.007 release 8
 */
void requestRegistrationState(int request, void *data,
                              size_t datalen, RIL_Token t)
{
    int err = 0;
    int response[VOICE_REGISTRATION_STATE_PARAMS];
    char *responseStr[VOICE_REGISTRATION_STATE_PARAMS];
    ATResponse *atresponse = NULL;
    const char *cmd;
    const char *prefix;
    char *line, *p;
    int skip;
    int count = DATA_REGISTRATION_STATE_PARAMS;
    bool use_cgreg = true;   /* default to +CGREG */
    bool hasAct = false;
    RIL_RAT networkType = RIL_RAT_UNKNOWN;
    RIL_RAT rat = RIL_RAT_UNKNOWN;
    int i;
    unsigned long CID = 0;

    /* IMPORTANT: Will take screen state lock here. Make sure to always call
                  releaseScreenStateLock BEFORE returning! */
    getScreenStateLock();
    if (!getScreenState()) {
        cmd = "AT+CGREG=2;+CREG=2";
        at_send_command(cmd, NULL); /* Ignore the response, not VITAL. */
    }

    memset(response, 0, sizeof(response));
    memset(responseStr, 0, sizeof(responseStr));

    if (request == RIL_REQUEST_REGISTRATION_STATE) {    /*i.e voice_registration_state*/
        count = VOICE_REGISTRATION_STATE_PARAMS;
        /* Only use +CREG if modem is not PS only */
        if ( ! is_modem_ps_only() ) {
            use_cgreg = false;
        }
    } else if (request != RIL_REQUEST_GPRS_REGISTRATION_STATE) {
        goto error;
    }


    if (!queryRegState(use_cgreg, &response[0], &CID, &hasAct)) {
        goto error;
    }

    if((response[0] == RIL_REG_STATE_NOT_REGISTERED || 
        response[0] == RIL_REG_STATE_SEARCHING_NOT_REGISTERED ||
        response[0] == RIL_REG_STATE_REGISTRATION_DENIED ||
        response[0] == RIL_REG_STATE_UNKNOWN ) && 
        (currentState() == RADIO_STATE_SIM_LOCKED_OR_ABSENT) &&
        (request == RIL_REQUEST_REGISTRATION_STATE)) {
         LOGD("%s update status from %d to %d ", __func__, response[0], response[0] + 10);
         response[0] += 10;
    }

    /* This was incorrect in the reference implementation. Go figure. FIXME */
    asprintf(&responseStr[0], "%d", response[0]);

    if (response[1] > 0)
        asprintf(&responseStr[1], "%04x", response[1]);
    else
        responseStr[1] = NULL;

    if (CID > 0)
        asprintf(&responseStr[2], "%08lx", CID);
    else
        responseStr[2] = NULL;

    /* AT command reference specifies the response we get:
     *    0 GSM
     *    1 GSM Compact                Not Supported
     *    2 UTRAN
     *    3 GSM w/EGPRS
     *    4 UTRAN w/HSDPA
     *    5 UTRAN w/HSUPA
     *    6 UTRAN w/HSUPA and HSDPA
     *    7 E-UTRAN
     */
    if (hasAct) {
        switch (response[3]) {
            case 0:
            case 1:
                networkType = RIL_RAT_GPRS;
                break;
            case 2:
                networkType = RIL_RAT_UMTS;
                break;
            case 3:
                networkType = RIL_RAT_EDGE;
                break;
            case 4:
                networkType = RIL_RAT_HSDPA;
                break;
            case 5:
                networkType = RIL_RAT_HSUPA;
                break;
            case 6:
                networkType = RIL_RAT_HSPA;
                break;
            case 7:
                networkType = RIL_RAT_LTE;
                break;
            default:
                networkType = RIL_RAT_UNKNOWN;
                break;
        }
    }
    /* Observed sometimes *CNTI returns newer RAT than +CREG/+CGREG */
    if (((response[0] == 1) || (response[0] == 5)) &&
        getRILRatAlternative(&rat) &&
        (ril_rat_umts_order_table[rat].order > ril_rat_umts_order_table[networkType].order)) {
        LOGD("%s update network type from %d to %d", __func__, networkType, rat);
        networkType = rat;
    }
    
    asprintf(&responseStr[3], "%d", networkType);

    if (request == RIL_REQUEST_GPRS_REGISTRATION_STATE) {
#ifdef SWI_RIL_VERSION_6
        /* SWI_TBD doesn't care for index 4 - if registration state is 3 (Registration denied) */
        
        /* The maximum number of simultaneous Data Calls that can be
         * established using RIL_REQUEST_SETUP_DATA_CALL */
        /* SWI_TBD hardcode to 1 now*/
        asprintf(&responseStr[5], "%d", 1);
#endif
    }
    else {
        /* Index 4 to 12 are CDMA only */ 
        
        /* SWI_TBD doesn't care for index 13 - if registration state is 3 (Registration denied) */
        
        /* Primary Scrambling Code */
        /* Only fill in when modem is registered on UMTS or newer network technology and everything ok */
        if (((response[0] == 1) || (response[0] == 5)) &&
            (networkType != RIL_RAT_GPRS) && 
            (networkType != RIL_RAT_EDGE) && 
            (networkType != RIL_RAT_UNKNOWN)) {
//HL not supported
#if 0
            at_response_free(atresponse);
            err = at_send_command_singleline("AT+UPSC?", "+UPSC:", &atresponse);
            if (err >= 0 &&
                atresponse->success != 0 &&
                atresponse->p_intermediates != NULL) {
        
                line = atresponse->p_intermediates->line;
            
                err = at_tok_start(&line);
                if (err >= 0) {
                    err = at_tok_nextint(&line, &skip);
                    if (err >= 0) {
                        asprintf(&responseStr[14], "%x", skip);
                    }
                }
            }
#endif
        }
    }
    
    /* Return the successful response and cache the value for next time */
    RIL_onRequestComplete(t, RIL_E_SUCCESS, responseStr,
                                       count * sizeof(char *));
    
finally:

#ifdef NO_LCD_OFF_IND
    if (!getScreenState()) {
        // turned off CREG, CGREG indication
        cmd = "AT+CGREG=0;+CREG=0";
        at_send_command(cmd, NULL);
    }
#endif
    releaseScreenStateLock(); /* Important! */

    for (i = 0; i < count; i++) {
        if (responseStr[i])
            free(responseStr[i]);
    }

    at_response_free(atresponse);
    return;

error:
    LOGE("%s err %d", __func__, err);
    /* ril.h of Gingerbread or newer allows GENERIC_FAILURE */
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    goto finally;
}

/**
 * Poll +COPS? and return a success, or if the loop counter reaches
 * REPOLL_OPERATOR_SELECTED, return generic failure.
 */
static void pollOperatorSelected(void *params)
{
    int err = 0;
    int response = 0;
    char *line = NULL;
    ATResponse *atresponse = NULL;
    struct operatorPollParams *poll_params;
    RIL_Token t;

    assert(params != NULL);

    poll_params = (struct operatorPollParams *) params;
    t = poll_params->t;

    if (poll_params->loopcount >= REPOLL_OPERATOR_SELECTED) {
        goto error;
    }

    err = at_send_command_singleline("AT+COPS?", "+COPS:", &atresponse);
    if (err < 0 || atresponse->success == 0)
        goto error;

    line = atresponse->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0)
        goto error;

    err = at_tok_nextint(&line, &response);
    if (err < 0)
        goto error;

    /* If we don't get more than the COPS: {0-4} we are not registered.
       Loop and try again. */
    if (!at_tok_hasmore(&line)) {
        poll_params->loopcount++;
        enqueueRILEvent(RIL_EVENT_QUEUE_NORMAL, pollOperatorSelected,
                        poll_params, &TIMEVAL_OPERATOR_SELECT_POLL);
    } else {
        /* We got operator, throw a success! */
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
        goto finished;
    }

finally:
    at_response_free(atresponse);
    return;

finished:
    free(poll_params);
    goto finally;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    goto finished;
}


/**
 * Debug print AT command lines
 */
static void printATMulitiValue(char * sp, ATResponse *atresponse)
{
    int i;
    char *line;
    ATLine *atlinep = NULL;
     
    for (i=0, atlinep=atresponse->p_intermediates;
            atlinep!=NULL; i++, atlinep=atlinep->p_next) {

        line = atlinep->line;
        LOGI("%s: %i '%s'", sp, i, line);
    }
}

/**
 * RIL_REQUEST_NEIGHBORING_CELL_IDS
 *
 * Request neighboring cell id in GSM network
 */
void requestGetNeighboringCellIDS(void *data, size_t datalen, RIL_Token t)
{
    ATResponse *atresponse = NULL;
    int err;
    char *line;
    RIL_NeighboringCell *resp = NULL;
    RIL_NeighboringCell **respp = NULL;
    int i;
    int j;
    ATLine *atlinep = NULL;
    int sysmode = SWI_UNKNOWN_MODE;
    int temp;

    char *outp = NULL;
    int num;
    int rscp;

    /* query +COPS? to decide 2G or 3G */
    /* 
     * 3GPP 27.007 section 7.3
     * +COPS?
     * +COPS: <mode>[,<format>,<oper>[,<AcT>]]
     * +CME ERROR: <err>
     * 
     * <AcT>: integer type; access technology selected
     * 0    GSM
     * 1    GSM Compact
     * 2    UTRAN
     * 3    GSM w/EGPRS (see NOTE 1)
     * 4    UTRAN w/HSDPA (see NOTE 2)
     * 5    UTRAN w/HSUPA (see NOTE 2)
     * 6    UTRAN w/HSDPA and HSUPA (see NOTE 2)
     * 7    E-UTRAN
     */

    err = at_send_command_singleline("AT+COPS?", "+COPS:", &atresponse);

    if (err < 0 || atresponse->success == 0)
        goto error;

    line = atresponse->p_intermediates->line;
    err = at_tok_start(&line);

    if (err < 0)
        goto error;

    /* mode */
    err = at_tok_nextint(&line, &temp);

    if (err < 0)
        goto error;

    /* format */
    err = at_tok_nextint(&line, &temp);

    if (err < 0)
        goto error;

    /* oper */
    err = at_tok_nextstr(&line, &outp);

    if (err < 0)
        goto error;
        
    /* AcT */
    err = at_tok_nextint(&line, &temp);

    if (err < 0)
        goto error;

    switch (temp) {
    case 0:
    case 1:
    case 3:
        sysmode = SWI_GSM_MODE;
        break;

    case 2:
    case 4:
    case 5:
    case 6:
    case 7:
        sysmode = SWI_WCDMA_MODE;
        break;
    default:
        sysmode = SWI_UNKNOWN_MODE;
    }
    LOGI("requestGetNeighboringCellIDS: system mode is %i", sysmode);

    if (sysmode == SWI_GSM_MODE) {
        /* SWI_TBD can't get LAC info, so return error right away for now 
         * err = at_send_command_multivalue("AT!GSMINFO?", "!GSMINFO:", &atresponse);
         */
        LOGE("requestGetNeighboringCellIDS modem runs GSM mode");
        goto error;
    }
    else if (sysmode == SWI_WCDMA_MODE) {
//HL not supported yet
#if 0
        /* +USET query 'Async Neighbour Set' */
        err = at_send_command_multivalue("AT+USET?2", "+USET:", &atresponse);
#else
        err = -1;
#endif
    }
    else
    {
        LOGE("requestGetNeighboringCellIDS modem runs in unknown mode");
        goto error;
    }

    if (err < 0 || atresponse->success == 0) {
        goto error;
    }
#if 0
    /* debug print response */
    printATMulitiValue("getNeighboringCell", atresponse);
#endif
    /*
     * Parse the AT command output
     */
    if (sysmode == SWI_GSM_MODE) {
        /* SWI_TBD do nothing for now */
    }
    else if (sysmode == SWI_WCDMA_MODE) {
        /* get list of Primary Scrambling Code and Level index of CPICH Received Signal Code Power */
        atlinep=atresponse->p_intermediates->p_next;
        swicheckp(atlinep, "requestGetNeighboringCellIDS AT line is NULL");
        /* skip 1st line and get count from 2nd line*/
        line = atlinep->line;
        err = at_tok_start(&line);
        if (err < 0)
            goto error;
        err = at_tok_nexthexint(&line, &num);
        if (err < 0)
            goto error;
        
        /* yes, there's an array of pointers and then an array of structures */
        respp = (RIL_NeighboringCell **)swimalloc(num * sizeof(RIL_NeighboringCell *), 
                                        "requestGetNeighboringCellIDS allocate RIL_NeighboringCell** memory failed");
        resp = (RIL_NeighboringCell *)swimalloc(num * sizeof(RIL_NeighboringCell), 
                                        "requestGetNeighboringCellIDS allocate RIL_NeighboringCell* memory failed");
        memset(resp, 0, (num * sizeof(RIL_NeighboringCell)));
        
        /* init the pointer array */
        for(i = 0; i < num ; i++) {
            respp[i] = &(resp[i]);
        }        
        
        /* parser each set of information */
        for (i = 0; i< num && atlinep != NULL; i++) {

            /* point to PSC */
            atlinep = atlinep->p_next;
            swicheckp(atlinep, "requestGetNeighboringCellIDS AT line is NULL");
            line = atlinep->line;
            err = at_tok_start(&line);
            if (err < 0)
                goto error;
            /* remove the leading space */
            err = at_tok_nextstr(&line, &outp);
            if (err < 0)
                goto error;
            asprintf(&(resp[i].cid), "%s", outp);

            /* skip SSC, SSTD, Tot Ec/Io, Ec/Io then point to RSCP */
            for (j = 0; j < 5; j++) {
                atlinep = atlinep->p_next;
                swicheckp(atlinep, "requestGetNeighboringCellIDS AT line is NULL");
            }
            line = atlinep->line;
            err = at_tok_start(&line);
            if (err < 0)
                goto error;
            /* translate string Hex to integer for RSCP */ 
            err = at_tok_nexthexint(&line, &rscp);
            if (err < 0)
                goto error;
            resp[i].rssi = rscp;

            /* skip Window Size */
            atlinep = atlinep->p_next;
            swicheckp(atlinep, "requestGetNeighboringCellIDS AT line is NULL");
        }
    }
    RIL_onRequestComplete(t, RIL_E_SUCCESS, respp,
                        (num * sizeof(RIL_NeighboringCell *)));

finally:
    if (atresponse != NULL) {
        at_response_free(atresponse);
    }
    
    if (resp != NULL) {
        for (i = 0; i < num; i++) {
            if (resp[i].cid != NULL) {
                free(resp[i].cid);
            }
        }
        free(resp);
    }
    if (respp != NULL) {
        free(respp);
    }
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    goto finally;
}

/**
 * RIL_REQUEST_SET_BAND_MODE
 *
 * Assign a specified band for RF configuration.
*/
void requestSetBandMode(void *data, size_t datalen, RIL_Token t)
{
    RIL_BandSelection bandMode = ((int *)data)[0];
    SWI_BandSelection bandSet = SWI_BAND_MAX;
    int err = 0;
    char *cmd = NULL;
    ATResponse *atresponse = NULL;

    /* Currently only allow automatic or "US band" (GSM-850 / PCS-1900 / WCDMA-850 / WCDMA-PCS-1900). */
    switch(bandMode) {
        case RIL_AUTOMATIC:
            bandSet = SWI_ALL_BANDS;
            break;

        case RIL_US_WCDMA_850_1900_GSM_850_1900:
            bandSet = SWI_WCDMA_850_1900_GSM_850_1900;
            break; 
            
        default:
            break;
    }
    
    if (bandMode != SWI_BAND_MAX) {
        asprintf(&cmd, "AT!BAND=%d", bandSet);
        err = at_send_command(cmd, &atresponse);
        free(cmd);
        if (err < 0 || atresponse->success == 0) {
            RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
        }
        else {
            setPowerOff();
            RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
        }
        at_response_free(atresponse);
    }
    else {
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    }    
}

/**
 * RIL_REQUEST_QUERY_AVAILABLE_BAND_MODE
 *
 * Query the list of band mode supported by RF.
 *
 * See also: RIL_REQUEST_SET_BAND_MODE
 */
void requestQueryAvailableBandMode(void *data, size_t datalen, RIL_Token t)
{
    int response[3];

    /* Currently only support automatic and "US band" (GSM-850 / PCS-1900 / WCDMA-850 / WCDMA-PCS-1900). */
    response[0] = 3; /* size of array */
    response[1] = RIL_AUTOMATIC;
    response[2] = RIL_US_WCDMA_850_1900_GSM_850_1900;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, response, sizeof(response));
}

/**
 * RIL_REQUEST_SET_NETWORK_SELECTION_AUTOMATIC
 *
 * Specify that the network should be selected automatically.
*/
void requestSetNetworkSelectionAutomatic(void *data, size_t datalen,
                                         RIL_Token t)
{
    int err = 0;
    struct operatorPollParams *poll_params = NULL;

    /* Workaround for HL8518 firmware issue */
    if(!IsRegistered())
        return RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

    err = at_send_command("AT+COPS=0", NULL);

    if (err < 0)
        goto error;

    net_sel_mode = 0;

    poll_params = malloc(sizeof(struct operatorPollParams));

    poll_params->loopcount = 0;
    poll_params->t = t;

    enqueueRILEvent(RIL_EVENT_QUEUE_NORMAL, pollOperatorSelected,
                    poll_params, &TIMEVAL_OPERATOR_SELECT_POLL);

    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    return;
}

/**
 * RIL_REQUEST_SET_NETWORK_SELECTION_MANUAL
 *
 * Manually select a specified network.
 *
 * The radio baseband/RIL implementation is expected to fall back to 
 * automatic selection mode if the manually selected network should go
 * out of range in the future.
 */
void requestSetNetworkSelectionManual(void *data, size_t datalen,
                                      RIL_Token t)
{
    /* 
     * AT+COPS=[<mode>[,<format>[,<oper>[,<AcT>]]]]
     *    <mode>   = 4 = Manual (<oper> field shall be present and AcT optionally) with fallback to automatic if manual fails.
     *    <format> = 2 = Numeric <oper>, the number has structure:
     *                   (country code digit 3)(country code digit 2)(country code digit 1)
     *                   (network code digit 2)(network code digit 1) 
     */

    int err = 0;
    char *cmd = NULL;
    ATResponse *atresponse = NULL;
    const char *mccMnc = (const char *) data;
    struct operatorPollParams *poll_params = NULL;

    /* Check inparameter. */
    if (mccMnc == NULL) {
        goto error;
    }
    /* Build and send command. */
    asprintf(&cmd, "AT+COPS=4,2,\"%s\"", mccMnc);
    err = at_send_command(cmd, &atresponse);
    if (err < 0 || atresponse->success == 0)
        goto error;

    net_sel_mode = 1;

    /* Make sure registration succeeds before returning RIL response.
       The RIL response will be returned in pollOperatorSelected().
     */
    poll_params = malloc(sizeof(struct operatorPollParams));

    poll_params->loopcount = 0;
    poll_params->t = t;

    enqueueRILEvent(RIL_EVENT_QUEUE_NORMAL, pollOperatorSelected,
                    poll_params, &TIMEVAL_OPERATOR_SELECT_POLL);

finally:
    at_response_free(atresponse);

    if (cmd != NULL)
        free(cmd);

    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    goto finally;
}

/**
 * RIL_REQUEST_QUERY_AVAILABLE_NETWORKS
 *
 * Scans for available networks.
*/
void requestQueryAvailableNetworks(void *data, size_t datalen, RIL_Token t)
{
    /* 
     * AT+COPS=?
     *   +COPS: [list of supported (<stat>,long alphanumeric <oper>
     *           ,short alphanumeric <oper>,numeric <oper>[,<AcT>])s]
     *          [,,(list of supported <mode>s),(list of supported <format>s)]
     *
     *   <stat>
     *     0 = unknown
     *     1 = available
     *     2 = current
     *     3 = forbidden 
     */

    int err = 0;
    ATResponse *atresponse = NULL;
    const char *statusTable[] =
        { "unknown", "available", "current", "forbidden" };
    char **responseArray = NULL;
    char *p;
    int n = 0;
    int i = 0;
    bool sentBreak= false;
    int count = 0;
    int totalCount = 180000/QUERY_NETWROK_TIMEOUT; /* Default AT timeout = 180 sec */
    bool compTimeout = false;

#if 0
    err = at_send_command_multiline("AT+COPS=?", "+COPS:", &atresponse);
#else
    /* Workaround for cancelling AT+COPS=? ANDROIDRIL-471*/
    DisableATTimeoutHandler();
    g_query_in_progress = false;    

    while(1) {
        err = at_query_networks_multi_timeout("AT+COPS=?", "+COPS:",&atresponse,QUERY_NETWROK_TIMEOUT);    
        if (err == AT_ERROR_TIMEOUT) {            
            LOGD("%s: Timeout occurred.\n",__func__);
            if (count >= totalCount) {
                LOGD("%s:Total Timeout occurred.\n",__func__);
                compTimeout = true;
                break;
            }
            count++;
            
            /* Check here if next request is available in request queue and it is a cancel of query network */
            if (isCancelQueryNetworkRequested()) {
                clearPendingQueryNetworksCmd();
                sentBreak = true;
                at_send_command_with_timeout("\r",NULL,ABORT_TIMEOUT);
                sleep(1);
                break;       
            }
        }
        else {
            break;
        }
        /* go in the loop again */
    }
    if (!sentBreak)
        clearPendingQueryNetworksCmd();       
    EnableATTimeoutHandler();

    /* if complete timeout occurred, handle the timeout */
    if (compTimeout)
        at_timeout_handler();
    
#endif
    LOGD("AT+COPS=? result:%d\n", err);

    if (err < 0 ||
        atresponse->success == 0 || 
        atresponse->p_intermediates == NULL)
        goto error;

    p = atresponse->p_intermediates->line;

    /* Determine the number of operators by counting the number of opening
       brackets. However, stop after getting two consecutive commas, because
       this signals the end of the operator list.  
     */
    while (p != NULL) {
        if (p = strstr(p,"),(")) 
        {         
            n++;
            p++;
        }    
    }

    /* Allocate array of strings, blocks of 4 strings. */
    responseArray = alloca(n * 4 * sizeof(char *));

    p = atresponse->p_intermediates->line;

    /* Loop and collect response information into the response array. */
    for (i = 0; i < n; i++) {
        int status = 0;
        char *line = NULL;
        char *longAlphaNumeric = NULL;
        char *shortAlphaNumeric = NULL;
        char *numeric = NULL;

        line = strstr(p,"(");
        if (line == NULL) {
            LOGE("Null pointer while parsing COPS response. This should not happen.");
            break;
        }
        else { 
            line++;
        }    
        /* <stat> */
        err = at_tok_nextint(&line, &status);
        if (err < 0)
            goto error;

        /* long alphanumeric <oper> */
        err = at_tok_nextstr(&line, &longAlphaNumeric);
        if (err < 0)
            goto error;

        /* short alphanumeric <oper> */            
        err = at_tok_nextstr(&line, &shortAlphaNumeric);
        if (err < 0)
            goto error;

        /* numeric <oper> */
        err = at_tok_nextstr(&line, &numeric);
        if (err < 0)
            goto error;
        
        p = line;
        
        responseArray[i * 4 + 0] = alloca(strlen(longAlphaNumeric) + 1);
        strcpy(responseArray[i * 4 + 0], longAlphaNumeric);

        responseArray[i * 4 + 1] = alloca(strlen(shortAlphaNumeric) + 1);
        strcpy(responseArray[i * 4 + 1], shortAlphaNumeric);

        responseArray[i * 4 + 2] = alloca(strlen(numeric) + 1);
        strcpy(responseArray[i * 4 + 2], numeric);

        

        /* 
         * Check if modem returned an empty string, and fill it with MNC/MMC 
         * if that's the case.
         */
        if (responseArray[i * 4 + 0] && strlen(responseArray[i * 4 + 0]) == 0) {
            responseArray[i * 4 + 0] = alloca(strlen(responseArray[i * 4 + 2])
                                              + 1);
            strcpy(responseArray[i * 4 + 0], responseArray[i * 4 + 2]);
        }

        if (responseArray[i * 4 + 1] && strlen(responseArray[i * 4 + 1]) == 0) {
            responseArray[i * 4 + 1] = alloca(strlen(responseArray[i * 4 + 2])
                                              + 1);
            strcpy(responseArray[i * 4 + 1], responseArray[i * 4 + 2]);
        }

        responseArray[i * 4 + 3] = alloca(strlen(statusTable[status]) + 1);
        sprintf(responseArray[i * 4 + 3], "%s", statusTable[status]);
    }

    RIL_onRequestComplete(t, RIL_E_SUCCESS, responseArray,
                          i * 4 * sizeof(char *));

finally:
    at_response_free(atresponse);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    goto finally;
}

/**
 * RIL_REQUEST_SET_PREFERRED_NETWORK_TYPE
 * Requests to set the preferred network type for searching and registering
 * (CS/PS domain, RAT, and operation mode).
 * 
 * @param[in] data 
 *     Pointer to the request data
 * @param datalen 
 *     request data length
 * @param t 
 *     RIL token
 *
 * @return
 *     none
 *
 * @par Abort:<br> 
 *     none
 *
 * @note
 *     This function won't work with newer modems such as MC7700 
 *     due to !SELRAT available selection set update
 */
void requestSetPreferredNetworkType(void *data, size_t datalen,
                                    RIL_Token t)
{
    ATResponse *atresponse = NULL;
    int err = 0;
    int rat;
    int arg;
    char *cmd = NULL;
    RIL_Errno rilerr = RIL_E_GENERIC_FAILURE;

    rat = ((int *) data)[0];

    switch (rat) {
    case RIL_RAT_WCDMA_PREPERRED:
        /* for GSM/WCDMA (WCDMA preferred) */
        if(IsHL8())
            arg = HL8_RAT_WCDMA_PREPERRED;
        else
            arg = HL7_RAT_WCDMA_PREPERRED;
        break;
    case RIL_RAT_GSM_ONLY:
        /* GSM only */
        arg = HL8_RAT_GSM_ONLY;
        break;
    case RIL_RAT_WCDMA_ONLY:
        /* WCDMA only */
        arg = HL8_RAT_WCDMA_ONLY;
        break;
    case RIL_RAT_GSM_WCDMA_AUTO:
        /* GSM/WCDMA (auto mode, according to PRL) */
        arg = HL8_RAT_GSM_PREPERRED;
        break;
    case RIL_RAT_LTE_ONLY:
        arg = HL7_RAT_LTE_ONLY;
        break;
    case RIL_RAT_LTE_WCDMA:
    case RIL_RAT_LTE_GSM_WCDMA:
        arg = HL7_RAT_LTE_PREPERRED;
        break;
    default:
        rilerr = RIL_E_MODE_NOT_SUPPORTED;
        goto error;
    }

    asprintf(&cmd, "AT+KSRAT=%d", arg);

    err = at_send_command(cmd, &atresponse);
    free(cmd);
    if (err < 0 || atresponse->success == 0)
        goto error;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

finally:
    at_response_free(atresponse);
    return;

error:
    RIL_onRequestComplete(t, rilerr, NULL, 0);
    goto finally;
}

/**
 * RIL_REQUEST_GET_PREFERRED_NETWORK_TYPE
 *
 * Query the preferred network type (CS/PS domain, RAT, and operation mode)
 * for searching and registering.
 */
void requestGetPreferredNetworkType(void *data, size_t datalen,
                                    RIL_Token t)
{
    int err = 0;
    int response = 0;
    int rat;
    char *line;
    ATResponse *atresponse;

    err = at_send_command_singleline("AT+KSRAT?", "+KSRAT:", &atresponse);
    if (err < 0 || atresponse->success == 0 )
        goto error;

    line = atresponse->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0)
        goto error;

    err = at_tok_nextint(&line, &rat);
    if (err < 0)
        goto error;

    assert(rat >= HL8_RAT_GSM_ONLY && rat <= HL7_RAT_LTE_PREPERRED);

    switch (rat) {
    case HL8_RAT_GSM_ONLY:
        response = RIL_RAT_GSM_ONLY;
        break;    
    case HL8_RAT_WCDMA_ONLY:
        response = RIL_RAT_WCDMA_ONLY;
        break;
    case HL8_RAT_GSM_PREPERRED:
        response = RIL_RAT_GSM_WCDMA_AUTO;
        break;
    case HL8_RAT_WCDMA_PREPERRED:
        response = RIL_RAT_WCDMA_PREPERRED;
        break;
    case HL7_RAT_LTE_ONLY:
        response = RIL_RAT_LTE_ONLY;
        break;
    case HL7_RAT_WCDMA_PREPERRED:
    case HL7_RAT_LTE_PREPERRED:
        response = RIL_RAT_LTE_WCDMA;
        break;
    default:
        LOGE("requestGetPreferredNetworkType got a rat type %d", rat);
        goto error;
        break;
    }
    RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(int));

finally:
    at_response_free(atresponse);
    return;

error:
    LOGE("getpreferrednwtype error" );
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    goto finally;
}

/**
 * RIL_REQUEST_QUERY_NETWORK_SELECTION_MODE
 *
 * Query current network selectin mode.
 */
void requestQueryNetworkSelectionMode(void *data, size_t datalen,
                                      RIL_Token t)
{
    int err;
    ATResponse *atresponse = NULL;
    int response = 0;
    int mode = -1;
    char *line;

    /* Workaround for HL8518 firmware issue */
    if(!IsRegistered())
        return RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
        
    err = at_send_command_singleline("AT+COPS?", "+COPS:", &atresponse);

    if (err < 0 || atresponse->success == 0)
        goto error;

    line = atresponse->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0)
        goto error;

    err = at_tok_nextint(&line, &response);
    if (err < 0)
        goto error;

    switch (response) {
    case 0:
        mode = 0;   /* automatic mode */
        break;    
    case 1:
    case 4:
        mode = 1;   /* manual mode */
        break;
    default:
        /* Any other values should never appear on a query.  Note that if the
           modem was explicitly de-registered using mode 2, then the mode would
           be 2, but this should never happen if the RIL is operating correctly.
         */
        goto error;
        break;
    }

    net_sel_mode = mode;

    /* Return the successful response and cache the value for next time. It is
       okay to cache the response even if the current mode is 4 (manual with auto
       fallback) and eventually goes to 0 (auto) because we will always get the
       +CREG/+CGREG AT notifications when the modem switches the mode, which will
       cause the old cached value (manual) to be cleared.
     */
    swiril_cache_RIL_onRequestComplete(t, RIL_E_SUCCESS, &mode, sizeof(int),
                                       RIL_REQUEST_QUERY_NETWORK_SELECTION_MODE);

finally:
    at_response_free(atresponse);
    return;

error:
    LOGE("requestQueryNetworkSelectionMode must never return error when radio is on: mode=%i", mode);
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    goto finally;
}

/**
 * RIL_REQUEST_SET_LOCATION_UPDATES
 *
 * Enables/disables network state change notifications due to changes in
 * LAC and/or CID (basically, +CREG=2 vs. +CREG=1).  
 *
 * Note:  The RIL implementation should default to "updates enabled"
 * when the screen is on and "updates disabled" when the screen is off.
 *
 * See also: RIL_REQUEST_SCREEN_STATE, RIL_UNSOL_RESPONSE_NETWORK_STATE_CHANGED.
 */
void requestSetLocationUpdates(void *data, size_t datalen, RIL_Token t)
{
    int enable = 0;
    int err = 0;
    char *cmd;
    ATResponse *atresponse = NULL;

    enable = ((int *) data)[0];
    if(enable != 0 && enable != 1) {
        goto error;
    }

    asprintf(&cmd, "AT+CREG=%d", (enable == 0 ? 1 : 2));
    err = at_send_command(cmd, &atresponse);
    free(cmd);

    if (err < 0 || atresponse->success == 0)
        goto error;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

finally:
    at_response_free(atresponse);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    goto finally;
}


/**
 *
 * RIL_UNSOL_SIGNAL_STRENGTH
 *
 * Call-back function to indicate signal strength has changed
 * 
 * @param [in] param
 *     generic call-back parameter -- not used
 *
 * @return
 *     none
 *
 * @par Abort:<br>
 *     none
 *
 * @note
 *     See also: RIL_REQUEST_SIGNAL_STRENGTH
 *
 */
static void onSignalStrengthChanged(void *param)
{
#if defined(SWI_RIL_VERSION_12)
    requestSignalStrength_r12(NULL, 0, NULL);
#elif defined(SWI_RIL_VERSION_6)
    requestSignalStrength_r6(NULL, 0, NULL);
#else
    requestSignalStrength(NULL, 0, NULL);
#endif
}


/**
 *
 * Interpret and handle the +CIEV AT unsolicited response
 * 
 * @param [in] s
 *     string containing +CIEV response line
 *
 * @return
 *     none
 *
 * @par Abort:<br>
 *     none
 *
 * @note
 *     none
 *
 */
void onIndicatorEvent(const char *s)
{
    /* This may be a signal strength change, but could be something
       else, so make sure.  If we get any errors, then just ignore the
       line and don't do anything with it.
     */
    int err;
    int result;
    char *line;
    char *start;

    /* the string in 's' should never be modified */
    start = line = strdup(s);

    err = at_tok_start(&line);
    if (err < 0)
        goto error;
    
    err = at_tok_nextint(&line, &result);
    if (err < 0)
        goto error;
    
    if (result == HL_CIND_INDEX_SIGNAL) {
        /* It is a signal strength indication */
        enqueueRILEvent(RIL_EVENT_QUEUE_NORMAL, onSignalStrengthChanged,
                        NULL, NULL);
    }
    else {
        LOGI("onIndicatorEvent: unhandled indication: %i", result);
    }

error:
    free(start);
}


/**
 * Handle the NITZ, Time Zone and DST information
 * 
 *
 * @return
 *     none
 *
 * @par Abort:<br>
 *     none
 *
 * @note
 *     none
 *
 */
void updateNITZTime(void)
{ 
        int yy, mm, dd, hr, min, sec;
        int sum_min, dayofyear,leapYearIndex, century_yr; 
        int mon;
        struct tm ctm={0};
        char *response;
        const unsigned short int mon_yday[2][13] =
        {
            /* Normal years.  */
            { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
            /* Leap years.  */
            { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 }
        };

        char propDST[PROPERTY_VALUE_MAX];
        char propTZ[PROPERTY_VALUE_MAX];
        char propNITZ[PROPERTY_VALUE_MAX];
        unsigned int dst = 0;
        
        if(property_get("ril.sierra.dst", propDST, "0")>0)
        {
            dst = atoi(propDST);
            if(dst>2) {
                LOGD("invalid DST value %d", dst);
                dst = 0;
            }
        }
        else
            LOGD("invalid DST value. And use default value 0!");
            
        if(!property_get("ril.sierra.tz", propTZ, "+0"))
            LOGD("invalid TZ value");
        
        if(!property_get("ril.sierra.nitz", propNITZ, "0"))
            LOGD("invalid NITZ value ");
        
        if(sscanf(propNITZ, "%d/%d/%d,%d:%d:%d", &yy, &mm, &dd, &hr, &min, &sec) != 6)
        {
            LOGD("invalid time info and skip update time!");
            return;
        }
        
        /* Work around for HL module report current time 
           instead of UTC time in "+CTZV:" indication */
        century_yr = yy + 2000;
        ctm.tm_year = century_yr - 1900;  //years since 1900
        ctm.tm_mon = mm - 1;              //months since January	0-11
        ctm.tm_mday = dd;
        mktime(&ctm);
        
        // calculate the UTC time from local time
        sum_min = (hr * MINUTES_OF_HOUR + min) - (atoi(propTZ) * 15);
        dayofyear = ctm.tm_yday + 1;
        if(sum_min < 0) {
            sum_min += MINUTES_OF_DAY;
            hr = sum_min / MINUTES_OF_HOUR;
            min = sum_min % MINUTES_OF_HOUR;
            dayofyear -= 1;
        }
        else if (sum_min >= MINUTES_OF_DAY){
            hr -= MINUTES_OF_DAY;
            hr = sum_min / MINUTES_OF_HOUR;
            min = sum_min % MINUTES_OF_HOUR;
            dayofyear += 1;
        }
        else {
            hr = sum_min / MINUTES_OF_HOUR;
            min = sum_min % MINUTES_OF_HOUR;
        }
        leapYearIndex =((century_yr % 4 == 0 && century_yr % 100 != 0) || (century_yr % 400 == 0));

        for (mon = 0; mon < 12; mon++) {
           if (dayofyear <= mon_yday[leapYearIndex][mon+1]) {
               mm = mon + 1;
               dd = dayofyear - mon_yday[leapYearIndex][mon];
               break;
           }
        }
        
        asprintf(&response, "%02d/%02d/%02d,%02d:%02d:%02d%s,%i",yy,mm,dd,
                                                           hr,min,sec, propTZ, dst);
        RIL_onUnsolicitedResponse (RIL_UNSOL_NITZ_TIME_RECEIVED, response, strlen(response));
       
        property_set( "ril.sierra.nitz", "" );
        property_set( "ril.sierra.tz", "" );
        property_set( "ril.sierra.dst", "" );
        if (response != NULL)
           free(response);
}        
 
/**
 *
 * Network Disconnect received - Deactivate PDP context
 *
 * Call-back function to deactivate PDP context
 *
 * @param [in] param
 *     none
 *
 * @return
 *     none
 *
 * @par Abort:<br>
 *     none
 *
 * @note
 *     - only called in response to network disconnect, so if there is an error
 *       we just LOG the error, and don't do anything else.
 *     - should be called through a RIL event, since AT commands cannot be
 *       sent by the reader thread.
 *
 */
static void sendPDPDeactivate(void *param)
{
    int  err;
    char *cmdp;
    ATResponse *atresponsep = NULL;

    /* De-activate the PDP context */
    if(isDataSessionActive())
    {
        asprintf( &cmdp,
              "AT+CGACT=0,%d",
              Default_CID()); /* Default PDP Context 1 */
        err = at_send_command(cmdp, &atresponsep);
        if (err < 0)
            LOGE("%s %s err %d", __func__, cmdp, err);
        free(cmdp);
        at_response_free(atresponsep);
    }
    checkDHCPnStopService();
    initDataCallResponseList();
    RIL_onUnsolicitedResponse(RIL_UNSOL_DATA_CALL_LIST_CHANGED,
                              NULL,
                              0);
}

/**
 *
 * Handle the +CTZV time zone change AT unsolicited response
 * 
 * @param [in] s
 *     string containing +CTZV response line -- not used
 *
 * @return
 *     none
 *
 * @par Abort:<br>
 *     none
 *
 * @note
 *     none
 *
 */
void onNetworkTimeReceived(const char *s)
{
    char *p = NULL;
    char *mresponse;
    char *tz;
    char *line = NULL;
    int err;

    line = p = strdup(s);
    err = at_tok_start(&p);
    if (err < 0)
        goto error;
        
    err = at_tok_nextstr(&p, &tz);
    if (err < 0)
        goto error;
    
    err = at_tok_nextstr(&p, &mresponse);
    if (err < 0)
        goto error;
    

    /* Work around for HL module store time info 
       and wait for "+CTZDST:" indication */
    LOGD("Set TZ to %s", tz);
    property_set( "ril.sierra.tz", tz );       
    LOGD("Set NITZ to %s", mresponse);
    property_set( "ril.sierra.nitz", mresponse );
       
    free(p);
    free(line);
    return;
    
error:   
    LOGE("invalid NITZ line %s\n", s); 
    free(p);
    free(line);
}

/**
 *
 * Handle the +CTZDST day light saving time AT unsolicited response
 * 
 * @param [in] s
 *     string containing +CTZDST response line
 *
 * @return
 *     none
 *
 * @par Abort:<br>
 *     none
 *
 * @note
 *     none
 *
 */
void onNetworkDSTReceived(const char *s)
{
    int int_dst = 0;
    char *p = NULL;
    int err;
    char *dst;

    p = strdup(s);
    
    err = at_tok_start(&p);
    if (err < 0) {
        LOGE("invalid DST line \n");
        goto error;
    }
    err = at_tok_nextint(&p, &int_dst);
    if (err < 0) {
        LOGE("invalid DST line \n");
        goto error;
    }
    
error:    
    asprintf( &dst,"%d",int_dst);
    LOGD("set dst to %d",int_dst);
    property_set( "ril.sierra.dst", dst );
    
    /* Both NITZ and DST received. Update time */
    updateNITZTime();
        
    free(dst);
    free(p);
}

/**
 *
 * RIL_UNSOL_RESPONSE_NETWORK_STATE_CHANGED
 * 
 * Handle +CREG and +CGREG registration status AT unsolicited response
 * 
 * @param [in] s
 *     string containing +CREG/+CGREG response line
 *
 * @return
 *     1 if registration updated
 *
 * @par Abort:<br>
 *     none
 *
 * @note
 *     maintains static variable containing the current registration status
 *
 */
int onNetworkRegistrationReceived(const char *s)
{
    /* Keep track of current registration status with a boolean flag.  This
       info is only needed here, so keep it local to the function.
     */
    static int regstatus=0;
    int newregstatus;
    int value;

    char *line;
    char *start;
    int err;

    /* the string in 's' should never be modified */
    start = line = strdup(s);

    err = at_tok_start(&line);
    if (err < 0)
        goto error;

    err = at_tok_nextint(&line, &value);
    if (err < 0)
        goto error;

    /* These values are from the <stat> field for +CREG/+CGREG */
    newregstatus = ( (NW_REGISTERED == value) ||
                     (NW_REGISTERED_ROAMING == value) );

    /* Always send network state changed */
    RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_NETWORK_STATE_CHANGED,
                              NULL, 0);

    /* Since we don't have a NITZ related AT unsolicited response, fake it by
       triggering from initial network registration.  The format of +CREG and
       +CGREG are the same, at least for the first value in the response, which
       is the registration status.
     */

    /* If the registration status changed, and we are now registered, 
       assume network time was also updated.
     */
    if (newregstatus != regstatus) {
        regstatus = newregstatus;

        if (regstatus) {
            //enqueueRILEvent(RIL_EVENT_QUEUE_NORMAL, sendNITZtime,
            //                NULL, NULL);
        }
        else
        {
            /* De-activate the PDP context if network is lost/searching */
            if(IsHL8()) /* deactivate PDP 1 will cause deregistration on HL75xx */
                enqueueRILEvent(RIL_EVENT_QUEUE_NORMAL, sendPDPDeactivate,NULL, &TIMEVAL_2S);
        }
        return 1;
    }
    else
    	return 0;

error:
    free(start);
    return 0;
}

/**
 *
 * Get the RAT from *CNTI
 *
 * @param[out] rat 
 *     Pointer to the RIL RAT value
 * 
 * @return
 *      true  - it gets RAT correctly
 *      false - otherwise
 *
 * @par Abort:<br> 
 *     none
 *
 * @note
 *     None
 */
bool getRILRatAlternative(RIL_RAT *rat)
{
    int err = 0;
    ATResponse *atresponse = NULL;
    char *techstrp;
    char *line;
    int discard;
    bool bRet = false;
    int i;
    
    //HL
    return false;

    err = at_send_command_singleline("AT*CNTI=0", "*CNTI:", &atresponse);
    if ((err < 0) ||
        (at_get_cme_error(atresponse) != CME_SUCCESS)) {
        goto done;
    }

    line = atresponse->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0)
        goto done;

    /* discard initial int */
    err = at_tok_nextint(&line, &discard);
    if (err < 0)
        goto done;
    
    /* get the current technology */
    err = at_tok_nextstr(&line, &techstrp);
    if (err < 0)
        goto done;

    i = 0;
    while (swi_rat_umts_table[i].ratStr != NULL) {
        if ( strcmp(techstrp, swi_rat_umts_table[i].ratStr) == 0 ) {
            *rat = swi_rat_umts_table[i].ril_rat;
            bRet = true;
            break;
        }
        i++;
    }

done:
    at_response_free(atresponse);
    return bRet;
}

/**
 *
 * Check if modem is operating in LTE mode
 *
 * @return
 *      true  - it is operating in LTE mode
 *      false - otherwise
 *
 * @par Abort:<br> 
 *     none
 *
 * @note
 *     None
 */
bool isOperatingLTE(void)
{
    RIL_RAT rat;
    bool bRet = false;
    
    if (getRILRatAlternative(&rat) && 
        (rat == RIL_RAT_LTE)) {
        bRet = true;
    }
    
    return bRet;
}

/**
 *
 * RIL_UNSOL_RESPONSE_NETWORK_STATE_CHANGED
 *
 * Call-back function to indicate network state has changed
 * 
 * @param [in] param
 *     generic call-back parameter -- not used
 *
 * @return
 *     none
 *
 * @par Abort:<br>
 *     none
 *
 * @note
 *     - If data session is active, this callback will send RIL_UNSOL_NETWOTK_STATE_CHANGED
 *       after every 60 seconds. Android Framework in turn will ask for 
 *       RIL_REQUEST_GPRS_REGISTRATION_STATE to update current RAT.
 *     - This polling is being done because RIL_UNSOL_NETWORK_STATE_CHANGED is not triggered 
 *       when RAT chnages from 3G to H
 *        
 */
static void pollCurrentRadioTechnology(void *param)
{
    if(getScreenState())
    {
        LOGD("%s Enter :", __func__);
        if (isDataSessionActive()) {
            s_RatPoll = true;
            RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_NETWORK_STATE_CHANGED, NULL, 0);
            enqueueRILEvent(RIL_EVENT_QUEUE_NORMAL, pollCurrentRadioTechnology,
                            NULL, &TIMEVAL_CURRENT_RAT_POLL);
        }
        else {
            s_RatPoll = false;
            LOGD("%s data session is not active!!", __func__);
        }
    }
    else
    {
        s_RatPoll = true;
        enqueueRILEvent(RIL_EVENT_QUEUE_NORMAL, pollCurrentRadioTechnology,
                    NULL, &TIMEVAL_CURRENT_RAT_POLL);
    }
}

/**
 *
 * start polling to get current radio technogy
 *
 * @return
 *     none
 *
 * @par Abort:<br> 
 *     none
 *
 * @note
 *     None
 */
void startPollingForRadioTechnology(void)
{
    LOGD("%s start polling to get current RAT with s_RatPoll: %d", __func__, s_RatPoll);
    if(s_RatPoll == false) {
        enqueueRILEvent(RIL_EVENT_QUEUE_NORMAL, pollCurrentRadioTechnology,
                        NULL, &TIMEVAL_CURRENT_RAT_POLL);
    }
}

/**
 * RIL_REQUEST_IMS_REGISTRATION_STATE
 *
 * Requests that IMS registration state.
 */
void requestQueryIMSRegistrationState(void *data, size_t datalen,
                                          RIL_Token t)
{
    ATResponse *atresponse = NULL;
    char *line;
    int reply[2] = {0,0};
    int err;
    int n, reg_info;

    err = at_send_command_singleline("AT+CIREG?", "+CIREG:", &atresponse);

    if (err < 0 ||
        atresponse->success == 0 ||
        atresponse->p_intermediates == NULL)
        goto error;

    line = atresponse->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0)
        goto error;

    /* +CIREP: <n>,<reg_info>[,<ext_info>] */
    err = at_tok_nextint(&line, &n);
    if (err < 0)
        goto error;

    err = at_tok_nexthexint(&line, &reg_info);
    if (err < 0)
        goto error;

    reply[0] = reg_info;    //IMS registration status : 1=registered
    reply[1]=1;             //HL7 support 1=3GPP only

    LOGI("%s: IMS state=%d, SMS format =%d", __func__, reply[0], reply[1]);
    RIL_onRequestComplete(t, RIL_E_SUCCESS, reply, sizeof(reply));

finally:
    at_response_free(atresponse);
    return;

error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    goto finally;

}

void initLTEProperties(void)
{
    char *lteOnGsm = "1";
    char *lteOnCdma = "0";

    /* Init the properties for HL7, hardcode for now*/
    property_set( "telephony.lteOnGsmDevice", lteOnGsm );
    LOGI("Setting %s=%s", "telephony.lteOnGsmDevice", lteOnGsm);
    property_set( "telephony.lteOnCdmaDevice", lteOnCdma );
    LOGI("Setting %s=%s", "telephony.lteOnCdmaDevice", lteOnCdma);
}




#ifdef SWI_RIL_VERSION_9

bool isAPNProfileIdentical(char *pAPNName)
{
    ATResponse *atresponse;
    ATLine *cursor;
    int err;
    int n = 0;
    int i = 0;
    char *out;
    char apn[255];
    int cid;

    memset(apn,0,sizeof(apn));

    if(!pAPNName)
    {
        LOGE("%s invalid parameters", __func__);
        return false;
    }

    err = at_send_command_multiline("AT+CGDCONT?", "+CGDCONT:",&atresponse);
    if (err != 0 || atresponse->success == 0) {
        goto error;
    }

    for (cursor = atresponse->p_intermediates; cursor != NULL;
         cursor = cursor->p_next) {
        char *line = cursor->line;

        err = at_tok_start(&line);
        if (err < 0)
            goto error;

        /*CID*/
        err = at_tok_nextint(&line, &cid);
        if (err < 0)
            goto error;

        /*Type*/
        err = at_tok_nextstr(&line, &out);
        if (err < 0)
            goto error;

        /*APN*/
        err = at_tok_nextstr(&line, &out);
        if (err < 0)
            goto error;

        strcpy(apn, out);

        /* Break the loop if default CID is matched */
        if(Default_CID() == cid)
            break;

        /*Address*/
        err = at_tok_nextstr(&line, &out);
        if (err < 0)
            goto error;
    }

    /*Check default profile 0 */
    if(strcmp(apn,pAPNName)) {
        LOGI("%s profile APN will update from %s to %s", __func__, apn, pAPNName);
        at_response_free(atresponse);
        return false;
    }
    at_response_free(atresponse);
    return true;
error:
    at_response_free(atresponse);
    return false;
}


void ResetRadio(void)
{
    ATResponse *atresponse;
    int err;

    err = at_send_command("AT+CFUN=4", &atresponse);
    if (err != 0 || atresponse->success == 0) {
        LOGE("PowerOff Radio Failed");
    }
    err = at_send_command("AT+CFUN=1", &atresponse);
    if (err != 0 || atresponse->success == 0) {
        LOGE("PowerOn Radio Failed");
    }
}


void requestSetInitialAPN(void *data, size_t datalen, RIL_Token t)
{
    ATResponse *atresponse = NULL;
    RIL_InitialAttachApn    *p_pf;
    int err;
    char *cmd;
    p_pf = (RIL_InitialAttachApn *)data;

    if (!p_pf->apn) {
        LOGI("%s : apn is invalid,NULL", __func__);
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
        return;
    }
    if (!strlen(p_pf->apn)) {
        LOGI("%s : apn is invalid, empty string", __func__);
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
        return;
    }

    if (isAPNProfileIdentical(p_pf->apn))
    {
        LOGI("%s : Profile identical.", __func__);
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
        return;
    }

    LOGI("%s APN info: apn=%s, protocol=%s, username=%s, password=%s",
        __func__,p_pf->apn,p_pf->protocol, p_pf->username,p_pf->password);

    asprintf(&cmd, "AT+CGDCONT=%d,\"IP\",\"%s\"", Default_CID(), p_pf->apn);
    err = at_send_command(cmd, &atresponse);
    free(cmd);
    if ((err < 0) || (atresponse->success == 0)) {
        LOGE("%s failed to set default profile", __func__);
        at_response_free(atresponse);
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    }
    else {
        at_response_free(atresponse);
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
        /*HL7528 need to reset radio after APN changed*/
        if(!IsHL8())
            ResetRadio();
    }
}



bool IsRegistered(void)
{
    int response[VOICE_REGISTRATION_STATE_PARAMS];
    bool hasAct = false;
    unsigned long CID = 0;
    
    memset(response, 0, sizeof(response));
    if (queryRegState(true, &response[0], &CID, &hasAct)) 
    {
        if((response[0] == RIL_REG_STATE_REGISTERED ) || (response[0] == RIL_REG_STATE_ROAMING))
            return true;
    }
    return false;
}
#endif


#ifdef SWI_RIL_VERSION_11

/**
 * Calculate number of total cells present by cmd AT+KCELL
 * considering GSM, LTE and UMTS
 */
static int calculateNumCells()
{
    ATResponse *atresponse;
    ATLine *cursor;
    int err;
    int nCells = 0;
    int cellType;

    err = at_send_command_multiline("AT+KCELL=0", "+KCELL:",&atresponse);
    if (err != 0 || atresponse->success == 0) {
        goto error;
    }

    for (cursor = atresponse->p_intermediates; cursor != NULL;
         cursor = cursor->p_next) {
        char *line = cursor->line;
        int cells = 0;

        err = at_tok_start(&line);
        if (err < 0)
            goto error;

        /* number of cells */
        err = at_tok_nextint(&line, &cells);
        if (err < 0)
            goto error;

        if (cells) {
           /* type of cell*/
           err = at_tok_nextint(&line, &cellType);
           if (err < 0)
               goto error;
           switch(cellType) {
               case RIL_CELL_GSM_SERVING:
               case RIL_CELL_GSM_NEIGHBOR:
               case RIL_CELL_UMTS_SERVING:
               case RIL_CELL_UMTS_NEIGHBOR:
               case RIL_CELL_UMTS_DETECTED:
               case RIL_CELL_LTE_SERVING:
               case RIL_CELL_LTE_NEIGHBOR:
                   nCells = nCells + cells;
                   break;
               default:
                   break;
            }
        }
    }
    at_response_free(atresponse);
    return nCells;
error:
    at_response_free(atresponse);
    return -1;
}

/**
 * Get MCC and MNC, need to fetch from 6 digit PLMN string
 */
void getmccmnc(char *pout, int *mcc, int *mnc)
{
    int i,j;
    char newstr[64];
    char smcc[4];
    char smnc[4];
    int len;
    bool bUnknown = false;
    int ret;
    LOGI("%s PLMN string=%s\n", __func__,pout);
    len = strlen(pout);
    if (len != 6) {
        LOGE("%s PLMN string not correct", __func__);
        return;
    }
    for (i = 0; i < len; i=i+2) {
        newstr[i]= pout[i+1];
        newstr[i+1]= pout[i];
    }
    newstr[len]='\0';

    LOGI("%s converted PLMN string=%s\n", __func__,newstr);
    
    /* Let us check now first three digits for MCC */
    for (i=0; i<3; i++) {
        smcc[i]=newstr[i];
        if (newstr[i] == 'f'){
           bUnknown = true;
           break;
        }
    }
    smcc[3]='\0';    
    if (!bUnknown) {
       ret = atoi(smcc);
       *mcc = ret;
    }
    
    LOGI("%s MCC=%d\n", __func__,*mcc);

    /* next three digits for MNC */
    if((newstr[4] == 'f') || (newstr[5] == 'f'))
        return;

    /* If MNC is 2 digit */
    if (newstr[3] == 'f') {
        j=0;
        smnc[j++] = newstr[4];
        smnc[j++] = newstr[5];
        smnc[j] ='\0';
    } 
    /* if MNC 3 digits */
    else {
        j=0;
        smnc[j++] = newstr[4];
        smnc[j++] = newstr[5];
        smnc[j++] = newstr[3];
        smnc[j] ='\0';
    }

    ret = atoi(smnc);
    *mnc = ret;
    LOGI("%s MNC=%d\n", __func__,*mnc);
    return;
}

/**
 * Calculate cell type and fill info for all cells found in a single line 
 */
static int fetchCellTypeAndFillCellsInfo(void *presp, char *line, int nCells, int *index)
{
    int err;
    int i;
    int cellType;
    int l_index = *index;
    int  iout;
    char *sout;
    int mcc = -1;
    int mnc = -1;
    //UMTS
    int rscp;
    int eco;
    //LTE
    int rsrp;
    int rsrq;
    RIL_CellInfo *resp = (RIL_CellInfo *)presp;
          
    /* type of cell*/
    err = at_tok_nextint(&line, &cellType);
    if (err < 0)
        goto error;

    /* It is believed that in a single line all cells will be of same type, either
     * GSM, or UMTS, or LTE 
     */
    switch(cellType) {
        case RIL_CELL_GSM_SERVING:
        case RIL_CELL_GSM_NEIGHBOR:
        case RIL_CELL_UMTS_SERVING:
        case RIL_CELL_UMTS_NEIGHBOR:
        case RIL_CELL_UMTS_DETECTED:
        case RIL_CELL_LTE_SERVING:
        case RIL_CELL_LTE_NEIGHBOR:
            /* 
             * Fill the cell information in the RIL_CellInfo 
             * For GSM cells:
             * +KCELL:
             * <nbGSMcells>[,<cell_typei>,<ARFCNi>,<BSICi>,<PLMNi>,
             * <LACi>,<CIi>,<RSSIi>,<TA>][,<cell_typei>,<ARFCNi>,
             * <BSICi>,<PLMNi>,<LACi>,<CIi>,<RSSIi>] [...]]
             *
             * For UMTS cells:
             * +KCELL:
             * <nbUMTScells>[,<cell_typek>,<dl_UARFCNk>,<PLMNk>,
             * <LACk>,<UMTS_CIk>,<scrambling_codek>,<rscpk>,
             *<ecnok>[,<pathlossk>]][...]]
             *
             * For LTE cells:
             * +KCELL: <nbLTEcells>[,<cell_type>,<PLMN>,<LTE_CI>,<PhyCellInd>,
             * <trackingAreaCode>,<RSRRResult>,<RSRQResult>,<LTE_TA>][<cell_type>,[[Earfcn>,
             * [<PhyCellID>,[<RSRPResult>,[<RSRQResult>]]]]][...]]
             */
            for (i = 0; i < nCells; i++) {
                switch(cellType){
                    case RIL_CELL_GSM_SERVING:
                    case RIL_CELL_GSM_NEIGHBOR:
                        resp[l_index].cellInfoType = 1;
                        break;
                    case RIL_CELL_UMTS_SERVING:
                    case RIL_CELL_UMTS_NEIGHBOR:
                    case RIL_CELL_UMTS_DETECTED:
                        resp[l_index].cellInfoType = 4;
                        break;
                    case RIL_CELL_LTE_SERVING:
                    case RIL_CELL_LTE_NEIGHBOR:
                        resp[l_index].cellInfoType = 3;
                        break;
                }
                if ((cellType == RIL_CELL_GSM_SERVING) || (cellType == RIL_CELL_UMTS_SERVING) || (cellType == RIL_CELL_LTE_SERVING) )
                    resp[l_index].registered = 1;
                else
                    resp[l_index].registered = 0;
                resp[l_index].timeStampType = RIL_TIMESTAMP_TYPE_OEM_RIL;
                resp[l_index].timeStamp = ril_nano_time();

                /* GSM cell */
                if ((cellType == RIL_CELL_GSM_SERVING) || (cellType == RIL_CELL_GSM_NEIGHBOR)) {
                    /* ARFCNi */
                    err = at_tok_nextint(&line, &iout);
                    if (err < 0)
                        goto error;

                    /* BSICi */
                    err = at_tok_nextint(&line, &iout);
                    if (err < 0)
                        goto error;

                    /* PLMNIi */
                    err = at_tok_nextstr(&line, &sout);
                    if ((err) < 0 || (sout == NULL))
                        goto error;

                    /* get mcc and mnc from this string */
                    resp[l_index].CellInfo.gsm.cellIdentityGsm.mcc = INT_MAX;
                    resp[l_index].CellInfo.gsm.cellIdentityGsm.mnc = INT_MAX;
                    getmccmnc(sout,&mcc, &mnc);
                    if (mcc != -1)
                        resp[l_index].CellInfo.gsm.cellIdentityGsm.mcc = mcc;
                    if (mnc != -1)
                        resp[l_index].CellInfo.gsm.cellIdentityGsm.mnc = mnc;

                    /* LAC */
                    err = at_tok_nexthexint(&line, &iout);
                    if (err < 0)
                        goto error;
                    resp[l_index].CellInfo.gsm.cellIdentityGsm.lac = INT_MAX;
                    if (iout != 0xffff)
                        resp[l_index].CellInfo.gsm.cellIdentityGsm.lac = iout;

                    /* Cell Id */
                    err = at_tok_nexthexint(&line, &iout);
                    if (err < 0)
                        goto error;
                    resp[l_index].CellInfo.gsm.cellIdentityGsm.cid = INT_MAX;
                    if (iout != 0xffff)
                        resp[l_index].CellInfo.gsm.cellIdentityGsm.cid = iout;

                    /* RSSI  TBD:*/
                    err = at_tok_nextint(&line, &iout);
                    if (err < 0)
                        goto error;
                    resp[l_index].CellInfo.gsm.signalStrengthGsm.bitErrorRate = RIL_GSM_SIGNAL_BIT_ERROR_RATE_DEFAULT;
                    resp[l_index].CellInfo.gsm.signalStrengthGsm.signalStrength = iout;

                    /* TA , only for serving cell */
                    if (cellType == RIL_CELL_GSM_SERVING) {
                        err = at_tok_nextint(&line, &iout);
                        if (err < 0)
                            goto error;
                    }
                    LOGI("%s: GSM Cell %d : CellType=%d, LAC=0x%x,cid =0x%x,RSSI=%d",__func__,l_index, cellType,
                         resp[l_index].CellInfo.gsm.cellIdentityGsm.lac,
                         resp[l_index].CellInfo.gsm.cellIdentityGsm.cid,
                         resp[l_index].CellInfo.gsm.signalStrengthGsm.signalStrength);                                            

                    /* fetch cell type again */
                    if ( i < (nCells -1)) {
                        err = at_tok_nextint(&line, &cellType);
                        if (err < 0)
                            goto error;
                    }
                }
                /* UMTS Cell */
                else if ((cellType == RIL_CELL_UMTS_SERVING) || (cellType == RIL_CELL_UMTS_NEIGHBOR) ||
                         (cellType == RIL_CELL_UMTS_DETECTED)) {
                    /* dl_UARFCNk */
                    err = at_tok_nextint(&line, &iout);
                    if (err < 0)
                        goto error;

                    /* PLMNIk */
                    err = at_tok_nextstr(&line, &sout);
                    if ((err) < 0 || (sout == NULL))
                        goto error;

                    /* get mcc and mnc from this string */
                    resp[l_index].CellInfo.wcdma.cellIdentityWcdma.mcc = INT_MAX;
                    resp[l_index].CellInfo.wcdma.cellIdentityWcdma.mnc = INT_MAX;
                    getmccmnc(sout,&mcc, &mnc);
                    if (mcc != -1)
                        resp[l_index].CellInfo.wcdma.cellIdentityWcdma.mcc = mcc;
                    if (mnc != -1)
                        resp[l_index].CellInfo.wcdma.cellIdentityWcdma.mnc = mnc;

                    /* LAC */
                    err = at_tok_nexthexint(&line, &iout);
                    if (err < 0)
                        goto error;
                    resp[l_index].CellInfo.wcdma.cellIdentityWcdma.lac = INT_MAX;
                    if (iout != 0xffff)
                        resp[l_index].CellInfo.wcdma.cellIdentityWcdma.lac = iout;

                    /* Cell Id */
                    err = at_tok_nexthexint(&line, &iout);
                    if (err < 0)
                        goto error;
                    resp[l_index].CellInfo.wcdma.cellIdentityWcdma.cid = INT_MAX;
                    if (iout != 0xfffffff)
                        resp[l_index].CellInfo.wcdma.cellIdentityWcdma.cid = iout;

                    /* scrambling code */
                    err = at_tok_nextint(&line, &iout);
                    if (err < 0)
                        goto error;
                    resp[l_index].CellInfo.wcdma.cellIdentityWcdma.psc = iout;

                    /* rscp  TBD:*/
                    err = at_tok_nextint(&line, &rscp);
                    if (err < 0)
                        goto error;

                    /* eco */
                    err = at_tok_nextint(&line, &eco);
                    if (err < 0)
                        goto error;
                    resp[l_index].CellInfo.wcdma.signalStrengthWcdma.bitErrorRate = RIL_GSM_SIGNAL_BIT_ERROR_RATE_DEFAULT;                    
                    resp[l_index].CellInfo.wcdma.signalStrengthWcdma.signalStrength = (rscp/eco); /* TO BE CHECKED */

                    LOGI("%s: UMTS Cell %d: CellType=%d, LAC=0x%x,cid =0x%x, scramblingcode=%d, RSSI=%d",__func__,l_index, cellType,
                         resp[l_index].CellInfo.wcdma.cellIdentityWcdma.lac,
                         resp[l_index].CellInfo.wcdma.cellIdentityWcdma.cid,
                         resp[l_index].CellInfo.wcdma.cellIdentityWcdma.psc,
                         resp[l_index].CellInfo.wcdma.signalStrengthWcdma.signalStrength);                      

                    /* pathlossk : To BE CHECKED */
                    err = at_tok_nextint(&line, &iout);
                    if (err < 0)
                        goto error;

                    /* fetch cell type again */
                    if ( i < (nCells -1)) {
                        err = at_tok_nextint(&line, &cellType);
                        if (err < 0)
                            goto error;
                    }
                }
                /* LTE CELL */
                else {
                     if (cellType == RIL_CELL_LTE_SERVING) {
                         /* PLMNI */
                         err = at_tok_nextstr(&line, &sout);
                         if ((err) < 0 || (sout == NULL))
                            goto error;

                         /* get mcc and mnc from this string */
                         resp[l_index].CellInfo.lte.cellIdentityLte.mcc = INT_MAX;
                         resp[l_index].CellInfo.lte.cellIdentityLte.mnc = INT_MAX;
                         getmccmnc(sout,&mcc, &mnc);
                         if (mcc != -1)
                             resp[l_index].CellInfo.lte.cellIdentityLte.mcc = mcc;
                         if (mnc != -1)
                             resp[l_index].CellInfo.lte.cellIdentityLte.mnc = mnc;

                         /* Cell Id */
                         err = at_tok_nexthexint(&line, &iout);
                         if (err < 0)
                             goto error;
                         resp[l_index].CellInfo.lte.cellIdentityLte.ci = INT_MAX;
                         if (iout != 0xfffffff)
                         resp[l_index].CellInfo.lte.cellIdentityLte.ci = iout;

                         /* phy cell id */
                         err = at_tok_nextint(&line, &iout);
                         if (err < 0)
                            goto error;

                         resp[l_index].CellInfo.lte.cellIdentityLte.pci = iout;

                         /* tracking area code */
                         err = at_tok_nextint(&line, &iout);
                         if (err < 0)
                            goto error;

                         resp[l_index].CellInfo.lte.cellIdentityLte.tac = iout;

                         /* RSRR */
                         err = at_tok_nextint(&line, &rsrp);
                         if (err < 0)
                            goto error;

                         /* RSRQ */
                         err = at_tok_nextint(&line, &rsrq);
                         if (err < 0)
                            goto error;
                         
                         /* LTE TA */
                         err = at_tok_nextint(&line, &iout);
                         if (err < 0)
                            goto error;
               
                         resp[l_index].CellInfo.lte.signalStrengthLte.signalStrength = rsrp; //To BE CHECKED
                         resp[l_index].CellInfo.lte.signalStrengthLte.rsrq = rsrq;
                         resp[l_index].CellInfo.lte.signalStrengthLte.rsrp = rsrp;
                         resp[l_index].CellInfo.lte.signalStrengthLte.rssnr = INT_MAX;
                         resp[l_index].CellInfo.lte.signalStrengthLte.cqi = INT_MAX;
                         resp[l_index].CellInfo.lte.signalStrengthLte.timingAdvance = iout;
                     }
                     else { /* LTE Neighbor */
                         /* earfcn */
                         err = at_tok_nextint(&line, &iout);
                         if (err < 0)
                            goto error;

                         /* phy cell id */
                         err = at_tok_nextint(&line, &iout);
                         if (err < 0)
                            goto error;

                         resp[l_index].CellInfo.lte.cellIdentityLte.pci = iout;

                         /* RSRP */
                         err = at_tok_nextint(&line, &rsrp);
                         if (err < 0)
                            goto error;
                         
                         /* rsrq */
                         err = at_tok_nextint(&line, &rsrq);
                         if (err < 0)
                            goto error;

                         resp[l_index].CellInfo.lte.cellIdentityLte.mcc = INT_MAX;
                         resp[l_index].CellInfo.lte.cellIdentityLte.mnc = INT_MAX;

                         resp[l_index].CellInfo.lte.cellIdentityLte.ci = INT_MAX;
                         resp[l_index].CellInfo.lte.cellIdentityLte.tac = INT_MAX;
                          
                         resp[l_index].CellInfo.lte.signalStrengthLte.signalStrength = rsrp; //To BE CHECKED
                         resp[l_index].CellInfo.lte.signalStrengthLte.rsrq = rsrq;
                         resp[l_index].CellInfo.lte.signalStrengthLte.rsrp = rsrp;
                         resp[l_index].CellInfo.lte.signalStrengthLte.rssnr = INT_MAX;
                         resp[l_index].CellInfo.lte.signalStrengthLte.cqi = INT_MAX;
                         resp[l_index].CellInfo.lte.signalStrengthLte.timingAdvance = INT_MAX;
                    }
                    LOGI("%s: LTE Cell %d: CellType=%d, Cid =0x%x, phy cid=%d, track area code=%d, rsrp=%d, rsrq=%d, ta=%d",
                         __func__,l_index,cellType,
                         resp[l_index].CellInfo.lte.cellIdentityLte.ci,
                         resp[l_index].CellInfo.lte.cellIdentityLte.pci,
                         resp[l_index].CellInfo.lte.cellIdentityLte.tac,
                         resp[l_index].CellInfo.lte.signalStrengthLte.rsrp,
                         resp[l_index].CellInfo.lte.signalStrengthLte.rsrq,
                         resp[l_index].CellInfo.lte.signalStrengthLte.timingAdvance);

                    /* fetch cell type again */
                    if ( i < (nCells -1)) {
                        err = at_tok_nextint(&line, &cellType);
                        if (err < 0)
                            goto error;
                    }
                }                     
                l_index++;
            }                       
            break;
        default:
            break;
    }
    *index = l_index;
    return 0;
error:
    LOGI("%s error in response parsing", __func__);
    return -1;
}

/**
 * RIL_REQUEST_GET_CELL_INFO_LIST
 *
 * Request all cells info including neighbor cell in GSM/UMTS/LTE network
 */
void requestGetAllCellsInfoList(void *data, size_t datalen, RIL_Token t)
{
    ATResponse *atresponse = NULL;
    ATLine *cursor;
    int err;
    char *line;
    RIL_CellInfo *resp = NULL;
    RIL_NeighboringCell **respp = NULL;
    int numCells = 0;
    int curIndex = 0;
    ATLine *atlinep = NULL;
    int rscp;

    /* calculate the number of total cells first, only GSM and UMTS for now */
    numCells = calculateNumCells();
    if (numCells == -1) {
        LOGE("%s failed to calculate number of cells", __func__);
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
        return;
    }
    if (numCells == 0) {
        LOGE("%s number of cells are 0", __func__);
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
        return;
    }

    LOGI("%s: number of cells are %d", __func__,numCells);

    /* allocate memory for the array of RIL_CellInfo based on number of cells */
    resp = (RIL_CellInfo *)swimalloc(numCells * sizeof(RIL_CellInfo), 
                                        "requestGetAllCellsInfoList allocate RIL_CellInfo memory failed");
    memset(resp, 0, (numCells * sizeof(RIL_CellInfo)));

    err = at_send_command_multiline("AT+KCELL=0", "+KCELL:",&atresponse);
    if (err != 0 || atresponse->success == 0) {
        goto error;
    }

    for (cursor = atresponse->p_intermediates; cursor != NULL;
         cursor = cursor->p_next) {
        char *line = cursor->line;
        int cells = 0;

        err = at_tok_start(&line);
        if (err < 0)
            goto error;

        /* number of cells */
        err = at_tok_nextint(&line, &cells);
        if (err < 0)
            goto error;

        if (cells) {
            err = fetchCellTypeAndFillCellsInfo(resp,line,cells,&curIndex);
            if (err < 0)
                goto error;        
        }
    }
    RIL_onRequestComplete(t, RIL_E_SUCCESS, resp, (numCells * sizeof(RIL_CellInfo)));
finally:
    at_response_free(atresponse);    
    free(resp);
    return;
error:
    LOGI("%s: error in response parsing", __func__);    
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    goto finally;
}
#endif

