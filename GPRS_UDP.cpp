// GPRS_UDP.cpp : 定义应用程序的类行为。
//

#include "stdafx.h"
#include "GPRS_UDP.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// CGPRS_UDPApp

BEGIN_MESSAGE_MAP(CGPRS_UDPApp, CWinApp)
END_MESSAGE_MAP()

//声明及初始化全局变量
HANDLE CGPRS_UDPApp::s_hEventShutDown = NULL;
HANDLE CGPRS_UDPApp::s_hSocketChange = NULL;
CGPRS_UDPApp* CGPRS_UDPApp::s_pthis = NULL;
bool CGPRS_UDPApp::s_bInvalidPositioning = true;
GPS_POSITION CGPRS_UDPApp::s_GpsPosition;
PDAINFO CGPRS_UDPApp::s_PdaInfo;
int CGPRS_UDPApp::s_cycle = 30;
PVEHICLEINFO CGPRS_UDPApp::s_pVehicleInfo = new VEHICLEINFO;
CMainSocket* CGPRS_UDPApp::s_pRecvUdpSocket;
int CGPRS_UDPApp::s_nSimNumDiffMrk = 0;
CString CGPRS_UDPApp::s_strPdaStatus = _T("0");   //PDA的状态初始化为“正常”
//CString CGPRS_UDPApp::s_strPdaStatus = _T("100");
CCriticalSection CGPRS_UDPApp::s_PdaStatusSection;
CCriticalSection CGPRS_UDPApp::s_CycleSection;

// CGPRS_UDPApp 构造
CGPRS_UDPApp::CGPRS_UDPApp()
	: CWinApp()
{
	// TODO: 在此处添加构造代码，
	// 将所有重要的初始化放置在 InitInstance 中
}


// 唯一的一个 CGPRS_UDPApp 对象
CGPRS_UDPApp theApp;

// CGPRS_UDPApp 初始化

