// AHosts.cpp : Defines the AHosts class
//

#include "stdafx.h"
#include <boost/bind.hpp>
#include "AHosts.hpp"

#if defined(_DEBUG) && defined(_MSC_VER)
#	ifndef DBG_NEW
#		define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
#		define new DBG_NEW
#	endif
#endif  // _DEBUG

boost::mt19937 AHostsRand((uint32_t)time(NULL) + _getpid());

// AHosts
//AHosts::AHosts()
//{
//}
//
//
//AHosts::~AHosts()
//{
//}

int AHosts::start()
{
	// start receive
	asio::error_code ec;
	asio::socket_base::reuse_address option(true);
	m_uSocket.set_option(option);
	if (m_uSocket.bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), 8453), ec))
		PELOG_ERROR_RETURN((PLV_ERROR, "UDP bind failed: %s\n", ec.message().c_str()), ec.value());
	int res = 0;
	if (res = listenUdp())
		PELOG_ERROR_RETURN((PLV_ERROR, "AHosts start failed.\n"), res);
	m_hbTimer.expires_from_now(boost::posix_time::milliseconds(HBTIMEMS));
	m_hbTimer.async_wait(boost::bind(&AHosts::onHeartbeat, this, asio::placeholders::error));

	return m_ioService.run();
}

int AHosts::listenUdp()
{
	m_uRemote = asio::ip::udp::endpoint();	// clear remote
	m_ucRecvBuf.resize(4096);
	m_uSocket.async_receive_from(asio::buffer(m_ucRecvBuf, m_ucRecvBuf.size()), m_uRemote,
		boost::bind(&AHosts::onUdpRequest, this, asio::placeholders::error, asio::placeholders::bytes_transferred));
	return 0;
}

void AHosts::onUdpRequest(const asio::error_code& error, size_t size)
{
	if (error)
		PELOG_ERROR_RETURNVOID((PLV_ERROR, "Receive request failed. %s\n", error.message()));
	PELOG_LOG((PLV_VERBOSE, "Got request from %s:%d size " PL_SIZET "\n",
		m_uRemote.address().to_string().c_str(), (int)m_uRemote.port(), size));
	m_ucRecvBuf.resize(size);
	m_jobs.insert(new AHostsJob(this, m_ioService, m_uSocket, m_uRemote, m_ucRecvBuf));
	if (listenUdp())
		PELOG_ERROR_RETURNVOID((PLV_ERROR, "UDP listening failed.\n"));
}

int AHosts::jobComplete(AHostsJob *job)
{
	if (m_jobs.find(job) == m_jobs.end())
		PELOG_ERROR_RETURN((PLV_ERROR, "Invalid job ptr.\n"), 1);
	PELOG_LOG((PLV_TRACE, "Job complete\n"));
	m_jobs.erase(job);
	delete job;
	return 0;
}

void AHosts::onHeartbeat(const asio::error_code& error)
{
	//PELOG_LOG((PLV_DEBUG, "Heart Beat\n"));
	if (!error)
	{
		boost::posix_time::ptime now = boost::posix_time::microsec_clock::local_time();
		for (auto i = m_jobs.begin(); i != m_jobs.end(); ++i)
			(*i)->heartBeat(now);
		m_hbTimer.expires_from_now(boost::posix_time::milliseconds(HBTIMEMS));
		m_hbTimer.async_wait(boost::bind(&AHosts::onHeartbeat, this, asio::placeholders::error));
	}
	else if (error)
	{
		PELOG_LOG((PLV_ERROR, "Heartbeat invalid state %s\n", error.message().c_str()));
	}
}


// AHostsJob

AHostsJob::AHostsJob(AHosts *ahosts, asio::io_service &ioService,
	const asio::ip::udp::socket &socket, const asio::ip::udp::endpoint &remote, const aulddays::abuf<char> &req)
	: m_ahosts(ahosts), m_ioService(ioService), m_status(JOB_BEGIN)
{
	//m_server = new UdpServer(this, m_ioService);
	m_client = new UdpClient(req, socket, remote, this, m_ioService);
	//m_server->setClient(m_client);
}

int AHostsJob::request(const aulddays::abuf<char> &req)
{
	m_request.reserve(std::max(req.size(), (size_t)512));
	m_request.scopyFrom(req);
	m_status = JOB_REQUESTING;
	const static asio::ip::udp::endpoint serverconf[] = {
		asio::ip::udp::endpoint(asio::ip::address::from_string("208.67.222.222"), 53),
		asio::ip::udp::endpoint(asio::ip::address::from_string("208.67.220.220"), 53),
	};
	for (size_t i = 0; i < sizeof(serverconf) / sizeof(serverconf[0]); ++i)
	{
		UdpServer *server = new UdpServer(this, m_ioService, serverconf[i], 20000);
		m_server.insert(server);
		server->send(m_request);
	}
	return 0;
}

