/**
 *
 * @ingroup swiim
 *
 * @file
 * image select fuction of image management 
 *
 * @author
 * Copyright: 2015 Sierra Wireless, Inc.
 *            All rights reserved
 *
 */

/* include files */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <stdbool.h>

#include "SwiDataTypes.h"
#define  LOG_TAG  "swi_imgmgr"
#include "swiril_log.h"
#include "swiim_img.h" 
#include "qmerrno.h"
#include "swiqmitool_common.h"

#define RESET_WITH_DM_CMD 0x10

extern struct ImageList devImgList;
static bool fwDwlComplete = false;
extern struct ImageFinalList devImgFinalList[50];
extern ULONG SetImagePath(
    CHAR    *path,
    ULONG   isMDM9x15,
    WORD    imgMask );


/**
 *
 * Call back function to notify FW download completion
 *
 * @param [in] status 
 *     Call back parameter indicate FW download completion status
 *
 * @return
 *    none
 *
 * @par Abort:<br> 
 *     none
 *
 * @note
 *     none
 *
 */ 
void FirmwareDwlCbk(ULONG status)
{
    if (eQCWWAN_ERR_NONE == status) {
        LOGI("Firmware Download Completed");
    } else {
        LOGE( "Firmware Not Downloaded");
    }
    /* set firmware complete to true */
    fwDwlComplete = true;

    /* Unsubscribe from the callback */
    SetFwDldCompletionCbk(NULL);
}


/**
 *
 * Register call back function
 *
 *
 * @return
 *     - true  if succeed
 *     - false if failed
 *         
 * @par Abort:<br> 
 *     none
 *
 * @note
 *     none
 *
 */       
bool SetFwDwldCompletionCallback()
{
    ULONG ret = 0;

    ret = SetFwDldCompletionCbk(FirmwareDwlCbk);

    if( (0 != ret) &&
        (4 != ret) )
        LOGE("SetFwDldCompletionCbk failed, ret : %lu. ", ret);

    return ((ret==0)||(ret==4));
}


/**
 *
 * Check whether the image from host exist in the module to
 *
 * @param [in] chBuildId 
 *     Image ID of downloaded image 
 * @param [in] pImageList 
 *     Device Image list to be compared 
 *
 * @return
 *     - true  if exist
 *     - false if not exist
 *         
 * @par Abort:<br> 
 *     none
 *
 * @note
 *     none
 *
 */       
bool IsImageExistInList(const char *chBuildId, struct sImageList *pImageList)
{
    LOGD("IsImageExistInList Entered.");
    bool fRet = false;

    LOGD("donwloaded image build id %s.", chBuildId);

    while(pImageList)
    {
        if (strcmp(chBuildId, pImageList->BuildId) == 0)
        {
            fRet = true;
            LOGD("donwloaded image found.");
            break;
        }
        pImageList = pImageList->mpNext;
    }

    LOGD("IsImageExistInList Exit.");
    return fRet;
}

/**
 *
 * Check whethere download image is succesful
 *
 * @param 
 *     none
 *
 * @return
 *     - true  if succeed
 *     - false if failed
 *         
 * @par Abort:<br> 
 *     none
 *
 * @note
 *     none
 *
 */       