BOOL CGPRS_UDPApp::InitInstance()
{
    // 在应用程序初始化期间，应调用一次 SHInitExtraControls 以初始化
    // 所有 Windows Mobile 特定控件，如 CAPEDIT 和 SIPPREF。
    SHInitExtraControls();

	// 标准初始化
	// 如果未使用这些功能并希望减小
	// 最终可执行文件的大小，则应移除下列
	// 不需要的特定初始化例程
	// 更改用于存储设置的注册表项
	// TODO: 应适当修改该字符串，
	// 例如修改为公司或组织名
	SetRegistryKey(_T("应用程序向导生成的本地应用程序"));

	//判断配置文件或SIM卡状态
	s_nSimNumDiffMrk = SimNumDiff();
	//如果不存在SIM卡在PDA中，则将软件关闭
	if(s_nSimNumDiffMrk == -1)
	{
		exit(0);
	}
	
	//没有配置文件或本次IMSI与前次软件运行时不同
	else if(s_nSimNumDiffMrk == 0 || s_nSimNumDiffMrk == 1)
	{
		//弹出输入手机号对话框
		CSimNumEnterDlg Initdlg;
		m_pMainWnd = &Initdlg;
		Initdlg.DoModal();

		//发短信验证号码合法性线程
		//CWinThread *pSmsSend_recvThread = new CWinThread((AFX_THREADPROC)CGPRS_UDPApp::SmsSend_recvThreadFunc, NULL);
		//pSmsSend_recvThread->CreateThread();
	}

	//如果存在配置文件且本次IMSI与前次软件运行时的一样，则将PDA状态置为正常
	else
	{
		s_PdaStatusSection.Lock();
		s_strPdaStatus = _T("0");
		s_PdaStatusSection.Unlock();
	}

	//将服务器IP、端口及手机SIM卡等信息写入全局变量s_PdaInfo
	s_PdaInfo.strIp = CProfile::GetProfileString(L"HeartBeat",L"ServerIp",L"",FILENAME);
	s_PdaInfo.strPort = CProfile::GetProfileString(L"HeartBeat",L"ServerPort",L"",FILENAME);
	s_PdaInfo.strSimNumber = CProfile::GetProfileString(L"HeartBeat",L"SIM",L"",FILENAME);

	//定义一个一般窗口类，使软件没有界面
	CWnd	WndTemp;
	m_pMainWnd = &WndTemp;

	//软件连接GPRS
	m_ConnectManager.ConnectToGPRS();

	//软件进行Socket连接，在构造函数中即可完成相关操作
	s_pRecvUdpSocket = new CMainSocket;

	s_pthis = this;

	//创建关于软件退出的事件
	s_hEventShutDown = CreateEvent(NULL, TRUE, FALSE, NULL);

	//创建关于Socket改变的事件
	s_hSocketChange = CreateEvent(NULL, FALSE, FALSE, NULL);

	//定位获取、数据发送、电源管理线程
	CWinThread *pSuspendThread = new CWinThread((AFX_THREADPROC)CGPRS_UDPApp::SysSuspendThreadFunc, (LPVOID)m_pMainWnd);
	pSuspendThread->CreateThread();

	//监听短信更改IP线程
	//CWinThread *pChangeIp_IntervalThread = new CWinThread((AFX_THREADPROC)CGPRS_UDPApp::ChangeIp_IntervalThreadFunc, (LPVOID)m_pMainWnd);
	//pChangeIp_IntervalThread->CreateThread();

	//侦听是否有请求关闭心跳软件短信的线程
	//CWinThread *pCloseHbThread = new CWinThread((AFX_THREADPROC)CGPRS_UDPApp::CloseHbThreadFunc, (LPVOID)m_pMainWnd);
	//pCloseHbThread->CreateThread();

	DWORD dwWaitRes;
	//将以上两个事件句柄组成一个数组进行下面的WaitForMultipleObjects
	HANDLE rgHandles[2] = {s_hEventShutDown, s_hSocketChange};
	while(TRUE)
	{
		dwWaitRes = WaitForMultipleObjects(2, rgHandles, FALSE, 100000);
		
		//s_hEventShutDown事件被触发，跳出循环，软件关闭
		if(dwWaitRes == WAIT_OBJECT_0)
		{
			break;
		}

		//s_hSocketChange事件被触发，重置服务器下发命令所用的PDA Socket
		if(dwWaitRes == WAIT_OBJECT_0+1)
		{
			s_pRecvUdpSocket->ResetSocket();
		}

		//等候超时，向服务器发送命令下发用的PDA Socket
		if(dwWaitRes == WAIT_TIMEOUT)
		{
			CString strRecvSocket = _T("##RS,") + s_PdaInfo.strSimNumber;
			char	lpBuf[DEFAULT_SEND_BYTES_NUM] = {'\0'};

			WideCharToMultiByte(CP_ACP,0,strRecvSocket,-1,lpBuf,DEFAULT_SEND_BYTES_NUM,NULL,NULL);
			s_pRecvUdpSocket->SendTo(lpBuf, DEFAULT_SEND_BYTES_NUM);
		}
	}

	Sleep(2000);
	CloseHandle(s_hEventShutDown);
	CloseHandle(s_hSocketChange);

	//INT_PTR nResponse = dlg.DoModal();
	//if (nResponse == IDOK)
	//{
		// TODO: 在此处放置处理何时用“确定”来关闭
		//  对话框的代码
	//}

	// 由于对话框已关闭，所以将返回 FALSE 以便退出应用程序，
	//  而不是启动应用程序的消息泵。
	return FALSE;
}

//发送位置字符串lpBuf到指定服务器的IP端口
void CGPRS_UDPApp::SendPosData(char* lpBuf)
{
	CMainSocket SendUdpSocket;    //初始化并创建发送位置信息给服务器的PDA Socket
	int		nSendLength;     //发送长度

	//是否已经连接到GPRS
	if(m_ConnectManager.IsConnectToGPRS())
	{
		//定位有效
		if(!s_bInvalidPositioning)
		{
			//数据发送
			nSendLength = SendUdpSocket.SendTo(lpBuf, DEFAULT_SEND_BYTES_NUM);
			
			//发送数据出现错误
			if(nSendLength == SOCKET_ERROR)
			{
				return;
			}
		}

		//还未定位
		else
		{
			char	chBufInit[DEFAULT_SEND_BYTES_NUM] = {'\0'};
			
			//发送固定格式的信息，告诉服务器PDA还在定位初始化
			CString	strBufInit = _T("&&IN1234,") + CGPRS_UDPApp::s_PdaInfo.strSimNumber + _T("#");

			WideCharToMultiByte(CP_ACP,0,strBufInit ,-1,chBufInit,DEFAULT_SEND_BYTES_NUM,NULL,NULL);
			nSendLength = SendUdpSocket.SendTo(chBufInit, DEFAULT_SEND_BYTES_NUM);
			if(nSendLength == SOCKET_ERROR)
			{
				return;
			}
		}
	}

	//如果GPRS连接失败，释放连接的资源，并重新连接到GPRS
	if(!m_ConnectManager.m_bIsConnFlag)
	{
		m_ConnectManager.ReleaseConnection();
		m_ConnectManager.ConnectToGPRS();
	}

	//将Socket连接关闭
	SendUdpSocket.Close();
}

