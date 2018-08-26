// TcpServer: A class that manages communication with tcp upstream servers

#pragma once
#include "AHosts.hpp"

class TcpServer : public DnsServer
{
public:
	virtual ~TcpServer(){}
	virtual int send(aulddays::abuf<char> &req);
	virtual int cancel();
	virtual int heartBeat(const std::chrono::steady_clock::time_point &now);
private:
};