bool CheckDownload()
{
    LOGD("CheckDownload Entered.");
    char szBuildID[MAXPATHSIZE] = "";
    bool fRet = true;
    int i = 0;
    BYTE nBuildIdLength = 0;
    ULONG nImageListSize = sizeof(struct PrefImageList);
    struct PrefImageList imageList;
    struct sImageList lstUpdatedDeviceAppImages;
    struct sImageList lstUpdatedDevicePriImages;
    struct sImageList *psImageList = NULL;

    /* Initialize the structure, so that pointers are initialized to NULL */ 
    memset(&lstUpdatedDeviceAppImages, 0, sizeof(lstUpdatedDeviceAppImages));
    memset(&lstUpdatedDevicePriImages, 0, sizeof(lstUpdatedDevicePriImages));

    /* construct image list from firmware images*/
    if(!LoadDeviceImages(&lstUpdatedDeviceAppImages, &lstUpdatedDevicePriImages))
    {
        LOGE("LoadDeviceImages Failed.");
        return false;
    }
    
    /* Get the image ID just downloaded*/
    ULONG result = GetImagesPreference(&nImageListSize, (BYTE *)&imageList);

    if (!result)
    {
        BYTE nListSize = imageList.listSize;

        // cycle through image list elements
        for (i=0; i< nListSize; i++)
        {
            BYTE ImageType = imageList.listEntries[i].imageType;

            nBuildIdLength = imageList.listEntries[i].buildIdLength;
            memset(szBuildID, 0, sizeof(szBuildID));
            strncpy( (char *)szBuildID,
                     (char *)imageList.listEntries[i].buildId,
                     nBuildIdLength);
            if (ImageType == eGOBI_MBN_TYPE_MODEM)
            {
                psImageList = &lstUpdatedDeviceAppImages;;
            }
            else if (ImageType == eGOBI_MBN_TYPE_PRI)
            {
                psImageList = &lstUpdatedDevicePriImages;
            }
            else
            {
                LOGE("Unsupported image type: %d.", ImageType);
                return false;
            }

            fRet = IsImageExistInList(szBuildID, psImageList);
            if (!fRet)
            {
                LOGE("Downloaded image did not exist in image list.");
                break;
            }
        }
    }
    else
    {
        LOGE("GetImagesPreference failed: %lu.", result);
    }

    /* deconstruct image list */
    ReleaseDeviceImages(&lstUpdatedDeviceAppImages, &lstUpdatedDevicePriImages);
    LOGD("CheckDownload Exit.");
    return fRet;
}

/**
 *
 * Image management with "--select" option
 *
 * @param [in] iSelectID 
 *     ID selected for image path
 * @param [in] chListPath 
 *     qualified path to image file
 *
 * @return
 *     true if function succeed
 *     FASLE if funciton failed
 *
 * @par Abort:<br> 
 *     none
 *
 * @note
 *     none
 *
 */       
bool SelectImage(int iSelectID, char *chSelectPath)
{
    LOGD("SelectImage Entered.");
    char newpathp[MAXPATHSIZE] = "";
    char chIdPath[MAXPATHSIZE] = "";
    struct qmifwinfo_s FwInfo;
    bool ret = false;
    int folderLen,i;
    bool cwe_format = false;
    ULONG bForceDownload = 0x00;
    
    strcpy(newpathp, chSelectPath);
    folderLen = strlen(newpathp);
    if (newpathp[folderLen - 1] != '/')
    {
        strcat(newpathp, "/");
    }
    snprintf(chIdPath, MAXPATHSIZE, "%d/", iSelectID);
    strcat(newpathp, chIdPath);

    /* Check which image format is supported CWE or G3K */
    swi_uint32 result = 0;

    if(bootupdated == false)
    {
        if (SLQSIsSpkgFormatRequired())
        {
            cwe_format = true;
            result = SLQSGetImageInfoMC77xx( (char *)newpathp, &FwInfo);
        }
        else {
            result = SLQSGetImageInfoMC83xx((char *)newpathp, &FwInfo);
        }

        LOGD( "ImageInfo at path %s returned status %lu", newpathp, result);
        if (result) {
            printf("\nTarget image info could not be retrieved. Error!\n");
            if (cwe_format)
                fprintf(stderr,"Make sure that CWE format image is available to download at path: %s\n",newpathp);
            else
                fprintf(stderr,"Make sure that G3K(AMSS+UQCN) format image is available to download at path: %s\n",newpathp);
            goto EXIT;
        }
    }
    else
    {
        printf("\nDownload in boot mode, support both CWE and MBN images,please use CWE image file first and use MBN if download for CWE fails\n");
        printf("\nUse Ctl+c to cancel download in 30s ");
        for(i=30; i>0; i--)
        {
            sleep(1);
            printf(".");
            fflush(stdout);
        }
        printf("\n");
    }
    /* Set callback function for firmware download completion */
    if (!SetFwDwldCompletionCallback())
    {
        LOGE("SetFwDwldCompletionCallback failed.");
        goto EXIT;
    } 

    result = UpgradeFirmware2k( newpathp, bForceDownload );
    LOGD( "select folder : %s ; len : %d", newpathp, strlen(newpathp));

    if(!result)
    {
        LOGD("UpgradeFirmware for %s succeed: %lu.", newpathp, result);
    }
    else
    {
        LOGE("UpgradeFirmware for %s failed: %lu.", newpathp, result);
        goto EXIT;
    }

    /* Fimware Download call back */
    LOGD( "Activating Selected Image. . ." );
    fwDwlComplete = false;
    while( !fwDwlComplete )
    {
        sleep(1);
    }

    printf("Download completed, verify downloaded image now.\r\n");    
    sleep(15); /* wait for download complete*/    

    /* Check whether downloading is succeful*/
    qmiDeviceDisconnect();
    qmiDeviceConnect();

    if (!CheckDownload())
    {
        LOGE("CheckDownload Failed, Download failed .");
        goto EXIT;
    }
    LOGD("Firmware download for %s succeed.", newpathp);
    ret = true;

EXIT:
    LOGD("SelectImage Exit.");    
    return ret;
}

