#include "stdafx.h"
#include "stdarg.h"
#include "stdio.h"
#include <time.h>
#include <string>
#include <sstream>
#include <boost/thread.hpp>
#include <boost/static_assert.hpp>

#include "pe_log.h"

#if defined(_DEBUG) && defined(_MSC_VER)
#	ifndef DBG_NEW
#		define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
#		define new DBG_NEW
#	endif
#endif  // _DEBUG

//FILE *pelog_out_stream = stderr;
//std::string pelog_out_file = "";
boost::mutex pelog_mutex;
int pelog_logLevel = PLV_DEBUG;

// helper for output stream
static struct PelogOutStream
{
	FILE *stream;
	PelogOutStream() : stream(stderr){}
	~PelogOutStream()
	{
		close();
	}
	void close()
	{
		if (stream != stdout && stream != stderr)
		{
			fclose(stream);
			stream = stderr;
		}
	}
	int set(const char *filename)
	{
		FILE *newstr = fopen(filename, "ab");
		if (newstr)
		{
			FILE *oldstr = stream;
			setvbuf(newstr, NULL, _IOFBF, 512);
			stream = newstr;
			fclose(oldstr);
			return 0;
		}
		return 1;
	}
	FILE *get()
	{
		return stream;
	}

} pelog_out_stream;

static const char *pelog_levelDesc[] =
{
	"DBG",
	"VRB",
	"TRC",
	"INF",
	"WRN",
	"ERR",
};
BOOST_STATIC_ASSERT(sizeof(pelog_levelDesc) / sizeof(pelog_levelDesc[0]) == PLV_MAXLEVEL);

int pelog_printf(int level, const char *format, ...)
{
	va_list v;
	if(level <= PLV_MINLEVEL || level >= PLV_MAXLEVEL)
	{
		pelog_printf(PLV_ERROR, "PELOG: Error log level %d\n", level);
		return -1;
	}
	if(level < pelog_logLevel)
		return 0;
	int ret = 0;
	{	// for lock_guard
		boost::lock_guard<boost::mutex> lock(pelog_mutex);
		FILE *out_stream = pelog_out_stream.get();
		time_t curTime = time(NULL);
		tm *curTm = localtime(&curTime);
		char buf[32];
		strftime(buf, 32, "%Y%m%d %H:%M:%S", curTm);
		std::ostringstream stm;
		stm << boost::this_thread::get_id();
		fprintf(out_stream, "[%s %s %s] ", pelog_levelDesc[level], buf, stm.str().c_str());
		va_start(v, format);
		ret = vfprintf(out_stream, format, v);
		va_end(v);
	}
	return ret;
}

int pelog_rawprintf(int level, const char *format, ...)
{
	va_list v;
	if (level <= PLV_MINLEVEL || level >= PLV_MAXLEVEL)
	{
		pelog_printf(PLV_ERROR, "PELOG: Error log level %d\n", level);
		return -1;
	}
	if (level < pelog_logLevel)
		return 0;
	int ret = 0;
	{	// for lock_guard
		boost::lock_guard<boost::mutex> lock(pelog_mutex);
		FILE *out_stream = pelog_out_stream.get();
		va_start(v, format);
		ret = vfprintf(out_stream, format, v);
		va_end(v);
	}
	return ret;
}

size_t PELOG_RAWWRITE(int level, const void *buf, size_t size, size_t count)
{
	if (level <= PLV_MINLEVEL || level >= PLV_MAXLEVEL)
	{
		pelog_printf(PLV_ERROR, "PELOG: Error log level %d\n", level);
		return 0;
	}
	if (level < pelog_logLevel)
		return count;
	size_t ret = 0;
	{	// for lock_guard
		boost::lock_guard<boost::mutex> lock(pelog_mutex);
		FILE *out_stream = pelog_out_stream.get();
		ret = fwrite(buf, size, count, out_stream);
	}
	return ret;
}

int pelog_setlevel(int level, int *old_level)
{
	if(old_level)
		*old_level = pelog_logLevel;
	if(level <= PLV_MINLEVEL || level >= PLV_MAXLEVEL)
	{
		pelog_printf(PLV_ERROR, "PELOG: Error log level %d\n", level);
		return -1;
	}
	pelog_printf(PLV_TRACE, "PELOG: Setting log lovel to %s\n", pelog_levelDesc[level]);
	pelog_logLevel = level;
	return 0;
}

int pelog_setlevel(const char *level, int *old_level)
{
	for(int i = 0; i < PLV_MAXLEVEL; ++i)
		if(0 == strcmp(level, pelog_levelDesc[i]))
			return pelog_setlevel(i, old_level);
	pelog_printf(PLV_ERROR, "PELOG: Unsupported log level \"%s\". Current level is %s\n", level, pelog_levelDesc[pelog_logLevel]);
	return -1;
}

int pelog_setfile(const char *fileName)
{
	return pelog_out_stream.set(fileName);
}

