#pragma once
// Manages configuration

#include <map>
#include "protocol.hpp"

struct AHostsConf
{
public:
	AHostsConf():m_loaded(false){}
	~AHostsConf(){}
	int load();

	bool m_loaded;

	// hosts settings, host_name => { type => {[values]} }
	std::map<std::string, std::map<int, std::vector<std::string> > > m_hosts;
};

