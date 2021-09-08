#include "LanServer.h"


bool LanServer::Start(const WCHAR* wpIp, int iPort, short shThreadCount, short shThreadRunningCount, bool bNagle, int iMaxUserCount)
{
	int retval;


	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return false;

	hcp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, shThreadRunningCount);
	if (hcp == NULL)
		return false;

	listenSock = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (listenSock == INVALID_SOCKET)
		return false;



	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));

	serveraddr.sin_family = AF_INET;
	//serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	InetPton(AF_INET, wpIp, &serveraddr.sin_addr);
	serveraddr.sin_port = htons(iPort);

	if (bNagle == true)
	{
		int tcpNoDelay = TRUE;
		setsockopt(listenSock, IPPROTO_TCP, TCP_NODELAY, (char*)& tcpNoDelay, sizeof(tcpNoDelay));
	}

	retval = bind(listenSock, (SOCKADDR*)& serveraddr, sizeof(serveraddr));
	if (retval == SOCKET_ERROR)
		return false;

	retval = listen(listenSock, SOMAXCONN);
	if (retval == SOCKET_ERROR)
		return false;


	_iMaxUserCount = iMaxUserCount;

	_sessionList = new SESSION_INFO[_iMaxUserCount];
	pThreadList = new HANDLE[shThreadCount];

	for (int i = _iMaxUserCount - 1; i >= 0; i--)
	{
		_indexStack.push(i);
	}


	acceptThread = (HANDLE)_beginthreadex(NULL, 0, AcceptThread, (LPVOID)this, 0, 0);
	if (acceptThread == NULL)
		return false;

	monitorThread = (HANDLE)_beginthreadex(NULL, 0, MonitorThread, (LPVOID)this, 0, 0);
	if (monitorThread == NULL)
		return false;

	for (int i = 0; i < shThreadCount; i++)
		pThreadList[i] = (HANDLE)_beginthreadex(NULL, 0, WorkerThread, (LPVOID)this, 0, 0);



	return true;
}

void LanServer::Stop()
{
	closesocket(listenSock);

	if (WaitForSingleObject(acceptThread, INFINITE) == WAIT_OBJECT_0)
	{
		CloseHandle(acceptThread);
	}

	WSACleanup();
}

unsigned WINAPI LanServer::AcceptThread(LPVOID arg)
{
	LanServer* lanServer = (LanServer*)arg;
	SOCKET clinetSocket;
	SOCKADDR_IN clientAddr;
	int addrlen = sizeof(SOCKADDR_IN);

	while (!lanServer->_bShutdown)
	{
		clinetSocket = accept(lanServer->listenSock, (SOCKADDR*)& clientAddr, &addrlen);
		if (clinetSocket == INVALID_SOCKET)
			break;

		if (lanServer->_iPlayerCount >= lanServer->_iMaxUserCount)
		{
			//연결끊기
			closesocket(clinetSocket);
			continue;
		}

		char ip[16];
		inet_ntop(AF_INET, &clientAddr.sin_addr, ip, 16);

		if (lanServer->OnConnectionRequest(ip, clientAddr.sin_port) == true)
		{
			SESSION_INFO* pSessionInfo = lanServer->CreateSession(clinetSocket, lanServer->_iSessionNumber++);

			CreateIoCompletionPort((HANDLE)clinetSocket, lanServer->hcp, (ULONG_PTR)pSessionInfo, 0);

			lanServer->OnClientJoin(&pSessionInfo->connectInfo, pSessionInfo->iSessionID);
			lanServer->RecvPost(pSessionInfo);

			InterlockedIncrement((LONG*)& lanServer->_iPlayerCount);
			InterlockedIncrement((LONG*)& lanServer->_acceptTPS);
			InterlockedIncrement((LONG*)& lanServer->_acceptTotal);

			int index = GET_SESSION_INDEX(pSessionInfo->iSessionID);
			int id = GET_SESSION_ID(pSessionInfo->iSessionID);

			wprintf(L"Accept SeesionID : [%d][%d][%d]\n", pSessionInfo->socket,index, id);


			//wprintf(L"Accept SeesionID : [%d][%d]\n", pSessionInfo->iSessionID);
		}
		else
		{
			continue;
		}



	}

	return 0;
}

