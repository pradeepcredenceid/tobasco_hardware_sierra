/**
 * \ingroup cbk
 *
 * \file qaGobiApiCbk.c
 *
 * \brief  Entry points for Gobi APIs for the Callback Management
 *
 * Copyright: © 2011-2013 Sierra Wireless, Inc. all rights reserved
 *
 */

#include "SwiDataTypes.h"
#include "sludefs.h"
#include "qmudefs.h"
#include "qmerrno.h"
#include "amudefs.h"
#include "qaQmiNotify.h"
#include "qaQmiBasic.h"
#include "qaGobiApiCbk.h"
#include "qaGobiApiUim.h"
#include "qaGobiApiIms.h"
#include "qaGobiApiImsa.h"
#include "qaCbkWdsSetEventReport.h"
#include "qaCbkDmsSetEventReport.h"
#include "qaCbkNasSetEventReport.h"
#include "qaCbkPdsSetEventReport.h"
#include "qaCbkWmsSetEventReport.h"
#include "qaCbkCatSetEventReport.h"
#include "qaCbkSetQosEventReport.h"
#include "qaCbkSwiOmaDmSetEventReport.h"
#include "qaCbkOmaDmSetEventReport.h"
#include "qaCbkVoiceUssdInd.h"
#include "qaCbkVoiceUssdNoWaitInd.h"
#include "qaCbkVoiceIndicationRegister.h"
#include "qaCbkVoiceSLQSVoiceAllCallStatusInd.h"
#include "qaCbkWmsMemoryFullInd.h"
#include "qaCbkWmsMessageWaitingInd.h"
#include "qaGobiApiDcs.h"
#include "qaCbkDmsSwiSetEventReport.h"
#include "qaCbkSwiOmaDmSetEventReportExt.h"
#include "qaCbkDcsSetNetChange.h"
#include "os/swi_osapi.h"
#include "qaGobiApiWds.h"

/* local data storage */
local struct TransferStatsDataType     transferStat;
local struct SignalStrengthDataType    signalStrength;
local struct CATEventDataType          catEvent;
local struct SLQSSignalStrengthsIndReq SLQSSignalStrengthInfo;

/* functions */

global ULONG SLQSSetSessionStateCallback(
    tFNSLQSSessionState     pCallback )
{
    ULONG  resultCode = eQCWWAN_ERR_NONE;
    if( NULL != pCallback )
    {
        resultCode = SLQSStartSrv(REGISTER_SRV, WDS_SRV);
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
             return resultCode;
        }

    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case
     * The Session state is broadcasted by the Modem and hence no request
     * need to be formulated.
     */
    qaQmiSetCallback( eQMI_CB_SESSION_STATE, pCallback );
    return eQCWWAN_ERR_NONE;
}

global enum eQMICallbackIndex
qmiInst2QosEvent( BYTE instance )
{
    return eQMI_CB_QOS_EVENT_0 + instance;
}

global enum eQMICallbackIndex
qmiInst2QosFlowStatusEvent( BYTE instance )
{
    return eQMI_CB_QOS_FLOW_STATUS_EVENT_0 + instance;
}

global enum eQMICallbackIndex
qmiInst2DcsNetEvent( BYTE instance )
{
    return eQMI_CB_DCS_NET_0 + instance;
}

global enum eQMICallbackIndex
qmiInst2WdsEvent( BYTE instance )
{
    return eQMI_CB_WDS_EVENT_0 + instance;
}

global ULONG iSLQSSetWdsEventCallback(
    tFNSLQSWDSEvent pCallback,
    BYTE            interval,
    BYTE            instance )
{
    ULONG  resultCode = eQCWWAN_ERR_NONE;
    BYTE   *pInParam;      /* ptr to param field rx'd from modem */
    BYTE   *pOutParam;     /* ptr to outbound param field */
    BYTE   *pReqBuf;       /* Pointer to ougoing request buffer */
    USHORT ParamLength;    /* Ret'd length of the QMI Parm field */
    enum   eQmiCbkSetStatus paramStatus;

    /* Storage for results and response variable */
    struct QmiCbkWdsSetEventReportResp response;

    if ( instance >= INVALID_INSTACNE )
    {
        return eQCWWAN_ERR_INVALID_ARG;
    }

    /* resolving callback by QMI instance id */
    void *pCbk = qaQmiGetCallback( qmiInst2WdsEvent(instance) );
    /*
     * Already in the same state as before ( subscribed/unsubscribed ), hence
     * no device update is required
     */
    if( (pCbk == NULL) && (pCallback == NULL) )
    {
        /* resolving callback by QMI instance id */
        qaQmiSetCallback( qmiInst2WdsEvent(instance), pCallback );
        transferStat.interval = 0; /* reset the reporting interval */
        return resultCode;
    }

    /* Initialize the pointer to the outgoing request buffer pointer */
    pReqBuf = qmgetreqbkp();

    /* Get a pointer to the start of the outbound QMI Parameter field */
    pOutParam = amgetparamp( AMTYPEQMIREQUEST, pReqBuf );

    /* Invoke the function which packs QMI message */
    paramStatus = (NULL == pCallback) ? QMI_CBK_PARAM_RESET : QMI_CBK_PARAM_SET;
    interval   = (NULL == pCallback ) ? 0 : interval;

    resultCode = PkQmiCbkWdsSetEventReport( &ParamLength,
               /* Data Bearer */            paramStatus,
               /* Dormancy State */         paramStatus,
               /* Mobile IP */              QMI_CBK_PARAM_NOCHANGE,
               /* Transfer stats */         paramStatus,
               /* Current Data Bearer */    paramStatus,
               /* Byte Totals/Packet Count - interval */ interval,
                                            pOutParam );

    if (resultCode != eQCWWAN_ERR_NONE)
    {
        qmrelreqbkp();
        return resultCode;
    }

    /* Prepare and send the blocking call */
    resultCode = SwiQmiMISendnWait( pReqBuf,
                                   eQMI_SVC_WDS,
                                   ParamLength,
                                   eQMI_TIMEOUT_2_S,
                                   &pInParam,
                                   &ParamLength,
                                   instance );

    /* Only parse out the response data if we got a positive return */
    if ( resultCode == eQCWWAN_ERR_NONE )
    {
        /* Copy to the caller's buffer */
        resultCode = UpkQmiCbkWdsSetEventReport( pInParam, &response );

        /* Failure Case in the unpack routine */
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
            qmrelreqbkp();
            return resultCode;
        }
    }
    /* The SwiQmiSendnWait has failed */
    else
    {
        qmrelreqbkp();
        return resultCode;
    }

    /* Enable/Disable the callback in the list */
    /* resolving callback by QMI instance id */
    qaQmiSetCallback( qmiInst2WdsEvent(instance), pCallback );
    transferStat.interval = interval; /* save user specified reporting interval */
    qmrelreqbkp();
    return resultCode;
}

global ULONG SLQSSetWdsEventCallback(
    tFNSLQSWDSEvent pCallback,
    BYTE            interval,
    BYTE            instance )
{
    BYTE   IPV4          = 4;
    BYTE   IPV6          = 6;
    ULONG  resultCode    = eQCWWAN_ERR_NONE;
    ULONG  rc            = -1;
    if( !WDS_IsGobiDevice() &&
        eQCWWAN_ERR_NONE != (rc = iSLQSMISetIPFamilyPreference(IPV4, instance )) )
    {
        return rc;
    }
    if(eQCWWAN_ERR_NONE == rc)
    {
        resultCode = iSLQSSetWdsEventCallback( pCallback,
                                               interval,
                                               instance );
    }

    if( !WDS_IsGobiDevice() &&
        eQCWWAN_ERR_NONE != (rc = iSLQSMISetIPFamilyPreference(IPV6, instance)) )
    {
        return rc;
    }
    if(eQCWWAN_ERR_NONE == rc)
    {
        resultCode = iSLQSSetWdsEventCallback( pCallback,
                                               interval,
                                               instance );
    }

    return resultCode;
}



global ULONG iSLQSSetWdsFirstInstEventCallback(
    tFNSLQSWDSEvent pCallback )
{
    return SLQSSetWdsEventCallback( pCallback,
                                    transferStat.interval,
                                    FIRST_INSTANCE );
}

global ULONG iSLQSSetWdsSecondInstEventCallback(
    tFNSLQSWDSEvent pCallback )
{
    return SLQSSetWdsEventCallback( pCallback,
                                    transferStat.interval,
                                    SECOND_INSTANCE );
}

global ULONG iSLQSSetWdsThirdInstEventCallback(
    tFNSLQSWDSEvent pCallback )
{
    return SLQSSetWdsEventCallback( pCallback,
                                    transferStat.interval,
                                    THIRD_INSTANCE );
}

global ULONG SetPowerCallback(
    tFNPower pCallback )
{
    ULONG  resultCode = eQCWWAN_ERR_NONE;
    BYTE   *pInParam;   /* ptr to param field rx'd from modem */
    BYTE   *pOutParam;  /* ptr to outbound param field */
    BYTE   *pReqBuf;    /* Pointer to ougoing request buffer */
    USHORT paramLength; /* Ret'd length of the QMI Parm field */
    enum   eQmiCbkSetStatus operatingMode;

    /* Storage for results and response variable */
    struct QmiCbkDmsSetEventReportResp response;

    void *pCbk = qaQmiGetCallback( eQMI_CB_POWER );
    /*
     * Already in the same state as before ( subscribed/unsubscribed ), hence
     * no device update is required
     */
    if( ((pCbk == NULL) && (pCallback == NULL)) ||
        ((pCbk != NULL) && (pCallback != NULL)) )
    {
        /* Update the list and exit */
        qaQmiSetCallback( eQMI_CB_POWER, pCallback );
        return resultCode;
    }

    /* Initialize the pointer to the outgoing request buffer pointer */
    pReqBuf = qmgetreqbkp();

    /* Get a pointer to the start of the outbound QMI Parameter field */
    pOutParam = amgetparamp( AMTYPEQMIREQUEST, pReqBuf );

    /* Set or reset the TLVs based on the registeration status */
    operatingMode =
            (NULL == pCallback ) ? QMI_CBK_PARAM_RESET : QMI_CBK_PARAM_SET;
    resultCode = PkQmiCbkDmsSetEventReport( &paramLength,
                          /* Power State */ QMI_CBK_PARAM_NOCHANGE,
                        /* Battery Level */ QMI_CBK_PARAM_NOCHANGE,
                            /* PIN State */ QMI_CBK_PARAM_NOCHANGE,
                    /* Activation Status */ QMI_CBK_PARAM_NOCHANGE,
                       /* Operating Mode */ operatingMode,
                           /* UIM Status */ QMI_CBK_PARAM_NOCHANGE,
               /* Wireless Disable State */ QMI_CBK_PARAM_NOCHANGE,
                                            pOutParam );

    if ( resultCode != eQCWWAN_ERR_NONE )
    {
        qmrelreqbkp();
        return resultCode; /* No memory */
    }
    /* Prepare and send the blocking call */
    resultCode = SwiQmiSendnWait( pReqBuf,
                                  eQMI_SVC_DMS,
                                  paramLength,
                                  eQMI_TIMEOUT_2_S, /* 2 Seconds */
                                  &pInParam,
                                  &paramLength );
    /* Only parse out the response data if we got a positive return */
    if ( resultCode == eQCWWAN_ERR_NONE )
    {
        /* Copy to the caller's buffer */
        resultCode = UpkQmiCbkDmsSetEventReport( pInParam, &response );
        /* Failure Case in the unpack routine */
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
            qmrelreqbkp();
            return resultCode;
        }
    }
    /* The SwiQmiSendnWait has failed */
    else
    {
        qmrelreqbkp();
        return resultCode;
    }

    /* Update the callback in the list */
    qaQmiSetCallback( eQMI_CB_POWER, pCallback );
    qmrelreqbkp();
    return resultCode;
}

