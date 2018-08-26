// TcpServer: A class that manages communication with tcp upstream servers

#pragma once
#include "AHosts.hpp"

class TcpServer : public DnsServer
{
public:
	TcpServer(AHostsJob *job, asio::io_service &ioService,
		const asio::ip::tcp::endpoint &remote, unsigned int timeout)
		: DnsServer(job, ioService, timeout), m_socket(m_ioService),
		m_remote(remote), m_start(std::chrono::steady_clock::time_point::min()), m_cancel(false){}
	virtual ~TcpServer(){}
	virtual int send(const aulddays::abuf<char> &req);
	virtual int cancel();
	virtual int heartBeat(const std::chrono::steady_clock::time_point &now);
private:
	void onCncted(const asio::error_code& error);
	void onReqSent(const asio::error_code& error, size_t size);
	void onResponse(const asio::error_code& error, size_t size, size_t oldsize);
	asio::ip::tcp::socket m_socket;
	asio::ip::tcp::endpoint m_remote;
	aulddays::abuf<char> m_res;
	aulddays::abuf<char> m_req;
	std::chrono::steady_clock::time_point m_start;
	bool m_cancel;
};
