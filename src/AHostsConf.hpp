#pragma once
// Manages configuration

#include <map>
#include <vector>
//#include "protocol.hpp"
#include "asio.hpp"

struct AHostsConf
{
public:
	AHostsConf():m_loaded(false){}
	~AHostsConf(){}
	int load(const char *conffile);

	bool m_loaded;

	uint16_t m_port;
	std::vector<asio::ip::udp::endpoint> m_uservers;
	std::vector<asio::ip::tcp::endpoint> m_tservers;
	std::vector<asio::ip::udp::endpoint> m_userversback;
	std::vector<asio::ip::tcp::endpoint> m_tserversback;
	size_t m_cacheSize;
	unsigned int m_timeout;
	unsigned int m_backTimeout;
	unsigned int m_earlyTimeout;
	std::string m_hostsFilename;
	// hosts settings, host_name => { type => {[values]} }
	std::map<abuf<char>, std::map<uint16_t, std::vector<abuf<char> > > > m_hosts;
};

