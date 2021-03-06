#pragma once

#include <random>
#include <set>
#include "asio.hpp"
#include "auto_buf.hpp"
#include "AHostsConf.hpp"
#include "AHostsCache.hpp"
#include "AHostsHandler.h"

extern std::mt19937 AHostsRand;

class AHostsJob;
class DnsClient
{
public:
	DnsClient(AHostsJob *job, asio::io_service &ioService, unsigned int timeout)
		: m_job(job), m_ioService(ioService), m_id(-1), m_timeout(timeout), m_status(CLIENT_BEGIN) {}
	virtual ~DnsClient(){}
	virtual int response(aulddays::abuf<char> &res) = 0;
	virtual int cancel() = 0;	// no response to client
	// return: 0 ok, >0 timedout, <0 error
	virtual int heartBeat(const std::chrono::steady_clock::time_point &now) = 0;
	bool responded() const { return m_status == CLIENT_RESPONDED; }
protected:
	AHostsJob *m_job;
	asio::io_service &m_ioService;
	uint16_t m_id;
	unsigned int m_timeout;
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
	DnsServer(AHostsJob *job, asio::io_service &ioService, int timeout)
		: m_job(job), m_ioService(ioService), m_id(-1), m_timeout(timeout), m_status(SERVER_BEGIN){}
	virtual ~DnsServer(){}
	//void setClient(DnsClient *client){ m_client = client; }
	virtual int send(const aulddays::abuf<char> &req) = 0;
	virtual int cancel() = 0;
	// return: 0 ok, >0 timedout, <0 error
	virtual int heartBeat(const std::chrono::steady_clock::time_point &now) = 0;
protected:
	AHostsJob *m_job;
	//DnsClient *m_client;
	asio::io_service &m_ioService;
	uint16_t m_id;
	int m_timeout;	// timeout threshold, in milli-seconds
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
	AHostsJob(AHosts *ahosts, asio::io_service &ioService);
	int runudp(const asio::ip::udp::socket &socket, const asio::ip::udp::endpoint &remote, const abuf<char> &req);
	~AHostsJob()
	{
		delete m_client;
		assert(m_server.size() == 0);
		for (auto i = m_finished.begin(); i != m_finished.end(); ++i)
			delete *i;
		m_finished.clear();
	}
	// status: 0 ok. >0 not replied. <0 error
	int clientComplete(DnsClient *client, int status = 0);
	int serverComplete(DnsServer *server, aulddays::abuf<char> &response);
	int request(const aulddays::abuf<char> &req);	// called by client to send request
	int heartBeat(const std::chrono::steady_clock::time_point &now);
	void finished();	// dump log
private:
	AHosts *m_ahosts;
	asio::io_service &m_ioService;
	DnsClient *m_client;
	std::set<DnsServer *> m_server;	// running servers
	std::vector<DnsServer *> m_finished;	// completed servers move from m_server to here
	bool m_backserversent;	// whether the request has been sent to back-servers 
	int sendBackservers();
	aulddays::abuf<char> m_request;
	AHostsHandler m_handler;
	enum
	{
		JOB_BEGIN,	// receiving request from client, only for tcp client
		JOB_REQUESTING,	// requesting recursive servers
		JOB_EARLYRET,	// returned the expired cache data
		JOB_NOEARLYRET,	// no (even expired) cache found
		JOB_GOTANSWER,	// got answer from some server
		JOB_RESPONDED	// sent response to client
	} m_status;	// client and server complete status
	//bool m_early;	// whether early return
	enum
	{
		JOB_OK,
		JOB_REQERROR,
		JOB_TIMEOUT,
		JOB_RESPERROR,
		JOB_STNUM
	} m_jobst;	// job finish status
	abuf<char> m_reqNameType;	// name and type in the request. should contain '\0'!
	abuf<char> m_reqPrint;	// Printable name:type of the request
	int m_questionNum;	// number of question items in request
	abuf<char> m_cached;
	std::chrono::steady_clock::time_point m_tstart, m_tearly, m_tanswer, m_treply, m_tfin;
};

class AHosts
{
public:
	AHosts() : m_conf(m_conf_underlay),
		m_uSocket(m_ioService, asio::ip::udp::v4()), m_hbTimer(m_ioService){}
	~AHosts(){}
	int init(const char *conffile)
	{ 
		if (int ret = m_conf_underlay.load(conffile))
			return ret;
		m_cache.setCapacity(m_conf.m_cacheSize);
		return 0;
	}
	int start();
	int stop(){ m_ioService.stop(); return 0; }
	int jobComplete(AHostsJob *job);
	const AHostsConf &getConf() const { return m_conf; }

	// cache
	AHostsCache m_cache;
private:
	int listenUdp();
	void onUdpRequest(const asio::error_code& error, size_t size);

	void onHeartbeat(const asio::error_code& error);

	AHostsConf m_conf_underlay;
	const AHostsConf &m_conf;	// A read-only reference of conf

	asio::io_service m_ioService;
	std::set<AHostsJob *> m_jobs;

	// udp stuff
	asio::ip::udp::socket m_uSocket;
	asio::ip::udp::endpoint m_uRemote;
	aulddays::abuf<char> m_ucRecvBuf;

	// timer to handle timeouts
	asio::steady_timer m_hbTimer;	// heartbeat timer
	static const int HBTIMEMS = 100;	// trigger heartbeat every 0.1 sec
};

class UdpClient : public DnsClient
{
public:
	UdpClient(AHostsJob *job, asio::io_service &ioService, unsigned int timeout);
	virtual int run(const abuf<char> &req, const asio::ip::udp::socket &socket, const asio::ip::udp::endpoint &remote);
	virtual ~UdpClient(){ };
	virtual int response(aulddays::abuf<char> &res);
	void onResponsed(const asio::error_code& error, size_t size);
	virtual int cancel();	// no response to client
	virtual int heartBeat(const std::chrono::steady_clock::time_point &now);
private:
	asio::ip::udp::socket m_socket;
	asio::ip::udp::endpoint m_remote;
	std::chrono::steady_clock::time_point m_start;
	bool m_cancel;
};

