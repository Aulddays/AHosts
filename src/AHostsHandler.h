#pragma once
#include "auto_buf.hpp"

class AHostsHandler
{
public:
	AHostsHandler(){};
	~AHostsHandler(){};

	// return: 0: success and recursive request is required
	// return: 1: success and req now is final response and no recursive request needed
	// return: <0: failure
	int processRequest(aulddays::abuf<char> &req);

	// return: 0: success, other: failure
	int processResponse(aulddays::abuf<char> &res);

private:
	aulddays::abuf<char> m_oriName;	// original name requested
	size_t m_appendNum;	// number of records to be inserted
	aulddays::abuf<char> m_append;	// answer record(s) to be inserted
};

