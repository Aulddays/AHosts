#include "stdafx.h"
#include "AHostsConf.hpp"

int AHostsConf::load()
{
	if (m_loaded)
		PELOG_LOG_RETURN((PLV_WARNING, "Conf already loaded.\n"), 0);

	m_loaded = true;
	FILE *fp = fopen("hosts.ext", "r");
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
			pos = strtok_s(line, " \t\r\n", &contex);
			do
			{
				if (!pos)
					break;
				if (!*pos)
					continue;
				key = pos;
				break;
			} while (pos = strtok_s(NULL, " \t\r\n", &contex));
			if (key == "")
				continue;
			// read values
			while (pos = strtok_s(NULL, " \t\r\n", &contex))
			{
				if (!*pos)
					continue;
				asio::error_code ec;
				asio::ip::address ip = asio::ip::address::from_string(pos, ec);
				if (ec)	// not a valid ip address, should be a cname
					m_hosts[key][QT_CNAME].push_back(pos);
				else if (ip.is_v4())
					m_hosts[key][QT_A].push_back(pos);
				else
					m_hosts[key][QT_AAAA].push_back(pos);
			}
			// check validity
			bool invalid = false;
			if (m_hosts[key].find(QT_CNAME) != m_hosts[key].end())
			{
				if (m_hosts[key][QT_CNAME].size() > 1)
					PELOG_ERROR_RETURN((PLV_ERROR,
						"Invalid hosts settings for %s. Can only have one CNAME, got %s and %s.\n",
						key.c_str(), m_hosts[key][QT_CNAME][0].c_str(), m_hosts[key][QT_CNAME][1].c_str()), 1);
				else if (m_hosts[key].find(QT_A) != m_hosts[key].end())
					PELOG_ERROR_RETURN((PLV_ERROR,
						"Invalid hosts settings for %s. IP %s is not allowed with CNAME %s.\n",
						key.c_str(), m_hosts[key][QT_A][0].c_str(), m_hosts[key][QT_CNAME][0].c_str()), 1);
				else if (m_hosts[key].find(QT_AAAA) != m_hosts[key].end())
					PELOG_ERROR_RETURN((PLV_ERROR,
						"Invalid hosts settings for %s. IP %s is not allowed with CNAME %s.\n",
						key.c_str(), m_hosts[key][QT_AAAA][0].c_str(), m_hosts[key][QT_CNAME][0].c_str()), 1);
			}
			// print status
			for (auto i = m_hosts[key].begin(); i != m_hosts[key].end(); ++i)
			{
				for (auto j = i->second.begin(); j != i->second.end(); ++j)
					PELOG_LOG((PLV_INFO, "hosts setting %s: %d[%s]\n",
						key.c_str(), (int)i->first, j->c_str()));
			}
		}
	}
	return 0;
}