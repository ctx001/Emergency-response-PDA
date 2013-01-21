// GPRS_UDP.h : PROJECT_NAME
//

#pragma once

#ifndef __AFXWIN_H__
	#error "include 'stdafx.h' before including this file for PCH"
#endif

#ifdef POCKETPC2003_UI_MODEL

#include <pm.h>
#include <connmgr.h>
#include <Pmpolicy.h>
#include <afxmt.h>
#include "resourceppc.h"
#include "GPRS_UDPDlg.h"
#include "Profile.h"
#include "SimNumEnterDlg.h"
#include "Sms.h"
#include "IMEI_IMSI.h"

#endif 

// CGPRS_UDPApp:
// please refer to GPRS_UDP.cpp for more detail
//

class CGPRS_UDPApp : public CWinApp
{
public:
	CGPRS_UDPApp();
	

public:
	virtual BOOL InitInstance();
	void SendPosData(char* lpBuf);   //send location-based string 'lpBuf' to designated server IP
	bool PhoneMode();     //determine if the wireless communication network is available
	int SimNumDiff();     //check if the current and previous IMSIs are the same
	//void CloseHeartBeat();
	static UINT AFX_CDECL SysSuspendThreadFunc(LPVOID lpParam);   //power manager thread to enable the app operational when backlight is off
	//static UINT AFX_CDECL SmsSend_recvThreadFunc(LPVOID lpParam);
	//static UINT AFX_CDECL CloseHbThreadFunc(LPVOID lpParam);
	//static UINT AFX_CDECL ChangeIp_IntervalThreadFunc(LPVOID lpParam);
	
	static CGPRS_UDPApp* s_pthis;    //pointer for this instance
	static HANDLE s_hEventShutDown;  //to indicate the event of app shutdown
	static HANDLE s_hSocketChange;   //to indicate the Socket has changed
	static bool s_bInvalidPositioning;   //to indicate if the positioning is valid
	static GPS_POSITION s_GpsPosition;
	static PDAINFO s_PdaInfo;    //property info for PDA
	static int s_cycle;    //info send cycle
	static CMainSocket* s_pRecvUdpSocket;   
	static int s_nSimNumDiffMrk;
	static CString s_strPdaStatus;   //check PDA status
	static PVEHICLEINFO s_pVehicleInfo;   //positioning related data for PDA
	static CCriticalSection s_PdaStatusSection;   //critical region variable
	static CCriticalSection s_CycleSection;   //critical region variable
	CConnectManager m_ConnectManager;


	DECLARE_MESSAGE_MAP()
	virtual int ExitInstance();
};

extern CGPRS_UDPApp theApp;
