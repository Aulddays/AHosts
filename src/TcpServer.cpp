// TcpServer: A class that manages communication with tcp upstream servers

#include "stdafx.h"
#include "AHosts.hpp"
#include "protocol.hpp"
#include "TcpServer.hpp"

#if defined(_DEBUG) && defined(_MSC_VER)
#	ifndef DBG_NEW
#		define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
#		define new DBG_NEW
#	endif
#endif  // _DEBUG

// TcpServer
int TcpServer::send(const aulddays::abuf<char> &req)
{
	m_start = std::chrono::steady_clock::now();
	// copy the request, and fill the message length field
	m_req.resize(req.size() + 2);
	*(uint16_t *)(char *)m_req = htons((uint16_t)req.size());
	memcpy(m_req.buf() + 2, req.buf(), req.size());
	// assign a new id to request
	std::uniform_int_distribution<> randid(1024, uint16_t(-1) - 1);
	m_id = randid(AHostsRand);
	*(uint16_t *)(m_req.buf() + 2) = htons(m_id);
	// connect to server
	PELOG_LOG((PLV_DEBUG, "To connect to server %s:%d. id(%d) size(" PL_SIZET ")\n",
		m_remote.address().to_string().c_str(), (int)m_remote.port(), m_id, req.size()));
	m_status = SERVER_SENDING;
	m_socket.async_connect(m_remote,
		std::bind(&TcpServer::onCncted, this, std::placeholders::_1 /*error*/));
	return 0;
}

void TcpServer::onCncted(const asio::error_code& error)
{
	if (error)
	{
		if (!m_cancel)
			PELOG_LOG((PLV_ERROR, "Tcp connect to server failed. %s\n", error.message().c_str()));
		m_status = SERVER_GOTANSWER;
		m_res.resize(0);
		m_job->serverComplete(this, m_res);
		return;
	}
	// send the request
	PELOG_LOG((PLV_DEBUG, "Tcp to send request %s:%d. id(%d) size(" PL_SIZET ")\n",
		m_remote.address().to_string().c_str(), (int)m_remote.port(), m_id, m_req.size()));
	asio::async_write(m_socket, asio::buffer(m_req, m_req.size()),
		std::bind(&TcpServer::onReqSent, this, std::placeholders::_1 /*error*/, std::placeholders::_2 /*bytes_transferred*/));
}

void TcpServer::onReqSent(const asio::error_code& error, size_t size)
{
	if (error)
	{
		if (!m_cancel)
			PELOG_LOG((PLV_ERROR, "Tcp send request to server failed. %s\n", error.message().c_str()));
		m_status = SERVER_GOTANSWER;
		m_res.resize(0);
		m_socket.close();
		m_job->serverComplete(this, m_res);
		return;
	}
	m_res.resize(512);
	PELOG_LOG((PLV_DEBUG, "Tcp request sent (" PL_SIZET "), waiting server response\n", size));
	m_status = SERVER_WAITING;
	asio::async_read(m_socket,
		asio::buffer(m_res, m_res.size()),
		asio::transfer_at_least(2),	// first 2 bytes store message length
		std::bind(&TcpServer::onResponse, this,
		std::placeholders::_1 /*error*/, std::placeholders::_2 /*bytes_transferred*/, 0));
}

void TcpServer::onResponse(const asio::error_code& error, size_t size, size_t oldsize)
{
	if (error)
	{
		if (!m_cancel)
			PELOG_LOG((PLV_ERROR, "Recv response from server failed. %d:%s\n", error.value(), error.message().c_str()));
		m_res.resize(0);
		m_status = SERVER_GOTANSWER;
		m_socket.close();
		m_job->serverComplete(this, m_res);
		return;
	}
	if (size > 0 && size + oldsize < 2)	// response length not yet known
	{
		asio::async_read(m_socket,
			asio::buffer(m_res + size + oldsize, m_res.size() - size - oldsize),
			asio::transfer_at_least(1),
			std::bind(&TcpServer::onResponse, this,
			std::placeholders::_1 /*error*/, std::placeholders::_2 /*bytes_transferred*/, size + oldsize));
		return;
	}
	uint16_t reslen = ntohs(*(uint16_t *)(char *)m_res);
	if (size == 0 && oldsize < reslen + 2u)
	{
		if (!m_cancel)
			PELOG_LOG((PLV_ERROR, "Tcp received incomplete response. %d:%d\n", (int)oldsize, (int)reslen));
		m_res.resize(0);
		m_status = SERVER_GOTANSWER;
		m_socket.close();
		m_job->serverComplete(this, m_res);
		return;
	}
	m_res.resize(reslen + 2);
	if (size + oldsize < reslen + 2u)	// not complete, read on
	{
		asio::async_read(m_socket,
			asio::buffer(m_res + size + oldsize, m_res.size() - size - oldsize),
			asio::transfer_at_least(m_res.size() - size - oldsize),
			std::bind(&TcpServer::onResponse, this,
			std::placeholders::_1 /*error*/, std::placeholders::_2 /*bytes_transferred*/, size + oldsize));
		return;
	}
	// Have got complete response
	m_socket.close();
	memmove(m_res, m_res + 2, reslen);	// delete the length field at the begening
	m_res.resize(m_res.size() - 2);
	if (m_id != ntohs(*(const uint16_t *)(const char *)m_res))
	{
		PELOG_LOG((PLV_ERROR, "Invalid ID from server. expect %d got %d size(" PL_SIZET ")\n",
			(int)m_id, (int)ntohs(*(const uint16_t *)(const char *)m_res), m_res.size()));
		m_res.resize(0);
	}
	else
	{
		PELOG_LOG((PLV_DEBUG, "Response from server got id(%d), size(" PL_SIZET ")\n", (int)m_id, size));
	}
	m_status = SERVER_GOTANSWER;
	m_job->serverComplete(this, m_res);
}

int TcpServer::cancel()
{
	PELOG_LOG((PLV_VERBOSE, "Tcp server cenceling.\n"));
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
int TcpServer::heartBeat(const std::chrono::steady_clock::time_point &now)
{
	int ret = 0;
	if (m_status >= SERVER_SENDING && m_status <= SERVER_WAITING)
	{
		int64_t passed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_start).count();
		if (passed > m_timeout || passed < (0 - (signed int)m_timeout))
		{
			PELOG_LOG((PLV_INFO, "Tcp server timed-out %d:%d.\n", int(passed), (int)m_timeout));
			cancel();
			ret = 1;
		}
	}
	return ret;
}
