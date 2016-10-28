#pragma once
// Manages configuration

#include <map>

struct AHostsConf
{
public:
	AHostsConf(){}
	~AHostsConf(){}
	int Load();

	// hosts settings, host_name => { type => {[values]} }
	std::map<std::string, std::map<int, std::vector<std::string> > > m_hosts;
};

