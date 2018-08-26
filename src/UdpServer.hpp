// TcpServer: A class that manages communication with udp upstream servers

#pragma once
#include "AHosts.hpp"

class UdpServer : public DnsServer
{
public:
	UdpServer(AHostsJob *job, asio::io_service &ioService,
		const asio::ip::udp::endpoint &remote, unsigned int timeout)
		: DnsServer(job, ioService, timeout), m_socket(m_ioService, asio::ip::udp::v4()),
		m_remote(remote), m_start(std::chrono::steady_clock::time_point::min()), m_cancel(false){}
	virtual ~UdpServer(){}
	virtual int send(aulddays::abuf<char> &req);
	virtual int cancel();
	virtual int heartBeat(const std::chrono::steady_clock::time_point &now);
private:
	void onReqSent(const asio::error_code& error, size_t size);
	void onResponse(const asio::error_code& error, size_t size);
	asio::ip::udp::socket m_socket;
	asio::ip::udp::endpoint m_remote;
	aulddays::abuf<char> m_res;
	aulddays::abuf<char> m_req;
	std::chrono::steady_clock::time_point m_start;
	bool m_cancel;
};
