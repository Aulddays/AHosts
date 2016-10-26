#pragma once

#include <boost/array.hpp>
#include <boost/random.hpp>
#include <set>
#include "asio.hpp"
#include "auto_buf.hpp"

extern boost::mt19937 AHostsRand;

class DnsServer;
class AHostsJob;
class DnsClient
{
public:
	DnsClient(AHostsJob *job, DnsServer *server, asio::io_service &ioService)
		: m_job(job), m_server(server), m_ioService(ioService), m_id(-1) {}
	virtual ~DnsClient(){}
	virtual int response(aulddays::abuf<char> &res) = 0;
protected:
	AHostsJob *m_job;
	DnsServer *m_server;
	asio::io_service &m_ioService;
	uint16_t m_id;
};

class DnsServer
{
public:
	DnsServer(AHostsJob *job, asio::io_service &ioService)
		: m_job(job), m_client(NULL), m_ioService(ioService) {}
	virtual ~DnsServer(){}
	void setClient(DnsClient *client){ m_client = client; }
	virtual int send(aulddays::abuf<char> &req) = 0;
protected:
	AHostsJob *m_job;
	DnsClient *m_client;
	asio::io_service &m_ioService;
};

class AHosts;

class AHostsJob
{
public:
	AHostsJob(AHosts *ahosts, asio::io_service &ioService,
		const asio::ip::udp::socket &socket, const asio::ip::udp::endpoint &remote, const aulddays::abuf<char> &req);
	~AHostsJob(){ delete m_client; delete m_server; }
	int clientComplete(DnsClient *client);
	int serverComplete(DnsServer *server);
private:
	AHosts *m_ahosts;
	asio::io_service &m_ioService;
	DnsClient *m_client;
	DnsServer *m_server;
	int m_status;	// client and server complete status
};

class AHosts
{
public:
	AHosts() : m_uSocket(m_ioService, asio::ip::udp::v4()), m_hbTimer(m_ioService){}
	~AHosts(){}
	int start();
	int stop(){ m_ioService.stop(); return 0; }
	int jobComplete(AHostsJob *job);
private:
	void onUdpRequest(const asio::error_code& error, size_t size);
	void onHeartbeat(const asio::error_code& error);

	int listenUdp();
	asio::io_service m_ioService;
	asio::ip::udp::socket m_uSocket;
	std::set<AHostsJob *> m_jobs;
	asio::ip::udp::endpoint m_uRemote;
	aulddays::abuf<char> m_ucRecvBuf;
	asio::deadline_timer m_hbTimer;	// heartbeat timer
	static const int HBTIMEMS = 100;	// trigger heartbeat every 0.1 sec
};

class UdpClient : public DnsClient
{
public:
	UdpClient(const aulddays::abuf<char> &req, const asio::ip::udp::socket &socket, const asio::ip::udp::endpoint &remote,
		DnsServer *server, AHostsJob *job, asio::io_service &ioService);
	virtual ~UdpClient(){ };
	virtual int response(aulddays::abuf<char> &res);
	void onResponsed(const asio::error_code& error, size_t size);
private:
	aulddays::abuf<char> m_req;
	asio::ip::udp::socket m_socket;
	asio::ip::udp::endpoint m_remote;
};

class UdpServer : public DnsServer
{
public:
	UdpServer(AHostsJob *job, asio::io_service &ioService)
		: DnsServer(job, ioService), m_socket(m_ioService, asio::ip::udp::v4()){}
	virtual ~UdpServer(){}
	virtual int send(aulddays::abuf<char> &req);
private:
	void onReqSent(asio::ip::udp::endpoint *remote, const asio::error_code& error, size_t size);
	void onResponse(asio::ip::udp::endpoint *remote, const asio::error_code& error, size_t size);
	asio::ip::udp::endpoint m_remote;
	asio::ip::udp::socket m_socket;
	aulddays::abuf<char> m_res;
};

