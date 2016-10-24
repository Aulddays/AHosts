// AHosts.cpp : Defines the AHosts class
//

#include "stdafx.h"
#include <boost/bind.hpp>
#include "AHosts.hpp"

SAHosts::SAHosts()
	: m_uSocket(m_ioService, asio::ip::udp::v4()), m_ucRecvBuf(4096)
{
}


SAHosts::~SAHosts()
{
}

int SAHosts::start()
{
	// start receive
	asio::error_code ec;
	if (m_uSocket.bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), 8453), ec))
		PELOG_ERROR_RETURN((PLV_ERROR, "UDP bind failed: %s\n", ec.message().c_str()), ec.value());
	m_uSocket.async_receive_from(asio::buffer(m_ucRecvBuf, 4096), m_uRemote,
		boost::bind(&SAHosts::onRequest, this, asio::placeholders::error, asio::placeholders::bytes_transferred));

	return m_ioService.run();
}

void SAHosts::onRequest(const asio::error_code& error, size_t size)
{
	if (error)
		PELOG_ERROR_RETURNVOID((PLV_ERROR, "Receive request failed. %s\n", error.message()));
	PELOG_LOG((PLV_VERBOSE, "Got request packet size " PL_SIZET "\n", size));
}