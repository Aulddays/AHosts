#include "stdafx.h"
#include <map>
#include "protocol.hpp"

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
		snprintf(out + pos - 1, 8, "(*%d)", pointer);
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
	out.resize(len + (len ? 2 : 1));	// 1 for first label length and 1 for last 0 label length
	const char *pin = str;
	char *pout = out;
	while (pin - str < (int)len)
	{
		const char *pend = strchr(pin, '.');
		if (pend == pin)	// str == "." or sr == "xxx..yy", treat as end
			break;
		if (!pend)
			pend = str + len;
		if (pend - pin > 63)	// max length for one label is 63
			return -1;
		*pout++ = pend - pin;	// length
		while (pin < pend)
			*pout++ = tolower(*pin++);
		if (*pin)
			++pin;
	}
	*pout++ = 0;
	out.resize(pout - out);
	return 0;
}

const char *type2name(uint16_t type)
{
	static const std::map<uint16_t, std::string> typemap = {
		{ RT_A, "A" }, { RT_NS, "NS" }, { RT_MD, "MD" }, { RT_MF, "MF" }, { RT_CNAME, "CNAME" },
		{ RT_SOA, "SOA" }, { RT_MB, "MB" }, { RT_MG, "MR" }, { RT_NULL, "NULL" },
		{ RT_WKS, "WKS" }, { RT_PTR, "PTR" }, { RT_HINFO, "HINFO" }, { RT_MINFO, "MINFO" },
		{ RT_MX, "MX" }, { RT_TXT, "TXT" }, { RT_RP, "RP" }, { RT_AFSDB, "AFSDB" },
		{ RT_X25, "X25" }, { RT_ISDN, "ISDN" }, { RT_RT, "RT" },
		{ RT_AAAA, "AAAA" }, { RT_SRV, "SRV" }, { RT_ANY, "ANY" }, { RT_OPT, "OPT_PSEUDO" }, };
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

static int dumpRdata(int level, const char *begin, const char *pos, uint16_t rtype, int len, bool ptr);
void dumpMessage(int level, const aulddays::abuf<char> &pkt, bool dumpnameptr)
{
	const unsigned char *pos = (const unsigned char *)pkt.buf();
	PELOG_RAWLOG((level, "  ID: %d\n", (int)ntohs(*(const uint16_t *)pos)));
	PELOG_RAWLOG((level, "  QR:%d OP:%d AA:%d TC:%d RD:%d RA:%d RCODE:%d\n",
		(int)(pos[2] >> 7), (int)((pos[2] >> 3) & 0xf), (int)((pos[2] >> 2) & 1),
		(int)((pos[2] >> 1) & 1), (int)(pos[2] & 1),
		(int)(pos[3] >> 7), (int)(pos[3] & 0xf)));
	int qdc = (int)ntohs(*(const uint16_t *)(pos + 4));
	int anc = (int)ntohs(*(const uint16_t *)(pos + 6));
	int nsc = (int)ntohs(*(const uint16_t *)(pos + 8));
	int arc = (int)ntohs(*(const uint16_t *)(pos + 10));
	PELOG_RAWLOG((level, "  QUERY: %d, ANSWER: %d, AUTHORITY: %d, ADDITIONAL: %d\n",
		qdc, anc, nsc, arc));
	pos += 12;
	if (qdc > 0)
	{
		PELOG_RAWLOG((level, "  ;; QUESTION SECTION:\n"));
		for (int i = 0; i < qdc; ++i)
		{
			int nameptr = (const char *)pos - pkt;
			int namel = getName((const char *)pos, pkt.size() - nameptr);
			if (namel < 0)
				PELOG_ERROR_RETURNVOID((PLV_ERROR, "Invalid name.\n"));
			abuf<char> namebuf;
			name2pname((const char *)pos, namel, namebuf);
			pos += namel;
			if ((const char *)pos - pkt + 4 > (int)pkt.size())
				PELOG_ERROR_RETURNVOID((PLV_ERROR, "Incomplete rdata\n"));
			uint16_t qtype = ntohs(*(const uint16_t *)pos);
			pos += 2;
			uint16_t qclass = ntohs(*(const uint16_t *)pos);
			pos += 2;
			if (dumpnameptr)
				PELOG_RAWLOG((level, "  (%d)%s\t%s\t%s\n", nameptr, namebuf.buf(),
					class2name(qclass), type2name(qtype)));
			else
				PELOG_RAWLOG((level, "  %s\t%s\t%s\n", namebuf.buf(), class2name(qclass), type2name(qtype)));
		}
	}
	int *secnums[] = {&anc, &nsc, &arc};
	static const char *secnames[] = {"ANSWER", "AUTHORITY", "ADDITIONAL"};
	for (size_t isec = 0; isec < sizeof(secnums) / sizeof(secnums[0]); ++isec)
	{
		if (*secnums[isec] > 0)
		{
			PELOG_RAWLOG((level, "  ;; %s SECTION:\n", secnames[isec]));
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
					PELOG_RAWLOG((level, "  %s\tudp:%d, ERCODE:%d, VERSION:%d, DO:%d\t", type2name(rtype), (int)rclass,
						(int)*pttl, (int)pttl[1], (int)(pttl[2] >> 7)));
				}
				else if (dumpnameptr)
					PELOG_RAWLOG((level, "  (%d)%s\t%d\t%s\t%s\t", nameptr, namebuf.buf(), ttl,
						class2name(rclass), type2name(rtype)));
				else
					PELOG_RAWLOG((level, "  %s\t%d\t%s\t%s\t", namebuf.buf(), ttl,
						class2name(rclass), type2name(rtype)));
				int res = dumpRdata(level, pkt, (const char *)pos, rtype, rlen, dumpnameptr);
				PELOG_RAWLOG((level, "\n"));
				if (res)
					PELOG_ERROR_RETURNVOID((PLV_ERROR, "Invalid rdata\n"));
				pos += rlen;
			}	// for each record
		}
	}	// for each section
	if ((char *)pos - pkt.buf() < (int)pkt.size())
		PELOG_LOG((PLV_WARNING, "Garbage data beyond normal message\n"));
}

