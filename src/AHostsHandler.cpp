#include "stdafx.h"
#include "AHostsHandler.h"
#include "protocol.hpp"

#ifdef _MSC_VER
#	define strtok_r strtok_s
#endif

int AHostsHandler::processRequest(aulddays::abuf<char> &req)
{
	m_appendNum = 0;
	m_oriNameType.resize(0);
	m_append.resize(0);

	abuf<char> dreq;
	codecMessage(false, req, dreq);

	// parse the header
	if (dreq.size() < 12)
		PELOG_ERROR_RETURN((PLV_ERROR, "Request too small " PL_SIZET "\n", dreq.size()), -1);
	if (((unsigned char)dreq[2]) & 1)
		PELOG_ERROR_RETURN((PLV_ERROR, "QR is bit is 1 (answer) in request\n"), -1);
	if (((((unsigned char)dreq[2]) >> 1) & 0xf) != 0)
		PELOG_ERROR_RETURN((PLV_VERBOSE, "OPCODE is not 0 (query) in request. pass\n"), 0);
	int qdcount = ntohs(*(uint16_t *)(dreq + 4));
	if (qdcount == 0)
		PELOG_ERROR_RETURN((PLV_TRACE, "No question in query. pass\n"), 0);
	if (qdcount > 1)
		PELOG_ERROR_RETURN((PLV_TRACE, "Multiple(%d) questions in query not supported. pass\n",
			qdcount), 0);
	// now we should have and only 1 question record
	// question format: qname, qtype, qclass
	int qnamelen = getName(dreq + 12, dreq.size() - 12);
	if (qnamelen <= 0 || dreq.size() < (12u + qnamelen + 2 + 2))	// head + qname + qtype + qclass
		PELOG_ERROR_RETURN((PLV_WARNING, "Invalid name in question.\n"), 0);
	aulddays::abuf<char> pqname;
	name2pname(dreq + 12, qnamelen, pqname);
	uint16_t qtype = htons(*(uint16_t *)(dreq + 12 + qnamelen));
	uint16_t qclass = htons(*(uint16_t *)(dreq + 12 + qnamelen + 2));
	PELOG_LOG((PLV_DEBUG, "request name %s type %d class %d", pqname.buf(), (int)qtype, (int)qclass));
	if ((qtype != RT_A && qtype != RT_AAAA && qtype != RT_CNAME) || qclass != RC_IN)	// not interesting question
		return 0;

	// search for local settings
	auto ihost = m_conf->m_hosts.find(pqname.buf());
	if (ihost == m_conf->m_hosts.end())	// host name not in conf
		return 0;
	const std::map<int, std::vector<abuf<char> > > &hconf = ihost->second;
	std::map<int, std::vector<abuf<char> > >::const_iterator itype;
	if ((qtype == RT_A || qtype == RT_AAAA) &&
		(itype = hconf.find(RT_CNAME)) != hconf.end())	// want A or AAAA and have cname
	{
		// oriName, to recover the question, which we will modify
		if (m_oriNameType.isNull())
			m_oriNameType.scopyFrom((dreq + 12), qnamelen + 2);
		// m_append, will be added to answers
		m_appendNum++;
		size_t appsize = qnamelen + 2 + 2 + 4 + 2 + itype->second[0].size() + 2;
		if (m_append.capacity() < m_append.size() + appsize)
			m_append.reserve(std::max((size_t)256, m_append.size() + m_append.size() / 2));
		m_append.resize(m_append.size() + appsize);
		char *pos = m_append + (m_append.size() - appsize);	// must resize() before get pos
		memcpy(pos, dreq + 12, qnamelen); pos += qnamelen;	// name
		*(uint16_t *)pos = htons(RT_CNAME); pos += 2;		// type
		*(uint16_t *)pos = htons(RC_IN); pos += 2;		// class
		*(uint32_t *)pos = htonl(3600); pos += 4;		// TTL
		abuf<char> newqname;
		pname2name(itype->second[0].buf(), newqname);	// TODO: should have checked return value on loading conf
		*(uint16_t *)pos = htons(newqname.size()); pos += 2;		// rdata len
		memcpy(pos, newqname.buf(), newqname.size()); pos += newqname.size();
		m_append.resize(pos - m_append);
		// modify question
		//memmem();
	}
	return 0;
}

int AHostsHandler::loadHostsExt(const char *filename, AHostsConf *conf)
{
	FILE *fp = fopen(filename, "r");
	if (!fp)
		PELOG_ERROR_RETURN((PLV_ERROR, "Unable to open hosts ext file %s.\n", filename), -1);
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
			const char *keystr = NULL;
			pos = strtok_r(line, " \t\r\n", &contex);
			do
			{
				if (!pos)
					break;
				if (!*pos)
					continue;
				keystr = pos;
				break;
			} while (pos = strtok_r(NULL, " \t\r\n", &contex));
			if (!keystr)
				continue;
			abuf<char> key;
			pname2name(keystr, key);
			// read values
			while (pos = strtok_r(NULL, " \t\r\n", &contex))
			{
				if (!*pos)
					continue;
				asio::error_code ec;
				asio::ip::address ip = asio::ip::address::from_string(pos, ec);
				if (ec)	// not a valid ip address, should be a cname
				{
					abuf<char> vname;
					pname2name(pos, vname);
					conf->m_hosts[key][RT_CNAME].push_back(vname);
					PELOG_LOG((PLV_INFO, "Adding hosts ext setting %s: %s(%d)[%s]\n",
						keystr, "CNAME", RT_CNAME, pos));
				}
				else if (ip.is_v4())
				{
					abuf<char> vip;
					vip.scopyFrom((const char *)ip.to_v4().to_bytes().data(), 4);
					conf->m_hosts[key][RT_A].push_back(vip);
					PELOG_LOG((PLV_INFO, "Adding hosts ext setting %s: %s(%d)[%s]\n",
						keystr, "A", RT_A, pos));
				}
				else
				{
					abuf<char> vip;
					vip.scopyFrom((const char *)ip.to_v6().to_bytes().data(), 16);
					conf->m_hosts[key][RT_AAAA].push_back(vip);
					PELOG_LOG((PLV_INFO, "Adding hosts ext setting %s: %s(%d)[%s]\n",
						keystr, "AAAA", RT_AAAA, pos));
				}
			}
			// check validity
			//bool invalid = false;
			if (conf->m_hosts[key].find(RT_CNAME) != conf->m_hosts[key].end())
			{
				if (conf->m_hosts[key][RT_CNAME].size() > 1)
					PELOG_ERROR_RETURN((PLV_ERROR,
						"Invalid hosts settings for %s. Can only have one CNAME, got %d.\n",
						keystr, (int)conf->m_hosts[key][RT_CNAME].size()), 1);
				else if (conf->m_hosts[key].find(RT_A) != conf->m_hosts[key].end())
					PELOG_ERROR_RETURN((PLV_ERROR,
						"Invalid hosts settings for %s. IP is not allowed with CNAME.\n", keystr), 1);
				else if (conf->m_hosts[key].find(RT_AAAA) != conf->m_hosts[key].end())
					PELOG_ERROR_RETURN((PLV_ERROR,
					"Invalid hosts settings for %s. IPv6 is not allowed with CNAME.\n", keystr), 1);
			}
		}
		fclose(fp);
	}
	return 0;
}