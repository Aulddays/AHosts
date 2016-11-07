#include "stdafx.h"
#include "protocol.hpp"
#include <boost/assign.hpp>
#include <map>

// get qname at str, return length. if error occurred, return -1
int getName(const char *str, size_t max)
{
	size_t ret = 1;
	while (*str > 0 && *str < 64 && ret < max)
	{
		ret += *str + 1;
		str += *str + 1;
	}
	if (ret > max || (*str && (ret + 1 > max || (*(const unsigned char *)str >> 6) != 3)))
		return -1;
	if (*str)	// must be a pointer
		return (int)ret + 1;
	return (int)ret;
}

// convert qname into dot-delimetered printable host name
void name2pname(const char *str, size_t len, aulddays::abuf<char> &out)
{
	// convert len to '.'
	size_t pos = 0;
	out.resize(len + 8);	// +8: to print a pointer (*32767)
	memcpy(out, str + 1, len - 1);
	while (*str > 0 && *str < 64)	// label length must < 64, so signed char is sufficient
	{
		pos += *str + 1;
		out[pos - 1] = '.';
		str += *str + 1;
	}
	if (pos == 0)	// first char is a pointer or 0, then we didn't have an extra leading len byte
		pos = 1;
	if ((*(const unsigned char *)str >> 6) == 3)
	{
		int pointer = ntohs(*(const uint16_t *)str) & 0x3fff;
		_snprintf_s(out + pos - 1, 8, _TRUNCATE, "(*%d)", pointer);
	}
	else
		out[pos - 1] = 0;
	// convert to lower case
	for (char *i = out; *i; ++i)
		*i = tolower(*i);
}

// convert dot-delimetered printable host name into qname. return non-zero on error
int pname2name(const char *str, aulddays::abuf<char> &out)
{
	size_t len = strlen(str);
	out.resize(len + 2);	// 1 for first label length and 1 for last 0 label length
	const char *pin = str;
	char *pout = out;
	while (pin - str < (int)len)
	{
		const char *pend = strchr(pin, '.');
		if (!pend)
			pend = str + len;
		if (pend - pin > 63 || pend == pin)	// max length for one label is 63
			return -1;
		*pout++ = pend - pin;	// length
		while (pin < pend)
			*pout++ = tolower(*pin++);
		++pin;
	}
	*pout = 0;
	return 0;
}

const char *type2name(uint16_t type)
{
	static const std::map<uint16_t, std::string> typemap = boost::assign::map_list_of
		(RT_A, "A") (RT_NS, "NS") (RT_CNAME, "CNAME") (RT_SOA, "SOA")
		(RT_WKS, "WKS") (RT_PTR, "PTR") (RT_MX, "MX") (RT_AAAA, "AAAA")
		(RT_SRV, "SRV") (RT_ANY, "ANY") (RT_OPT, "OPT_PSEUDO");
	auto itype = typemap.find(type);
	if (itype != typemap.end())
		return itype->second.c_str();
	PELOG_LOG((PLV_VERBOSE, "Unknown type %d\n", (int)type));
	return "UKN";
}

//   0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
// +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
// |                      ID                       |
// |QR|   Opcode  |AA|TC|RD|RA|   Z    |   RCODE   |
// |                    QDCOUNT                    |
// |                    ANCOUNT                    |
// |                    NSCOUNT                    |
// |                    ARCOUNT                    |
// +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