static int decompressName(const abuf<char> &in, const unsigned char *&pin,
	abuf<char> &out, unsigned char *&pout);
static int compressName(const abuf<char> &in, const unsigned char *&pin,
	abuf<char> &out, unsigned char *&pout, std::map<std::string, uint16_t> &names);
static int codecRdata(bool compress, const abuf<char> &in, const unsigned char *&pin,
	abuf<char> &out, unsigned char *&pout, uint16_t rtype, int len,
	std::map<std::string, uint16_t> &names);
int codecMessage(bool compress, const aulddays::abuf<char> &in, aulddays::abuf<char> &out)
{
	out.resize(in.size() + 512);
	const unsigned char *pin = (const unsigned char *)in.buf();
	const unsigned char * const pib = (const unsigned char *)in.buf();
	unsigned char *pout = (unsigned char *)out.buf();
	// Must not define a pob here since `out.buf()` may change
	std::map<std::string, uint16_t> names;	// known names for compression

	// head
	int qdc = (int)ntohs(*(const uint16_t *)(pin + 4));
	int anc = (int)ntohs(*(const uint16_t *)(pin + 6));
	int nsc = (int)ntohs(*(const uint16_t *)(pin + 8));
	int arc = (int)ntohs(*(const uint16_t *)(pin + 10));
	memcpy(pout, pin, 12);
	pin += 12;
	pout += 12;

	// query section
	for (int i = 0; i < qdc; ++i)
	{
		if (int res = (compress ? compressName(in, pin, out, pout, names) : decompressName(in, pin, out, pout)))
			PELOG_ERROR_RETURN((PLV_ERROR, "%scompressName failed.\n", compress ? "" : "de"), res);
		if (pin + 4 - pib > (int)in.size() || pout + 4 - (unsigned char *)out.buf() > (int)out.size())
			PELOG_ERROR_RETURN((PLV_ERROR, "Invalid question record.\n"), -1);
		memcpy(pout, pin, 4);
		pin += 4;
		pout += 4;
	}

	int *secnums[] = { &anc, &nsc, &arc };
	static const char *secnames[] = { "ANSWER", "AUTHORITY", "ADDITIONAL" };
	for (size_t isec = 0; isec < sizeof(secnums) / sizeof(secnums[0]); ++isec)
	{
		if (*secnums[isec] > 0)
		{
			for (int ient = 0; ient < *secnums[isec]; ++ient)
			{
				if (int res = compress ? compressName(in, pin, out, pout, names) : decompressName(in, pin, out, pout))
					PELOG_ERROR_RETURN((PLV_ERROR, "%scompressName failed %s %d.\n",
						compress ? "" : "de", secnames[isec], isec), res);
				if (pin + 10 - pib > (int)in.size() || pout + 10 - (unsigned char *)out.buf() > (int)out.size())
					PELOG_ERROR_RETURN((PLV_ERROR, "Incomplete %s record %d.\n", secnames[isec], isec), -1);
				uint16_t rtype = ntohs(*(const uint16_t *)pin);
				uint16_t rlen = ntohs(*(const uint16_t *)(pin + 8));
				int lenpos = (char *)pout + 8 - out.buf();	// pos to write back new rlen.
				memcpy(pout, pin, 10);
				pin += 10;
				pout += 10;
				if (pin + rlen - pib > (int)in.size() || pout + rlen - (unsigned char *)out.buf() > (int)out.size())
					PELOG_ERROR_RETURN((PLV_ERROR, "Incomplete %s rdata %d.\n", secnames[isec], isec), -1);
				int rdatapos = (char *)pout - out.buf();	// pos of rdata
				if (int res = codecRdata(compress, in, pin, out, pout, rtype, rlen, names))
					PELOG_ERROR_RETURN((PLV_ERROR, "Invalid %s rdata %d.\n", secnames[isec], isec), res);
				int rdataend = (char *)pout - out.buf();	// end of rdata
				// Write back new rdata. poses must not be pointer since in.buf() may change
				*(uint16_t *)(out.buf() + lenpos) = htons((uint16_t)(rdataend - rdatapos));
			}	// for each record
		}
	}	// for each section
	out.resize((char *)pout - out.buf());
	return 0;
}