//电源管理线程，实现在黑屏情况下的软件运行
UINT AFX_CDECL CGPRS_UDPApp::SysSuspendThreadFunc(LPVOID lpParam)
{
	bool		bRun = true;         //指示软件是否仍运行
	bool		bCallIdleReset = false;   //重置Idle时间
	CEDEVICE_POWER_STATE DeviceState;   //设备电源状态
	time_t		GpsBlckTm = 0;    //GPS在持续没有收到有效定位信息情况下的关闭时间
	time_t		CommBlckTm = 0;   //GPRS在持续没有收到信号情况下的关闭时间
	time_t		GpsRstrtTm = 0;   //GPS重置时间
	CTime		refTime = CTime::GetCurrentTime();   //获取当前时间
	time_t		RefTimeOnTime_t = (time_t)refTime.GetTime();   //获取当前时间的time_t格式
	char		lpBuf[DEFAULT_SEND_BYTES_NUM] = {'\0'};
	HANDLE		hGPSPowerReq = SetPowerRequirement(L"GPD0:", D0, POWER_NAME|POWER_FORCE, NULL, NULL);

	// handle to the native event that is signalled when the GPS
	// device state changes
	HANDLE hDeviceStateChange = CreateEvent(NULL, FALSE, FALSE, NULL);

	//handle to the native event that is signalled when the GPS
	// devices gets a new location
	HANDLE hNewLocationData = CreateEvent(NULL, FALSE, FALSE, NULL);

	// handle to the gps device
	HANDLE hGpsHandle = GPSOpenDevice( hNewLocationData, hDeviceStateChange, NULL, 0);

	// size of a POWER_BROADCAST message
	DWORD cbPowerMsgSize = sizeof POWER_BROADCAST + (MAX_PATH * sizeof TCHAR);
 
	// Initialize our MSGQUEUEOPTIONS structure
	MSGQUEUEOPTIONS mqo;
	mqo.dwSize = sizeof(MSGQUEUEOPTIONS);
	mqo.dwFlags = MSGQUEUE_NOPRECOMMIT;
	mqo.dwMaxMessages = 4;
	mqo.cbMaxMessage = cbPowerMsgSize;
	mqo.bReadAccess = TRUE;

	// Create a message queue to receive power notifications
	HANDLE hPowerMsgQ = CreateMsgQueue(NULL, &mqo);

	// Request power notifications 
	HANDLE hPowerNotifications = RequestPowerNotifications(hPowerMsgQ,
		PBT_TRANSITION | PBT_POWERINFOCHANGE);

	//将模式调为UNATTENDEDMODE
	PowerPolicyNotify(PPN_UNATTENDEDMODE, TRUE);

	//将电源通知信息队列的消息、关闭软件的消息、有新位置信息到来的消息和设备状态改变的消息组成数组
	HANDLE rgHandles[4] = {hPowerMsgQ, s_hEventShutDown, hNewLocationData, hDeviceStateChange};

	while (bRun)
	{
		DWORD dwWaitRes = WaitForMultipleObjects(4, rgHandles, FALSE, 5000);
		switch (dwWaitRes)
		{
			//电源通知信息队列的消息发生
			case WAIT_OBJECT_0:
				{
					DWORD cbRead;
					DWORD dwFlags;
					POWER_BROADCAST *ppb = (POWER_BROADCAST*) new BYTE[cbPowerMsgSize];
					ZeroMemory(ppb, cbPowerMsgSize);

					if(ReadMsgQueue(hPowerMsgQ, ppb, cbPowerMsgSize, &cbRead, 
							0, &dwFlags))
					{
						switch (ppb->Message)
						{
							//系统电源状态发生改变
							case PBT_TRANSITION:
								{
									CString strPowerState = ppb->SystemPowerState;

									// handle power states
									//系统电源状态被置为unattended
									if (!strPowerState.Compare(_T("unattended")))
									{
										//Idle时间被重置
										bCallIdleReset = true;
										SystemIdleTimerReset();
										ResetEvent(hPowerMsgQ);
									}
									break;
								}

							case PBT_POWERINFOCHANGE:
							default:
								{
									GetDevicePower(L"BKL1:", POWER_NAME|POWER_FORCE, &DeviceState);

									//如果背景灯的亮度亮于D2
									if(DeviceState < D2)
									{
										//指示Idle时间重置的BOOL变量置为false
										bCallIdleReset = false;
									}

									if(bCallIdleReset)
									{
										//Idle时间被重置并将hPowerMsgQ事件重置
										SystemIdleTimerReset();
										ResetEvent(hPowerMsgQ);
									}

									break;
								}
						}
					}
					break;
				}

			//软件关闭的事件被触发，软件关闭
			case WAIT_OBJECT_0+1:
				{
					bRun = false;
					break;
				}

			//有新位置信息到来
			case WAIT_OBJECT_0+2:
				{
					GetDevicePower(L"BKL1:", POWER_NAME|POWER_FORCE, &DeviceState);

					//背景灯的亮度亮于D2
					if(DeviceState < D2)
					{
						bCallIdleReset = false;
					}

					if(bCallIdleReset)
					{
						SystemIdleTimerReset();
						ResetEvent(hPowerMsgQ);
					}

					CString		strSend;
					CProtocol	Protocol;

					//初始化相关字段
					ZeroMemory(&s_GpsPosition,sizeof(GPS_DEVICE));
					s_GpsPosition.dwVersion = GPS_VERSION_1;
					s_GpsPosition.dwSize = sizeof(GPS_POSITION);

					//定位成功
					if(ERROR_SUCCESS == GPSGetPosition(hGpsHandle, &s_GpsPosition, 5000, 0))
					{
						//定位数据有实际意义，执行定位数据类型转换等操作
						if(s_GpsPosition.dblLatitude != 0.0 && s_GpsPosition.dblLongitude != 0.0
							&& s_GpsPosition.stUTCTime.wYear > 1970 && s_GpsPosition.stUTCTime.wYear < 30827
							&& s_GpsPosition.dwSatelliteCount >= 4)
						{
							//pDlg->GpsNormalText();
							strSend = Protocol.TransGPSIDtoAnHua(s_GpsPosition);
							WideCharToMultiByte(CP_ACP,0,strSend ,-1,lpBuf,DEFAULT_SEND_BYTES_NUM,NULL,NULL);
							Protocol.BufferToStruct(lpBuf, s_GpsPosition);
							//pDlg->DisplayData(s_pVehicleInfo);

							s_bInvalidPositioning = false;
							
							//GPS关闭时间保持为零
							GpsBlckTm = 0;
							break;
						}

						//定位数据没有实际意义
						else
						{
							//GPS关闭时间为零
							if(0 == GpsBlckTm)
							{
								//记住当前的时间
								CTime CurrTm = CTime::GetCurrentTime();
								GpsBlckTm = (time_t)CurrTm.GetTime();
							}

							//如果持续没有GPS信号的时间超过接受范围，将GPS设备关闭
							if (TimeBias(GpsBlckTm) > GPS_COMMU_CEASE_INTERVAL)
							{
								GPSCloseDevice(hGpsHandle);
								hGpsHandle = NULL;
								ResetEvent(hGpsHandle);
								ResetEvent(hNewLocationData);
								ResetEvent(hDeviceStateChange);
							}
						}
					}

					//pDlg->GpsInitialText();
					//pDlg->DisplayData(pDlg->pVehicleInfoEmpty);
					s_bInvalidPositioning = true;

					break;
				}

			// 如果设备状态改变
			case WAIT_OBJECT_0+3:
				{
					GetDevicePower(L"BKL1:", POWER_NAME|POWER_FORCE, &DeviceState);

					if(DeviceState < D2)
					{
						bCallIdleReset = false;
					}

					if(bCallIdleReset)
					{
						SystemIdleTimerReset();
						ResetEvent(hPowerMsgQ);
					}

					break;
				}

			// 如果等候超时
			default:
				{
					GetDevicePower(L"BKL1:", POWER_NAME|POWER_FORCE, &DeviceState);

					if(DeviceState < D2)
					{
						bCallIdleReset = false;
					}

					if(bCallIdleReset)
					{
						SystemIdleTimerReset();
						ResetEvent(hPowerMsgQ);
					}

					break;
				}
		}

		//如果有无线通信网络可用
		if(s_pthis->PhoneMode())
		{
			//GPRS连接关闭的时间设为零
			CommBlckTm = 0;

			//如果GPS设备处于打开状态
			if(NULL != hGpsHandle)
			{
				//判断是否两次上报位置信息的时间间隔是否超过
				//上报周期，如果达到上报周期，就上报给服务器
				time_t	deltaSec = TimeBias(RefTimeOnTime_t);
				
				s_CycleSection.Lock();
				if(deltaSec > (time_t)s_cycle)
				{
					s_pthis->SendPosData(lpBuf);
					refTime = CTime::GetCurrentTime();
					RefTimeOnTime_t = (time_t)refTime.GetTime();
				}
				s_CycleSection.Unlock();
			}
		}

		//如果没有无线通信网络可用
		else
		{
			//如果GPS设备处于打开状态
			if(NULL != hGpsHandle)
			{
				//释放并重新建立GPRS连接
				s_pthis->m_ConnectManager.ReleaseConnection();
				s_pthis->m_ConnectManager.ConnectToGPRS();

				//如果GPRS连接关闭的时间为零，则记录下该时刻
				if(0 == CommBlckTm)
				{
					CTime CurrTm = CTime::GetCurrentTime();
					CommBlckTm = (time_t)CurrTm.GetTime();
				}

				//如果GPRS连接持续断开，且时间超过一定时限，将关闭GPS设备
				if (TimeBias(CommBlckTm) > GPS_COMMU_CEASE_INTERVAL)
				{
					GPSCloseDevice(hGpsHandle);
					hGpsHandle = NULL;
					ResetEvent(hGpsHandle);
					ResetEvent(hNewLocationData);
					ResetEvent(hDeviceStateChange);
				}
			}
		}

		//如果GPS设备已经关闭
		if(NULL == hGpsHandle)
		{
			//如果GPS重启时间为零，则记录下此时时间
			if(0 == GpsRstrtTm)
			{
				CTime CurrTm = CTime::GetCurrentTime();
				GpsRstrtTm = (time_t)CurrTm.GetTime();
			}

			//如果所需GPS重启时间累积达到一定时限，则打开GPS设备、连接
			//GPRS并将GpsBlckTm、CommBlckTm、GpsRstrtTm置零
			if((TimeBias(GpsRstrtTm) > GPS_COMMU_CEASE_INTERVAL))
			{
				hGpsHandle = GPSOpenDevice( hNewLocationData, hDeviceStateChange, NULL, 0);
				s_pthis->m_ConnectManager.ReleaseConnection();
				s_pthis->m_ConnectManager.ConnectToGPRS();
				GpsBlckTm = 0;
				CommBlckTm = 0;
				GpsRstrtTm = 0;
				s_bInvalidPositioning = true;
			}
		}
	}

	//在线程结束前结束相关操作，并释放资源
	PowerPolicyNotify(PPN_UNATTENDEDMODE, FALSE);
	StopPowerNotifications(hPowerNotifications);
	CloseMsgQueue(hPowerMsgQ);
	CloseHandle(hPowerNotifications);
	CloseHandle(hPowerMsgQ);
	ReleasePowerRequirement(hGPSPowerReq); // allow GPS device to go in sleep
	
	if(NULL == hGpsHandle)
	{
		GPSCloseDevice(hGpsHandle);
	}
	
	CloseHandle(hGpsHandle);
	CloseHandle(hGPSPowerReq);
	CloseHandle(hDeviceStateChange);
	CloseHandle(hNewLocationData);

	return 0;
}