static int dump_rdata(const char *begin, const char *pos, uint16_t rtype, int len);
void dump_packet(const aulddays::abuf<char> &pkt)
{
	const unsigned char *pos = (const unsigned char *)pkt.buf();
	fprintf(stderr, "  ID: %d\n", (int)ntohs(*(const uint16_t *)pos));
	fprintf(stderr, "  QR:%d OP:%d AA:%d TC:%d RD:%d RA:%d RCODE:%d\n",
		(int)(pos[2] >> 7), (int)((pos[2] >> 3) & 0xf), (int)((pos[2] >> 2) & 1),
		(int)((pos[2] >> 1) & 1), (int)(pos[2] & 1),
		(int)(pos[3] >> 7), (int)(pos[3] & 0xf));
	int qdc = (int)ntohs(*(const uint16_t *)(pos + 4));
	int anc = (int)ntohs(*(const uint16_t *)(pos + 6));
	int nsc = (int)ntohs(*(const uint16_t *)(pos + 8));
	int arc = (int)ntohs(*(const uint16_t *)(pos + 10));
	fprintf(stderr, "  QUERY: %d, ANSWER: %d, AUTHORITY: %d, ADDITIONAL: %d\n",
		qdc, anc, nsc, arc);
	pos += 12;
	if (qdc > 0)
	{
		fprintf(stderr, "  ;; QUERY SECTION:\n");
		for (int i = 0; i < qdc; ++i)
		{
			int nameptr = (const char *)pos - pkt;
			int namel = getName((const char *)pos, pkt.size() - nameptr);
			if (namel < 0)
				PELOG_ERROR_RETURNVOID((PLV_ERROR, "Invalid name.\n"));
			abuf<char> namebuf;
			name2pname((const char *)pos, namel, namebuf);
			pos += namel;
			uint16_t qtype = ntohs(*(const uint16_t *)pos);
			pos += 2;
			uint16_t qclass = ntohs(*(const uint16_t *)pos);
			pos += 2;
			fprintf(stderr, "  (%d)%s\t%s\t%s\n", nameptr, namebuf.buf(),
				class2name(qclass), type2name(qtype));
		}
	}
	int *secnums[] = {&anc, &nsc, &arc};
	static const char *secnames[] = {"ANSWER", "AUTHORITY", "ADDITIONAL"};
	for (size_t isec = 0; isec < sizeof(secnums) / sizeof(secnums[0]); ++isec)
	{
		if (*secnums[isec] > 0)
		{
			fprintf(stderr, "  ;; %s SECTION:\n", secnames[isec]);
			for (int ient = 0; ient < *secnums[isec]; ++ient)
			{
				int nameptr = (const char *)pos - pkt;
				int namel = getName((const char *)pos, pkt.size() - nameptr);
				if (namel < 0)
					PELOG_ERROR_RETURNVOID((PLV_ERROR, "Invalid name.\n"));
				abuf<char> namebuf;
				name2pname((const char *)pos, namel, namebuf);
				pos += namel;
				if ((const char *)pos - pkt + 10 > (int)pkt.size())
					PELOG_ERROR_RETURNVOID((PLV_ERROR, "Incomplete rdata\n"));
				uint16_t rtype = ntohs(*(const uint16_t *)pos);
				pos += 2;
				uint16_t rclass = ntohs(*(const uint16_t *)pos);
				pos += 2;
				const unsigned char *pttl = pos;	// for opt extended RCODE and flags
				uint32_t ttl = ntohl(*(const uint32_t *)pos);
				pos += 4;
				uint16_t rlen = ntohs(*(const uint16_t *)pos);
				pos += 2;
				if ((const char *)pos - pkt + rlen > (int)pkt.size())
					PELOG_ERROR_RETURNVOID((PLV_ERROR, "Incomplete rdata\n"));
				if (rtype == RT_OPT)
				{
					fprintf(stderr, "  %s\tudp:%d, ERCODE:%d, VERSION:%d, DO:%d\t", type2name(rtype), (int)rclass,
						(int)*pttl, (int)pttl[1], (int)(pttl[2] >> 7));
				}
				else
					fprintf(stderr, "  (%d)%s\t%d\t%s\t%s\t", nameptr, namebuf.buf(), ttl,
						class2name(rclass), type2name(rtype));
				int res = dump_rdata(pkt, (const char *)pos, rtype, rlen);
				fprintf(stderr, "\n");
				if (res)
					PELOG_ERROR_RETURNVOID((PLV_ERROR, "Invalid rdata\n"));
				pos += rlen;
			}	// for each record
		}
	}	// for each section
}

