include $(top_srcdir)/common.mk

# pre-compiled header improves compilation speed a little, but quite little. `make -j8` is more helpful
BUILT_SOURCES = stdafx.h.gch
stdafx.h.gch: stdafx.h
	$(CXXCOMPILE) -w -c stdafx.h
clean-local: clean-gch
.PHONY: clean-gch
clean-gch:
	-rm -f stdafx.h.gch

bin_PROGRAMS = ahosts-server
ahosts_server_SOURCES = ah_memmem.c ah_memmem.h AHostsCache.hpp AHostsConf.cpp AHostsConf.hpp AHosts.cpp AHostsHandler.cpp AHostsHandler.h AHosts.hpp asio.hpp auto_buf.hpp pe_log.cpp pe_log.h protocol.cpp protocol.hpp server.cpp stdafx.h
ahosts_server_CXXFLAGS = $(AM_CXXFLAGS) -Winvalid-pch
ahosts_server_LDADD = $(BOOST_THREAD_LIBS) $(BOOST_THREAD_LDFLAGS)

