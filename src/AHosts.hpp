#pragma once

#include <boost/array.hpp>
#include <boost/random.hpp>
#include <set>
#include "asio.hpp"
#include "auto_buf.hpp"

extern boost::mt19937 AHostsRand;

class DnsServer;
class DnsClient
{
public:
	DnsClient(DnsServer *server, asio::io_service &ioService)
		: m_server(server), m_ioService(ioService), m_id(-1) {}
	virtual ~DnsClient(){}
	virtual int response(aulddays::abuf<char> &res) = 0;
protected:
	DnsServer *m_server;
	asio::io_service &m_ioService;
	uint16_t m_id;
};

class DnsServer
{
public:
	DnsServer(asio::io_service &ioService) : m_client(NULL), m_ioService(ioService) {}
	virtual ~DnsServer(){}
	void setClient(DnsClient *client){ m_client = client; }
	virtual int send(aulddays::abuf<char> &req) = 0;
protected:
	DnsClient *m_client;
	asio::io_service &m_ioService;
};

class AHosts;

class AHostsJob
{
public:
	AHostsJob(AHosts *ahosts, asio::io_service &ioService,
		asio::ip::udp::socket *socket, asio::ip::udp::endpoint *remote, aulddays::abuf<char> *req);
private:
	AHosts *m_ahosts;
	asio::io_service &m_ioService;
	DnsClient *m_client;
	DnsServer *m_server;
};

class AHosts
{
public:
	AHosts() : m_uSocket(m_ioService, asio::ip::udp::v4()){}
	~AHosts(){}
	int start();
private:
	void onUdpRequest(asio::ip::udp::endpoint *remote, aulddays::abuf<char> *buf, const asio::error_code& error, size_t size);
	int listenUdp();
	asio::io_service m_ioService;
	asio::ip::udp::socket m_uSocket;
	std::set<AHostsJob *> m_jobs;
	//asio::ip::udp::endpoint m_uRemote;
	//aulddays::abuf<char> m_ucRecvBuf;

};

class UdpClient : public DnsClient
{
public:
	UdpClient(aulddays::abuf<char> *req, asio::ip::udp::socket *socket, asio::ip::udp::endpoint *remote,
		DnsServer *server, asio::io_service &ioService);
	virtual ~UdpClient(){ delete m_req; delete m_remote; };
	virtual int response(aulddays::abuf<char> &res);
	void onResponsed(const asio::error_code& error, size_t size);
private:
	aulddays::abuf<char> *m_req;
	asio::ip::udp::socket *m_socket;
	asio::ip::udp::endpoint *m_remote;
};

class UdpServer : public DnsServer
{
public:
	UdpServer(asio::io_service &ioService) : DnsServer(ioService), m_socket(m_ioService, asio::ip::udp::v4()){}
	virtual ~UdpServer(){}
	virtual int send(aulddays::abuf<char> &req);
private:
	void onReqSent(asio::ip::udp::endpoint *remote, const asio::error_code& error, size_t size);
	void onResponse(const asio::error_code& error, size_t size);
	asio::ip::udp::endpoint m_remote;
	asio::ip::udp::socket m_socket;
	aulddays::abuf<char> m_res;
};

