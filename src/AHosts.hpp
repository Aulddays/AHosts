#pragma once

#include <boost/array.hpp>
#include <boost/random.hpp>
#include <set>
#include "asio.hpp"
#include "auto_buf.hpp"

extern boost::mt19937 AHostsRand;

class AHostsJob;
class DnsClient
{
public:
	DnsClient(AHostsJob *job, asio::io_service &ioService)
		: m_job(job), m_ioService(ioService), m_id(-1), m_status(CLIENT_BEGIN) {}
	virtual ~DnsClient(){}
	virtual int response(aulddays::abuf<char> &res) = 0;
	virtual int cancel() = 0;	// no response to client
protected:
	AHostsJob *m_job;
	asio::io_service &m_ioService;
	uint16_t m_id;
	enum
	{
		CLIENT_BEGIN,	// receiving request, only for tcp client
		CLIENT_WAITING,		// waiting server to respond
		CLIENT_RESPONDING,	// got server response, sending to client
		CLIENT_RESPONDED,	// response sent. client complete
	} m_status;
};

class DnsServer
{
public:
	DnsServer(AHostsJob *job, asio::io_service &ioService)
		: m_job(job), m_ioService(ioService), m_status(SERVER_BEGIN){}
	virtual ~DnsServer(){}
	//void setClient(DnsClient *client){ m_client = client; }
	virtual int send(aulddays::abuf<char> &req) = 0;
protected:
	AHostsJob *m_job;
	//DnsClient *m_client;
	asio::io_service &m_ioService;
	enum
	{
		SERVER_BEGIN,	// waiting for request
		SERVER_SENDING,	// sending request to recursive server
		SERVER_WAITING,		// request sent, receiving response
		SERVER_GOTANSWER,	// response got and sent to client, server complete
	} m_status;
};

class AHosts;

class AHostsJob
{
public:
	AHostsJob(AHosts *ahosts, asio::io_service &ioService,
		const asio::ip::udp::socket &socket, const asio::ip::udp::endpoint &remote, const aulddays::abuf<char> &req);
	~AHostsJob()
	{
		delete m_client;
		assert(m_server.size() == 0);
		for (auto i = m_finished.begin(); i != m_finished.end(); ++i)
			delete *i;
		m_finished.clear();
	}
	int clientComplete(DnsClient *client);
	int serverComplete(DnsServer *server, aulddays::abuf<char> &response);
	int request(const aulddays::abuf<char> &req);	// called by client to send request
private:
	AHosts *m_ahosts;
	asio::io_service &m_ioService;
	DnsClient *m_client;
	std::set<DnsServer *> m_server;	// running servers
	std::vector<DnsServer *> m_finished;	// completed servers move from m_server to here
	aulddays::abuf<char> m_request;
	enum
	{
		JOB_BEGIN,	// receiving request from client, only for tcp client
		JOB_REQUESTING,	// requesting recursive servers
		JOB_GOTANSWER,	// got answer from some server
		JOB_RESPONDED	// sent response to client
	} m_status;	// client and server complete status
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
		AHostsJob *job, asio::io_service &ioService);
	virtual ~UdpClient(){ };
	virtual int response(aulddays::abuf<char> &res);
	void onResponsed(const asio::error_code& error, size_t size);
	virtual int cancel();	// no response to client
private:
	//aulddays::abuf<char> m_req;
	asio::ip::udp::socket m_socket;
	asio::ip::udp::endpoint m_remote;
};

class UdpServer : public DnsServer
{
public:
	UdpServer(AHostsJob *job, asio::io_service &ioService)
		: DnsServer(job, ioService), m_socket(m_ioService, asio::ip::udp::v4()),
		m_remote(asio::ip::address::from_string("208.67.222.222"), 53){}
	virtual ~UdpServer(){}
	virtual int send(aulddays::abuf<char> &req);
private:
	void onReqSent(const asio::error_code& error, size_t size);
	void onResponse(const asio::error_code& error, size_t size);
	asio::ip::udp::socket m_socket;
	asio::ip::udp::endpoint m_remote;
	aulddays::abuf<char> m_res;
};
