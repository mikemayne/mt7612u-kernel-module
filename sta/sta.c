/*
 ***************************************************************************
 * Ralink Tech Inc.
 * 4F, No. 2 Technology 5th Rd.
 * Science-based Industrial Park
 * Hsin-chu, Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2004, Ralink Technology, Inc.
 *
 * All rights reserved. Ralink's source code is an unpublished work and the
 * use of a copyright notice does not imply otherwise. This source code
 * contains confidential trade secret material of Ralink Tech. Any attemp
 * or participation in deciphering, decoding, reverse engineering or in any
 * way altering the source code is stricitly prohibited, unless the prior
 * written consent of Ralink Technology, Inc. is obtained.
 ***************************************************************************

	Module Name:
	sta.c

	Abstract:
	initialization for STA module

	Revision History:
	Who 		When			What
	--------	----------		----------------------------------------------
*/

#include "rt_config.h"


VOID STARxErrorHandle(struct rtmp_adapter *pAd, RX_BLK *pRxBlk)
{

}

/*
	========================================================================
	Routine Description:
		Check Rx descriptor, return NDIS_STATUS_FAILURE if any error found
	========================================================================
*/
INT RTMPCheckRxError(
	IN struct rtmp_adapter *pAd,
	IN HEADER_802_11 *pHeader,
	IN RX_BLK *pRxBlk,
	IN RXINFO_STRUC *pRxInfo)
{
	if(pRxInfo == NULL)
		return NDIS_STATUS_FAILURE;

	/* Phy errors & CRC errors*/
	if (pRxInfo->Crc)
	{
		INT dBm = (pRxBlk->rssi[0]) - pAd->BbpRssiToDbmDelta;

		/* Check RSSI for Noise Hist statistic collection.*/
		if (dBm <= -87)
			pAd->StaCfg.RPIDensity[0] += 1;
		else if (dBm <= -82)
			pAd->StaCfg.RPIDensity[1] += 1;
		else if (dBm <= -77)
			pAd->StaCfg.RPIDensity[2] += 1;
		else if (dBm <= -72)
			pAd->StaCfg.RPIDensity[3] += 1;
		else if (dBm <= -67)
			pAd->StaCfg.RPIDensity[4] += 1;
		else if (dBm <= -62)
			pAd->StaCfg.RPIDensity[5] += 1;
		else if (dBm <= -57)
			pAd->StaCfg.RPIDensity[6] += 1;
		else if (dBm > -57)
			pAd->StaCfg.RPIDensity[7] += 1;

		return NDIS_STATUS_FAILURE;
	}

	/* Add Rx size to channel load counter, we should ignore error counts*/
	//pAd->StaCfg.CLBusyBytes += (pRxBlk->MPDUtotalByteCnt + 14);

	/* Drop ToDs promiscous frame, it is opened due to CCX 2 channel load statistics*/
	if (pHeader != NULL)
	{
		if (pHeader->FC.ToDs
		)
		{
			return NDIS_STATUS_FAILURE;
		}
	}

	/* Paul 04-03 for OFDM Rx length issue*/
	if (pRxBlk->MPDUtotalByteCnt > MAX_AGGREGATION_SIZE)
	{
		DBGPRINT_RAW(RT_DEBUG_ERROR, ("received packet too long\n"));
		return NDIS_STATUS_FAILURE;
	}

	/*
		Drop not U2M frames, cant's drop here because we will drop beacon in this case
		I am kind of doubting the U2M bit operation
	*/
	/*
		if (pRxInfo->U2M == 0)
			return NDIS_STATUS_FAILURE;
	*/

	/* drop decyption fail frame*/
	if (pRxInfo->Decrypted && pRxInfo->CipherErr)
	{

		if (pRxInfo->CipherErr == 2)
			{DBGPRINT_RAW(RT_DEBUG_TRACE,("RxErr: ICV ok but MICErr"));}
		else if (pRxInfo->CipherErr == 1)
			{DBGPRINT_RAW(RT_DEBUG_TRACE,("RxErr: ICV Err"));}
		else if (pRxInfo->CipherErr == 3)
			DBGPRINT_RAW(RT_DEBUG_TRACE,("RxErr: Key not valid"));

		if (INFRA_ON(pAd) && pRxInfo->MyBss) {
			if ((pRxInfo->CipherErr & 1) == 1) {
				RTMPSendWirelessEvent(pAd, IW_ICV_ERROR_EVENT_FLAG,
									pAd->MacTab.Content[BSSID_WCID].Addr,
									BSS0, 0);
			}

			/* MIC Error*/
			if (pRxInfo->CipherErr == 2) {
				CIPHER_KEY *pWpaKey = &pAd->SharedKey[BSS0][pRxBlk->key_idx];

#ifdef WPA_SUPPLICANT_SUPPORT
				if (pAd->StaCfg.wpa_supplicant_info.WpaSupplicantUP)
					WpaSendMicFailureToWpaSupplicant(pAd->net_dev,pHeader->Addr2,
									(pWpaKey->Type == PAIRWISEKEY) ? TRUE:FALSE,
									(INT) pRxBlk->key_idx, NULL);
				else
#endif /* WPA_SUPPLICANT_SUPPORT */
					RTMPReportMicError(pAd, pWpaKey);

				RTMPSendWirelessEvent(pAd, IW_MIC_ERROR_EVENT_FLAG,
									pAd->MacTab.Content[BSSID_WCID].Addr,
									BSS0, 0);
			}
		}

		DBGPRINT_RAW(RT_DEBUG_TRACE,("%s(): %d (len=%d, Mcast=%d, MyBss=%d, Wcid=%d, KeyId=%d)\n",
						__FUNCTION__, pRxInfo->CipherErr, pRxBlk->MPDUtotalByteCnt,
						pRxInfo->Mcast | pRxInfo->Bcast, pRxInfo->MyBss, pRxBlk->wcid,
			pRxBlk->key_idx));

		dump_rxinfo(pAd, pRxInfo);
		dump_rxwi(pAd, pRxBlk->pRxWI);
		hex_dump("ErrorPkt",  (UCHAR *)pHeader, pRxBlk->MPDUtotalByteCnt);

		if (pHeader == NULL)
			return NDIS_STATUS_SUCCESS;

		return NDIS_STATUS_FAILURE;
	}

	return NDIS_STATUS_SUCCESS;
}


