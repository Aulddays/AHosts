#include "stdafx.h"
#include "AHostsHandler.h"
#include "protocol.hpp"

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
	const std::map<int, std::vector<std::string> > &hconf = ihost->second;
	std::map<int, std::vector<std::string> >::const_iterator itype;
	if ((qtype == RT_A || qtype == RT_AAAA) &&
		(itype = hconf.find(RT_CNAME)) != hconf.end())	// want A or AAAA and have cname
	{
		// oriName, to recover the question, which we will modify
		if (m_oriNameType.isNull())
			m_oriNameType.scopyFrom((dreq + 12), qnamelen + 2);
		// m_append, will be added to answers
		m_appendNum++;
		size_t appsize = qnamelen + 2 + 2 + 4 + 2 + itype->second[0].length() + 2;
		if (m_append.capacity() < m_append.size() + appsize)
			m_append.reserve(std::max((size_t)256, m_append.size() + m_append.size() / 2));
		m_append.resize(m_append.size() + appsize);
		char *pos = m_append + (m_append.size() - appsize);	// must resize() before get pos
		memcpy(pos, dreq + 12, qnamelen); pos += qnamelen;	// name
		*(uint16_t *)pos = htons(RT_CNAME); pos += 2;		// type
		*(uint16_t *)pos = htons(RC_IN); pos += 2;		// class
		*(uint32_t *)pos = htonl(3600); pos += 4;		// TTL
		abuf<char> newqname;
		pname2name(itype->second[0].c_str(), newqname);	// TODO: should have checked return value on loading conf
		*(uint16_t *)pos = htons(newqname.size()); pos += 2;		// rdata len
		memcpy(pos, newqname.buf(), newqname.size()); pos += newqname.size();
		m_append.resize(pos - m_append);
		// modify question
		//memmem();
	}
	return 0;
}