global ULONG SetActivationStatusCallback(
    tFNActivationStatus pCallback )
{
    ULONG  resultCode = eQCWWAN_ERR_NONE;
    BYTE   *pInParam;   /* ptr to param field rx'd from modem */
    BYTE   *pOutParam;  /* ptr to outbound param field */
    BYTE   *pReqBuf;    /* Pointer to ougoing request buffer */
    USHORT paramLength; /* Ret'd length of the QMI Parm field */
    enum   eQmiCbkSetStatus activationStatus;

    /* Storage for results and response variable */
    struct QmiCbkDmsSetEventReportResp response;

    void *pCbk = qaQmiGetCallback( eQMI_CB_ACTIVATION_STATUS );
    /*
     * Already in the same state as before ( subscribed/unsubscribed ), hence
     * no device update is required
     */
    if( ((pCbk == NULL) && (pCallback == NULL)) ||
        ((pCbk != NULL) && (pCallback != NULL)) )
    {
        /* Update the list and exit */
        qaQmiSetCallback( eQMI_CB_ACTIVATION_STATUS, pCallback );
        return resultCode;
    }

    /* Initialize the pointer to the outgoing request buffer pointer */
    pReqBuf = qmgetreqbkp();

    /* Get a pointer to the start of the outbound QMI Parameter field */
    pOutParam = amgetparamp( AMTYPEQMIREQUEST, pReqBuf );

    /* Invoke the function which packs the QMI message */
    activationStatus =
                (NULL == pCallback ) ? QMI_CBK_PARAM_RESET : QMI_CBK_PARAM_SET;
    resultCode = PkQmiCbkDmsSetEventReport( &paramLength,
                          /* Power State */ QMI_CBK_PARAM_NOCHANGE,
                        /* Battery Level */ QMI_CBK_PARAM_NOCHANGE,
                            /* PIN State */ QMI_CBK_PARAM_NOCHANGE,
                    /* Activation Status */ activationStatus,
                       /* Operating Mode */ QMI_CBK_PARAM_NOCHANGE,
                           /* UIM Status */ QMI_CBK_PARAM_NOCHANGE,
               /* Wireless Disable State */ QMI_CBK_PARAM_NOCHANGE,
                                            pOutParam );

    if ( resultCode != eQCWWAN_ERR_NONE )
    {
        qmrelreqbkp();
        return resultCode; /* No memory */
    }

    /* Prepare and send the blocking call */
    resultCode = SwiQmiSendnWait( pReqBuf,
                                  eQMI_SVC_DMS,
                                  paramLength,
                                  eQMI_TIMEOUT_2_S, /* 2 Seconds */
                                  &pInParam,
                                  &paramLength );

    /* Only parse out the response data if we got a positive return */
    if ( resultCode == eQCWWAN_ERR_NONE )
    {
        /* Copy to the caller's buffer */
        resultCode = UpkQmiCbkDmsSetEventReport( pInParam, &response );

        /* Failure Case in the unpack routine */
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
            qmrelreqbkp();
            return resultCode;
        }
    }
    /* The SwiQmiSendnWait has failed */
    else
    {
        qmrelreqbkp();
        return resultCode;
    }

    /* Disable/Enable the callback from the list */
    qaQmiSetCallback( eQMI_CB_ACTIVATION_STATUS, pCallback );
    qmrelreqbkp();
    return resultCode;
}

global ULONG SetMobileIPStatusCallback(
    tFNMobileIPStatus pCallback )
{
    ULONG  resultCode = eQCWWAN_ERR_NONE;
    BYTE   *pInParam;   /* ptr to param field rx'd from modem */
    BYTE   *pOutParam;  /* ptr to outbound param field */
    BYTE   *pReqBuf;    /* Pointer to ougoing request buffer */
    USHORT paramLength; /* Ret'd length of the QMI Parm field */
    enum   eQmiCbkSetStatus MIPStatus;

    /* Storage for results and response variable */
    struct QmiCbkWdsSetEventReportResp response;

    void *pCbk = qaQmiGetCallback( eQMI_CB_MOBILE_IP );
    /*
     * Already in the same state as before ( subscribed/unsubscribed ), hence
     * no device update is required
     */
    if( ((pCbk == NULL) && (pCallback == NULL)) ||
        ((pCbk != NULL) && (pCallback != NULL)) )
    {
        /* Update the list and exit */
        qaQmiSetCallback( eQMI_CB_MOBILE_IP, pCallback );
        return resultCode;
    }

    /* Initialize the pointer to the outgoing request buffer pointer */
    pReqBuf = qmgetreqbkp();

    /* Get a pointer to the start of the outbound QMI Parameter field */
    pOutParam = amgetparamp( AMTYPEQMIREQUEST, pReqBuf );

    /* Invoke the function which packs the QMI message */
    MIPStatus = (NULL == pCallback ) ? QMI_CBK_PARAM_RESET : QMI_CBK_PARAM_SET;
    resultCode = PkQmiCbkWdsSetEventReport( &paramLength,
               /* Data Bearer */            QMI_CBK_PARAM_NOCHANGE,
               /* Dormancy State */         QMI_CBK_PARAM_NOCHANGE,
               /* Mobile IP */              MIPStatus,
               /* Byte Totals */            QMI_CBK_PARAM_NOCHANGE,
               /* Current Data Bearer */    QMI_CBK_PARAM_NOCHANGE,
       /* Bytes totals callback interval */ 0,
                                            pOutParam );

    if ( resultCode != eQCWWAN_ERR_NONE )
    {
        qmrelreqbkp();
        return resultCode; /* No memory */
    }

    /* Prepare and send the blocking call */
    resultCode = SwiQmiSendnWait( pReqBuf,
                                  eQMI_SVC_WDS,
                                  paramLength,
                                  eQMI_TIMEOUT_2_S,
                                  &pInParam,
                                  &paramLength );

    /* Only parse out the response data if we got a positive return */
    if ( resultCode == eQCWWAN_ERR_NONE )
    {
        /* Copy to the caller's buffer */
        resultCode = UpkQmiCbkWdsSetEventReport( pInParam, &response );

        /* Failure Case in the unpack routine */
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
            qmrelreqbkp();
            return resultCode;
        }
    }
    /* The SwiQmiSendnWait has failed */
    else
    {
        qmrelreqbkp();
        return resultCode;
    }

    /* Enable/Disable the callback from the list */
    qaQmiSetCallback( eQMI_CB_MOBILE_IP, pCallback );
    qmrelreqbkp();
    return resultCode;
}

