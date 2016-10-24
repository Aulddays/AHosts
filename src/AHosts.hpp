#pragma once

#include <boost/array.hpp>
#include "asio.hpp"
#include "auto_buf.hpp"

class SAHosts
{
public:
	SAHosts();
	~SAHosts();
	int start();
private:
	void onRequest(const asio::error_code& error, size_t size);
	asio::io_service m_ioService;
	asio::ip::udp::socket m_uSocket;
	asio::ip::udp::endpoint m_uRemote;
	aulddays::abuf<char> m_ucRecvBuf;
};