int AHostsJob::clientComplete(DnsClient *client)
{
	if (client != m_client)
		PELOG_ERROR_RETURN((PLV_ERROR, "Invalid client ptr\n"), 1);
	if (m_status != JOB_GOTANSWER)
		PELOG_ERROR_RETURN((PLV_ERROR, "Invalid job status %d\n", m_status), 1);
	m_status = JOB_RESPONDED;
	PELOG_LOG((PLV_VERBOSE, "Client complete\n"));
	if (m_server.size() == 0)	// client and server both completed
		m_ahosts->jobComplete(this);
	// if m_server.size() == 0, do not have to cancel servers from here 
	return 0;
}

int AHostsJob::serverComplete(DnsServer *server, aulddays::abuf<char> &response)
{
	// remove server
	auto iserver = m_server.find(server);
	if (iserver == m_server.end())
		PELOG_ERROR_RETURN((PLV_ERROR, "Invalid server ptr\n"), 1);
	PELOG_LOG((PLV_VERBOSE, "Server complete\n"));
	m_server.erase(iserver);
	m_finished.push_back(server);
	// deal with the response
	if (m_status < JOB_GOTANSWER)
	{
		if (response.size() > 0)
		{
			m_client->response(response);
			m_status = JOB_GOTANSWER;
			if (m_server.size() > 0)	// cancel other server since we've got answer
			{
				PELOG_LOG((PLV_VERBOSE, "Canceling all pending servers\n"));
				for (auto i = m_server.begin(); i != m_server.end(); ++i)
					(*i)->cancel();
			}
		}
		else if (m_server.size() == 0)	// all server finished and no answer
		{
			m_status = JOB_GOTANSWER;
			m_client->cancel();
		}
	}
	if (m_server.size() == 0 && m_status == JOB_RESPONDED)
	{
		m_ahosts->jobComplete(this);
	}
	return 0;
}

int AHostsJob::heartBeat(const boost::posix_time::ptime &now)
{
	int ret = 0;
	for (auto i = m_server.begin(); i != m_server.end(); ++i)
		ret |= (*i)->heartBeat(now);
	ret |= m_client->heartBeat(now);
	return ret;
}


// UdpServer
int UdpServer::send(aulddays::abuf<char> &req)
{
	m_start = boost::posix_time::microsec_clock::local_time();
	boost::uniform_int<> randport(16385, 32767);
	bool bindok = false;
	for (int i = 0; i < 10; ++i)
	{
		asio::error_code ec;
		if (!m_socket.bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), randport(AHostsRand)), ec))
		{
			bindok = true;
			break;
		}
		else
		{
			PELOG_LOG((PLV_VERBOSE, "Bind failed %d:%s\n", ec.value(), ec.message().c_str()));
		}
	}
	if (!bindok)
	{
		PELOG_LOG((PLV_ERROR, "Failed to bind when sending recursive request\n"));
		m_status = SERVER_GOTANSWER;
		m_res.resize(0);
		m_job->serverComplete(this, m_res);
		return 1;
	}
	PELOG_LOG((PLV_DEBUG, "To send to server %s:%d on %d. size " PL_SIZET "\n",
		m_remote.address().to_string().c_str(), (int)m_remote.port(), (int)m_socket.local_endpoint().port(), req.size()));
	m_status = SERVER_SENDING;
	m_socket.async_send_to(asio::buffer(req, req.size()), m_remote,
		boost::bind(&UdpServer::onReqSent, this, asio::placeholders::error, asio::placeholders::bytes_transferred));
	return 0;
}

void UdpServer::onReqSent(const asio::error_code& error, size_t size)
{
	if (error)
	{
		if (!m_cancel)
			PELOG_LOG((PLV_ERROR, "Send request to server failed. %s\n", error.message().c_str()));
		m_status = SERVER_GOTANSWER;
		m_res.resize(0);
		m_job->serverComplete(this, m_res);
		return;
	}
	m_res.resize(4096);
	PELOG_LOG((PLV_DEBUG, "Request sent (" PL_SIZET "), waiting server response\n", size));
	m_status = SERVER_WAITING;
	m_socket.async_receive_from(asio::buffer(m_res, m_res.size()), m_remote,
		boost::bind(&UdpServer::onResponse, this, asio::placeholders::error, asio::placeholders::bytes_transferred));
}

void UdpServer::onResponse(const asio::error_code& error, size_t size)
{
	if (error)
	{
		if (!m_cancel)
			PELOG_LOG((PLV_ERROR, "Recv response from server failed. %s\n", error.message().c_str()));
		m_res.resize(0);
	}
	else
	{
		PELOG_LOG((PLV_DEBUG, "Response from server got (" PL_SIZET ")\n", size));
		m_res.resize(size);
	}
	m_status = SERVER_GOTANSWER;
	m_job->serverComplete(this, m_res);
}