global ULONG SetRoamingIndicatorCallback(
    tFNRoamingIndicator pCallback )
{
    ULONG  resultCode = eQCWWAN_ERR_NONE;
    if ( NULL != pCallback )
    {
        resultCode = SLQSStartSrv(REGISTER_SRV, NAS_SRV);
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
             return resultCode;
        }
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case
     * The Roaming Indicator is broadcasted by the Modem and hence no request
     * need to be formulated.
     */
    qaQmiSetCallback( eQMI_CB_ROAMING_INDICATOR, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SetDataCapabilitiesCallback(
    tFNDataCapabilities pCallback )
{
    ULONG  resultCode = eQCWWAN_ERR_NONE;
    if ( NULL != pCallback )
    {
        resultCode = SLQSStartSrv(REGISTER_SRV, NAS_SRV);
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
             return resultCode;
        }
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case
     * The Data Capabilities are broadcasted by the Modem and hence no request
     * need to be formulated.
     */
    qaQmiSetCallback ( eQMI_CB_DATA_CAPABILITIES, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SetSignalStrengthCallback(
    tFNSignalStrength pCallback,
    BYTE              thresholdsSize,
    INT8              *pThresholds )
{
    ULONG  resultCode  = eQCWWAN_ERR_NONE;
    BYTE   *pInParam;   /* ptr to param field rx'd from modem */
    BYTE   *pOutParam;  /* ptr to outbound param field */
    BYTE   *pReqBuf;    /* Pointer to outgoing request buffer */
    USHORT paramLength; /* Ret'd length of the QMI Parm field */
    enum   eQmiCbkSetStatus signalStrengthStatus;

    /* Storage for results and response variable */
    struct QmiCbkNasSetEventReportResp response;

    /* pThresholds is OUT parameter and hence should not be NULL */
    if ( !pThresholds )
        return eQCWWAN_ERR_INVALID_ARG;

    /* validate thresholdsSize and pThresholds value*/
    if( NULL == pCallback )
    {
        if( (0 != thresholdsSize) &&
            (0 != pThresholds[0]) )
            return eQCWWAN_ERR_INVALID_ARG;
    }
    else if ( (thresholdsSize < 1) ||
              (thresholdsSize > SIGSTRENGTH_THRESHOLD_ARR_SZ) )
         return eQCWWAN_ERR_INVALID_ARG;

    /* Initialize the pointer to the outgoing request buffer pointer */
    pReqBuf = qmgetreqbkp();

    /* Get a pointer to the start of the outbound QMI Parameter field */
    pOutParam = amgetparamp( AMTYPEQMIREQUEST, pReqBuf );

    /* Invoke the function which packs the QMI message */
    signalStrengthStatus =
                (NULL == pCallback ) ? QMI_CBK_PARAM_RESET : QMI_CBK_PARAM_SET;
    resultCode = PkQmiCbkNasSetEventReport( &paramLength,
            /* Signal Strength Indicator */ signalStrengthStatus,
                                            thresholdsSize,
                                            pThresholds,
                  /* RF Band Information */ QMI_CBK_PARAM_NOCHANGE,
          /* Registration Reject Reasons */ QMI_CBK_PARAM_NOCHANGE,
                       /* RSSI Indicator */ QMI_CBK_PARAM_NOCHANGE,
                       /* ECIO Indicator */ QMI_CBK_PARAM_NOCHANGE,
                         /* IO Indicator */ QMI_CBK_PARAM_NOCHANGE,
                       /* SINR Indicator */ QMI_CBK_PARAM_NOCHANGE,
                 /* Error Rate Indicator */ QMI_CBK_PARAM_NOCHANGE,
                        /*RSRQ Indicator */ QMI_CBK_PARAM_NOCHANGE,
             /* ECIO Threshold Indicator */ QMI_CBK_PARAM_NOCHANGE,
             /* SINR Threshold Indicator */ QMI_CBK_PARAM_NOCHANGE,
                                            NULL,
                                            pOutParam );

    if ( resultCode != eQCWWAN_ERR_NONE )
    {
        qmrelreqbkp();
        return resultCode; /* No memory */
    }

    /* Prepare and send the blocking call */
    resultCode = SwiQmiSendnWait( pReqBuf,
                                  eQMI_SVC_NAS,
                                  paramLength,
                                  eQMI_TIMEOUT_2_S, /* 2 Seconds */
                                  &pInParam,
                                  &paramLength );

    /* Only parse out the response data if we got a positive return */
    if ( resultCode == eQCWWAN_ERR_NONE )
    {
        /* Copy to the caller's buffer */
        resultCode = UpkQmiCbkNasSetEventReport( pInParam, &response );

        /* Failure Case in the unpack routine */
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
            qmrelreqbkp();
            return resultCode;
        }
    }
    /* The SwiQmiSendnWait has failed */
    else
    {
        qmrelreqbkp();
        return resultCode;
    }

    /* Disable/Enable the callback from the list */
    qaQmiSetCallback( eQMI_CB_SIGNAL_STRENGTH, pCallback );

    /* save the value of input parameters */
    signalStrength.thresholdsSize = thresholdsSize;
    slmemcpy( (void *)signalStrength.thresholds,
              (void *)pThresholds,
               thresholdsSize );

    qmrelreqbkp();
    return resultCode;
}

global ULONG iSetSignalStrengthCallback(
    tFNSignalStrength pCallback )
{
    return SetSignalStrengthCallback( pCallback,
                                      signalStrength.thresholdsSize,
                                      signalStrength.thresholds );
}

global ULONG SetRFInfoCallback(
    tFNRFInfo pCallback )
{
    ULONG  resultCode  = eQCWWAN_ERR_NONE;
    BYTE   *pInParam;   /* ptr to param field rx'd from modem */
    BYTE   *pOutParam;  /* ptr to outbound param field */
    BYTE   *pReqBuf;    /* Pointer to outgoing request buffer */
    USHORT paramLength; /* Ret'd length of the QMI Parm field */
    enum   eQmiCbkSetStatus RFInfo;

    /* Storage for results and response variable */
    struct QmiCbkNasSetEventReportResp response;

    void *pCbk = qaQmiGetCallback( eQMI_CB_RF_INFO );
    /*
     * Already in the same state as before ( subscribed/unsubscribed ), hence
     * no device update is required
     */
    if( ((pCbk == NULL) && (pCallback == NULL)) ||
        ((pCbk != NULL) && (pCallback != NULL)) )
    {
        /* Update the list and exit */
        qaQmiSetCallback( eQMI_CB_RF_INFO, pCallback );
        return resultCode;
    }

    /* Initialize the pointer to the outgoing request buffer pointer */
    pReqBuf = qmgetreqbkp();

    /* Get a pointer to the start of the outbound QMI Parameter field */
    pOutParam = amgetparamp( AMTYPEQMIREQUEST, pReqBuf );

    /* Invoke the function which packs the QMI message */
    RFInfo = (NULL == pCallback ) ? QMI_CBK_PARAM_RESET : QMI_CBK_PARAM_SET;
    resultCode = PkQmiCbkNasSetEventReport( &paramLength,
            /* Signal Strength Indicator */ QMI_CBK_PARAM_NOCHANGE,
                                            0,
                                            NULL,
                  /* RF Band Information */ RFInfo,
           /*Registration Reject Reasons */ QMI_CBK_PARAM_NOCHANGE,
                       /* RSSI Indicator */ QMI_CBK_PARAM_NOCHANGE,
                       /* ECIO Indicator */ QMI_CBK_PARAM_NOCHANGE,
                         /* IO Indicator */ QMI_CBK_PARAM_NOCHANGE,
                       /* SINR Indicator */ QMI_CBK_PARAM_NOCHANGE,
                 /* Error Rate Indicator */ QMI_CBK_PARAM_NOCHANGE,
                        /*RSRQ Indicator */ QMI_CBK_PARAM_NOCHANGE,
             /* ECIO Threshold Indicator */ QMI_CBK_PARAM_NOCHANGE,
             /* SINR Threshold Indicator */ QMI_CBK_PARAM_NOCHANGE,
                                            NULL,
                                            pOutParam );

    if ( resultCode != eQCWWAN_ERR_NONE )
    {
        qmrelreqbkp();
        return resultCode; /* No memory */
    }

    /* Prepare and send the blocking call */
    resultCode = SwiQmiSendnWait( pReqBuf,
                                  eQMI_SVC_NAS,
                                  paramLength,
                                  eQMI_TIMEOUT_2_S, /* 2 Seconds */
                                  &pInParam,
                                  &paramLength );

    /* Only parse out the response data if we got a positive return */
    if ( resultCode == eQCWWAN_ERR_NONE )
    {
        /* Copy to the caller's buffer */
        resultCode = UpkQmiCbkNasSetEventReport( pInParam, &response );

        /* Failure Case in the unpack routine */
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
            qmrelreqbkp();
            return resultCode;
        }
    }
    /* The SwiQmiSendnWait has failed */
    else
    {
        qmrelreqbkp();
        return resultCode;
    }

    /* Disable/Enable the callback from the list */
    qaQmiSetCallback( eQMI_CB_RF_INFO, pCallback );
    qmrelreqbkp();
    return resultCode;
}

global ULONG SetLURejectCallback(
    tFNLUReject pCallback )
{
    ULONG  resultCode  = eQCWWAN_ERR_NONE;
    BYTE   *pInParam;   /* ptr to param field rx'd from modem */
    BYTE   *pOutParam;  /* ptr to outbound param field */
    BYTE   *pReqBuf;    /* Pointer to outgoing request buffer */
    USHORT paramLength; /* Ret'd length of the QMI Parm field */
    enum   eQmiCbkSetStatus LUReject;

    /* Storage for results and response variable */
    struct QmiCbkNasSetEventReportResp response;

    void *pCbk = qaQmiGetCallback( eQMI_CB_LU_REJECT );
    /*
     * Already in the same state as before ( subscribed/unsubscribed ), hence
     * no device update is required
     */
    if( ((pCbk == NULL) && (pCallback == NULL)) ||
        ((pCbk != NULL) && (pCallback != NULL)) )
    {
        /* Update the list and exit */
        qaQmiSetCallback( eQMI_CB_LU_REJECT, pCallback );
        return resultCode;
    }

    /* Initialize the pointer to the outgoing request buffer pointer */
    pReqBuf = qmgetreqbkp();

    /* Get a pointer to the start of the outbound QMI Parameter field */
    pOutParam = amgetparamp( AMTYPEQMIREQUEST, pReqBuf );

    /* Invoke the function which packs the QMI message */
    LUReject = (NULL == pCallback ) ? QMI_CBK_PARAM_RESET : QMI_CBK_PARAM_SET;
    resultCode = PkQmiCbkNasSetEventReport( &paramLength,
            /* Signal Strength Indicator */ QMI_CBK_PARAM_NOCHANGE,
                                            0,
                                            NULL,
                  /* RF Band Information */ QMI_CBK_PARAM_NOCHANGE,
          /* Registration Reject Reasons */ LUReject,
                       /* RSSI Indicator */ QMI_CBK_PARAM_NOCHANGE,
                       /* ECIO Indicator */ QMI_CBK_PARAM_NOCHANGE,
                         /* IO Indicator */ QMI_CBK_PARAM_NOCHANGE,
                       /* SINR Indicator */ QMI_CBK_PARAM_NOCHANGE,
                 /* Error Rate Indicator */ QMI_CBK_PARAM_NOCHANGE,
                        /*RSRQ Indicator */ QMI_CBK_PARAM_NOCHANGE,
             /* ECIO Threshold Indicator */ QMI_CBK_PARAM_NOCHANGE,
             /* SINR Threshold Indicator */ QMI_CBK_PARAM_NOCHANGE,
                                            NULL,
                                            pOutParam );

    if ( resultCode != eQCWWAN_ERR_NONE )
    {
        qmrelreqbkp();
        return resultCode; /* No memory */
    }

    /* Prepare and send the blocking call */
    resultCode = SwiQmiSendnWait( pReqBuf,
                                  eQMI_SVC_NAS,
                                  paramLength,
                                  eQMI_TIMEOUT_2_S, /* 2 Seconds */
                                  &pInParam,
                                  &paramLength );

    /* Only parse out the response data if we got a positive return */
    if ( resultCode == eQCWWAN_ERR_NONE )
    {
        /* Copy to the caller's buffer */
        resultCode = UpkQmiCbkNasSetEventReport( pInParam, &response );

        /* Failure Case in the unpack routine */
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
            qmrelreqbkp();
            return resultCode;
        }
    }
    /* The SwiQmiSendnWait has failed */
    else
    {
        qmrelreqbkp();
        return resultCode;
    }

    /* Disable/Enable the callback from the list */
    qaQmiSetCallback( eQMI_CB_LU_REJECT, pCallback );
    qmrelreqbkp();
    return resultCode;
}

ULONG SetNewSMSCallback(
    tFNNewSMS pCallback )
{
    ULONG  resultCode  = eQCWWAN_ERR_NONE;  /* Return Value for the API */
    BYTE   *pInParam;   /* ptr to QMI Parameters received from modem */
    BYTE   *pOutParam;  /* ptr to QMI Parameters send to modem */
    BYTE   *pReqBuf;    /* Pointer to outgoing request buffer */
    USHORT paramLength; /* Ret'd length of the QMI Parm field */
    enum   eQmiCbkSetStatus newSMS;

    /* Storage for results and response variable */
    struct QmiCbkWmsSetEventReportResp response;

    void *pCbk = qaQmiGetCallback( eQMI_CB_NEW_SMS );
    /*
     * Already in the same state as before ( subscribed/unsubscribed ), hence
     * no device update is required
     */
    if( ((pCbk == NULL) && (pCallback == NULL)) ||
        ((pCbk != NULL) && (pCallback != NULL)) )
    {
        /* Update the list and exit */
        qaQmiSetCallback( eQMI_CB_NEW_SMS, pCallback );
        return resultCode;
    }

    /* Initialize the pointer to the outgoing request buffer pointer */
    pReqBuf = qmgetreqbkp();

    /* Get a pointer to the start of the outbound QMI Parameter field */
    pOutParam = amgetparamp( AMTYPEQMIREQUEST, pReqBuf );

    /* Packs the QMI message */
    newSMS = (NULL == pCallback ) ? QMI_CBK_PARAM_RESET : QMI_CBK_PARAM_SET;
    resultCode = PkQmiCbkWmsSetEventReport( &paramLength,
                         /* New Message */  newSMS,
                                            pOutParam );

    if ( resultCode != eQCWWAN_ERR_NONE )
    {
        qmrelreqbkp();
        return resultCode; /* No memory */
    }

    /* Prepare and send the blocking call */
    resultCode = SwiQmiSendnWait( pReqBuf,
                                  eQMI_SVC_WMS,
                                  paramLength,
                                  eQMI_TIMEOUT_2_S, /* 2 Seconds */
                                  &pInParam,
                                  &paramLength );

    /* Only parse out the response data if we got a positive return */
    if ( resultCode == eQCWWAN_ERR_NONE )
    {
        /* Copy to the caller's buffer */
        resultCode = UpkQmiCbkWmsSetEventReport( pInParam, &response );

        /* Failure Case in the unpack routine */
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
            qmrelreqbkp();
            return resultCode;
        }
    }
    /* The SwiQmiSendnWait has failed */
    else
    {
        qmrelreqbkp();
        return resultCode;
    }

    /* Disable/Enable the callback from the list */
    qaQmiSetCallback( eQMI_CB_NEW_SMS, pCallback );
    qmrelreqbkp();
    return resultCode;
}

ULONG SLQSSetSMSEventCallback( tFNSMSEvents pCallback )
{
    ULONG  resultCode  = eQCWWAN_ERR_NONE;  /* Return Value for the API */
    BYTE   *pInParam;   /* ptr to QMI Parameters received from modem */
    BYTE   *pOutParam;  /* ptr to QMI Parameters send to modem */
    BYTE   *pReqBuf;    /* Pointer to outgoing request buffer */
    USHORT paramLength; /* Ret'd length of the QMI Parm field */
    enum   eQmiCbkSetStatus SMSEvent;

    /* Storage for results and response variable */
    struct QmiCbkWmsSetEventReportResp response;

    void *pCbk = qaQmiGetCallback( eQMI_CB_SMS_EVENT );
    /*
     * Already in the same state as before ( subscribed/unsubscribed ), hence
     * no device update is required
     */
    if( ((pCbk == NULL) && (pCallback == NULL)) ||
        ((pCbk != NULL) && (pCallback != NULL)) )
    {
        /* Update the list and exit */
        qaQmiSetCallback( eQMI_CB_SMS_EVENT, pCallback );
        return resultCode;
    }

    /* Initialize the pointer to the outgoing request buffer pointer */
    pReqBuf = qmgetreqbkp();

    /* Get a pointer to the start of the outbound QMI Parameter field */
    pOutParam = amgetparamp( AMTYPEQMIREQUEST, pReqBuf );

    /* Packs the QMI message */
    SMSEvent = (NULL == pCallback ) ? QMI_CBK_PARAM_RESET : QMI_CBK_PARAM_SET;
    resultCode = PkQmiCbkWmsSetEventReport( &paramLength,
                          /* SMS Events */  SMSEvent,
                                            pOutParam );

    if ( resultCode != eQCWWAN_ERR_NONE )
    {
        qmrelreqbkp();
        return resultCode; /* No memory */
    }

    /* Prepare and send the blocking call */
    resultCode = SwiQmiSendnWait( pReqBuf,
                                  eQMI_SVC_WMS,
                                  paramLength,
                                  eQMI_TIMEOUT_2_S, /* 2 Seconds */
                                  &pInParam,
                                  &paramLength );

    /* Only parse out the response data if we got a positive return */
    if ( resultCode == eQCWWAN_ERR_NONE )
    {
        /* Copy to the caller's buffer */
        resultCode = UpkQmiCbkWmsSetEventReport( pInParam, &response );

        /* Failure Case in the unpack routine */
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
            qmrelreqbkp();
            return resultCode;
        }
    }
    /* The SwiQmiSendnWait has failed */
    else
    {
        qmrelreqbkp();
        return resultCode;
    }

    /* Disable/Enable the callback from the list */
    qaQmiSetCallback( eQMI_CB_SMS_EVENT, pCallback );
    qmrelreqbkp();
    return resultCode;
}

ULONG SetNMEACallback(
    tFNNewNMEA pCallback )
{
    ULONG  resultCode  = eQCWWAN_ERR_NONE;  /* Return Value for the API */
    BYTE   *pInParam;   /* ptr to QMI Parameters received from modem */
    BYTE   *pOutParam;  /* ptr to QMI Parameters send to modem */
    BYTE   *pReqBuf;    /* Pointer to outgoing request buffer */
    USHORT paramLength; /* Ret'd length of the QMI Parm field */
    enum   eQmiCbkSetStatus newNMEA;

    /* Storage for results and response variable */
    struct QmiCbkPdsSetEventReportResp response;

    void *pCbk = qaQmiGetCallback( eQMI_CB_NMEA );
    /*
     * Already in the same state as before ( subscribed/unsubscribed ), hence
     * no device update is required
     */
    if( ((pCbk == NULL) && (pCallback == NULL)) ||
        ((pCbk != NULL) && (pCallback != NULL)) )
    {
        /* Update the list and exit */
        qaQmiSetCallback( eQMI_CB_NMEA, pCallback );
        return resultCode;
    }

    /* Initialize the pointer to the outgoing request buffer pointer */
    pReqBuf = qmgetreqbkp();

    /* Get a pointer to the start of the outbound QMI Parameter field */
    pOutParam = amgetparamp( AMTYPEQMIREQUEST, pReqBuf );

    /* Invoke the function which packs the QMI message */
    newNMEA = (NULL == pCallback ) ? QMI_CBK_PARAM_RESET : QMI_CBK_PARAM_SET;
    resultCode = PkQmiCbkPdsSetEventReport( &paramLength,
                  /* Position Data NMEA */  newNMEA,
                /* Parsed Position Data */  QMI_CBK_PARAM_NOCHANGE,
                                            pOutParam );

    if ( resultCode != eQCWWAN_ERR_NONE )
    {
        qmrelreqbkp();
        return resultCode; /* No memory */
    }

    /* Prepare and send the blocking call */
    resultCode = SwiQmiSendnWait( pReqBuf,
                                  eQMI_SVC_PDS,
                                  paramLength,
                                  eQMI_TIMEOUT_2_S, /* 2 Seconds */
                                  &pInParam,
                                  &paramLength );

    /* Only parse out the response data if we got a positive return */
    if ( resultCode == eQCWWAN_ERR_NONE )
    {
        /* Copy to the caller's buffer */
        resultCode = UpkQmiCbkPdsSetEventReport( pInParam, &response );

        /* Failure Case in the unpack routine */
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
            qmrelreqbkp();
            return resultCode;
        }
    }
    else
    {
        qmrelreqbkp();
        return resultCode;
    }

    /* Disable/Enable the callback from the list */
    qaQmiSetCallback( eQMI_CB_NMEA, pCallback );
    qmrelreqbkp();
    return resultCode;
}

