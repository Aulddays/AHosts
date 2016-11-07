// dns protocol constants
#pragma once

#include "auto_buf.hpp"

enum ResourceType
{
	RT_A = 1,
	RT_NS = 2,
	RT_CNAME = 5,
	RT_SOA = 6,
	RT_WKS = 11,
	RT_PTR = 12,
	RT_MX = 15,
	RT_AAAA = 28,
	RT_SRV = 33,
	RT_OPT = 41,
	RT_ANY = 255,
};

enum RescourceClass
{
	RC_IN = 1
};

const char *type2name(uint16_t type);

inline const char *class2name(uint16_t cls)
{
	return cls == RC_IN ? "IN" : "UNK";
}

// get qname at str, return length. if error occurred, return -1
int getName(const char *str, size_t max);

// convert qname into dot-delimetered printable host name
void name2pname(const char *str, size_t len, aulddays::abuf<char> &out);

// convert dot-delimetered printable host name into qname. return non-zero on error
int pname2name(const char *str, aulddays::abuf<char> &out);

// compress/decompress dns packet
int decompressPacket(const aulddays::abuf<char> in, aulddays::abuf<char> &out);
int compressPacket(const aulddays::abuf<char> in, aulddays::abuf<char> &out);

void dump_packet(const aulddays::abuf<char> &pkt);