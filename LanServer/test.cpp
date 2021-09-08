#include <stdio.h>
#include "LanServer.h"

class TestServer : public LanServer
{
protected:
	virtual bool OnConnectionRequest(char* pIp, int iPort)
	{
		return true;
	}

	virtual void OnClientJoin(CONNECT_INFO* pConnectInfo, int iSessionID)
	{

	}

	virtual void OnClientLeave(int iSessionID)
	{

	}

	virtual void OnRecv(int iSessionID, CPacket* pPacket)
	{

	}
	virtual void OnSend(int iSessionID, int iSendsize)
	{

	}

	virtual void OnWorkerThreadBegin()
	{

	}
	virtual void OnWorkerThreadEnd()
	{

	}

	virtual void OnError(int iErrorcode, WCHAR* wpBuf)
	{

	}
};


void main()
{
	TestServer* lanServer = new TestServer();
	lanServer->Start(L"127.0.0.1", 6000, 4, 4, true, 3000);

	while (1)
	{

	}
}