#pragma once
#include "auto_buf.hpp"
#include "AHostsConf.hpp"

class AHostsHandler
{
public:
	AHostsHandler(const AHostsConf *conf): m_conf(conf){};
	~AHostsHandler(){};

	// return: 0: success and recursive request is required
	// return: 1: success and req now is final response and no recursive request needed
	// return: <0: failure
	int processRequest(aulddays::abuf<char> &req);

	// return: 0: success, other: failure
	int processResponse(aulddays::abuf<char> &res);

	static int loadHostsExt(const char *filename, AHostsConf *conf);

private:
	const AHostsConf *m_conf;
	aulddays::abuf<char> m_oriNameType;	// original name and type requested
	size_t m_appendNum;	// number of records to be inserted
	aulddays::abuf<char> m_append;	// answer record(s) to be inserted
};

