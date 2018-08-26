// TcpServer: A class that manages communication with udp upstream servers

#include "stdafx.h"
#include "AHosts.hpp"
#include "protocol.hpp"
#include "UdpServer.hpp"

#if defined(_DEBUG) && defined(_MSC_VER)
#	ifndef DBG_NEW
#		define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
#		define new DBG_NEW
#	endif
#endif  // _DEBUG

// UdpServer
int UdpServer::send(const aulddays::abuf<char> &req)
{
	m_start = std::chrono::steady_clock::now();
	// look for an available local port
	std::uniform_int_distribution<> randport(16385, 32767);
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
	m_req.scopyFrom(req);
	// assign a new id to request
	std::uniform_int_distribution<> randid(1024, uint16_t(-1) - 1);
	m_id = randid(AHostsRand);
	*(uint16_t *)(char *)m_req = htons(m_id);
	PELOG_LOG((PLV_DEBUG, "To send to server %s:%d on %d. id(%d) size(" PL_SIZET ")\n",
		m_remote.address().to_string().c_str(), (int)m_remote.port(), (int)m_socket.local_endpoint().port(), m_id, req.size()));
	m_status = SERVER_SENDING;
	m_socket.async_send_to(asio::buffer(m_req, m_req.size()), m_remote,
		std::bind(&UdpServer::onReqSent, this, std::placeholders::_1 /*error*/, std::placeholders::_2 /*bytes_transferred*/));
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
	m_res.resize(0);
	PELOG_LOG((PLV_DEBUG, "Request sent (" PL_SIZET "), waiting server response\n", size));
	m_status = SERVER_WAITING;
	// since we do not know the response size yet, we just peek with empty buffer at first to get the size
	m_socket.async_receive_from(asio::buffer(m_res, 0), m_remote, MSG_PEEK,
		std::bind(&UdpServer::onResponse, this, std::placeholders::_1 /*error*/, std::placeholders::_2 /*bytes_transferred*/));
}

void UdpServer::onResponse(const asio::error_code& error, size_t size)
{
	// if peek result
	if (!error && !m_cancel && m_res.size() == 0
#ifdef _MSC_VER
		|| error && !m_cancel && m_res.size() == 0 && error.value() == ERROR_MORE_DATA
#endif
		)
	{
		m_res.resize(m_socket.available());
		PELOG_LOG((PLV_DEBUG, "Response size from server " PL_SIZET "\n", m_res.size()));
		m_socket.async_receive_from(asio::buffer(m_res, m_res.size()), m_remote,
			std::bind(&UdpServer::onResponse, this, std::placeholders::_1 /*error*/, std::placeholders::_2 /*bytes_transferred*/));
		return;
	}
	if (error)
	{
		if (!m_cancel)
		{
			if (m_res.size() == 0)	// The peek was performed
				PELOG_LOG((PLV_ERROR, "Recv response from server failed. %d:%s\n", error.value(), error.message().c_str()));
		}
		m_res.resize(0);
	}
	else if (m_id != ntohs(*(const uint16_t *)(const char *)m_res))
	{
		PELOG_LOG((PLV_ERROR, "Invalid ID from server. expect %d got %d size(" PL_SIZET ")\n",
			(int)m_id, (int)ntohs(*(const uint16_t *)(const char *)m_res), size));
		m_res.resize(0);
	}
	else
	{
		PELOG_LOG((PLV_DEBUG, "Response from server got id(%d), size(" PL_SIZET ")\n", (int)m_id, size));
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

// return: 0 ok, >0 timedout, <0 error
int UdpServer::heartBeat(const std::chrono::steady_clock::time_point &now)
{
	int ret = 0;
	if (m_status >= SERVER_SENDING && m_status <= SERVER_WAITING)
	{
		int64_t passed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_start).count();
		if (passed > m_timeout || passed < (0 - (signed int)m_timeout))
		{
			PELOG_LOG((PLV_INFO, "Server timed-out %d:%d.\n", int(passed), (int)m_timeout));
			cancel();
			ret = 1;
		}
	}
	return ret;
}