/**
 *
 * Image management with "--selectmbn" option
 *
 * @param [in] iSelectID 
 *     ID selected for image path
 * @param [in] chListPath 
 *     qualified path to image file
 *
 * @return
 *     true if function succeed
 *     FASLE if funciton failed
 *
 * @par Abort:<br> 
 *     none
 *
 * @note
 *     none
 *
 */       
bool SelectMbnImage(int iSelectID, char *chSelectPath)
{
    LOGD("SelectMbnImage Entered.");
    char newpathp[MAXPATHSIZE] = "";
    char chIdPath[MAXPATHSIZE] = "";
    struct qmifwinfo_s FwInfo;
    bool ret = false;
    int folderLen;
    bool bSupportSPKG = false;
    
    strcpy(newpathp, chSelectPath);
    folderLen = strlen(newpathp);
    if (newpathp[folderLen - 1] != '/')
    {
        strcat(newpathp, "/");
    }
    snprintf(chIdPath, MAXPATHSIZE, "%d/", iSelectID);
    strcat(newpathp, chIdPath);

    /* With --selectmbn option, G3K format is used for all versions of bootloader */
    swi_uint32 result = 0;

    if(bootupdated == true)
    {
        printf("--selectmbn option does not work in boot mode\n");
        goto EXIT;
    }
    
    result = SLQSGetImageInfoMC83xx((char *)newpathp, &FwInfo); 
    LOGD( "ImageInfo at path %s returned status %lu", newpathp, result); 
    if (result) {
        printf("--selectmbn option supports only G3K format image\n");
        fprintf(stderr,"Make sure that G3K(AMSS+UQCN) format image is available to download at path: %s\n",newpathp);
        goto EXIT;         
    }    
 
    /* Set callback function for firmware download completion */
    if (!SetFwDwldCompletionCallback())
    {
        LOGE("SetFwDwldCompletionCallback failed.");
        goto EXIT;
    }

    /* If CWE format is actually supported for this Boot loader */
    bSupportSPKG = SLQSIsSpkgFormatRequired();

    /* Force G3K downlaod method irrespective of Bootloader version */
    result = UpgradeFirmwareG3k( newpathp);    
    if(!result)
    {
        LOGD("Firmware Upgrade for %s succeed: %lu.", newpathp, result);
    }
    else
    {
        printf("Firmware Upgrade for %s failed: %lu.\n", newpathp, result);
        if (bSupportSPKG)
            fprintf(stderr,"Make sure mbn download is supported in the connected modem\n");
        goto EXIT;
    }

    /* Fimware Download call back */
    LOGD( "Activating Selected Image. . ." );
    fwDwlComplete = false;
    while( !fwDwlComplete )
    {
        sleep(1);
    }

    printf("Download completed, verify downloaded image now.\r\n");    
    sleep(15); /* wait for download complete*/    

    /* Check whether downloading is succeful*/
    qmiDeviceDisconnect();
    qmiDeviceConnect();

    if (!CheckDownload())
    {
        LOGE("CheckDownload Failed, Download failed .");
        if (bSupportSPKG)
            fprintf(stderr,"Make sure mbn download is supported in the connected modem\n");
        goto EXIT;
    }
    LOGD("Firmware download for %s succeed.", newpathp);
    ret = true;

EXIT:
    LOGD("SelectMbnImage Exit.");    
    return ret;
}

