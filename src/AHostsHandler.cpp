#include "stdafx.h"
#include "AHostsHandler.h"

// get qname at str, return length. if error occurred, return -1
static int getQName(const unsigned char *str, size_t max);
static void qname2pname(const unsigned char *str, size_t len, aulddays::abuf<char> &out);

int AHostsHandler::processRequest(aulddays::abuf<char> &req)
{
	// parse the header
	if (req.size() < 12)
		PELOG_ERROR_RETURN((PLV_ERROR, "Request too small " PL_SIZET "\n", req.size()), -1);
	if (((unsigned char)req[2]) & 1)
		PELOG_ERROR_RETURN((PLV_ERROR, "QR is bit is 1 (answer) in request\n"), -1);
	if (((((unsigned char)req[2]) >> 1) & 0xf) != 0)
		PELOG_ERROR_RETURN((PLV_VERBOSE, "OPCODE is not 0 (query) in request. pass\n"), 0);
	int qdcount = ntohs(*(uint16_t *)(req + 4));
	if (qdcount == 0)
		PELOG_ERROR_RETURN((PLV_TRACE, "No question in query. pass\n"), 0);
	if (qdcount > 1)
		PELOG_ERROR_RETURN((PLV_TRACE, "Multiple(%d) questions in query not supported. pass\n",
			qdcount), 0);
	// now we should have and only 1 question record
	// question format: qname, qtype, qclass
	int qnamelen = getQName((unsigned char *)(req + 12), req.size() - 12);
	if (qnamelen <= 0 || req.size() < (12u + qnamelen + 2 + 2))	// head + qname + qtype + qclass
		PELOG_ERROR_RETURN((PLV_WARNING, "Invalid name in question.\n"), 0);
	aulddays::abuf<char> pqname;
	qname2pname((unsigned char *)(req + 12), qnamelen, pqname);
	uint16_t qtype = htons(*(uint16_t *)(req + 12 + qnamelen));
	uint16_t qclass = htons(*(uint16_t *)(req + 12 + qnamelen + 2));
	PELOG_LOG((PLV_DEBUG, "request name %s type %d class %d", pqname.buf(), (int)qtype, (int)qclass));
	if ((qtype != QT_A && qtype != QT_AAAA && qtype != QT_CNAME) || qclass != QC_IN)	// not interesting question
		return 0;
	return 0;
}

// get qname at str, return length. if error occurred, return -1
static int getQName(const unsigned char *str, size_t max)
{
	size_t ret = 1;
	while (*str > 0 && ret < max)
	{
		ret += *str + 1;
		str += *str + 1;
	}
	if (ret > max || *str)
		return -1;
	return (int)ret;
}

static void qname2pname(const unsigned char *str, size_t len, aulddays::abuf<char> &out)
{
	// convert len to '.'
	size_t pos = 0;
	out.resize(len - 1);
	memcpy(out, str + 1, len - 1);
	while (*str > 0)
	{
		pos += *str + 1;
		out[pos - 1] = '.';
		str += *str + 1;
	}
	out[pos - 1] = 0;
	// convert to lower case
	for (char *i = out; *i; ++i)
		*i = tolower(*i);
}