int CGPRS_UDPApp::ExitInstance()
{
	// TODO: 在此添加专用代码和/或调用基类
	return 0;
}

/*void CGPRS_UDPApp::CloseHeartBeat()
{
	m_ConnectManager.ReleaseConnection();
	delete s_pRecvUdpSocket;
	delete s_pVehicleInfo;
	SetEvent(s_hEventShutDown);
}*/

//查看是否有通信网络可用，有的话返回真，无返回假
bool CGPRS_UDPApp::PhoneMode()
{
	HKEY	hKey;
	LPBYTE	szRadioReadyState = new BYTE [4];
	DWORD	dwBufLen = 4;
	int		nKeyValue;

	// Let us open the registry to get a handle to the Phone Registry key. 
	if (ERROR_SUCCESS == RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"System\\State\\Phone\\", 0,  KEY_READ, &hKey)) 
	{
		RegQueryValueEx(hKey, L"Radio Ready State", NULL, NULL, 
			szRadioReadyState, &dwBufLen); 

		nKeyValue = *szRadioReadyState;
	}

	delete[] szRadioReadyState;
	RegCloseKey(hKey);

	if(nKeyValue == 15 || nKeyValue == 31)
	{
		return true;
	}

	return false;
}

//在软件运行之初查看是否有配置文件，以及SIM卡的验证信息。如果
//PDA中没有SIM卡返回－1、如果没有配置文件返回0、如果有配置文件
//且与前次软件运行时的卡IMSI不同返回1、如果有配置文件且与前次
//软件运行时的卡IMSI相同返回2
int CGPRS_UDPApp::SimNumDiff()
{
	CFileStatus		status;
	CIMEI_IMSI		Imei_Imsi;

	//获取SIM卡的IMSI信息
	Imei_Imsi.GetHWInfo(Imei_Imsi.mygenralinfo);
	s_PdaInfo.strImsi = Imei_Imsi.mygenralinfo.SubscriberNumber.c_str();

	//PDA中没有SIM卡
	if(!s_PdaInfo.strImsi.Compare(_T("Unavailable")))
	{
		return -1;
	}

	//没有配置文件
	if(!CFile::GetStatus(FILENAME,status))
	{
		return 0;
	}

	//与前次软件运行时的卡IMSI不同
	CString strImsiRtrn = CProfile::GetProfileString(L"HeartBeat",L"IMSI",L"",FILENAME);
	if(s_PdaInfo.strImsi.Compare(strImsiRtrn))
	{
		return 1;
	}

	//与前次软件运行时的卡IMSI相同
	else
	{
		return 2;
	}
}