int test = 0;

unsigned int WINAPI LanServer::WorkerThread(LPVOID arg)
{
	LanServer* lanServer = (LanServer*)arg;

	DWORD dwTransferred;
	OVERLAPPED* pOverlapped;
	SESSION_INFO* pSessionInfo;

	int retval;

	while (!lanServer->_bShutdown)
	{
		dwTransferred = 0;
		pOverlapped = NULL;
		pSessionInfo = NULL;

		retval = GetQueuedCompletionStatus(lanServer->hcp, &dwTransferred, (PULONG_PTR)& pSessionInfo, (LPOVERLAPPED*)& pOverlapped, INFINITE);

		if (pOverlapped == NULL)
		{
			if (retval == 0)
			{
				int errorCode = WSAGetLastError();
				wprintf(L"IOCP ErrorCode : [%d] \n", errorCode);
				break;
			}
			else if (dwTransferred == 0 && pSessionInfo == NULL)
			{
				//종료!
				wprintf(L"IOCP 종료 \n");
				lanServer->Disconnect(pSessionInfo);
				break;
			}
		}
		else
		{
			if (pOverlapped == &pSessionInfo->recv_overlapped)
			{
				lanServer->RecvProc(dwTransferred, pSessionInfo);
			}

			if (pOverlapped == &pSessionInfo->send_overlapped)
			{
				lanServer->SendProc(dwTransferred, pSessionInfo);
			}


			if (InterlockedDecrement((LONG*)& pSessionInfo->iIOCount) == 0)
			{
				wprintf(L"WorkerThread IOCount\n");
				lanServer->Disconnect(pSessionInfo);
			}

		}
	}

	return 0;

}

unsigned WINAPI LanServer::MonitorThread(LPVOID arg)
{
	LanServer* lanServer = (LanServer*)arg;

	while (1)
	{
		wprintf(L"Accept Total [%d]\n", lanServer->_acceptTotal);
		wprintf(L"Recv TPS [%d]\n", lanServer->_recvTPS);
		wprintf(L"Send TPS [%d]\n", lanServer->_sendTPS);
		wprintf(L"Accept TPS [%d]\n", lanServer->_acceptTPS);
		wprintf(L"Connect User [%d]\n\n", lanServer->_iPlayerCount);

		InterlockedExchange((LONG*)& lanServer->_recvTPS, 0);
		InterlockedExchange((LONG*)& lanServer->_sendTPS, 0);
		InterlockedExchange((LONG*)& lanServer->_acceptTPS, 0);

		Sleep(999);
	}
}
bool LanServer::RecvProc(DWORD dwTransferred, SESSION_INFO* pSessionInfo)
{
	if (dwTransferred == 0)
	{
		//종료
		Disconnect(pSessionInfo);
		return false;
	}

	//wprintf(L"Recv[Session:%d][data:%d]\n", pSessionInfo->iSessionID, dwTransferred);

	pSessionInfo->cRecvQ.MoveRear(dwTransferred);

	do
	{
		st_PACKET_HEADER stHeader;
		int qSize = pSessionInfo->cRecvQ.GetUseSize();

		if (sizeof(st_PACKET_HEADER) > qSize)
			break;

		pSessionInfo->cRecvQ.Peek((char*)& stHeader, sizeof(st_PACKET_HEADER));

		if (stHeader.wSize + sizeof(st_PACKET_HEADER) > (WORD)qSize)
			break;

		pSessionInfo->cRecvQ.MoveFront(sizeof(st_PACKET_HEADER));

		CPacket cPacket;
		if (stHeader.wSize != pSessionInfo->cRecvQ.Dequeue(cPacket.GetBufferPtr(), stHeader.wSize))
		{
			break;
		}

		InterlockedIncrement((LONG*)& _recvTPS);

		cPacket.MoveWritePos(stHeader.wSize);
		SendPacket(pSessionInfo->iSessionID, &cPacket);

	} while (1);

	SendPost(pSessionInfo);
	RecvPost(pSessionInfo);

	return true;
}