ULONG SetPDSStateCallback(
    tFNPDSState pCallback )
{
    ULONG  resultCode = eQCWWAN_ERR_NONE;
    if ( NULL != pCallback )
    {
        resultCode = SLQSStartSrv(REGISTER_SRV, PDS_SRV);
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
             return resultCode;
        }
    }

    qaQmiSetCallback ( eQMI_CB_PDS_STATE, pCallback );
    return eQCWWAN_ERR_NONE;
}

ULONG SetCATEventCallback(
    tFNCATEvent pCallback,
    ULONG       eventMask,
    ULONG       *pErrorMask )
{
    ULONG  resultCode  = eQCWWAN_ERR_NONE;
    BYTE   *pInParam;   /* ptr to param field rx'd from modem */
    BYTE   *pOutParam;  /* ptr to outbound param field */
    BYTE   *pReqBuf;    /* Pointer to outgoing request buffer */
    USHORT paramLength; /* Ret'd length of the QMI Parm field */

    /* Storage for results and response variable */
    struct QmiCbkCatSetEventReportResp response;

    if ( NULL == pCallback )
    {
         /* Disable the Callback by invoking qaQmiRemoveCallback */
         qaQmiSetCallback ( eQMI_CB_CAT_EVENT, pCallback );
         /* reset user supplied parameters */
         catEvent.eventMask = 0;
         catEvent.pErrorMask = NULL;
         return resultCode;
    }

    /* OUT parameter and hence should not be NULL */
    if ( !pErrorMask )
        return eQCWWAN_ERR_INVALID_ARG;

    /* Initialize the pointer to the outgoing request buffer pointer */
    pReqBuf = qmgetreqbkp();

    /* Get a pointer to the start of the outbound QMI Parameter field */
    pOutParam = amgetparamp( AMTYPEQMIREQUEST, pReqBuf );

    /* Invoke the function which packs the QMI message */
    eventMask = (NULL == pCallback ) ? 0 : eventMask;
    resultCode = PkQmiCbkCatSetEventReport( &paramLength,
                 /* Event Mask to be Set */ eventMask,
                                            pOutParam );

    if ( resultCode != eQCWWAN_ERR_NONE )
    {
        qmrelreqbkp();
        return resultCode; /* No memory */
    }

    /* Prepare and send the blocking call */
    resultCode = SwiQmiSendnWait( pReqBuf,
                                  eQMI_SVC_CAT,
                                  paramLength,
                                  eQMI_TIMEOUT_2_S, /* 2 Seconds */
                                  &pInParam,
                                  &paramLength );

    /* parse out the response data if the response was received succesfully */
    if ( resultCode == eQCWWAN_ERR_NONE )
    {
        /* Initializing Error Mask */
        *pErrorMask         = 0;
        response.pErrorMask = pErrorMask;

        /* Copy to the caller's buffer */
        resultCode = UpkQmiCbkCatSetEventReport( pInParam, &response );

        /* Failure Case in the unpack routine */
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
            qmrelreqbkp();
             return resultCode;
        }
    }
    else
    {
        qmrelreqbkp();
        return resultCode;
    }

    /* Disable/Enable the callback from the list */
    qaQmiSetCallback( eQMI_CB_CAT_EVENT, pCallback );

    /* save user supplied parameters */
    catEvent.eventMask  = eventMask;
    catEvent.pErrorMask = pErrorMask;

    qmrelreqbkp();
    return resultCode;
}

global ULONG iSetCATEventCallback(
    tFNCATEvent pCallback )
{
    return SetCATEventCallback( pCallback,
                                catEvent.eventMask,
                                catEvent.pErrorMask );
}

ULONG SetDeviceStateChangeCbk(
    tFNDeviceStateChange pCallback )
{
    qaQmiSetCallback( eQMI_CB_DCS_DEVICE_STATE_CHANGE, pCallback );
    return eQCWWAN_ERR_NONE;
}

ULONG SetNetChangeCbk(
    BYTE            instance,
    tFNNet          pCallback,
    ULONG           loMark,
    ULONG           hiMark,
    ULONG           period)
{
    ULONG  resultCode = eQCWWAN_ERR_NONE;
    BYTE   *pInParam;      /* ptr to param field rx'd from modem */
    BYTE   *pOutParam;     /* ptr to outbound param field */
    BYTE   *pReqBuf;       /* Pointer to ougoing request buffer */
    USHORT ParamLength;    /* Ret'd length of the QMI Parm field */

    /* Storage for results and response variable */
    struct QmiCbkDcsSetNetChangeCbkResp response;

    if ( instance >= INVALID_INSTACNE )
    {
        return eQCWWAN_ERR_INVALID_ARG;
    }
    else if ( (pCallback != NULL) && (period == 0) )
    {
        return eQCWWAN_ERR_INVALID_ARG;
    }

    /* resolving callback by QMI instance id */
    void *pCbk = qaQmiGetCallback( qmiInst2DcsNetEvent(instance) );
    /*
     * Already in the same state as before ( subscribed/unsubscribed ), hence
     * no device update is required
     */
    if( ((pCbk == NULL) && (pCallback == NULL)) ||
        ((pCbk != NULL) && (pCallback != NULL)) )
    {
        /* resolving callback by QMI instance id */
        qaQmiSetCallback( qmiInst2DcsNetEvent(instance), pCallback );
        return resultCode;
    }

    /* Initialize the pointer to the outgoing request buffer pointer */
    pReqBuf = qmgetreqbkp();

    /* Get a pointer to the start of the outbound QMI Parameter field */
    pOutParam = amgetparamp( AMTYPEQMIREQUEST, pReqBuf );

    /* Invoke the function which packs QMI message */
    resultCode = PkQmiCbkDcsSetNetChange( &ParamLength,
                                            loMark,
                                            hiMark,
                                            period,
                                            pOutParam );

    if (resultCode != eQCWWAN_ERR_NONE)
    {
        qmrelreqbkp();
        return resultCode;
    }

    /* Prepare and send the blocking call */
    resultCode = SwiQmiMISendnWait( pReqBuf,
                                   eQMI_SVC_DCS,
                                   ParamLength,
                                   eQMI_TIMEOUT_2_S,
                                   &pInParam,
                                   &ParamLength,
                                   instance );

    /* Only parse out the response data if we got a positive return */
    if ( resultCode == eQCWWAN_ERR_NONE )
    {
        /* Copy to the caller's buffer */
        resultCode = UpkQmiCbkDcsSetNetChange( pInParam, &response );

        /* Failure Case in the unpack routine */
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
            qmrelreqbkp();
            return resultCode;
        }
    }
    /* The SwiQmiSendnWait has failed */
    else
    {
        qmrelreqbkp();
        return resultCode;
    }

    /* Enable/Disable the callback in the list */
    /* resolving callback by QMI instance id */
    qaQmiSetCallback( qmiInst2DcsNetEvent(instance), pCallback );
    qmrelreqbkp();
    return resultCode;
}

ULONG SetFwDldCompletionCbk(
    tFNFwDldCompletion pCallback )
{
    qaQmiSetCallback( eQMI_CB_FMS_FW_DWLD_STATUS, pCallback );
    return eQCWWAN_ERR_NONE;
}

ULONG SetSLQSOMADMAlertCallback(
    tFNSLQSOMADMAlert pCallback )
{
    ULONG  resultCode  = eQCWWAN_ERR_NONE;
    BYTE   *pInParam;   /* ptr to param field rx'd from modem */
    BYTE   *pOutParam;  /* ptr to outbound param field */
    BYTE   *pReqBuf;    /* Pointer to outgoing request buffer */
    USHORT paramLength; /* Ret'd length of the QMI Parm field */

    /* Storage for results and response variable */
    struct QmiCbkSwiOmaDmSetEventReportResp response;

    void *pCbk = qaQmiGetCallback( eQMI_CB_SWIOMADM_ALERT );
    /*
     * Already in the same state as before ( subscribed/unsubscribed ), hence
     * no device update is required
     */
    if( ((pCbk == NULL) && (pCallback == NULL)) ||
        ((pCbk != NULL) && (pCallback != NULL)) )
    {
        /* Update the list and exit */
        qaQmiSetCallback( eQMI_CB_SWIOMADM_ALERT, pCallback );
        return resultCode;
    }

    /* Initialize the pointer to the outgoing request buffer pointer */
    pReqBuf = qmgetreqbkp();

    /* Get a pointer to the start of the outbound QMI Parameter field */
    pOutParam = amgetparamp( AMTYPEQMIREQUEST, pReqBuf );

    /* Invoke the function which packs the QMI message */
    resultCode = PkQmiCbkSwiOmaDmSetEventReport( &paramLength, pOutParam );

    if ( resultCode != eQCWWAN_ERR_NONE )
    {
        qmrelreqbkp();
        return resultCode; /* No memory */
    }

    /* Prepare and send the blocking call */
    resultCode = SwiQmiSendnWait( pReqBuf,
                                  eQMI_SVC_SWIOMA_MDM9600,
                                  paramLength,
                                  eQMI_TIMEOUT_2_S, /* 2 Seconds */
                                  &pInParam,
                                  &paramLength );

    /* parse out the response data if the response was received succesfully */
    if ( eQCWWAN_ERR_NONE == resultCode )
    {
        /* Copy to the caller's buffer */
        resultCode = UpkQmiCbkSwiOmaDmSetEventReport( pInParam, &response );

        /* Failure Case in the unpack routine */
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
            qmrelreqbkp();
            return resultCode;
        }
    }
    else
    {
        qmrelreqbkp();
        return resultCode;
    }

    /* Disable/Enable the callback from the list */
    qaQmiSetCallback ( eQMI_CB_SWIOMADM_ALERT, pCallback );
    qmrelreqbkp();
    return resultCode;
}

/* This function is used to add support for receiving alerts for the OMADM for the SL9090 module.
   This should be called to implant the callback for SL9090 module for SPRINT requirements */
global ULONG SetSLQSOMADMAlertCallbackExt(
    tFNSLQSOMADMAlert pCallback )
{
    ULONG  resultCode  = eQCWWAN_ERR_NONE;
    BYTE   *pInParam;   /* ptr to param field rx'd from modem */
    BYTE   *pOutParam;  /* ptr to outbound param field */
    BYTE   *pReqBuf;    /* Pointer to outgoing request buffer */
    USHORT paramLength; /* Ret'd length of the QMI Parm field */

    /* Storage for results and response variable */
    struct QmiCbkSwiOmaDmSetEventReportRespExt response;

    void *pCbk = qaQmiGetCallback( eQMI_CB_SWIOMADM_ALERT );
    /*
     * Already in the same state as before ( subscribed/unsubscribed ), hence
     * no device update is required
     */
    if( ((pCbk == NULL) && (pCallback == NULL)) ||
        ((pCbk != NULL) && (pCallback != NULL)) )
    {
        /* Update the list and exit */
        qaQmiSetCallback( eQMI_CB_SWIOMADM_ALERT, pCallback );
        return resultCode;
    }

    /* Initialize the pointer to the outgoing request buffer pointer */
    pReqBuf = qmgetreqbkp();

    /* Get a pointer to the start of the outbound QMI Parameter field */
    pOutParam = amgetparamp( AMTYPEQMIREQUEST, pReqBuf );

    /* Invoke the function which packs the QMI message */
    resultCode = PkQmiCbkSwiOmaDmSetEventReportExt( &paramLength, pOutParam );

    if ( resultCode != eQCWWAN_ERR_NONE )
    {
        qmrelreqbkp();
        return resultCode; /* No memory */
    }

    /* Prepare and send the blocking call */
    resultCode = SwiQmiSendnWait( pReqBuf,
                                  eQMI_SVC_SWIOMA_MDM6600,
                                  paramLength,
                                  eQMI_TIMEOUT_2_S, /* 2 Seconds */
                                  &pInParam,
                                  &paramLength );

    /* parse out the response data if the response was received succesfully */
    if ( eQCWWAN_ERR_NONE == resultCode )
    {
        /* Copy to the caller's buffer */
        resultCode = UpkQmiCbkSwiOmaDmSetEventReportExt( pInParam, &response );

        /* Failure Case in the unpack routine */
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
            qmrelreqbkp();
            return resultCode;
        }
    }
    else
    {
        qmrelreqbkp();
        return resultCode;
    }

    /* Disable/Enable the callback from the list */
    qaQmiSetCallback ( eQMI_CB_SWIOMADM_ALERT, pCallback );
    qmrelreqbkp();
    return resultCode;
}



