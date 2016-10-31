// server.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "AHosts.hpp"

#if defined(_DEBUG) && defined(_MSC_VER)
#	ifndef DBG_NEW
#		define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
#		define new DBG_NEW
#	endif
#endif  // _DEBUG

AHosts ahosts;

#ifdef _WIN32
BOOL WINAPI sighdl(DWORD code)
{
	switch (code)
	{
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		PELOG_LOG((PLV_INFO, "Stopping\n"));
		ahosts.stop();
		return TRUE;
	default:
		return FALSE;
	}
}
#else
void sighdl(int code)
{
	if (code == SIGINT || code == SIGTERM || code == SIGABRT)
	{
		PELOG_LOG((PLV_INFO, "Stopping\n"));
		ahosts.stop();
	}
}
#endif

int main(int argc, char* argv[])
{
#if defined(_DEBUG) && defined(_MSC_VER)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

#ifdef _WIN32
	SetConsoleCtrlHandler(sighdl, TRUE);
#else
	signal(SIGINT, sighdl);
	signal(SIGTERM, sighdl);
	signal(SIGABRT, sighdl);
	signal(SIGUSR1, sighdl);
#endif

	ahosts.start();
	return 0;
}


