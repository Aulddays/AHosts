include $(top_srcdir)/common.mk

bin_PROGRAMS = ahosts-server
ahosts_server_SOURCES = ah_memmem.c ah_memmem.h AHostsCache.hpp AHostsConf.cpp AHostsConf.hpp AHosts.cpp AHostsHandler.cpp AHostsHandler.h AHosts.hpp asio.hpp auto_buf.hpp pe_log.cpp pe_log.h protocol.cpp protocol.hpp server.cpp stdafx.h
ahosts_server_SOURCES += libconfig/grammar.c libconfig/grammar.h libconfig/libconfig.c libconfig/libconfig.h libconfig/parsectx.h libconfig/scanctx.c libconfig/scanctx.h libconfig/scanner.c libconfig/scanner.h libconfig/strbuf.c libconfig/strbuf.h libconfig/strvec.c libconfig/strvec.h libconfig/util.c libconfig/util.h libconfig/wincompat.c libconfig/wincompat.h
ahosts_server_CXXFLAGS = $(AM_CXXFLAGS) -Winvalid-pch
ahosts_server_LDADD = -lpthread