/**
 * Calculate the Image index corresponding the actual PRI Image list
 *
 * @param [out] imageIndex 
 *     Index of the Image in the device to be selected
 *
 * @return
 *     true if function succeed
 *     false if function failed
 *
 * @par Abort:<br> 
 *     none
 *
 * @note
 *     none
 */
bool ComputeImageIndex ( BYTE *imageIdx )
{
    struct ImageIDEntries *pPriImgEntry;
    BYTE idx = 0, priImageIdx = 0;

    /* Figure out the index of PRI image type in the received image list */
    /* If no PRI image exist on the device */
    if( !GetPRIImageIdx( &priImageIdx ) ) {
        LOGE("FAIL SAFE Image Active!" );
    }

    /* Store the pointer to PRI image list */
    pPriImgEntry   = &devImgList.imageIDEntries[priImageIdx];
    if (*imageIdx > pPriImgEntry->imageIDSize) {
        LOGE( "Invalid Image Location selected");
        return false;
    }

    /* Compute the PRI Image Index with respect to the ID which is selected */
    for( idx = 0; idx < pPriImgEntry->imageIDSize; idx++ ) {
        if( devImgFinalList[idx].IsLatest ) {
            /* Condition to check the valid Image Index */
            if( !(--(*imageIdx)) ) {
                *imageIdx = idx;
                return true;
            }
        }
    }
    return false;
}

/**
 * Activate the image on the device selected by the user.
 *
 * @param [in] imageIndex 
 *     Index of the Image in the device to be selected
 *
 * @return
 *     true if function succeed
 *     false if function failed
 *
 * @par Abort:<br> 
 *     none
 *
 * @note
 *     none
 */