INT StaAllowToSendPacket(
	struct rtmp_adapter *pAd,
	struct rtmp_wifi_dev *wdev,
	struct sk_buff *pPacket,
	UCHAR *pWcid)
{
	bool allowToSend;

	if (!INFRA_ON(pAd) && !ADHOC_ON(pAd))
	{
		return FALSE;
	}
	else
	{
		if (ADHOC_ON(pAd))
		{
			RTMP_SET_PACKET_WCID(pPacket, MCAST_WCID);
		}
		if (INFRA_ON(pAd) && (0
		    )) {
			MAC_TABLE_ENTRY *pEntry;
			u8 *pSrcBufVA = GET_OS_PKT_DATAPTR(pPacket);
			pEntry = MacTableLookup(pAd, pSrcBufVA);

			if (pEntry && (IS_ENTRY_DLS(pEntry)
				))
			{
				*pWcid = pEntry->wcid;
			}
			else
				*pWcid = 0;
		} else {
			*pWcid = 0;
		}

		allowToSend = TRUE;
	}

	return allowToSend;
}


INT StaAllowToSendPacket_new(
	IN struct rtmp_adapter *pAd,
	IN struct rtmp_wifi_dev *wdev,
	IN struct sk_buff *pPacket,
	IN UCHAR *pWcid)
{
	MAC_TABLE_ENTRY *pEntry;
	PACKET_INFO PacketInfo;
	UCHAR *pSrcBufVA;
	UINT SrcBufLen;

	RTMP_QueryPacketInfo(pPacket, &PacketInfo, &pSrcBufVA, &SrcBufLen);
	if (MAC_ADDR_IS_GROUP(pSrcBufVA))
	{
		*pWcid = MCAST_WCID;
		return TRUE;
	}

	pEntry = MacTableLookup(pAd, pSrcBufVA);
	if (pEntry && (pEntry->Sst == SST_ASSOC))
	{
		*pWcid = (UCHAR)pEntry->wcid;
		return TRUE;
	}

	return FALSE;
}


INT STAInitialize(struct rtmp_adapter *pAd)
{
	struct rtmp_wifi_dev *wdev = &pAd->StaCfg.wdev;

	wdev->wdev_type = WDEV_TYPE_STA;
	wdev->tx_pkt_allowed = StaAllowToSendPacket;
	wdev->allow_data_tx = TRUE;

	return 0;
}

