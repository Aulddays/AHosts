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
	if (m_uSocket.bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), 8453), ec))
		PELOG_ERROR_RETURN((PLV_ERROR, "UDP bind failed: %s\n", ec.message().c_str()), ec.value());
	int res = 0;
	if (res = listenUdp())
		PELOG_ERROR_RETURN((PLV_ERROR, "AHosts start failed.\n"), res);

	return m_ioService.run();
}

int AHosts::listenUdp()
{
	asio::ip::udp::endpoint *remote = new asio::ip::udp::endpoint;
	aulddays::abuf<char> *buf = new aulddays::abuf<char>(4096);
	m_uSocket.async_receive_from(asio::buffer(*buf, buf->size()), *remote,
		boost::bind(&AHosts::onUdpRequest, this, remote, buf, asio::placeholders::error, asio::placeholders::bytes_transferred));
	return 0;
}

void AHosts::onUdpRequest(asio::ip::udp::endpoint *remote, aulddays::abuf<char> *req, const asio::error_code& error, size_t size)
{
	if (error)
		PELOG_ERROR_RETURNVOID((PLV_ERROR, "Receive request failed. %s\n", error.message()));
	PELOG_LOG((PLV_VERBOSE, "Got request from %s:%d size " PL_SIZET "\n",
		remote->address().to_string().c_str(), (int)remote->port(), size));
	req->resize(size);
	m_jobs.insert(new AHostsJob(this, m_ioService, &m_uSocket, remote, req));
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

// AHostsJob

AHostsJob::AHostsJob(AHosts *ahosts, asio::io_service &ioService,
	asio::ip::udp::socket *socket, asio::ip::udp::endpoint *remote, aulddays::abuf<char> *req)
	: m_ahosts(ahosts), m_ioService(ioService), m_status(0)
{
	m_server = new UdpServer(this, m_ioService);
	m_client = new UdpClient(req, socket, remote, m_server, this, m_ioService);
	m_server->setClient(m_client);
}

int AHostsJob::clientComplete(DnsClient *client)
{
	if (client != m_client)
		PELOG_ERROR_RETURN((PLV_ERROR, "Invalid client ptr\n"), 1);
	m_status |= 1;
	PELOG_LOG((PLV_VERBOSE, "Client complete\n"));
	if ((m_status & (1 | (1 << 1))) == (1 | (1 << 1)))	// client and server both completed
		m_ahosts->jobComplete(this);
	return 0;
}

int AHostsJob::serverComplete(DnsServer *server)
{
	if (server != m_server)
		PELOG_ERROR_RETURN((PLV_ERROR, "Invalid server ptr\n"), 1);
	PELOG_LOG((PLV_VERBOSE, "Server complete\n"));
	m_status |= (1 << 1);
	if ((m_status & (1 | (1 << 1))) == (1 | (1 << 1)))	// client and server both completed
		m_ahosts->jobComplete(this);
	return 0;
}

// UdpServer
int UdpServer::send(aulddays::abuf<char> &req)
{
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
		PELOG_ERROR_RETURN((PLV_ERROR, "Failed to bind when sending recursive request\n"), 1);
	asio::ip::udp::endpoint *remote =
		new asio::ip::udp::endpoint(asio::ip::address::from_string("208.67.222.222"), 53);
	PELOG_LOG((PLV_DEBUG, "To send to server %s:%d on %d. size " PL_SIZET "\n",
		remote->address().to_string().c_str(), (int)remote->port(), (int)m_socket.local_endpoint().port(), req.size()));
	m_socket.async_send_to(asio::buffer(req, req.size()), *remote,
		boost::bind(&UdpServer::onReqSent, this, remote, asio::placeholders::error, asio::placeholders::bytes_transferred));
	return 0;
}

void UdpServer::onReqSent(asio::ip::udp::endpoint *remote, const asio::error_code& error, size_t size)
{
	if (error)
	{
		PELOG_LOG((PLV_ERROR, "Send request to server failed. %s\n", error.message().c_str()));
		// TODO: inform job
		return;
	}
	m_res.resize(4096);
	PELOG_LOG((PLV_DEBUG, "Request sent (" PL_SIZET "), waiting server response\n", size));
	m_socket.async_receive_from(asio::buffer(m_res, m_res.size()), *remote,
		boost::bind(&UdpServer::onResponse, this, remote, asio::placeholders::error, asio::placeholders::bytes_transferred));
}

void UdpServer::onResponse(asio::ip::udp::endpoint *remote, const asio::error_code& error, size_t size)
{
	if (error)
	{
		PELOG_LOG((PLV_ERROR, "Recv response from server failed. %s\n", error.message().c_str()));
		m_res.resize(0);
	}
	else
	{
		PELOG_LOG((PLV_DEBUG, "Response from server got (" PL_SIZET ")\n", size));
		m_res.resize(size);
	}
	delete remote;
	m_client->response(m_res);
	m_job->serverComplete(this);
}

// UdpClient
UdpClient::UdpClient(aulddays::abuf<char> *req, asio::ip::udp::socket *socket, asio::ip::udp::endpoint *remote,
	DnsServer *server, AHostsJob *job, asio::io_service &ioService)
	: DnsClient(job, server, ioService), m_req(req), m_socket(socket), m_remote(remote)
{
	m_id = *(uint16_t *)(char *)*req;
	int res = m_server->send(*req);
}

int UdpClient::response(aulddays::abuf<char> &res)
{
	PELOG_LOG((PLV_DEBUG, "To send to client %s:%d on %d. size " PL_SIZET "\n",
		m_remote->address().to_string().c_str(), (int)m_remote->port(), (int)m_socket->local_endpoint().port(), res.size()));
	m_socket->async_send_to(asio::buffer(res, res.size()), *m_remote,
		boost::bind(&UdpClient::onResponsed, this, asio::placeholders::error, asio::placeholders::bytes_transferred));
	return 0;
}

void UdpClient::onResponsed(const asio::error_code& error, size_t size)
{
	if (error)
		PELOG_LOG((PLV_ERROR, "Send response to client failed. %s\n", error.message().c_str()));
	else
		PELOG_LOG((PLV_DEBUG, "Response sent to client (" PL_SIZET ")\n", size));
	m_job->clientComplete(this);
}