// RDATA format information used for parsing/compressing/decompressing/dumping
enum RdataFields	// basic fields
{
	// fields with fixed sizes
	RDATA_UINT32,
	RDATA_UINT16,
	RDATA_UINT8,
	RDATA_IP,
	RDATA_IPV6,
	RDATA_SIZEMAX = RDATA_IPV6,
	// fields with variable sizes
	RDATA_NAME,
	RDATA_RNAME,		// same as RDATA_NAME but no compress (no pointer)
	RDATA_STRING,
	RDATA_TXT,
	RDATA_DUMPHEX,
	RDATA_UNKNOWN,
};
static const size_t RdataFieldSize[] = 
{
	4,	// RDATA_UINT32,
	2,	// RDATA_UINT16,
	1,	// RDATA_UINT8, 
	4,	// RDATA_IP,    
	16,	// RDATA_IPV6,  
};
static_assert(sizeof(RdataFieldSize) / sizeof(RdataFieldSize[0]) == RDATA_SIZEMAX + 1,
	"RdataFieldSize and RDATA_SIZEMAX mismatch");
// RDATA field list for each type
static const std::map<uint16_t, std::vector<RdataFields>> rdfmt = {
	{ RT_A, { RDATA_IP } },
	{ RT_NS, { RDATA_NAME } },
	{ RT_MD, { RDATA_RNAME } },
	{ RT_MF, { RDATA_RNAME } },
	{ RT_CNAME, { RDATA_NAME } },
	{ RT_SOA, { RDATA_NAME, RDATA_NAME, RDATA_UINT32, RDATA_UINT32, RDATA_UINT32, RDATA_UINT32, RDATA_UINT32 } },
	{ RT_MB, { RDATA_RNAME } },
	{ RT_MG, { RDATA_RNAME } },
	{ RT_MR, { RDATA_RNAME } },
	{ RT_NULL, { RDATA_DUMPHEX } },
	{ RT_WKS, { RDATA_IP, RDATA_UINT8, RDATA_DUMPHEX } },
	{ RT_PTR, { RDATA_NAME } },
	{ RT_HINFO, { RDATA_STRING, RDATA_STRING } },
	{ RT_MINFO, { RDATA_RNAME, RDATA_RNAME } },
	{ RT_MX, { RDATA_UINT16, RDATA_NAME } },
	{ RT_TXT, { RDATA_TXT } },
	{ RT_RP, { RDATA_RNAME, RDATA_RNAME } },
	{ RT_AFSDB, { RDATA_UINT16, RDATA_RNAME } },
	{ RT_X25, { RDATA_STRING } },
	{ RT_ISDN, { RDATA_DUMPHEX } },
	{ RT_RT, { RDATA_UINT16, RDATA_RNAME } },
	{ RT_AAAA, { RDATA_IPV6 } },
	{ RT_SRV, { RDATA_UINT16, RDATA_UINT16, RDATA_UINT16, RDATA_RNAME } },
	{ RT_OPT, { RDATA_DUMPHEX } } };
