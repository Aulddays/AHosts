#include "stdafx.h"
#include "AHostsConf.hpp"
#include "AHostsHandler.h"
#include "libconfig/libconfig.h"

static void strsplit(std::string str, char deli, std::vector<std::string> &parts)
{
	parts.clear();
	size_t pos = 0;
	size_t pend = 0;
	while ((pend = str.find(deli, pos)) != str.npos)
	{
		parts.resize(parts.size() + 1);
		parts.back().assign(str, pos, pend - pos);
		pos = pend + 1;
	}
	parts.resize(parts.size() + 1);
	parts.back().assign(str, pos, str.npos);
}

template<class endpoint>
int parseServers(const config_t *config, const char *name, std::vector<endpoint> &servers);

int AHostsConf::load(const char *conffile)
{
	if (m_loaded)
		PELOG_LOG_RETURN((PLV_WARNING, "Conf already loaded.\n"), 0);

	config_t config;
	config_init(&config);
	if (CONFIG_FALSE == config_read_file(&config, conffile))
	{
		PELOG_ERROR_RETURN((PLV_ERROR, "Error loading config file (line %d): %s\n",
			config_error_line(&config), config_error_text(&config)), 1);
	}

	// Logging
	pelog_setfile_rotate(config_get_int(&config, "Log.logrotate_filesize_kb", -1),
		config_get_int(&config, "Log.logrotate_history_num", -1),
		config_get_string(&config, "Log.logfile", ""),
		config_get_bool(&config, "Log.loglinebuf", false));
	pelog_setlevel(config_get_string(&config, "Log.loglevel", "TRC"));

	// listen port
	m_port = config_get_int(&config, "Port", 53);
	PELOG_LOG((PLV_INFO, "Port: %d\n", (int)m_port));

	// servers
	parseServers(&config, "ServersUdp", m_uservers);
	parseServers(&config, "ServersTcp", m_tservers);
	if (m_uservers.empty() && m_tservers.empty())
		PELOG_ERROR_RETURN((PLV_ERROR, "No valid servers found\n"), -1);

	m_cacheSize = config_get_int(&config, "CacheSize", 5000);
	PELOG_LOG((PLV_INFO, "CacheSize: " PL_SIZET "\n", m_cacheSize));

	m_timeout = config_get_int(&config, "Timeout", 20000);
	PELOG_LOG((PLV_INFO, "Timeout: %u\n", m_timeout));

	m_earlyTimeout = config_get_int(&config, "EarlyTimeout", 1500);
	PELOG_LOG((PLV_INFO, "EarlyTimeout: %u\n", m_earlyTimeout));

	m_hostsFilename = config_get_string(&config, "HostsExtFile", "");
	PELOG_LOG((PLV_INFO, "HostsExtFile: %s\n", m_hostsFilename.c_str()));

	if (m_hostsFilename != "" &&
		AHostsHandler::loadHostsExt(m_hostsFilename.c_str(), this))
	{
		PELOG_ERROR_RETURN((PLV_ERROR, "Failed to load %s\n", m_hostsFilename.c_str()), -1);
	}

	m_loaded = true;
	return 0;
}

template<class endpoint>
int parseServers(const config_t *config, const char *name, std::vector<endpoint> &servers)
{
	std::string serversconf = config_get_string(config, name, "");
	servers.clear();
	std::vector<std::string> sservers;
	strsplit(serversconf, ';', sservers);
	for (auto i = sservers.begin(); i != sservers.end(); ++i)
	{
		if (i->length() == 0)
			continue;
		std::vector<std::string> ipport;
		strsplit(*i, ':', ipport);
		unsigned short port = 53;
		if (ipport.size() > 1 && ipport[1].length() > 0)
		{
			int iport = atoi(ipport[1].c_str());
			if (iport <= 0 || iport >= USHRT_MAX)
				PELOG_ERROR_RETURN((PLV_ERROR, "Invalid server address %s\n", i->c_str()), -1);
			port = (unsigned short)iport;
		}
		asio::error_code ec;
		asio::ip::address ip = asio::ip::address::from_string(ipport[0], ec);
		if (ec)
			PELOG_ERROR_RETURN((PLV_ERROR, "Invalid server address %s\n", i->c_str()), -1);
		servers.emplace_back(ip, port);
		PELOG_LOG((PLV_INFO, "Add %s server %s:%d\n", name, ipport[0].c_str(), (int)port));
	}
	return 0;
}