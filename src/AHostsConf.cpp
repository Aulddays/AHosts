#include "stdafx.h"
#include <boost/property_tree/info_parser.hpp>
#include "AHostsConf.hpp"

#ifdef _MSC_VER
#	define strtok_r strtok_s
#endif

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

int AHostsConf::load(const char *conffile)
{
	if (m_loaded)
		PELOG_LOG_RETURN((PLV_WARNING, "Conf already loaded.\n"), 0);

	boost::property_tree::ptree config;
	try
	{
		boost::property_tree::info_parser::read_info(conffile, config);
	}
	catch (boost::property_tree::info_parser::info_parser_error &e)
	{
		PELOG_LOG((PLV_ERROR, "%s\n", e.what()));
		PELOG_ERROR_RETURN((PLV_ERROR, "Error loading config file\n"), 1);
	}

	// listen port
	m_port = config.get("Port", 53);
	PELOG_LOG((PLV_INFO, "Port: %d\n", (int)m_port));

	// servers
	{
		std::string servers = config.get("Servers", "");
		m_servers.clear();
		std::vector<std::string> sservers;
		strsplit(servers, ';', sservers);
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
			try
			{
				m_servers.push_back(asio::ip::udp::endpoint(asio::ip::address::from_string(ipport[0]), port));
			}
			catch (...)
			{
				PELOG_ERROR_RETURN((PLV_ERROR, "Invalid server address %s\n", i->c_str()), -1);
			}
			PELOG_LOG((PLV_INFO, "Add server %s:%d\n", ipport[0].c_str(), (int)port));
		}
		if (m_servers.empty())
			PELOG_ERROR_RETURN((PLV_ERROR, "No valid servers found %s\n"), -1);
	}

	m_cacheSize = config.get("CacheSize", (size_t)5000);
	PELOG_LOG((PLV_INFO, "CacheSize: " PL_SIZET "\n", m_cacheSize));

	m_timeout = config.get("Timeout", 20000u);
	PELOG_LOG((PLV_INFO, "Timeout: %u\n", m_timeout));

	m_earlyTimeout = config.get("EarlyTimeout", 1500u);
	PELOG_LOG((PLV_INFO, "EarlyTimeout: %u\n", m_earlyTimeout));

	m_hostsFilename = config.get("HostsExtFile", "");
	PELOG_LOG((PLV_INFO, "HostsExtFile: %s\n", m_hostsFilename.c_str()));

	if (m_hostsFilename != "")
	{
		FILE *fp = fopen(m_hostsFilename.c_str(), "r");
		if (!fp)
			PELOG_LOG((PLV_WARNING, "Unable to open config file hosts.ext.\n"));
		else
		{
			aulddays::abuf<char> line(1024);
			while (fgets(line, 1024, fp))
			{
				char *contex = NULL;
				// remove coments
				char *pos = strchr(line, '#');
				if (pos)
					*pos = 0;
				// find key
				std::string key = "";
				pos = strtok_r(line, " \t\r\n", &contex);
				do
				{
					if (!pos)
						break;
					if (!*pos)
						continue;
					key = pos;
					break;
				} while (pos = strtok_r(NULL, " \t\r\n", &contex));
				if (key == "")
					continue;
				// read values
				while (pos = strtok_r(NULL, " \t\r\n", &contex))
				{
					if (!*pos)
						continue;
					asio::error_code ec;
					asio::ip::address ip = asio::ip::address::from_string(pos, ec);
					if (ec)	// not a valid ip address, should be a cname
						m_hosts[key][RT_CNAME].push_back(pos);
					else if (ip.is_v4())
						m_hosts[key][RT_A].push_back(pos);
					else
						m_hosts[key][RT_AAAA].push_back(pos);
				}
				// check validity
				//bool invalid = false;
				if (m_hosts[key].find(RT_CNAME) != m_hosts[key].end())
				{
					if (m_hosts[key][RT_CNAME].size() > 1)
						PELOG_ERROR_RETURN((PLV_ERROR,
							"Invalid hosts settings for %s. Can only have one CNAME, got %s and %s.\n",
							key.c_str(), m_hosts[key][RT_CNAME][0].c_str(), m_hosts[key][RT_CNAME][1].c_str()), 1);
					else if (m_hosts[key].find(RT_A) != m_hosts[key].end())
						PELOG_ERROR_RETURN((PLV_ERROR,
							"Invalid hosts settings for %s. IP %s is not allowed with CNAME %s.\n",
							key.c_str(), m_hosts[key][RT_A][0].c_str(), m_hosts[key][RT_CNAME][0].c_str()), 1);
					else if (m_hosts[key].find(RT_AAAA) != m_hosts[key].end())
						PELOG_ERROR_RETURN((PLV_ERROR,
							"Invalid hosts settings for %s. IP %s is not allowed with CNAME %s.\n",
							key.c_str(), m_hosts[key][RT_AAAA][0].c_str(), m_hosts[key][RT_CNAME][0].c_str()), 1);
				}
				// print status
				for (auto i = m_hosts[key].begin(); i != m_hosts[key].end(); ++i)
				{
					for (auto j = i->second.begin(); j != i->second.end(); ++j)
						PELOG_LOG((PLV_INFO, "hosts setting %s: %d[%s]\n",
							key.c_str(), (int)i->first, j->c_str()));
				}
			}
			fclose(fp);
		}
	}

	m_loaded = true;
	return 0;
}