global ULONG SLQSSetServingSystemCallback(
    tFNServingSystem pCallback )
{
    ULONG  resultCode = eQCWWAN_ERR_NONE;
    if ( NULL != pCallback )
    {
        resultCode = SLQSStartSrv(REGISTER_SRV, NAS_SRV);
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
             return resultCode;
        }
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case
     * The Serving System registration state and/or radio technology are
     * broadcast by the Modem and hence no request need to be formulated.
     */
    qaQmiSetCallback( eQMI_CB_NAS_SERV_SYS, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SetOMADMStateCallback(
    tFNOMADMState pCallback )
{
    ULONG  resultCode  = eQCWWAN_ERR_NONE;
    BYTE   *pInParam;   /* ptr to param field rx'd from modem */
    BYTE   *pOutParam;  /* ptr to outbound param field */
    BYTE   *pReqBuf;    /* Pointer to outgoing request buffer */
    USHORT paramLength; /* Ret'd length of the QMI Parm field */
    enum   eQmiCbkSetStatus sessionState;

    /* Storage for results and response variable */
    struct QmiCbkOmaDmSetEventReportResp response;

    void *pCbk = qaQmiGetCallback( eQMI_CB_OMADM_STATE );
    /*
     * Already in the same state as before ( subscribed/unsubscribed ), hence
     * no device update is required
     */
    if( ((pCbk == NULL) && (pCallback == NULL)) ||
        ((pCbk != NULL) && (pCallback != NULL)) )
    {
        /* Update the list and exit */
        qaQmiSetCallback( eQMI_CB_OMADM_STATE, pCallback );
        return resultCode;
    }

    /* Initialize the pointer to the outgoing request buffer pointer */
    pReqBuf = qmgetreqbkp();

    /* Get a pointer to the start of the outbound QMI Parameter field */
    pOutParam = amgetparamp( AMTYPEQMIREQUEST, pReqBuf );

    /* Invoke the function which packs the QMI message */
    sessionState =
            (NULL == pCallback ) ? QMI_CBK_PARAM_RESET : QMI_CBK_PARAM_SET;
    resultCode = PkQmiCbkOmaDmSetEventReport( &paramLength,
                    /* Session State Event */ sessionState,
                                              pOutParam );

    if ( resultCode != eQCWWAN_ERR_NONE )
    {
        qmrelreqbkp();
        return resultCode; /* No memory */
    }

    /* Prepare and send the blocking call */
    resultCode = SwiQmiSendnWait( pReqBuf,
                                  eQMI_SVC_OMA,
                                  paramLength,
                                  eQMI_TIMEOUT_2_S, /* 2 Seconds */
                                  &pInParam,
                                  &paramLength );

    /* parse out the response data if the response was received succesfully */
    if ( eQCWWAN_ERR_NONE == resultCode )
    {
        /* Copy to the caller's buffer */
        resultCode = UpkQmiCbkOmaDmSetEventReport( pInParam, &response );

        /* Failure Case in the unpack routine */
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
            qmrelreqbkp();
            return resultCode;
        }
    }
    else
    {
        qmrelreqbkp();
        return resultCode;
    }

    /* Disable/Enable the callback from the list */
    qaQmiSetCallback( eQMI_CB_OMADM_STATE, pCallback );
    qmrelreqbkp();
    return resultCode;
}

global ULONG SLQSSetBandPreferenceCbk(
    tFNBandPreference pCallback )
{
    ULONG  resultCode  = eQCWWAN_ERR_NONE;

    if( pCallback )
    {
        resultCode = SLQSNasIndicationRegister( CBK_ENABLE_EVENT,
                                                CBK_ENABLE_EVENT,
                                                CBK_ENABLE_EVENT );
        if( eQCWWAN_ERR_NONE != resultCode )
        {
            return resultCode;
        }
    }
    else
    {
        resultCode = SLQSNasIndicationRegister( CBK_DISABLE_EVENT,
                                                CBK_NOCHANGE,
                                                CBK_NOCHANGE );
        if( eQCWWAN_ERR_NONE != resultCode )
        {
            return resultCode;
        }
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case
     * The Band Preference selection is set by the API SLQSetBandPreference
     * and hence no request need to be formulated.
     */
    qaQmiSetCallback( eQMI_CB_NAS_BAND_PREF, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SetUSSDReleaseCallback(
    tFNUSSDRelease pCallback )
{
    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case
     * The USSDRelease is broadcasted by the Modem and hence no request
     * need to be formulated.
     */
     qaQmiSetCallback( eQMI_CB_VOICE_USSD_RELEASE_IND, pCallback );
     return eQCWWAN_ERR_NONE;
}

global ULONG SetUSSDNotificationCallback(
    tFNUSSDNotification pCallback )
{
    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case
     * The Ussd Notification is broadcasted by the Modem and hence no request
     * need to be formulated.
     */
     qaQmiSetCallback( eQMI_CB_VOICE_USSD_IND, pCallback );
     return eQCWWAN_ERR_NONE;
}

ULONG SLQSSetSignalStrengthsCallback(
   tFNSLQSSignalStrengths           pCallback,
   struct SLQSSignalStrengthsIndReq *pSLQSSignalStrengthsIndReq )
{
    ULONG  resultCode  = eQCWWAN_ERR_NONE;
    BYTE   *pInParam;   /* ptr to param field rx'd from modem */
    BYTE   *pOutParam;  /* ptr to outbound param field */
    BYTE   *pReqBuf;    /* Pointer to outgoing request buffer */
    USHORT paramLength; /* Ret'd length of the QMI Parm field */
    enum   eQmiCbkSetStatus SLQSSignalStrengthsInfoStatus;

    /* Storage for results and response variable */
    struct QmiCbkNasSetEventReportResp response;

    void *pCbk = qaQmiGetCallback( eQMI_CB_RSSI_INFO );

    /* If the requested action (subscribe/unsubscribe) matches the current
     * state, sending a QMI request is not required.
     */
    if( ((pCbk == NULL) && (pCallback == NULL)) ||
        ((pCbk != NULL) && (pCallback != NULL)) )
    {
        /* Update the list and exit */
        qaQmiSetCallback( eQMI_CB_RSSI_INFO, pCallback );
        return resultCode;
    }

    /* input parameter validation */
    if ( NULL == pSLQSSignalStrengthsIndReq )
    {
        return eQCWWAN_ERR_INVALID_ARG;
    }

    /* Initialize the pointer to the outgoing request buffer pointer */
    pReqBuf = qmgetreqbkp();

    /* Get a pointer to the start of the outbound QMI Parameter field */
    pOutParam = amgetparamp( AMTYPEQMIREQUEST, pReqBuf );

    /* Pack the QMI request */
    SLQSSignalStrengthsInfoStatus =
                (NULL == pCallback ) ? QMI_CBK_PARAM_RESET : QMI_CBK_PARAM_SET;
    resultCode = PkQmiCbkNasSetEventReport( &paramLength,
            /* Signal Strength Indicator */ QMI_CBK_PARAM_NOCHANGE,
                                            0,
                                            NULL,
                  /* RF Band Information */ QMI_CBK_PARAM_NOCHANGE,
          /* Registration Reject Reasons */ QMI_CBK_PARAM_NOCHANGE,
                       /* RSSI Indicator */ SLQSSignalStrengthsInfoStatus,
                       /* ECIO Indicator */ SLQSSignalStrengthsInfoStatus,
                         /* IO Indicator */ SLQSSignalStrengthsInfoStatus,
                       /* SINR Indicator */ SLQSSignalStrengthsInfoStatus,
                 /* Error Rate Indicator */ SLQSSignalStrengthsInfoStatus,
                        /*RSRQ Indicator */ SLQSSignalStrengthsInfoStatus,
             /* ECIO Threshold Indicator */ SLQSSignalStrengthsInfoStatus,
             /* SINR Threshold Indicator */ SLQSSignalStrengthsInfoStatus,
                                            pSLQSSignalStrengthsIndReq,
                                            pOutParam );

    if ( resultCode != eQCWWAN_ERR_NONE )
    {
        qmrelreqbkp();
        return resultCode;
    }

    /* Send the QMI request and wait for the QMI response */
    resultCode = SwiQmiSendnWait( pReqBuf,
                                  eQMI_SVC_NAS,
                                  paramLength,
                                  eQMI_TIMEOUT_2_S, /* 2 Seconds */
                                  &pInParam,
                                  &paramLength );

    if ( resultCode == eQCWWAN_ERR_NONE )
    {
        /* unpack the response to the caller's buffer */
        if( eQCWWAN_ERR_NONE !=
            ( resultCode = UpkQmiCbkNasSetEventReport( pInParam, &response ) ) )
        {
            qmrelreqbkp();
            return resultCode; /* failure */
        }
    }
    else
    {
        qmrelreqbkp();
        return resultCode; /* failure */
    }

    /* Add(Remove) the callback to(from) the callback list */
    qaQmiSetCallback( eQMI_CB_RSSI_INFO, pCallback );

    /* Save the values of the callback function's parameters for callback
     * persistence across power cycles.
     */
    slmemcpy( (void *)&SLQSSignalStrengthInfo,
              (void *)pSLQSSignalStrengthsIndReq,
              sizeof( SLQSSignalStrengthInfo ) );

    qmrelreqbkp();
    return resultCode;
}

global ULONG iSLQSSetSignalStrengthsCallback(
    tFNSLQSSignalStrengths pCallback )
{
    return SLQSSetSignalStrengthsCallback( pCallback,
                                           &SLQSSignalStrengthInfo );
}

global ULONG SLQSVoiceSetSUPSNotificationCallback(
    tFNSUPSNotification pCallback )
{
    ULONG  resultCode  = eQCWWAN_ERR_NONE;
    BYTE   *pInParam;   /* ptr to param field rx'd from modem */
    BYTE   *pOutParam;  /* ptr to outbound param field */
    BYTE   *pReqBuf;    /* Pointer to outgoing request buffer */
    USHORT paramLength; /* Ret'd length of the QMI Parm field */
    enum   eQmiCbkSetStatus SLQSSUPSNotificationInfoStatus;

    /* Storage for results and response variable */
    struct QmiCbkVoiceIndicationRegisterResp response;

    void *pCbk = qaQmiGetCallback( eQMI_CB_VOICE_SUPS_NOTIFICATION_IND );

    /* If the requested action (subscribe/unsubscribe) matches the current
     * state, sending a QMI request is not required.
     */
    if( ((pCbk == NULL) && (pCallback == NULL)) ||
        ((pCbk != NULL) && (pCallback != NULL)) )
    {
        /* Update the list and exit */
        qaQmiSetCallback( eQMI_CB_VOICE_SUPS_NOTIFICATION_IND, pCallback );
        return resultCode;
    }

    /* Initialize the pointer to the outgoing request buffer pointer */
    pReqBuf = qmgetreqbkp();

    /* Get a pointer to the start of the outbound QMI Parameter field */
    pOutParam = amgetparamp( AMTYPEQMIREQUEST, pReqBuf );

    /* Pack the QMI request */
    SLQSSUPSNotificationInfoStatus =
                (NULL == pCallback ) ? QMI_CBK_PARAM_RESET : QMI_CBK_PARAM_SET;
    resultCode = PkQmiCbkVoiceIndicationRegister(&paramLength,
                               /* DTMF Events */ QMI_CBK_PARAM_NOCHANGE,
                      /* Voice Privacy Events */ QMI_CBK_PARAM_NOCHANGE,
                  /* SUPS Notificaiton Events */ SLQSSUPSNotificationInfoStatus,
                                                 pOutParam);
    if ( resultCode != eQCWWAN_ERR_NONE )
    {
        qmrelreqbkp();
        return resultCode;
    }

    /* Send the QMI request and wait for the QMI response */
    resultCode = SwiQmiSendnWait( pReqBuf,
                                  eQMI_SVC_VOICE,
                                  paramLength,
                                  eQMI_TIMEOUT_2_S, /* 2 Seconds */
                                  &pInParam,
                                  &paramLength );

    if ( resultCode == eQCWWAN_ERR_NONE )
    {
        /* unpack the response to the caller's buffer */
        if(eQCWWAN_ERR_NONE !=
           (resultCode = UpkQmiCbkVoiceIndicationRegister(pInParam, &response)))
        {
            qmrelreqbkp();
            return resultCode; /* failure */
        }
    }
    else
    {
        qmrelreqbkp();
        return resultCode; /* failure */
    }

    /* Add(Remove) the callback to(from) the callback list */
    qaQmiSetCallback( eQMI_CB_VOICE_SUPS_NOTIFICATION_IND, pCallback );
    qmrelreqbkp();
    return resultCode;
}

global ULONG SLQSSetSDKTerminatedCallback(
        tFNSDKTerminated pCallback )
{
    qaQmiSetCallback( eQMI_CB_DCS_SDK_TERMINATED, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SLQSVoiceSetAllCallStatusCallBack (
    tFNAllCallStatus pCallback )
{
    ULONG  resultCode = eQCWWAN_ERR_NONE;
    if ( NULL != pCallback )
    {
        resultCode = SLQSStartSrv(REGISTER_SRV, VOICE_SRV);
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
             return resultCode;
        }
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case.
     */
    qaQmiSetCallback( eQMI_CB_VOICE_ALL_CALL_STATUS, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SLQSSetTransLayerInfoCallback( tFNtransLayerInfo pCallback )
{
    ULONG               resultCode              = eQCWWAN_ERR_NONE;
    setIndicationRegReq SetIndicationRegReqInfo;
    BYTE                bState;

    if( pCallback )
    {
        bState = CBK_ENABLE_EVENT;
        SetIndicationRegReqInfo.pRegTransLayerInfoEvt = &bState;
        SetIndicationRegReqInfo.pRegTransNWRegInfoEvt = NULL;
        SetIndicationRegReqInfo.pRegCallStatInfoEvt   = NULL;

        resultCode = SLQSSetIndicationRegister( &SetIndicationRegReqInfo );
        if( eQCWWAN_ERR_NONE != resultCode )
        {
            return resultCode;
        }
    }
    else
    {
        bState = CBK_DISABLE_EVENT;
        SetIndicationRegReqInfo.pRegTransLayerInfoEvt = &bState;
        SetIndicationRegReqInfo.pRegTransNWRegInfoEvt = NULL;
        SetIndicationRegReqInfo.pRegCallStatInfoEvt   = NULL;

        resultCode = SLQSSetIndicationRegister( &SetIndicationRegReqInfo );
        if( eQCWWAN_ERR_NONE != resultCode )
        {
            return resultCode;
        }
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case
     * The transport layer information is broadcasted by the Modem and hence
     * no request need to be formulated.
     */
     qaQmiSetCallback( eQMI_CB_WMS_TRANS_LAYER_INFO_IND, pCallback );
     return eQCWWAN_ERR_NONE;
}

global ULONG SLQSSetTransNWRegInfoCallback( tFNtransNWRegInfo pCallback )
{
    ULONG               resultCode              = eQCWWAN_ERR_NONE;
    setIndicationRegReq SetIndicationRegReqInfo;
    BYTE                bState;

    if( pCallback )
    {
        bState = CBK_ENABLE_EVENT;
        SetIndicationRegReqInfo.pRegTransLayerInfoEvt = NULL;
        SetIndicationRegReqInfo.pRegTransNWRegInfoEvt = &bState;
        SetIndicationRegReqInfo.pRegCallStatInfoEvt   = NULL;

        resultCode = SLQSSetIndicationRegister( &SetIndicationRegReqInfo );
        if( eQCWWAN_ERR_NONE != resultCode )
        {
            return resultCode;
        }
    }
    else
    {
        bState = CBK_DISABLE_EVENT;
        SetIndicationRegReqInfo.pRegTransLayerInfoEvt = NULL;
        SetIndicationRegReqInfo.pRegTransNWRegInfoEvt = &bState;
        SetIndicationRegReqInfo.pRegCallStatInfoEvt   = NULL;

        resultCode = SLQSSetIndicationRegister( &SetIndicationRegReqInfo );
        if( eQCWWAN_ERR_NONE != resultCode )
        {
            return resultCode;
        }
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case
     * The transport network registration information is broadcasted by the
     * Modem and hence no request need to be formulated.
     */
     qaQmiSetCallback( eQMI_CB_WMS_TRANS_NW_REG_INFO_IND, pCallback );
     return eQCWWAN_ERR_NONE;
}

global ULONG SLQSSetSysSelectionPrefCallBack(
        tFNSysSelectionPref pCallback )
{
    ULONG  resultCode  = eQCWWAN_ERR_NONE;

    if( pCallback )
    {
        resultCode = SLQSNasIndicationRegister( CBK_ENABLE_EVENT,
                                                CBK_ENABLE_EVENT,
                                                CBK_ENABLE_EVENT );
        if( eQCWWAN_ERR_NONE != resultCode )
        {
            return resultCode;
        }
    }
    else
    {
        resultCode = SLQSNasIndicationRegister( CBK_DISABLE_EVENT,
                                                CBK_NOCHANGE,
                                                CBK_NOCHANGE );
        if( eQCWWAN_ERR_NONE != resultCode )
        {
            return resultCode;
        }
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case
     * The System Selection Preference selection is set by the API
     * SLQSSetSysSelectionPref and hence no request need to be formulated.
     */
    qaQmiSetCallback( eQMI_CB_NAS_SYS_SEL_PREFERENCE, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SLQSUIMSetRefreshCallBack (
    tFNUIMRefresh pCallback )
{
    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case
     */
    qaQmiSetCallback( eQMI_CB_UIM_REFRESH_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SLQSUIMSetStatusChangeCallBack (
    tFNUIMStatusChangeInfo pCallback )
{
    ULONG resultCode           = eQCWWAN_ERR_NONE;
    ULONG eventMaskDeregister  = EVENT_MASK_DEREGISTER_ALL;
    ULONG eventMaskCard        = EVENT_MASK_CARD;

    UIMEventRegisterReqResp EventRegisterReqResp;

    /* Registers for event notifications from the card */
    if( pCallback )
    {
        EventRegisterReqResp.eventMask = eventMaskCard;
        resultCode = SLQSUIMEventRegister( &EventRegisterReqResp );
        if( eQCWWAN_ERR_NONE != resultCode )
        {
            return resultCode;
        }
    }
    else
    {
        EventRegisterReqResp.eventMask = eventMaskDeregister;
        resultCode = SLQSUIMEventRegister( &EventRegisterReqResp );

        if( eQCWWAN_ERR_NONE != resultCode )
        {
            return resultCode;
        }
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case
     */
    qaQmiSetCallback( eQMI_CB_UIM_STATUS_CHANGE_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SLQSVoiceSetPrivacyChangeCallBack (
    tFNPrivacyChange pCallback )
{
    ULONG resultCode  = eQCWWAN_ERR_NONE;
    BYTE  regVPEvents = REGISTER_EVENT;

    voiceIndicationRegisterInfo indicationRegisterVPreq;

    /* Registers for event notifications from the card */
    if( !pCallback )
    {
        regVPEvents = DEREGISTER_EVENT;
    }
    indicationRegisterVPreq.pRegDTMFEvents         = NULL;
    indicationRegisterVPreq.pRegVoicePrivacyEvents = &regVPEvents;
    indicationRegisterVPreq.pSuppsNotifEvents      = NULL;
    resultCode = SLQSVoiceIndicationRegister( &indicationRegisterVPreq );
    if( eQCWWAN_ERR_NONE != resultCode )
    {
        return resultCode;
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case.
     */
    qaQmiSetCallback( eQMI_CB_VOICE_PRIVACY_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SLQSVoiceSetDTMFEventCallBack(
    tFNDTMFEvent pCallback )
{
    ULONG resultCode    = eQCWWAN_ERR_NONE;
    BYTE  regDTMFEvents = REGISTER_EVENT;

    voiceIndicationRegisterInfo indicationRegisterDTMFreq;

    /* Registers for event notifications from the card */
    if( !pCallback )
    {
        regDTMFEvents = DEREGISTER_EVENT;
    }
    indicationRegisterDTMFreq.pRegDTMFEvents         = &regDTMFEvents;
    indicationRegisterDTMFreq.pRegVoicePrivacyEvents = NULL;
    indicationRegisterDTMFreq.pSuppsNotifEvents      = NULL;
    resultCode = SLQSVoiceIndicationRegister( &indicationRegisterDTMFreq );
    if( eQCWWAN_ERR_NONE != resultCode )
    {
        return resultCode;
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case
     */
    qaQmiSetCallback( eQMI_CB_VOICE_DTMF_IND, pCallback );
    return resultCode;
}

ULONG SLQSVoiceSetSUPSCallBack( tFNSUPSInfo pCallback )
{
    ULONG resultCode    = eQCWWAN_ERR_NONE;
    BYTE  regSUPSEvents = REGISTER_EVENT;

    voiceIndicationRegisterInfo indicationRegisterSUPSreq;

    /* Registers for event notifications from the card */
    if( !pCallback )
    {
        regSUPSEvents = DEREGISTER_EVENT;
    }
    indicationRegisterSUPSreq.pSuppsNotifEvents      = &regSUPSEvents;
    indicationRegisterSUPSreq.pRegVoicePrivacyEvents = NULL;
    indicationRegisterSUPSreq.pRegDTMFEvents         = NULL;
    resultCode = SLQSVoiceIndicationRegister( &indicationRegisterSUPSreq );
    if( eQCWWAN_ERR_NONE != resultCode )
    {
        return resultCode;
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case
     */
    qaQmiSetCallback( eQMI_CB_VOICE_SUPS_IND, pCallback );
    return resultCode;
}

global ULONG SLQSNasSysInfoCallBack( tFNSysInfo pCallback )
{
    ULONG resultCode       = eQCWWAN_ERR_NONE;
    BYTE  regSysInfoEvents = REGISTER_EVENT;

    nasIndicationRegisterReq req;

    /* Registers for event notifications from the card */
    if( !pCallback )
    {
        regSysInfoEvents = DEREGISTER_EVENT;
    }

    req.pSystemSelectionInd  = NULL;
    req.pDDTMInd             = NULL;
    req.pServingSystemInd    = NULL;
    req.pDualStandByPrefInd  = NULL;
    req.pSubscriptionInfoInd = NULL;
    req.pNetworkTimeInd      = NULL;
    req.pSysInfoInd          = &regSysInfoEvents;
    req.pSignalStrengthInd   = NULL;
    req.pErrorRateInd        = NULL;
    req.pHDRNewUATIAssInd    = NULL;
    req.pHDRSessionCloseInd  = NULL;
    req.pManagedRoamingInd   = NULL;

    resultCode = SLQSNasIndicationRegisterExt( &req );
    if( eQCWWAN_ERR_NONE != resultCode )
    {
        return resultCode;
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case.
     */
    qaQmiSetCallback( eQMI_CB_NAS_SYS_INFO_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SLQSNasNetworkTimeCallBack( tFNNetworkTime pCallback )
{
    ULONG resultCode           = eQCWWAN_ERR_NONE;
    BYTE  regNetworkTimeEvents = REGISTER_EVENT;

    nasIndicationRegisterReq req;

    /* Registers for event notifications from the card */
    if( !pCallback )
    {
        regNetworkTimeEvents = DEREGISTER_EVENT;
    }

    req.pSystemSelectionInd  = NULL;
    req.pDDTMInd             = NULL;
    req.pServingSystemInd    = NULL;
    req.pDualStandByPrefInd  = NULL;
    req.pSubscriptionInfoInd = NULL;
    req.pNetworkTimeInd      = &regNetworkTimeEvents;
    req.pSysInfoInd          = NULL;
    req.pSignalStrengthInd   = NULL;
    req.pErrorRateInd        = NULL;
    req.pHDRNewUATIAssInd    = NULL;
    req.pHDRSessionCloseInd  = NULL;
    req.pManagedRoamingInd   = NULL;

    resultCode = SLQSNasIndicationRegisterExt( &req );
    if( eQCWWAN_ERR_NONE != resultCode )
    {
        return resultCode;
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case.
     */
    qaQmiSetCallback( eQMI_CB_NAS_NETWORK_TIME_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

ULONG SLQSWmsMemoryFullCallBack( tFNMemoryFull pCallback )
{
    /* Disable/Enable the callback from the list */
    qaQmiSetCallback( eQMI_CB_WMS_MEMORY_FULL, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SLQSVoiceSetOTASPStatusCallBack(
    tFNOTASPStatus pCallback )
{
    ULONG  resultCode = eQCWWAN_ERR_NONE;
    if ( NULL != pCallback )
    {
        resultCode = SLQSStartSrv(REGISTER_SRV, VOICE_SRV );
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
             return resultCode;
        }
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case
     */
    qaQmiSetCallback( eQMI_CB_VOICE_OTASP_STATUS_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SLQSVoiceInfoRecCallback( tFNInfoRec pCallback )
{
    ULONG  resultCode = eQCWWAN_ERR_NONE;
    if ( NULL != pCallback )
    {
        resultCode = SLQSStartSrv(REGISTER_SRV, VOICE_SRV);
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
             return resultCode;
        }
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case
     */
    qaQmiSetCallback( eQMI_CB_VOICE_INFO_REC_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SLQSWmsMessageWaitingCallBack( tFNMessageWaiting pCallback )
{
    /* Disable/Enable the callback from the list */
    qaQmiSetCallback( eQMI_CB_WMS_MESSAGE_WAITING_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SLQSSetQosNWStatusCallback(
    tFNQosNWStatus     pCallback )
{
    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case
     */
    qaQmiSetCallback( eQMI_CB_QOS_NETWORK_STATUS, pCallback );
    return eQCWWAN_ERR_NONE;
}

ULONG SLQSNasSigInfoCallBack( tFNSigInfo pCallback )
{
    ULONG resultCode       = eQCWWAN_ERR_NONE;
    BYTE  regSysInfoEvents = REGISTER_EVENT;
    BYTE  regSigInfoEvents = REGISTER_EVENT;

    nasIndicationRegisterReq req;

    /* Registers for event notifications from the card */
    if( !pCallback )
    {
        regSigInfoEvents = DEREGISTER_EVENT;
    }

    req.pSystemSelectionInd  = NULL;
    req.pDDTMInd             = NULL;
    req.pServingSystemInd    = NULL;
    req.pDualStandByPrefInd  = NULL;
    req.pSubscriptionInfoInd = NULL;
    req.pNetworkTimeInd      = NULL;
    req.pSysInfoInd          = &regSysInfoEvents;
    req.pSignalStrengthInd   = &regSigInfoEvents;
    req.pErrorRateInd        = NULL;
    req.pHDRNewUATIAssInd    = NULL;
    req.pHDRSessionCloseInd  = NULL;
    req.pManagedRoamingInd   = NULL;

    resultCode = SLQSNasIndicationRegisterExt( &req );
    if( eQCWWAN_ERR_NONE != resultCode )
    {
        return resultCode;
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case.
     */
     qaQmiSetCallback( eQMI_CB_NAS_SIG_INFO_IND, pCallback );
     return eQCWWAN_ERR_NONE;
}

global ULONG SLQSSetQosEventCallback(
    BYTE            instance,
    tFNSLQSQOSEvent pCallback)
{
    ULONG  resultCode = eQCWWAN_ERR_NONE;
    BYTE   *pInParam;      /* ptr to param field rx'd from modem */
    BYTE   *pOutParam;     /* ptr to outbound param field */
    BYTE   *pReqBuf;       /* Pointer to ougoing request buffer */
    USHORT ParamLength;    /* Ret'd length of the QMI Parm field */
    enum   eQmiCbkSetStatus paramStatus;
    struct QmiCbkSetQosEventReportResp response;

    if (instance >= INVALID_INSTACNE)
    {
        return eQCWWAN_ERR_INVALID_ARG;
    }

    /* resolving callback by QMI instance id */
    void *pCbk = qaQmiGetCallback( qmiInst2QosEvent(instance) );
    /*
     * Already in the same state as before ( subscribed/unsubscribed ), hence
     * no device update is required
     */
    if( ((pCbk == NULL) && (pCallback == NULL)) ||
        ((pCbk != NULL) && (pCallback != NULL)) )
    {
        /* resolving callback by QMI instance id */
        qaQmiSetCallback( qmiInst2QosEvent(instance), pCallback );
        return resultCode;
    }

    /* Initialize the pointer to the outgoing request buffer pointer */
    pReqBuf = qmgetreqbkp();

    /* Get a pointer to the start of the outbound QMI Parameter field */
    pOutParam = amgetparamp( AMTYPEQMIREQUEST, pReqBuf );

    /* Invoke the function which packs QMI message */
    paramStatus = (NULL == pCallback) ? QMI_CBK_PARAM_RESET : QMI_CBK_PARAM_SET;

    resultCode = PkQmiCbkSetQosEventReport( &ParamLength,
               /* Global Flow */            paramStatus,
               /* Net Qos Profile */        paramStatus,
                                            pOutParam );

    if (resultCode != eQCWWAN_ERR_NONE)
    {
        qmrelreqbkp();
        return resultCode;
    }

    /* Prepare and send the blocking call */
    resultCode = SwiQmiMISendnWait( pReqBuf,
                                  eQMI_SVC_QOS,
                                  ParamLength,
                                  eQMI_TIMEOUT_2_S,
                                  &pInParam,
                                  &ParamLength,
                                  instance );

    /* Only parse out the response data if we got a positive return */
    if ( resultCode == eQCWWAN_ERR_NONE )
    {
        /* Copy to the caller's buffer */
        resultCode = UpkQmiCbkSetQosEventReport( pInParam, &response );

        /* Failure Case in the unpack routine */
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
            qmrelreqbkp();
            return resultCode;
        }
    }
    /* The SwiQmiSendnWait has failed */
    else
    {
        qmrelreqbkp();
        return resultCode;
    }

    /* Enable/Disable the callback in the list */
    /* resolving callback by QMI instance id */
    qaQmiSetCallback( qmiInst2QosEvent(instance), pCallback );
    qmrelreqbkp();
    return resultCode;
}

global ULONG SLQSSetModemTempCallback(
    tFNModemTempInfo pCallback )
{
    ULONG  resultCode = eQCWWAN_ERR_NONE;
    BYTE   *pInParam;   /* ptr to param field rx'd from modem */
    BYTE   *pOutParam;  /* ptr to outbound param field */
    BYTE   *pReqBuf;    /* Pointer to ougoing request buffer */
    USHORT paramLength; /* Ret'd length of the QMI Parm field */
    enum   eQmiCbkSetStatus TemperatureInfo;

    /* Storage for results and response variable */
    struct QmiCbkDmsSwiSetEventReportResp response;

    void *pCbk = qaQmiGetCallback( eQMI_CB_MODEM_TEMP );
    /*
     * Already in the same state as before ( subscribed/unsubscribed ), hence
     * no device update is required
     */
    if( ((pCbk == NULL) && (pCallback == NULL)) ||
        ((pCbk != NULL) && (pCallback != NULL)) )
    {
        /* Update the list and exit */
        qaQmiSetCallback( eQMI_CB_MODEM_TEMP, pCallback );
        return resultCode;
    }

    /* Initialize the pointer to the outgoing request buffer pointer */
    pReqBuf = qmgetreqbkp();

    /* Get a pointer to the start of the outbound QMI Parameter field */
    pOutParam = amgetparamp( AMTYPEQMIREQUEST, pReqBuf );

    /* Set or reset the TLVs based on the registeration status */
    TemperatureInfo =
            (NULL == pCallback ) ? QMI_CBK_PARAM_RESET : QMI_CBK_PARAM_SET;
    resultCode = PkQmiCbkDmsSwiSetEventReport( &paramLength,
                        /* Temperature Info */ TemperatureInfo,
                                 /* Voltage */ QMI_CBK_PARAM_NOCHANGE,
                                               pOutParam );

    if ( resultCode != eQCWWAN_ERR_NONE )
    {
        qmrelreqbkp();
        return resultCode; /* No memory */
    }

    /* Prepare and send the blocking call */
    resultCode = SwiQmiSendnWait( pReqBuf,
                                  eQMI_SVC_DMS,
                                  paramLength,
                                  eQMI_TIMEOUT_5_S, /* 5 Seconds */
                                  &pInParam,
                                  &paramLength );

    /* Only parse out the response data if we got a positive return */
    if ( resultCode == eQCWWAN_ERR_NONE )
    {
        /* Copy to the caller's buffer */
        resultCode = UpkQmiCbkDmsSwiSetEventReport( pInParam, &response );
        /* Failure Case in the unpack routine */
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
            qmrelreqbkp();
            return resultCode;
        }
    }
    /* The SwiQmiSendnWait has failed */
    else
    {
        qmrelreqbkp();
        return resultCode;
    }

    /* Update the callback in the list */
    qaQmiSetCallback( eQMI_CB_MODEM_TEMP, pCallback );
    qmrelreqbkp();
    return resultCode;
}

global ULONG SLQSSetPacketSrvStatusCallback(
    tFNPacketSrvState pCallback )
{
    ULONG  resultCode = eQCWWAN_ERR_NONE;
    if ( NULL != pCallback )
    {
        resultCode = SLQSStartSrv(REGISTER_SRV, WDS_SRV);
        if ( resultCode != eQCWWAN_ERR_NONE )
        {
             return resultCode;
        }

    }
    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case
     * The Session state is broadcasted by the Modem and hence no request
     * need to be formulated.
     */
    qaQmiSetCallback( eQMI_CB_PKT_SERV_STATUS, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SLQSSetSwiHDRPersCallback(
    tFNHDRPersonaity    pCallback )
{
    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case
     * The Session state is broadcasted by the Modem and hence no request
     * need to be formulated.
     */
    qaQmiSetCallback( eQMI_CB_NAS_HDR_PERS_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SLQSSetSIPConfigCallback (
tFNImsSIPConfig pCallback )
{
    ULONG resultCode  = eQCWWAN_ERR_NONE;
    BYTE  regVPEvents = REGISTER_EVENT;

    imsCfgIndRegisterInfo indicationRegisterReq;

    /* Registers for event notifications from the card */
    if( !pCallback )
    {
        regVPEvents = DEREGISTER_EVENT;
    }
    indicationRegisterReq.pSIPConfigEvents    = &regVPEvents;
    indicationRegisterReq.pRegMgrConfigEvents = NULL;
    indicationRegisterReq.pSMSConfigEvents    = NULL;
    indicationRegisterReq.pUserConfigEvents   = NULL;
    indicationRegisterReq.pVoIPConfigEvents   = NULL;
    resultCode = SLQSImsConfigIndicationRegister( &indicationRegisterReq );
    if( eQCWWAN_ERR_NONE != resultCode )
    {
        return resultCode;
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case.
     */
    qaQmiSetCallback( eQMI_CB_IMS_SIP_CONFIG_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SLQSSetRegMgrConfigCallback (
    tFNImsRegMgrConfig pCallback )
{
    ULONG resultCode  = eQCWWAN_ERR_NONE;
    BYTE  regVPEvents = REGISTER_EVENT;

    imsCfgIndRegisterInfo indicationRegisterReq;

    /* Registers for event notifications from the card */
    if( !pCallback )
    {
        regVPEvents = DEREGISTER_EVENT;
    }
    indicationRegisterReq.pSIPConfigEvents    = NULL;
    indicationRegisterReq.pRegMgrConfigEvents = &regVPEvents;
    indicationRegisterReq.pSMSConfigEvents    = NULL;
    indicationRegisterReq.pUserConfigEvents   = NULL;
    indicationRegisterReq.pVoIPConfigEvents   = NULL;
    resultCode = SLQSImsConfigIndicationRegister( &indicationRegisterReq );
    if( eQCWWAN_ERR_NONE != resultCode )
    {
        return resultCode;
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case.
     */
    qaQmiSetCallback( eQMI_CB_IMS_REG_MGR_CONFIG_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SLQSSetIMSSMSConfigCallback (
    tFNImsSMSConfig pCallback )
{
    ULONG resultCode  = eQCWWAN_ERR_NONE;
    BYTE  regVPEvents = REGISTER_EVENT;

    imsCfgIndRegisterInfo indicationRegisterReq;

    /* Registers for event notifications from the card */
    if( !pCallback )
    {
        regVPEvents = DEREGISTER_EVENT;
    }
    indicationRegisterReq.pSIPConfigEvents    = NULL;
    indicationRegisterReq.pRegMgrConfigEvents = NULL;
    indicationRegisterReq.pSMSConfigEvents    = &regVPEvents;
    indicationRegisterReq.pUserConfigEvents   = NULL;
    indicationRegisterReq.pVoIPConfigEvents   = NULL;
    resultCode = SLQSImsConfigIndicationRegister( &indicationRegisterReq );
    if( eQCWWAN_ERR_NONE != resultCode )
    {
        return resultCode;
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case.
     */
    qaQmiSetCallback( eQMI_CB_IMS_SMS_CONFIG_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SLQSSetIMSUserConfigCallback (
    tFNImsUserConfig pCallback )
{
    ULONG resultCode  = eQCWWAN_ERR_NONE;
    BYTE  regVPEvents = REGISTER_EVENT;

    imsCfgIndRegisterInfo indicationRegisterReq;

    /* Registers for event notifications from the card */
    if( !pCallback )
    {
        regVPEvents = DEREGISTER_EVENT;
    }
    indicationRegisterReq.pSIPConfigEvents    = NULL;
    indicationRegisterReq.pRegMgrConfigEvents = NULL;
    indicationRegisterReq.pSMSConfigEvents    = NULL;
    indicationRegisterReq.pUserConfigEvents   = &regVPEvents;
    indicationRegisterReq.pVoIPConfigEvents   = NULL;
    resultCode = SLQSImsConfigIndicationRegister( &indicationRegisterReq );
    if( eQCWWAN_ERR_NONE != resultCode )
    {
        return resultCode;
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case.
     */
    qaQmiSetCallback( eQMI_CB_IMS_USER_CONFIG_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SLQSSetIMSVoIPConfigCallback (
    tFNImsVoIPConfig pCallback )
{
    ULONG resultCode  = eQCWWAN_ERR_NONE;
    BYTE  regVPEvents = REGISTER_EVENT;

    imsCfgIndRegisterInfo indicationRegisterReq;

    /* Registers for event notifications from the card */
    if( !pCallback )
    {
        regVPEvents = DEREGISTER_EVENT;
    }
    indicationRegisterReq.pSIPConfigEvents    = NULL;
    indicationRegisterReq.pRegMgrConfigEvents = NULL;
    indicationRegisterReq.pSMSConfigEvents    = NULL;
    indicationRegisterReq.pUserConfigEvents   = NULL;
    indicationRegisterReq.pVoIPConfigEvents   = &regVPEvents;
    resultCode = SLQSImsConfigIndicationRegister( &indicationRegisterReq );
    if( eQCWWAN_ERR_NONE != resultCode )
    {
        return resultCode;
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case.
     */
    qaQmiSetCallback( eQMI_CB_IMS_VOIP_CONFIG_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SLQSSetIMSARegStatusCallback( 
    tFNImsaRegStatus pCallback )
{
    ULONG resultCode  = eQCWWAN_ERR_NONE;
    BYTE  regVPEvents = REGISTER_EVENT;

    IMSAIndRegisterInfo indicationRegisterReq;

    /* Registers for event notifications from the card */
    if( !pCallback )
    {
        regVPEvents = DEREGISTER_EVENT;
    }
    indicationRegisterReq.pRegStatusConfig    = &regVPEvents;
    indicationRegisterReq.pServiceStatusConfig = NULL;
    indicationRegisterReq.pRatHandoverStatusConfig    = NULL;
    resultCode = SLQSRegisterIMSAIndication( &indicationRegisterReq );
    if( eQCWWAN_ERR_NONE != resultCode )
    {
        return resultCode;
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case.
     */
    qaQmiSetCallback( eQMI_CB_IMSA_REG_STATUS_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SLQSSetIMSASvcStatusCallback( 
    tFNImsaSvcStatus pCallback )
{
   ULONG resultCode  = eQCWWAN_ERR_NONE;
    BYTE  regVPEvents = REGISTER_EVENT;

    IMSAIndRegisterInfo indicationRegisterReq;

    /* Registers for event notifications from the card */
    if( !pCallback )
    {
        regVPEvents = DEREGISTER_EVENT;
    }
    indicationRegisterReq.pRegStatusConfig    = NULL;
    indicationRegisterReq.pServiceStatusConfig = &regVPEvents;
    indicationRegisterReq.pRatHandoverStatusConfig    = NULL;
    resultCode = SLQSRegisterIMSAIndication( &indicationRegisterReq );
    if( eQCWWAN_ERR_NONE != resultCode )
    {
        return resultCode;
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case.
     */
    qaQmiSetCallback( eQMI_CB_IMSA_SVC_STATUS_IND, pCallback );
    return eQCWWAN_ERR_NONE;
   
}

global ULONG SLQSSetIMSARatStatusCallback( 
    tFNImsaRatStatus pCallback )
{
    ULONG resultCode  = eQCWWAN_ERR_NONE;
    BYTE  regVPEvents = REGISTER_EVENT;

    IMSAIndRegisterInfo indicationRegisterReq;

    /* Registers for event notifications from the card */
    if( !pCallback )
    {
        regVPEvents = DEREGISTER_EVENT;
    }
    indicationRegisterReq.pRegStatusConfig    = NULL;
    indicationRegisterReq.pServiceStatusConfig = NULL;
    indicationRegisterReq.pRatHandoverStatusConfig    = &regVPEvents;
    resultCode = SLQSRegisterIMSAIndication( &indicationRegisterReq );
    if( eQCWWAN_ERR_NONE != resultCode )
    {
        return resultCode;
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case.
     */
    qaQmiSetCallback( eQMI_CB_IMSA_RAT_HANDOVER_STATUS_IND, pCallback );
    return eQCWWAN_ERR_NONE;
    
}

ULONG SLQSSetQosStatusCallback(
    BYTE instance,
    tFNQosStatus pCallback
    )
{

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case
     */
    qaQmiSetCallback( qmiInst2QosFlowStatusEvent(instance), pCallback );
    return eQCWWAN_ERR_NONE;
}

ULONG SLQSSetQosPriEventCallback(
    tFNQosPriEvent pCallback )
{

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case
     */
    qaQmiSetCallback( eQMI_CB_QOS_PRI_FLOW, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SetUSSDNoWaitIndicationCallback(
    tFNUSSDNoWaitIndication pCallback )
{
    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case
     * The Ussd No wait indication is broadcasted by the Modem and hence no request
     * need to be formulated.
     */
     qaQmiSetCallback( eQMI_CB_VOICE_USSD_NO_WAIT_IND, pCallback );
     return eQCWWAN_ERR_NONE;
}

ULONG SetDDTMPrefIndicationCallback(
      tFNDDTMIndication pCallback )
{
    ULONG resultCode =  eQCWWAN_ERR_NONE;

    if( pCallback )
    {
        resultCode = SLQSNasIndicationRegister( CBK_ENABLE_EVENT,
                                                CBK_ENABLE_EVENT,
                                                CBK_ENABLE_EVENT );
        if( eQCWWAN_ERR_NONE != resultCode )
        {
            return resultCode;
        }
    }
    else
    {
        resultCode = SLQSNasIndicationRegister( CBK_DISABLE_EVENT,
                                                CBK_DISABLE_EVENT,
                                                CBK_DISABLE_EVENT );
        if( eQCWWAN_ERR_NONE != resultCode )
        {
            return resultCode;
        }
    }
    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case
     * The DDTMPrefIndication is broadcasted by the Modem and hence no request
     * need to be formulated.
     */
    qaQmiSetCallback( eQMI_CB_NAS_DDTM_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SetLocCradleMountCallback (
      tFNSetCradleMount pCallback )
{
    ULONG resultCode  = eQCWWAN_ERR_NONE;
    ULONGLONG  regLocEvents = LOC_EVENT_MASK_ENG_STATE;

    LOCEventRegisterReqResp locRegisterReq;

    /* Registers for event notifications from the card */
    if( !pCallback )
    {
        regLocEvents = 0x00;
    }

    locRegisterReq.eventRegister   = regLocEvents;
    resultCode = SLQSLOCEventRegister( &locRegisterReq );
    if( eQCWWAN_ERR_NONE != resultCode )
    {
        return resultCode;
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case.
     */
    qaQmiSetCallback( eQMI_CB_LOC_CRADLE_MOUNT_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SetLocEventTimeSyncCallback (
        tFNSetEventTimeSync pCallback )
{
    ULONG resultCode  = eQCWWAN_ERR_NONE;
    ULONGLONG  regLocEvents = LOC_EVENT_MASK_TIME_SYNC;

    LOCEventRegisterReqResp locRegisterReq;

    /* Registers for event notifications from the card */
    if( !pCallback )
    {
        regLocEvents = 0x00;
    }

    locRegisterReq.eventRegister   = regLocEvents;
    resultCode = SLQSLOCEventRegister( &locRegisterReq );
    if( eQCWWAN_ERR_NONE != resultCode )
    {
        return resultCode;
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case.
     */
    qaQmiSetCallback( eQMI_CB_LOC_EVENT_TIME_SYNC_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SetLocInjectTimeCallback (
        tFNInjectTimeStatus pCallback )
{
    ULONG resultCode  = eQCWWAN_ERR_NONE;
    ULONGLONG  regLocEvents = LOC_EVENT_MASK_INJECT_TIME;

    LOCEventRegisterReqResp locRegisterReq;

    /* Registers for event notifications from the card */
    if( !pCallback )
    {
        regLocEvents = 0x00;
    }

    locRegisterReq.eventRegister   = regLocEvents;
    resultCode = SLQSLOCEventRegister( &locRegisterReq );
    if( eQCWWAN_ERR_NONE != resultCode )
    {
        return resultCode;
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case.
     */
    qaQmiSetCallback( eQMI_CB_LOC_INJECT_TIME_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SetLocSensorStreamingCallback (
              tFNSensorStreaming pCallback )
{
    ULONG resultCode  = eQCWWAN_ERR_NONE;
    ULONGLONG  regLocEvents = LOC_EVENT_MASK_SENSOR_STREAM;

    LOCEventRegisterReqResp locRegisterReq;

    /* Registers for event notifications from the card */
    if( !pCallback )
    {
        regLocEvents = 0x00;
    }

    locRegisterReq.eventRegister   = regLocEvents;
    resultCode = SLQSLOCEventRegister( &locRegisterReq );
    if( eQCWWAN_ERR_NONE != resultCode )
    {
        return resultCode;
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case.
     */
    qaQmiSetCallback( eQMI_CB_LOC_SENSOR_STREAMING_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SetLocInjectSensorDataCallback (
        tFNInjectSensorData pCallback )
{
    ULONG resultCode  = eQCWWAN_ERR_NONE;
    ULONGLONG  regLocEvents = LOC_EVENT_MASK_SENSOR_STREAM;

    LOCEventRegisterReqResp locRegisterReq;

    /* Registers for event notifications from the card */
    if( !pCallback )
    {
        regLocEvents = 0x00;
    }

    locRegisterReq.eventRegister   = regLocEvents;
    resultCode = SLQSLOCEventRegister( &locRegisterReq );
    if( eQCWWAN_ERR_NONE != resultCode )
    {
        return resultCode;
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case.
     */
    qaQmiSetCallback( eQMI_CB_LOC_SENSOR_DATA_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SetLocEventPositionCallback (
              tFNEventPosition pCallback )
{
    ULONG resultCode  = eQCWWAN_ERR_NONE;
    ULONGLONG  regLocEvents = LOC_EVENT_POSITION_REPORT;

    LOCEventRegisterReqResp locRegisterReq;

    /* Registers for event notifications from the card */
    if( !pCallback )
    {
        regLocEvents = 0x00;
    }

    locRegisterReq.eventRegister   = regLocEvents;
    resultCode = SLQSLOCEventRegister( &locRegisterReq );
    if( eQCWWAN_ERR_NONE != resultCode )
    {
        return resultCode;
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case.
     */
    qaQmiSetCallback( eQMI_CB_LOC_POSITION_REPORT_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SetLocGnssSvInfoCallback(
    tFNGnssSvInfo pCallback )
{
    ULONG resultCode  = eQCWWAN_ERR_NONE;
    ULONGLONG  regLocEvents = LOC_EVENT_MASK_GNSS_SV_INFO;

    LOCEventRegisterReqResp locRegisterReq;

    if( !pCallback )
    {
        regLocEvents = 0x00;
    }

    locRegisterReq.eventRegister   = regLocEvents;
    resultCode = SLQSLOCEventRegister( &locRegisterReq );
    if( eQCWWAN_ERR_NONE != resultCode )
    {
        return resultCode;
    }

    /* No Parameter check required as the Callback can have NULL ( disable )
     * and valid address ( enable ) case.
     */
    qaQmiSetCallback( eQMI_CB_LOC_GNSS_SV_INFO_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SetLocOpModeCallback (
              tFNOpMode pCallback )
{
    qaQmiSetCallback( eQMI_CB_LOC_OP_MODE_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SetLocDeleteAssistDataCallback (
              tFNDelAssistData pCallback )
{
    qaQmiSetCallback( eQMI_CB_LOC_DELETE_ASSIST_DATA_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SLQSSetLocInjectUTCTimeCallback (
              tFNInjectUTCTime pCallback )
{
    qaQmiSetCallback( eQMI_CB_LOC_INJECT_UTC_TIME_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SLQSSetLocGetServerCallback (
              tFNGetServer pCallback )
{
    qaQmiSetCallback( eQMI_CB_LOC_GET_SERVER_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SLQSSetLocSetServerCallback (
              tFNSetServer pCallback )
{
    qaQmiSetCallback( eQMI_CB_LOC_SET_SERVER_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SLQSSetLocGetOpModeCallback (
              tFNGetOpMode pCallback )
{
    qaQmiSetCallback( eQMI_CB_LOC_GET_OP_MODE_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}

global ULONG SLQSSetLocGetFixCriteriaCallback (
              tFNGetFixCriteria pCallback )
{
    qaQmiSetCallback( eQMI_CB_LOC_GET_FIX_CRITERIA_IND, pCallback );
    return eQCWWAN_ERR_NONE;
}