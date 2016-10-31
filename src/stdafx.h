// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once


#include "targetver.h"

#define _CRT_SECURE_NO_WARNINGS

#if defined(_DEBUG) && defined(_MSC_VER)
#	define _CRTDBG_MAP_ALLOC
#	include <crtdbg.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "asio.hpp"
#include "auto_buf.hpp"
#include "pe_log.h"

// TODO: reference additional headers your program requires here