bool LanServer::SendProc(DWORD dwTransferred, SESSION_INFO* pSessionInfo)
{
	//wprintf(L"Send[Session:%d][data:%d]\n", pSessionInfo->iSessionID, dwTransferred);

	int qSize = dwTransferred;

	while (1)
	{
		st_PACKET_HEADER stHeader;

		if (sizeof(st_PACKET_HEADER) > qSize)
		{
			break;
		}

		pSessionInfo->cSendQ.Peek((char*)& stHeader, sizeof(st_PACKET_HEADER));

		if (stHeader.wSize + sizeof(st_PACKET_HEADER) > (WORD)qSize)
		{
			break;
		}


		int dataSize = sizeof(st_PACKET_HEADER) + stHeader.wSize;
		pSessionInfo->cSendQ.MoveFront(dataSize);
		qSize -= dataSize;


		InterlockedIncrement((LONG*)& _sendTPS);
	}


	InterlockedDecrement((LONG*)& pSessionInfo->bSendFlag);

	SendPost(pSessionInfo);

	return true;
}

bool LanServer::SendPacket(int iSessionID, CPacket* pPacket)
{
	SESSION_INFO* pSessionInfo = SessionLock(iSessionID);

	if (pSessionInfo == NULL)
	{
		wprintf(L"이상체크");
		return false;
	}

	CPacket packet;
	st_PACKET_HEADER stHeader;
	stHeader.wSize = pPacket->GetDataSize();

	packet.Clear();
	packet.PutData((char*)& stHeader, sizeof(stHeader));
	packet.PutData(pPacket->GetBufferPtr(), pPacket->GetDataSize());

	pSessionInfo->cSendQ.Enqueue(packet.GetBufferPtr(), packet.GetDataSize());

	ReleaseLock(pSessionInfo);

	return true;
}

void LanServer::ReleaseSession(SESSION_INFO* pSessionInfo)
{
	InterlockedExchange((LONG*)& pSessionInfo->bDisconnectFlag, 1);
	if (CancelIo((HANDLE)pSessionInfo->socket) == 0)
	{
		int tt = GetLastError();
		wprintf(L"CancelIO Error %d\n",tt);
	}
}

bool LanServer::Disconnect(SESSION_INFO* pSessionInfo)
{
	if (pSessionInfo->bDisconnectFlag == true)
	{		
		int index = GET_SESSION_INDEX(pSessionInfo->iSessionID);
		int id = GET_SESSION_ID(pSessionInfo->iSessionID);

		wprintf(L"Disconnect SeesionID : [%d][%d][%d]\n", pSessionInfo->socket, index, id);
		
		closesocket(pSessionInfo->socket);

		pSessionInfo->socket = INVALID_SOCKET;
		pSessionInfo->cRecvQ.ClearBuffer();
		pSessionInfo->cSendQ.ClearBuffer();

		InterlockedDecrement((LONG*)& _iPlayerCount);

		_indexStack.push(index);
		
		//sessionList.erase(iSessionID);// (pSession);
		//delete pSession;

		return true;
	}

	return false;
}


SESSION_INFO* LanServer::SessionLock(int iSessionID)
{
	int index = GET_SESSION_INDEX(iSessionID);

	if (iSessionID != _sessionList[index].iSessionID || _sessionList[index].bDisconnectFlag == true)
	{
		if (InterlockedDecrement((LONG*)& _sessionList[index].iIOCount) == 0)
		{
			Disconnect(&_sessionList[index]);
			return NULL;
		}
	}


	if (InterlockedIncrement((LONG*)& _sessionList[index].iIOCount) == 1)
	{
		if (InterlockedDecrement((LONG*)& _sessionList[index].iIOCount) == 0)
		{
			Disconnect(&_sessionList[index]);
			return NULL;
		}
	}

	return &_sessionList[index];
}

void LanServer::ReleaseLock(SESSION_INFO* pSessionInfo)
{
	if (InterlockedDecrement((LONG*)& pSessionInfo->iIOCount) == 0)
	{
		wprintf(L"락해제하다가 아웃\n");
		//연결끊기		
	}
}