int UdpServer::cancel()
{
	PELOG_LOG((PLV_VERBOSE, "Server cenceling.\n"));
	m_cancel = true;
	if (m_status < SERVER_SENDING)
	{
		m_status = SERVER_GOTANSWER;
		m_res.resize(0);
		m_job->serverComplete(this, m_res);
	}
	else if (m_status == SERVER_SENDING || m_status == SERVER_WAITING)
		m_socket.close();
	return 0;
}

int UdpServer::heartBeat(const boost::posix_time::ptime &now)
{
	if (m_status >= SERVER_SENDING && m_status <= SERVER_WAITING)
	{
		int64_t passed = (now - m_start).total_milliseconds();
		if (passed > m_timeout || passed < (0 - (signed int)m_timeout))
		{
			PELOG_LOG((PLV_INFO, "Server timed-out %d:%d.\n", int(passed), (int)m_timeout));
			cancel();
		}
	}
	return 0;
}

// UdpClient
UdpClient::UdpClient(const aulddays::abuf<char> &req, const asio::ip::udp::socket &socket, const asio::ip::udp::endpoint &remote,
	AHostsJob *job, asio::io_service &ioService)
	: DnsClient(job, ioService, 5000), m_socket(ioService, asio::ip::udp::v4()), m_remote(remote),
	m_start(boost::posix_time::min_date_time), m_cancel(false)
{
	asio::error_code ec;
	asio::socket_base::reuse_address option(true);
	m_socket.set_option(option);
	m_socket.bind(asio::ip::udp::endpoint(socket.local_endpoint()), ec);
	if (ec)
		PELOG_LOG((PLV_ERROR, "Socket bind failed. %s\n", ec.message().c_str()));
	m_id = htons(*(const uint16_t *)(const char *)req);
	int res = m_job->request(req);
	m_status = CLIENT_WAITING;
}

int UdpClient::response(aulddays::abuf<char> &res)
{
	if (m_status != CLIENT_WAITING)
		PELOG_ERROR_RETURN((PLV_ERROR, "Client in invalid status %d\n", m_status), 1);
	m_start = boost::posix_time::microsec_clock::local_time();
	m_status = CLIENT_RESPONDING;
	PELOG_LOG((PLV_DEBUG, "To send to client %s:%d on %d. size " PL_SIZET "\n",
		m_remote.address().to_string().c_str(), (int)m_remote.port(), (int)m_socket.local_endpoint().port(), res.size()));
	m_socket.async_send_to(asio::buffer(res, res.size()), m_remote,
		boost::bind(&UdpClient::onResponsed, this, asio::placeholders::error, asio::placeholders::bytes_transferred));
	return 0;
}

void UdpClient::onResponsed(const asio::error_code& error, size_t size)
{
	if (error)
		if (!m_cancel)
			PELOG_LOG((PLV_ERROR, "Send response to client failed. %s\n", error.message().c_str()));
	else
		PELOG_LOG((PLV_DEBUG, "Response sent to client (" PL_SIZET ")\n", size));
	asio::error_code ec;
	m_socket.close(ec);
	if (m_status != CLIENT_RESPONDING)
		PELOG_ERROR_RETURNVOID((PLV_ERROR, "Client in invalid status %d\n", m_status));
	m_status = CLIENT_RESPONDED;
	m_job->clientComplete(this);
}

int UdpClient::cancel()
{
	PELOG_LOG((PLV_VERBOSE, "Cancel client\n"));
	asio::error_code ec;
	m_cancel = true;
	if (m_status <= CLIENT_WAITING)
	{
		// do not respond. client would natually timeout
		m_status = CLIENT_RESPONDED;
		m_socket.close(ec);
		m_job->clientComplete(this);
	}
	else if (m_status == CLIENT_RESPONDING)
	{
		m_socket.close(ec);	// cancel() may not work on some system, so just close
	}
	else
	{
		assert(m_status == CLIENT_RESPONDED);
		PELOG_LOG((PLV_ERROR, "Trying to cancel a completed client\n"));
		return 1;
	}
	return 0;
}

int UdpClient::heartBeat(const boost::posix_time::ptime &now)
{
	if (m_status == CLIENT_RESPONDING)
	{
		int64_t passed = (now - m_start).total_milliseconds();
		if (passed > m_timeout || passed < (0 - (signed int)m_timeout))
		{
			PELOG_LOG((PLV_INFO, "Client timed-out %d:%d.\n", int(passed), (int)m_timeout));
			cancel();
		}
	}
	return 0;
}

