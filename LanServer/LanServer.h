#pragma once
#pragma comment(lib,"ws2_32")
#pragma comment(lib, "winmm.lib")

#include <WinSock2.h>
#include <stack>
#include <unordered_map>
#include <process.h>
#include <WS2tcpip.h>


#include "CPacket.h"
#include "CRingBuffer.h"

struct st_PACKET_HEADER
{
	WORD wSize;			// 패킷 사이즈.
};

struct CONNECT_INFO
{
	char* cpIp;
	int iPort;
};

#define COMBINE_ID_INDEX(ID,INDEX) ((ID << 48) | INDEX)
#define GET_SESSION_ID(ID) (ID >> 48)
#define GET_SESSION_INDEX(ID)  (ID & 0xffff)

struct SESSION_INFO
{
	int iSessionID;
	OVERLAPPED recv_overlapped;
	OVERLAPPED send_overlapped;
	SOCKET socket;
	WSABUF wsabuf;
	CRingBuffer cRecvQ;
	CRingBuffer cSendQ;

	int iIOCount;
	LONG bSendFlag;
	LONG bDisconnectFlag;

	CONNECT_INFO connectInfo;
};


class LanServer
{

public :
	bool Start(const WCHAR* wpIp, int iPort, short shThreadCount, short shThreadRunningCount, bool bNagle, int iMaxUserCount);
	void Stop();
	int GetSessionCount() { return _iPlayerCount; }

	bool Disconnect(SESSION_INFO* pSessionInfo);
	bool SendPacket(int iSessionID, CPacket* pPacket);

	//int GetAcceptTotal() { return _acceptTotal; }
	//int GetAcceptTPS() { return _acceptTPS; }
	//int GetRecvTPS() { return _recvTPS; }
	//int GetSendTPS() { return _sendTPS; }
	//int GetConnectUser() {return _iConnectCount;}



protected:
	virtual bool OnConnectionRequest(char* pIp, int iPort) = 0;
	virtual void OnClientJoin(CONNECT_INFO* pConnectInfo,int iSessionID) = 0;
	virtual void OnClientLeave(int iSessionID) = 0;

	virtual void OnRecv(int iSessionID, CPacket* pPacket) = 0;
	virtual void OnSend(int iSessionID, int iSendsize) = 0;

	virtual void OnWorkerThreadBegin() = 0;
	virtual void OnWorkerThreadEnd() = 0;

	virtual void OnError(int iErrorcode, WCHAR* wpBuf) = 0;

private:

	static unsigned int WINAPI WorkerThread(LPVOID arg);
	static unsigned int WINAPI AcceptThread(LPVOID arg);
	static unsigned int WINAPI MonitorThread(LPVOID arg);

	SESSION_INFO* CreateSession(SOCKET socket,int iSessionNumber);
	void ReleaseSession(SESSION_INFO* pSessionInfo);

	void RecvPost(SESSION_INFO* pSessionInfo);
	void SendPost(SESSION_INFO* pSessionInfo);

	bool RecvProc(DWORD dwTransferred, SESSION_INFO* pSessionInfo);
	bool SendProc(DWORD dwTransferred, SESSION_INFO* pSessionInfo);

	SESSION_INFO* SessionLock(int iSessionID);
	void ReleaseLock(SESSION_INFO* pSessionInfo);

	int GetIndex();

	int _acceptTPS = 0;
	int _recvTPS = 0;
	int _sendTPS = 0;

	int _acceptTotal = 0;
	int _iPlayerCount = 0;
	int _iMaxUserCount = 0;	


	int _iSessionNumber = 0;
	bool _bShutdown = false;

	HANDLE acceptThread;
	HANDLE monitorThread;

	HANDLE* pThreadList;	
	SOCKET listenSock;
	//std::unordered_map<int, SESSION_INFO*> sessionList;	

	std::stack<int> _indexStack;
	SESSION_INFO* _sessionList;
	HANDLE hcp;
};