// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#ifndef _AHOSTS_SRC_STDAFX_H_	// gcc complains #pragma once on .h files, so use this guard
#define _AHOSTS_SRC_STDAFX_H_

#if defined(_MSC_VER)

#include "targetver.h"

#if defined(_DEBUG)
#	define _CRTDBG_MAP_ALLOC
#	include <crtdbg.h>
#endif	// if defined(_DEBUG)

#endif // if defined(_MSC_VER)

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define ASIO_STANDALONE
#include "asio.hpp"
#include "auto_buf.hpp"
using namespace aulddays;
#include "pe_log.h"
//#include "protocol.hpp"

// WARNING: _snprintf_s on Windows has different return value than snprintf of C99
// so they are actually not identical
#ifdef _MSC_VER
#	define snprintf(str, size, fmt, ...) _snprintf_s((str), (size), _TRUNCATE, (fmt), __VA_ARGS__)
#endif

#endif