bool SelectDeviceImage( BYTE imageIndex )
{
    struct PrefImageList prefImageList;
    struct ImageIdElement *pActPRIImg = NULL;
    struct ImageIDEntries *pActModemImg = NULL;
    ULONG resultCode = 0, imageListSize = 0, imageTypeSize = 0;
    int modemImageFound = false;
    BYTE modemImdIdx = 0, priImageIdx = 0;
    BYTE imageType[IMG_BUF_SIZE];

    /* Populate the device Image structure without displaying */
    if (!ListDeviceImages (false))
    {
        return false;
    }

    while(1)
    {
        /* Calculate the Image index corresponding the actual PRI Image list */
        if (!ComputeImageIndex( &imageIndex )) {
            return false;
        }

        /* Get the PRI image index from the Image list */
        if( !GetPRIImageIdx( &priImageIdx ) ) {
            return false;
        }

        /* Store the pointer to PRI image list */
        pActPRIImg = &( devImgList.imageIDEntries[priImageIdx].\
                          imageIDElement[imageIndex] );

        /* Fill the PrefImageList structure with PRI image info */
        prefImageList.listSize = 1;
        prefImageList.listEntries[0].imageType = IMG_TYPE_PRI;
        memcpy( (void *)prefImageList.listEntries[0].imageId,
                (void *)pActPRIImg->imageID,
                GOBI_MBN_IMG_ID_STR_LEN );
        prefImageList.listEntries[0].buildIdLength = pActPRIImg->buildIDLength;
        if( 0 != pActPRIImg->buildIDLength )
        {
            strcpy( prefImageList.listEntries[0].buildId,
                    pActPRIImg->buildID );
        }

        /* Store the pointer to Modem image list */
        pActModemImg = &( devImgList.imageIDEntries[!priImageIdx] );

        /* Check if the corresponding MODEM image exist on device.
         * If the CDMA image is selected by the user then Modem image with same
         * build id should exist
         */
        for(modemImdIdx = 0;
            modemImdIdx < pActModemImg->imageIDSize;
            modemImdIdx++)
        {
            CHAR *pbuildID = pActModemImg->imageIDElement[modemImdIdx].buildID;
            ULONG buildIDlen =
                     pActModemImg->imageIDElement[modemImdIdx].buildIDLength;

            /* If the corresponding MODEM image is found */
            if (CheckModemImage(imageIndex,
                                modemImdIdx,
                                pbuildID,
                                buildIDlen)) {
                modemImageFound = true;
                break;
            }          
        }

        /* For CDMA, it might be possible that generic CDMA modem image is available.
           That image will fit with any of the CDMA carrier */
        if( !modemImageFound )
        {
            struct qmifwinfo_s *pPRIImgInfo = &devImgFinalList[imageIndex].imgInfo;

            if (pPRIImgInfo->dev.g.Technology == eGOBI_IMG_TECH_CDMA)
            {
                for(modemImdIdx = 0; modemImdIdx < pActModemImg->imageIDSize; modemImdIdx++)
                {
                    CHAR *pbuildID = pActModemImg->imageIDElement[modemImdIdx].buildID;
                    ULONG buildIDlen = pActModemImg->imageIDElement[modemImdIdx].buildIDLength;

                    /* If the Generic CDMA MODEM image is found */
                    if (CheckModemCdmaGenericImage(imageIndex,
                                modemImdIdx,
                                pbuildID,
                                buildIDlen)) 
                    {
                        modemImageFound = true;
                        break;
                    }
                }
            }  
        }

        /* If corresponding MODEM image does not exist on the device, return */
        if( !modemImageFound ) {
                printf("Failed to activate selected image"
                      ": MODEM image does not exist on the device\r\n");
                return false;
        }

        /* Reset the flag to false */
        modemImageFound = false;

        /* MODEM image exist, retrieve the information */
        prefImageList.listSize++;
        prefImageList.listEntries[1].imageType = IMG_TYPE_MODEM;
        memcpy( (void *)prefImageList.listEntries[1].imageId,
                (void *)pActModemImg->imageIDElement[modemImdIdx].imageID,
                GOBI_MBN_IMG_ID_STR_LEN );
        prefImageList.listEntries[1].buildIdLength =
             pActModemImg->imageIDElement[modemImdIdx].buildIDLength;

        if( 0 != pActModemImg->imageIDElement[modemImdIdx].buildIDLength )
        {
            strcpy( prefImageList.listEntries[1].buildId,
                    pActModemImg->imageIDElement[modemImdIdx].buildID );
        }

        imageListSize = sizeof( prefImageList );
        imageTypeSize = IMG_BUF_SIZE;
        memset( (void *)imageType, 0, imageTypeSize );

        /** 
         * Set the Image Path - Not required
         * Workaround for the issue in SLQS as the FW Download callback would
         * not be invoked without setting the image path.
         * Set to some path which does not exist
         */
        resultCode = SetImagePath( "Generic", (ULONG)FALSE, RESET_WITH_DM_CMD );

        if( resultCode )
        {
            printf("Failed to set Image path"
                             "Failure Code : %lu\r\n", resultCode );
            continue;
        }

        /* Set the preference for selected image in device */
        resultCode = SetImagesPreference( imageListSize,
                                          (BYTE *)&prefImageList,
                                          0,
                                          0xFF,
                                          &imageTypeSize,
                                          imageType );

        if( resultCode )
        {
            printf("Failed to activate selected image"
                             "Failure Code : %lu\r\n", resultCode );
            continue;
        }

        /* Subscribe to Device State Change callback */
        if (!SetFwDwldCompletionCallback())
        {
            printf("SetFwDwldCompletionCallback failed.\r\n");
            return false;
        }

        /* Reset the device to activate the image */
        resultCode = SetPower( RESET_MODEM );
        if( resultCode )
        {
            printf("Failed to reset: Failure Code : %lu \r\n", resultCode );
            continue;
        }

        /* Wait until selected image gets activated */
        printf("Activating Selected Image. . .\r\n" );
        fwDwlComplete = false;
        while( !fwDwlComplete )
        {
            sleep(1);
        }

        printf("Select Device completed, verify new image now.\r\n");
        sleep(15);

        /* re-connect to device after device reset */  
        qmiDeviceDisconnect();
        qmiDeviceConnect();

        /* If we fail to list the device images, return */
        if( !ListDeviceImages(false) )
        {
            return false;
        }

        if( (UNKNOWN_EXECUTING_IMG_IDX == 
                devImgList.imageIDEntries[priImageIdx].executingImage) ||
            (FAIL_SAFE_IMG_IDX == 
                devImgList.imageIDEntries[!priImageIdx].executingImage) )
        {
            printf("FAIL SAFE Image Active!!\r\n");
            return false;
        }

        imageIndex = ( devImgList.imageIDEntries[priImageIdx].executingImage );
        pActPRIImg = &( devImgList.imageIDEntries[priImageIdx].\
                          imageIDElement[imageIndex] );


        /* Check if the selected image gets activated */
        if( !strcmp( pActPRIImg->buildID,
                               prefImageList.listEntries[0].buildId ) )
        {
            printf("Selected Image activated successfully.\r\n");
            return true;
        }
        else
        {
            printf("Failed to activate selected image.\r\n");
            return false;
        }
    }
    return false;
}

