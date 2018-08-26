// TcpServer: A class that manages communication with tcp upstream servers

#include "stdafx.h"
#include "AHosts.hpp"
#include "protocol.hpp"
#include "TcpServer.hpp"

#if defined(_DEBUG) && defined(_MSC_VER)
#	ifndef DBG_NEW
#		define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
#		define new DBG_NEW
#	endif
#endif  // _DEBUG

// TcpServer