/*UINT AFX_CDECL CGPRS_UDPApp::SmsSend_recvThreadFunc(LPVOID lpParam)
{
	int		nCounter = 0;
	CSms	Sms;
	SMSINFO strSmsSender;
	DWORD dwWaitRes;

	//删除收件箱中所有包含"号码设置成功！"的内容
	Sms.DltInboxSms(_T("号码设置成功！"));

	//产生一个随机数作为验证码
	srand((unsigned)GetTickCount());
	int nRnd = rand();
	CString strRnd = IntToCString(nRnd);
	CString strSmsContents = _T("号码设置成功！验证码：") + strRnd;
	LPTSTR	SmsContents = strSmsContents.GetBuffer(strSmsContents.GetLength());
	strSmsContents.ReleaseBuffer();

	//给自己发送包含SmsContents内容的短信
	CString strRecipient = _T("86") + s_PdaInfo.strSimNumber;
	LPTSTR lpszRecipient = strRecipient.GetBuffer(strRecipient.GetLength());
	strRecipient.ReleaseBuffer();
	Sms.SendSMS(FALSE, TRUE, NULL, lpszRecipient, SmsContents);

	while(nCounter < SMS_TOLERANT_COUNTER)
	{
		dwWaitRes = WaitForSingleObject(s_hEventShutDown, 5000);
		if(dwWaitRes == WAIT_OBJECT_0)
		{
			break;
		}

		if(dwWaitRes == WAIT_TIMEOUT)
		{
			strSmsSender = Sms.GetInboxSms(SmsContents);
			if(!strSmsSender.strSim.IsEmpty())
			{
				//短信为自己发送的
				if(strSmsSender.strSim.Find(strRecipient) != -1)
				{
					s_PdaStatusSection.Lock();
					s_strPdaStatus = _T("0");
					s_PdaStatusSection.Unlock();

					CProfile::WriteProfileString(L"HeartBeat",L"IMSI",s_PdaInfo.strImsi,FILENAME);

					//删除收件箱中所有包含SmsContents的内容
					Sms.DltInboxSms(SmsContents);
					break;
				}

				else
				{
					//删除收件箱中所有包含SmsContents的内容
					Sms.DltInboxSms(SmsContents);
				}
			}

			nCounter++;
		}
	}

	return 0;
}*/