SESSION_INFO* LanServer::CreateSession(SOCKET socket, int iSessionNumber)
{
	int index = GetIndex();

	_sessionList[index].iSessionID = COMBINE_ID_INDEX(iSessionNumber, index);
	_sessionList[index].socket = socket;

	memset(&_sessionList[index].recv_overlapped, 0, sizeof(OVERLAPPED));
	memset(&_sessionList[index].send_overlapped, 0, sizeof(OVERLAPPED));

	_sessionList[index].cRecvQ.ClearBuffer();
	_sessionList[index].cSendQ.ClearBuffer();

	_sessionList[index].iIOCount = 0;
	_sessionList[index].bSendFlag = 0;
	_sessionList[index].bDisconnectFlag = 0;

	return &_sessionList[index];
}


void LanServer::RecvPost(SESSION_INFO* pSessionInfo)
{
	WSABUF wBuf[2];
	int bufCount = 1;
	DWORD recvVal = 0;
	DWORD flag = 0;

	ZeroMemory(&wBuf, sizeof(WSABUF) * 2);

	wBuf[0].buf = pSessionInfo->cRecvQ.GetRearBufferPtr();
	wBuf[0].len = pSessionInfo->cRecvQ.DirectEnqueueSize();

	if (pSessionInfo->cRecvQ.GetFreeSize() > pSessionInfo->cRecvQ.DirectEnqueueSize())
	{
		wBuf[1].buf = pSessionInfo->cRecvQ.GetBufferPtr();
		wBuf[1].len = pSessionInfo->cRecvQ.GetFreeSize() - pSessionInfo->cRecvQ.DirectEnqueueSize();
		bufCount++;
	}

	ZeroMemory(&pSessionInfo->recv_overlapped, sizeof(pSessionInfo->recv_overlapped));

	InterlockedIncrement((LONG*)& pSessionInfo->iIOCount);

	int retval = WSARecv(pSessionInfo->socket, wBuf, bufCount, &recvVal, &flag, &pSessionInfo->recv_overlapped, NULL);
	if (retval == SOCKET_ERROR)
	{
		int error = GetLastError();
		if (error != WSA_IO_PENDING)
		{
			wprintf(L"[RecvPost][%d]\n", error);
			if (InterlockedDecrement((LONG*)& pSessionInfo->iIOCount) == 0)
			{
				wprintf(L"RecvPost IOCount\n");
				Disconnect(pSessionInfo);
				//연결끊기
			}
		}
	}
}

void LanServer::SendPost(SESSION_INFO* pSessionInfo)
{
	WSABUF wBuf[2];
	int bufCount = 1;
	DWORD recvVal = 0;
	ZeroMemory(&wBuf, sizeof(WSABUF) * 2);

	if (InterlockedExchange((LONG*)& pSessionInfo->bSendFlag, 1) == 0)
	{
		if (pSessionInfo->cSendQ.GetUseSize() == 0)
		{
			InterlockedExchange((LONG*)& pSessionInfo->bSendFlag, 0);
			return;
		}
		
		wBuf[0].buf = pSessionInfo->cSendQ.GetFrontBufferPtr();
		wBuf[0].len = pSessionInfo->cSendQ.GetUseSize();

		if (pSessionInfo->cSendQ.GetUseSize() > pSessionInfo->cSendQ.DirectDequeueSize())
		{
			wBuf[1].buf = pSessionInfo->cSendQ.GetBufferPtr();
			wBuf[1].len = pSessionInfo->cSendQ.GetUseSize() - pSessionInfo->cSendQ.DirectDequeueSize();
			bufCount++;
		}

		ZeroMemory(&pSessionInfo->send_overlapped, sizeof(pSessionInfo->send_overlapped));


		InterlockedIncrement((LONG*)& pSessionInfo->iIOCount);

		int retval = WSASend(pSessionInfo->socket, wBuf, bufCount, &recvVal, 0, &pSessionInfo->send_overlapped, NULL);
		if (retval == SOCKET_ERROR)
		{
			int error = GetLastError();
			if (error != WSA_IO_PENDING)
			{
				wprintf(L"[SendPost][%d]\n", error);
				if (InterlockedDecrement((LONG*)& pSessionInfo->iIOCount) == 0)
				{
					wprintf(L"SendPost IOCount\n");
					Disconnect(pSessionInfo);
					//연결끊기
				}
			}
		}

	}
}

int LanServer::GetIndex()
{
	int index = 0;
	if (_indexStack.size() == 0)
	{
		return index;
	}
	else
	{
		index = _indexStack.top();
		_indexStack.pop();
		return index;
	}
}