// For all non-common types, just treat them as raw bytes.
// Actually only types with RDATA_NAME fields must be taken special care of (since they may be compressed)
// Those without a RDATA_NAME that are in rdfmt are only for prettier dump
static const std::vector<RdataFields> hexdump = { RDATA_UNKNOWN };

static int dumpRdata(int level, const char *begin, const char *str, uint16_t rtype, int len, bool ptr)
{
	auto itype = rdfmt.find(rtype);
	const std::vector<RdataFields> &fmt = itype != rdfmt.end() ? itype->second : hexdump;

	const char *pos = str;
	for (auto fld = fmt.begin(); fld != fmt.end(); ++fld)
	{
		switch (*fld)
		{
		case RDATA_NAME:
		case RDATA_RNAME:
		{
			int nameptr = pos - begin;
			int namel = getName((const char *)pos, len - (pos - str));
			if (namel < 0)
				PELOG_ERROR_RETURN((PLV_ERROR, "Invalid name.\n"), -1);
			abuf<char> namebuf;
			name2pname((const char *)pos, namel, namebuf);
			if (ptr && *fld == RDATA_NAME)
				PELOG_RAWLOG((level, "(%d)%s ", nameptr, namebuf.buf()));
			else
				PELOG_RAWLOG((level, "%s ", namebuf.buf()));
			pos += namel;
			break;
		}
		case RDATA_IP:
		{
			if (len - (pos - str) < 4)
				PELOG_ERROR_RETURN((PLV_ERROR, "Invalid IP.\n"), -1);
			const unsigned char *pip = (const unsigned char *)pos;
			PELOG_RAWLOG((level, "%d.%d.%d.%d ", (int)pip[0], (int)pip[1], (int)pip[2], (int)pip[3]));
			pos += 4;
			break;
		}
		case RDATA_IPV6:
		{
			if (len - (pos - str) < 16)
				PELOG_ERROR_RETURN((PLV_ERROR, "Invalid IPv6.\n"), -1);
			asio::ip::address_v6::bytes_type ipbytes;
			memcpy(ipbytes.data(), pos, 16);
			asio::ip::address_v6 ip(ipbytes);
			PELOG_RAWLOG((level, "%s ", ip.to_string().c_str()));
			pos += 16;
			break;
		}
		case RDATA_UINT8:
		{
			if (len - (pos - str) < 1)
				PELOG_ERROR_RETURN((PLV_ERROR, "Invalid uint8.\n"), -1);
			PELOG_RAWLOG((level, "%d ", *(const uint8_t *)pos));
			++pos;
			break;
		}
		case RDATA_UINT16:
		{
			if (len - (pos - str) < 2)
				PELOG_ERROR_RETURN((PLV_ERROR, "Invalid uint16.\n"), -1);
			PELOG_RAWLOG((level, "%d ", ntohs(*(const uint16_t *)pos)));
			pos += 2;
			break;
		}
		case RDATA_UINT32:
		{
			if (len - (pos - str) < 4)
				PELOG_ERROR_RETURN((PLV_ERROR, "Invalid uint32.\n"), -1);
			PELOG_RAWLOG((level, "%u ", ntohl(*(const uint32_t *)pos)));
			pos += 4;
			break;
		}
		case RDATA_STRING:
		{
			int slen = *(const unsigned char *)pos;
			if (len - (pos - str) < slen + 1)
				PELOG_ERROR_RETURN((PLV_ERROR, "Invalid string.\n"), -1);
			PELOG_RAWLOG((level, "\""));
			PELOG_RAWWRITE(level, pos + 1, 1, slen);
			PELOG_RAWLOG((level, "\" "));
			pos += slen + 1;
			break;
		}
		case RDATA_TXT:
		{
			while (len > pos - str)
			{
				int slen = *(const unsigned char *)pos;
				if (len - (pos - str) < slen + 1)
					PELOG_ERROR_RETURN((PLV_ERROR, "Invalid string.\n"), -1);
				PELOG_RAWLOG((level, "\""));
				PELOG_RAWWRITE(level, pos + 1, 1, slen);
				PELOG_RAWLOG((level, "\" "));
				pos += slen + 1;
			}
			break;
		}
		case RDATA_DUMPHEX:
		default:
		{
			for (int i = 0; i < len - (pos - str); ++i)
				PELOG_RAWLOG((level, "\\%02X", (int)(unsigned char)pos[i]));
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

static int codecRdata(bool compress, const abuf<char> &in, const unsigned char *&pin,
	abuf<char> &out, unsigned char *&pout, uint16_t rtype, int len,
	std::map<std::string, uint16_t> &names)
{
	if ((const char *)pin - in.buf() + len > (int)in.size())
		PELOG_ERROR_RETURN((PLV_ERROR, "Rdata size error.\n"), -1);
	const unsigned char *pib = pin + len;

	auto itype = rdfmt.find(rtype);
	const std::vector<RdataFields> &fmt = itype != rdfmt.end() ? itype->second : hexdump;
	//const char *pos = str;
	for (auto fld = fmt.begin(); fld != fmt.end(); ++fld)
	{
		int inleft = pib - pin;
		int outleft = (int)out.size() - ((char *)pout - out.buf());
		if (inleft < 0 || outleft < 0 || outleft < inleft)
			PELOG_ERROR_RETURN((PLV_ERROR, "Rdata size error.\n"), -1);
		if (*fld <= RDATA_SIZEMAX)	// fixed sized raw copy
		{
			size_t csize = RdataFieldSize[*fld];
			if (inleft < (int)csize || outleft < (int)csize)
				PELOG_ERROR_RETURN((PLV_ERROR, "Rdata size error.\n"), -1);
			memcpy(pout, pin, csize);
			pout += csize;
			pin += csize;
			continue;
		}
		switch (*fld)
		{
		case RDATA_NAME:
		case RDATA_RNAME:
		{
			if (int res = (compress && *fld == RDATA_NAME) ?
					compressName(in, pin, out, pout, names) : decompressName(in, pin, out, pout))
				PELOG_ERROR_RETURN((PLV_ERROR, "%scompressName failed.\n", compress ? "" : "de"), res);
			break;
		}
		case RDATA_STRING:
		{
			if (inleft <= 0 || outleft <= 0)
				PELOG_ERROR_RETURN((PLV_ERROR, "Invalid string.\n"), -1);
			int slen = *(const unsigned char *)pin;
			if (inleft < slen + 1 || outleft < slen + 1)
				PELOG_ERROR_RETURN((PLV_ERROR, "Invalid string.\n"), -1);
			memcpy(pout, pin, slen + 1);
			pin += slen + 1;
			pout += slen + 1;
			break;
		}
		case RDATA_TXT:
		{
			while (inleft > 0)
			{
				int slen = *(const unsigned char *)pin;
				if (inleft < slen + 1 || outleft < slen + 1)
					PELOG_ERROR_RETURN((PLV_ERROR, "Invalid txt.\n"), -1);
				memcpy(pout, pin, slen + 1);
				pin += slen + 1;
				pout += slen + 1;
				inleft -= slen + 1;
				outleft -= slen + 1;
			}
			break;
		}
		case RDATA_DUMPHEX:
		default:
		{
			memcpy(pout, pin, inleft);
			pin += inleft;
			pout += inleft;
			break;
		}
		}
	}
	if (pin != pib)
		PELOG_ERROR_RETURN((PLV_ERROR, "Unknown bytes in rdata.\n"), -1);
	return 0;
}

static int decompressName(const abuf<char> &in, const unsigned char *&pin,
	abuf<char> &out, unsigned char *&pout)
{
	const unsigned char * const pib = (const unsigned char *)in.buf();
	const unsigned char * const pie = pib + in.size();
	unsigned char * pob = (unsigned char *)out.buf();
	// A decompressed name is at most 256B long. Make sure out has that extra space
	if (in.size() - (pin - pib) + 256 > out.size() - (pout - pob))
	{
		int iout = pout - pob;	// out.buf() would change after resize, reserve pout
		out.resize(out.size() + 512);
		assert(in.size() - (pin - pib) + 256 <= out.size() - (pout - pob));
		pob = (unsigned char *)out.buf();
		pout = pob + iout;
	}
	unsigned char * const poe = pout + (out.size() - (pout - pob) - (in.size() - (pin - pib)));
	const unsigned char *ppin = pin;	// pin should not move through pointer, so make a copy
	bool pointered = false;	// whether ppin has jumped through a pointer
	while (ppin < pie && pout < poe)
	{
		if (*ppin >= 64)	// a pointer
		{
			unsigned int pointer = ntohs(*(const uint16_t *)ppin) & 0x3fff;
			if (pointer > in.size())
				PELOG_ERROR_RETURN((PLV_ERROR, "Invalid pointer in name.\n"), -1);
			if (!pointered)
			{
				pin += 2;
				pointered = true;
			}
			ppin = pib + pointer;
			continue;
		}
		else if (*ppin == 0)	// end
		{
			*pout++ = 0;
			if (!pointered)
				++pin;
			return 0;
		}
		else	// a normal label
		{
			int len = *ppin;
			if (ppin + len + 1 > pie || pout + len + 1 > poe)
				PELOG_ERROR_RETURN((PLV_ERROR, "Invalid name.\n"), -1);
			*pout++ = *ppin++;
			for (int i = 0; i < len; ++i)
				*pout++ = tolower(*ppin++);
			if (!pointered)
				pin += len + 1;
		}
	}
	// pin or pout exceeds limit, something must be wrong
	PELOG_ERROR_RETURN((PLV_ERROR, "Invalid name.\n"), -1);
}

static int compressName(const abuf<char> &in, const unsigned char *&pin,
	abuf<char> &out, unsigned char *&pout, std::map<std::string, uint16_t> &names)
{
	const unsigned char * const pib = (const unsigned char *)in.buf();
	unsigned char * const pob = (unsigned char *)out.buf();
	unsigned char * const poe = pob + out.size();

	// find the input end, and check the validity of pin to avoid buffer overflow
	const unsigned char * pie = pin;
	for ( ; *pie; pie += *pie + 1)
	{
		if (*pie >= 64)
			PELOG_ERROR_RETURN((PLV_ERROR, "Trying to compress an already compressd name.\n"), -1);
		if (pie - pib + *pie + 1 >= (int)in.size())
			PELOG_ERROR_RETURN((PLV_ERROR, "Trying to compress an invalid name.\n"), -1);
	}
	++pie;	// now pie is the first byte after the input name

	while (pout < poe)
	{
		if (*pin == 0)	// end
		{
			*pout++ = *pin++;
			return 0;
		}
		// test whether current name can be compressed
		auto iname = names.find((char *)pin);
		if (iname != names.end())	// a known name, just replace it with a pointer
		{
			if (pout + 1 >= poe)
				PELOG_ERROR_RETURN((PLV_ERROR, "Insufficient compression buffer.\n"), -1);
			*(uint16_t *)pout = htons(iname->second | (uint16_t)0xc000);
			pin = pie;
			pout += 2;
			return 0;
		}
		// must be a new name
		names[(char *)pin] = pout - pob;	// Add it to known names
		if (pout - pob + *pin + 1 >= (int)out.size())
			PELOG_ERROR_RETURN((PLV_ERROR, "Insufficient compression buffer.\n"), -1);
		memcpy(pout, pin, *pin + 1);
		pout += *pin + 1;
		pin += *pin + 1;
	}
	// pin or pout exceeds limit, something must be wrong
	PELOG_ERROR_RETURN((PLV_ERROR, "Invalid name.\n"), -1);
}

// get first name type in first question, return total number of questions
int getNameType(const abuf<char> &pkt, abuf<char> &nametype)
{
	if (pkt.size() < 12)
		return -1;
	int ret = (int)ntohs(*(const uint16_t *)(pkt.buf() + 4));
	if (ret <= 0)
		return 0;
	int namelen = getName(pkt + 12, pkt.size() - 12);
	if (namelen < 0)
		return -1;
	nametype.resize(namelen + 2);
	memcpy(nametype, pkt + 12, namelen + 2);
	return ret;
}

// raw name-type to printable version
int nametype2print(const abuf<char> &nametype, abuf<char> &pnametype)
{
	return nametype2print(nametype, nametype.size(), pnametype);
}
int nametype2print(const char *nametype, size_t nametypelen, abuf<char> &pnametype)
{
	pnametype.resize(nametypelen + 14);
	name2pname(nametype, nametypelen - 2, pnametype);
	size_t len = strlen(pnametype);
	pnametype.resize(pnametype.capacity());
	if (pnametype.size() < len + 12)
		pnametype.resize(len + 12);
	pnametype[len++] = ':';
	uint16_t type = ntohs(*(const uint16_t *)(nametype + (nametypelen - 2)));
	const char *ptype = type2name(type);
	strncpy(pnametype + len, ptype, pnametype.size() - len);
	pnametype[pnametype.size() - 1] = 0;
	return 0;
}

// update TTLs in pkt (minus (now - uptime)).
// return TTL of whole pkt, -1: error
int manageTtl(abuf<char> &pkt, time_t uptime, time_t now)
{
	int rcode = (int)((unsigned char)pkt[3] & 0xf);
	if (rcode != 0 && rcode != 3)
		PELOG_ERROR_RETURN((PLV_ERROR, "Invalid RCODE %d.\n", rcode), -1);

	int ret = INT_MAX;
	time_t ttldiff = now > uptime ? now - uptime : 0;
	const unsigned char *pos = (const unsigned char *)pkt.buf();
	int qdc = (int)ntohs(*(const uint16_t *)(pos + 4));
	int anc = (int)ntohs(*(const uint16_t *)(pos + 6));
	int nsc = (int)ntohs(*(const uint16_t *)(pos + 8));
	int arc = (int)ntohs(*(const uint16_t *)(pos + 10));
	pos += 12;

	// questions
	for (int i = 0; i < qdc; ++i)
	{
		int nameptr = (const char *)pos - pkt;
		int namel = getName((const char *)pos, pkt.size() - nameptr);
		if (namel < 0)
			PELOG_ERROR_RETURN((PLV_ERROR, "Invalid name.\n"), -1);
		pos += namel;
		if ((const char *)pos - pkt + 4 >(int)pkt.size())
			PELOG_ERROR_RETURN((PLV_ERROR, "Incomplete rdata\n"), -1);
		pos += 4;	// type and class
	}

	int *secnums[] = { &anc, &nsc, &arc };
	for (size_t isec = 0; isec < sizeof(secnums) / sizeof(secnums[0]); ++isec)
	{
		for (int ient = 0; ient < *secnums[isec]; ++ient)
		{
			int nameptr = (const char *)pos - pkt;
			int namel = getName((const char *)pos, pkt.size() - nameptr);
			if (namel < 0)
				PELOG_ERROR_RETURN((PLV_ERROR, "Invalid name.\n"), -1);
			pos += namel;
			if ((const char *)pos - pkt + 10 >(int)pkt.size())
				PELOG_ERROR_RETURN((PLV_ERROR, "Incomplete rdata\n"), -1);
			uint16_t rtype = ntohs(*(const uint16_t *)pos);
			pos += 4;
			uint32_t ttl = ntohl(*(const uint32_t *)pos);
			// update ttl
			if (rtype != RT_OPT)
			{
				if (ttl <= 1)	// already expired
				{
					// answer section expires the whole message.
					// and also a SOA record, if one is found (probably answer is not available)
					if (isec == 0 || rtype == RT_SOA)
						ret = 0;
					ttl = 1;
				}
				else if (ttldiff > 0)
				{
					if (ttldiff >= ttl)	// expired
					{
						if (isec == 0 || rtype == RT_SOA)
							ret = 0;
						ttl = 1;
					}
					else
					{
						ttl -= (uint32_t)ttldiff;
						if (isec == 0 || rtype == RT_SOA)
							ret = std::min(ret, (int)ttl);
					}
				}
				else if (isec == 0 || rtype == RT_SOA)
					ret = std::min(ret, (int)ttl);
				*(uint32_t *)pos = htonl(ttl);	// update ttl
			}
			pos += 4;
			uint16_t rlen = ntohs(*(const uint16_t *)pos);
			pos += 2;
			if ((const char *)pos - pkt + rlen > (int)pkt.size())
				PELOG_ERROR_RETURN((PLV_ERROR, "Incomplete rdata\n"), -1);
			// if SOA is found in authority section, probably answer is not available
			// alse extract TTL from the rdata of SOA
			if (isec == 1 && rtype == RT_SOA)
			{
				// format: name*2 + uint32*5. TTL is the last uint32, so just grab it at the end
				ttl = ntohl(*(const uint32_t *)(pos + rlen - sizeof(uint32_t)));
				ttl = std::min(ttl, (uint32_t)300);	// cache 5min at most for SOA
				ret = std::min(ret, (int)ttl);
			}
			pos += rlen;
		}	// for each record
	}	// for each section
	if (ret == INT_MAX)
	{
		PELOG_LOG((PLV_WARNING, "Could not get TTL from packet, default to 30\n"));
		ret = 30;
	}
	PELOG_LOG((PLV_DEBUG, "Packet TTL %d\n", ret));
	return ret;
}

// check if we got complete answer to the question
bool checkAnswer(const abuf<char> &pkt)
{
	int rcode = (int)((unsigned char)pkt[3] & 0xf);
	if (rcode != 0)
		PELOG_ERROR_RETURN((PLV_ERROR, "Invalid RCODE %d to check answer.\n", rcode), false);
	bool soa = false;

	// question
	const unsigned char *pos = (const unsigned char *)pkt.buf();
	int qdc = ntohs(*(uint16_t *)(pkt + 4));
	if (qdc <= 0)	// no question
		return false;
	int anc = (int)ntohs(*(const uint16_t *)(pos + 6));
	int nsc = (int)ntohs(*(const uint16_t *)(pos + 8));
	int arc = (int)ntohs(*(const uint16_t *)(pos + 10));
	pos += 12;
	int qnamelen = getName((const char *)pos, pkt.size() - 12);
	if (qnamelen <= 0 || pkt.size() < (12u + qnamelen + 2 + 2))	// head + qname + qtype + qclass
		return false;
	abuf<char> qname;
	qname.scopyFrom((const char *)pos, qnamelen);
	pos += qnamelen;
	uint16_t qtype = ntohs(*(uint16_t *)pos);
	uint16_t qclass = ntohs(*(uint16_t *)(pos + 2));
	pos += 4;

	std::map<abuf<char>, abuf<char> > ansmap;
	int *secnums[] = { &anc, &nsc, &arc };
	for (size_t isec = 0; isec < sizeof(secnums) / sizeof(secnums[0]); ++isec)
	{
		for (int ient = 0; ient < *secnums[isec]; ++ient)	// iterate all answers
		{
			int nameptr = (const char *)pos - pkt;
			int namel = getName((const char *)pos, pkt.size() - nameptr);
			if (namel < 0)
				PELOG_ERROR_RETURN((PLV_ERROR, "Invalid name.\n"), false);
			abuf<char> aname;
			aname.scopyFrom((const char *)pos, namel);
			pos += namel;
			uint16_t atype = ntohs(*(uint16_t *)pos);
			uint16_t aclass = ntohs(*(uint16_t *)(pos + 2));
			if ((const char *)pos - pkt + 10 >(int)pkt.size())
				PELOG_ERROR_RETURN((PLV_ERROR, "Incomplete rdata\n"), false);
			uint16_t rtype = ntohs(*(const uint16_t *)pos);
			if (rtype == RT_SOA)
				soa = true;
			pos += 8;
			uint16_t rlen = ntohs(*(const uint16_t *)pos);
			pos += 2;
			if ((const char *)pos - pkt + rlen > (int)pkt.size())
				PELOG_ERROR_RETURN((PLV_ERROR, "Incomplete rdata\n"), false);
			if (isec == 0 && aclass == qclass)
			{
				if (atype == qtype)	// requested type
					ansmap[aname] = abuf<char>();
				else if (atype == RT_CNAME && ansmap.find(aname) == ansmap.end())	// cname, only valid if no other answer for this name
				{
					int cnamel = getName((const char *)pos, rlen);
					if (cnamel != rlen)
						PELOG_ERROR_RETURN((PLV_ERROR, "Invalid cname.\n"), false);
					abuf<char> cname;
					cname.scopyFrom((const char *)pos, cnamel);
					ansmap[aname] = cname;
				}
			}
			pos += rlen;
		}	// for each record
	}

	// check final answer in ansmap
	const abuf<char> *cur = &qname;
	while (true)
	{
		auto iname = ansmap.find(*cur);
		if (iname == ansmap.end())
			break;
		else if (iname->second.isNull())
			return true;
		cur = &(iname->second);
	}
	return soa;
}