#pragma once
// Manages configuration

#include <map>
#include <vector>
#include "protocol.hpp"
#include "asio.hpp"

struct AHostsConf
{
public:
	AHostsConf():m_loaded(false){}
	~AHostsConf(){}
	int load(const char *conffile);

	bool m_loaded;

	uint16_t m_port;
	std::vector<asio::ip::udp::endpoint> m_servers;
	// hosts settings, host_name => { type => {[values]} }
	std::map<std::string, std::map<int, std::vector<std::string> > > m_hosts;
};