// RDATA format information used for parsing/compressing/decompressing/dumping
enum RdataFields	// basic fields
{
	RDATA_NAME,
	RDATA_UINT32,
	RDATA_UINT16,
	RDATA_IP,
	RDATA_IPV6,
	RDATA_DUMPHEX,
	RDATA_UNKNOWN,
};
// RDATA field list for each type
static const std::map<uint16_t, std::vector<RdataFields> > rdfmt = boost::assign::map_list_of
	(RT_CNAME, boost::assign::list_of(RDATA_NAME))
	(RT_NS, boost::assign::list_of(RDATA_NAME))
	(RT_PTR, boost::assign::list_of(RDATA_NAME))
	(RT_A, boost::assign::list_of(RDATA_IP))
	(RT_SOA, boost::assign::list_of(RDATA_NAME)(RDATA_NAME)(RDATA_UINT32)(RDATA_UINT32)(RDATA_UINT32)(RDATA_UINT32)(RDATA_UINT32))
	(RT_OPT, boost::assign::list_of(RDATA_DUMPHEX))
	(RT_MX, boost::assign::list_of(RDATA_UINT16)(RDATA_NAME));
// For all non-common types, just treat them as raw bytes.
// Actually only types with RDATA_NAME fields must be taken special care of (since they may be compressed)
// Those without a RDATA_NAME that are in rdfmt are only for prettier dump
static const std::vector<RdataFields> hexdump = boost::assign::list_of(RDATA_UNKNOWN);

static int dump_rdata(const char *begin, const char *str, uint16_t rtype, int len)
{
	auto itype = rdfmt.find(rtype);
	const std::vector<RdataFields> &fmt = itype != rdfmt.end() ? itype->second : hexdump;

	const char *pos = str;
	for (auto fld = fmt.begin(); fld != fmt.end(); ++fld)
	{
		switch (*fld)
		{
		case RDATA_NAME:
		{
			int nameptr = pos - begin;
			int namel = getName((const char *)pos, len - (pos - str));
			if (namel < 0)
				PELOG_ERROR_RETURN((PLV_ERROR, "Invalid name.\n"), -1);
			abuf<char> namebuf;
			name2pname((const char *)pos, namel, namebuf);
			fprintf(stderr, "(%d)%s ", nameptr, namebuf.buf());
			pos += namel;
			break;
		}
		case RDATA_IP:
		{
			if (len - (pos - str) < 4)
				PELOG_ERROR_RETURN((PLV_ERROR, "Invalid IP.\n"), -1);
			const unsigned char *pip = (const unsigned char *)pos;
			fprintf(stderr, "%d.%d.%d.%d ", (int)pip[0], (int)pip[1], (int)pip[2], (int)pip[3]);
			pos += 4;
			break;
		}
		case RDATA_UINT16:
		{
			if (len - (pos - str) < 2)
				PELOG_ERROR_RETURN((PLV_ERROR, "Invalid uint16.\n"), -1);
			fprintf(stderr, "%d ", ntohs(*(const uint16_t *)pos));
			pos += 2;
			break;
		}
		case RDATA_UINT32:
		{
			if (len - (pos - str) < 4)
				PELOG_ERROR_RETURN((PLV_ERROR, "Invalid uint32.\n"), -1);
			fprintf(stderr, "%u ", ntohl(*(const uint32_t *)pos));
			pos += 4;
			break;
		}
		default:
		{
			for (int i = 0; i < len - (pos - str); ++i)
				fprintf(stderr, "\\%02X", (int)(unsigned char)pos[i]);
			pos += len - (pos - str);
			if (*fld != RDATA_DUMPHEX)
				PELOG_ERROR_RETURN((PLV_ERROR, "Not supported.\n"), -1);
			break;
		}
		}
	}
	if (pos - str != len)
		PELOG_ERROR_RETURN((PLV_ERROR, "Unknown bytes in rdata.\n"), -1);
	return 0;
}