/**
 * Activate the raw image on the device selected by the user.
 *
 * @param [in] imageUqcnIndex imageAmssIndex
 *     Index of the uqcn and amss Image in the device to be selected
 *
 * @return
 *     true if function succeed
 *     false if function failed
 *
 * @par Abort:<br> 
 *     none
 *
 * @note
 *     none
 */
bool SelectRawDeviceImage( BYTE imageUqcnIndex, BYTE imageAmssIndex )
{
    struct PrefImageList prefImageList;
    struct ImageIdElement *pActPRIImg = NULL;
    struct ImageIDEntries *pActModemImg = NULL;
    ULONG resultCode = 0, imageListSize = 0, imageTypeSize = 0;
    int modemImageFound = false;
    BYTE modemImdIdx = 0, priImageIdx = 0;
    BYTE imageType[IMG_BUF_SIZE];

    /* Populate the device Image structure without displaying */
    if (!ListRawDeviceImages (false))
    {
        return false;
    }
    while (1) {       
        imageUqcnIndex--;

        /* Get the PRI image index from the Image list */
        if( !GetPRIImageIdx( &priImageIdx ) )
        {
            fprintf(stderr,"\tFAIL SAFE Image Active!!\n\n" );
            return false;
        }

        /* Store the pointer to PRI image list */
        pActPRIImg = &( devImgList.imageIDEntries[priImageIdx].\
                          imageIDElement[imageUqcnIndex] );

        /* Fill the PrefImageList structure with PRI image info */
        prefImageList.listSize = 1;
        prefImageList.listEntries[0].imageType = IMG_TYPE_PRI;
        memcpy( (void *)prefImageList.listEntries[0].imageId,
                (void *)pActPRIImg->imageID,
                GOBI_MBN_IMG_ID_STR_LEN );

        prefImageList.listEntries[0].buildIdLength = pActPRIImg->buildIDLength;
        if( 0 != pActPRIImg->buildIDLength )
        {
            strcpy( prefImageList.listEntries[0].buildId,
                    pActPRIImg->buildID );
        }

        /* Store the pointer to Modem image list */
        pActModemImg = &( devImgList.imageIDEntries[!priImageIdx] );
   
        /* MODEM image exist, retrieve the information */
        prefImageList.listSize++;
        prefImageList.listEntries[1].imageType = IMG_TYPE_MODEM;
        memcpy( (void *)prefImageList.listEntries[1].imageId,
                (void *)pActModemImg->imageIDElement[imageAmssIndex].imageID,
                GOBI_MBN_IMG_ID_STR_LEN );
        prefImageList.listEntries[1].buildIdLength = pActModemImg->imageIDElement[imageAmssIndex].buildIDLength;
        if( 0 != pActModemImg->imageIDElement[imageAmssIndex].buildIDLength )
        {
            strcpy( prefImageList.listEntries[1].buildId,
                    pActModemImg->imageIDElement[imageAmssIndex].buildID );
        }

        imageListSize = sizeof( prefImageList );
        imageTypeSize = IMG_BUF_SIZE;
        memset( (void *)imageType, 0, imageTypeSize );

        /* Set the preference for selected image in device */
        resultCode = SetImagesPreference( imageListSize,
                                     (BYTE *)&prefImageList,
                                      0,
                                      0xFF,
                                      &imageTypeSize,
                                      imageType );

        if( resultCode != 0 )
        {
            fprintf( stderr, "Failed to activate selected image\n"\
                        "Failure Code : %lu\n", resultCode );
            continue;
        }

        /* Subscribe to Device State Change callback */
        if (!SetFwDwldCompletionCallback())
        {
            fprintf(stderr,"SetFwDwldCompletionCallback failed.");
            return false;
        }

        /* Reset the device to activate the image */
        resultCode = SetPower( RESET_MODEM );
        if( resultCode )
        {
            fprintf(stderr,"Failed to reset: Failure Code : %lu", resultCode );
            continue;
        }

        /* Wait until selected image gets activated */
        fprintf(stderr, "Activating Selected Image. . ." );
        fwDwlComplete = false;
        while( !fwDwlComplete )
        {
            sleep(1);
        }

        printf("\r\nSelect Raw Device completed, verify new image now.\r\n");
        sleep(15);

        /* re-connect to device after device reset */  
        qmiDeviceDisconnect();
        qmiDeviceConnect();

        /* If we fail to list the device images, return */
        if( !ListRawDeviceImages(false) )
        {
            return false;
        }

        if( (UNKNOWN_EXECUTING_IMG_IDX == 
                devImgList.imageIDEntries[priImageIdx].executingImage) ||
            (FAIL_SAFE_IMG_IDX == 
                devImgList.imageIDEntries[!priImageIdx].executingImage) )
        {
            fprintf(stderr,"FAIL SAFE Image Active!!");
            return false;
        }

        imageUqcnIndex = ( devImgList.imageIDEntries[priImageIdx].executingImage );
        pActPRIImg = &( devImgList.imageIDEntries[priImageIdx].\
                          imageIDElement[imageUqcnIndex] );

        /* Check if the selected image gets activated */
        if(!strcmp( pActPRIImg->buildID,
                           prefImageList.listEntries[0].buildId ) )
        {
            fprintf( stderr, "\nSelected Image activated successfully\n");
            return true;
        }
        else
        {   
            fprintf( stderr, "\nFailed to activate selected image\n");
            return false;
        }
    }
    return false;
}