/*UINT AFX_CDECL CGPRS_UDPApp::CloseHbThreadFunc(LPVOID lpParam)
{
	CSms	Sms;
	SMSINFO strSmsSender;
	CString strRecipient = _T("86") + s_PdaInfo.strSimNumber;

	//删除收件箱中所有包含"GBXT"的内容
	Sms.DltInboxSms(CLOSEHEARTBEAT);

	while(TRUE)
	{
		Sleep(5000);

		strSmsSender = Sms.GetInboxSms(CLOSEHEARTBEAT);
		if(!strSmsSender.strSim.IsEmpty())
		{
			//短信为自己发送的
			if(strSmsSender.strSim.Find(strRecipient) != -1)
			{
				if(CheckCloseHbDate(strSmsSender.strContent))
				{
					//删除收件箱中所有包含"GBXT"的内容
					Sms.DltInboxSms(CLOSEHEARTBEAT);
					break;
				}
			}

			//删除收件箱中所有包含"GBXT"的内容
			Sms.DltInboxSms(CLOSEHEARTBEAT);
		}
	}

	s_pthis->CloseHeartBeat();
	return 0;
}*/

/*UINT AFX_CDECL CGPRS_UDPApp::ChangeIp_IntervalThreadFunc(LPVOID lpParam)
{
	CSms	Sms;
	SMSINFO strSmsSender;
	CString strRecipient = _T("86") + s_PdaInfo.strSimNumber;
	CString strIp;
	CString strInterval;
	DWORD	dwWaitRes;

	//删除收件箱中所有包含IPCHANGESMS和INTERVALCHANGESMS的内容
	Sms.DltInboxSms(IPCHANGESMS);
	Sms.DltInboxSms(INTERVALCHANGESMS);

	while(TRUE)
	{
		dwWaitRes = WaitForSingleObject(s_hEventShutDown, 5000);
		if(dwWaitRes == WAIT_OBJECT_0)
		{
			break;
		}

		if(dwWaitRes == WAIT_TIMEOUT)
		{
			strSmsSender = Sms.GetInboxSms(IPCHANGESMS);
			if(!strSmsSender.strSim.IsEmpty())
			{
				//短信为自己发送的
				if(strSmsSender.strSim.Find(strRecipient) != -1)
				{
					strIp = strSmsSender.strContent.Right(strSmsSender.strContent.GetLength() - IPCHANGESMS.GetLength());
					if(CheckInPutIp(strIp))
					{
						s_PdaInfo.strIp = strIp;
						CProfile::WriteProfileString(L"HeartBeat",L"ServerIp",s_PdaInfo.strIp,FILENAME);
						SetEvent(s_hSocketChange);
					}

					//删除收件箱中所有包含IPCHANGESMS的内容
					Sms.DltInboxSms(IPCHANGESMS);
				}

				else
				{
					//删除收件箱中所有包含IPCHANGESMS的内容
					Sms.DltInboxSms(IPCHANGESMS);
				}

				strSmsSender.strSim.Empty();
			}

			else
			{
				strSmsSender = Sms.GetInboxSms(INTERVALCHANGESMS);
				if(!strSmsSender.strSim.IsEmpty())
				{
					//短信为自己发送的
					if(strSmsSender.strSim.Find(strRecipient) != -1)
					{
						strInterval = strSmsSender.strContent.Right(strSmsSender.strContent.GetLength() - IPCHANGESMS.GetLength());
						if(CheckInPutInterval(strInterval))
						{
							char lpInterval[4] = {'\0'};

							WideCharToMultiByte(CP_ACP,0,strInterval,-1,lpInterval,strInterval.GetLength(),NULL,NULL);
							s_CycleSection.Lock();
							s_cycle = atoi(lpInterval);
							s_CycleSection.Unlock();
						}

						//删除收件箱中所有包含INTERVALCHANGESMS的内容
						Sms.DltInboxSms(INTERVALCHANGESMS);
					}

					else
					{
						//删除收件箱中所有包含INTERVALCHANGESMS的内容
						Sms.DltInboxSms(INTERVALCHANGESMS);
					}

					strSmsSender.strSim.Empty();
				}
			}
		}
	}

	return 0;
}*/