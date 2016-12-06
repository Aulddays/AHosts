#pragma once
#include <list>
#include <map>
#include <algorithm>
#include <time.h>
#include "auto_buf.hpp"
#include "ah_memmem.h"

#if defined(_DEBUG) && !defined(AHOSTS_CACHE_DEBUG)
#define AHOSTS_CACHE_DEBUG
#include "protocol.hpp"
#endif

using aulddays::abuf;
// an LRU cache for dns results
class AHostsCache
{
public:
	AHostsCache(size_t maxitems = 10000):m_maxsize(maxitems){}
	~AHostsCache()
	{
		for (auto i = m_map.begin(); i != m_map.end(); ++i)
		{
			free((void *)i->first.data);
			free(i->second->second.data);
		}
		m_map.clear();
		m_visit.clear();
	}

	// ttl is merely a value to store in cache, actually cache expiration is caller's response
	int set(const char *key, size_t klen, const char *value, size_t vlen, int32_t ttl)
	{
		if (m_maxsize == 0)	// cache disabled
			return 0;
		Key dkey = { klen, key };
		auto idata = m_map.find(dkey);
		Value dval = { 0, 0, 0, NULL };
#ifdef AHOSTS_CACHE_DEBUG
		abuf<char> keydbg;
		nametype2print(key, klen, keydbg);
#endif
		if (idata != m_map.end())	// already in cache, update pos
		{
#ifdef AHOSTS_CACHE_DEBUG
			PELOG_LOG((PLV_DEBUG, "cache set key %s, already in cache.\n", keydbg.buf()));
#endif
			dkey = idata->second->first;
			dval = idata->second->second;
			m_visit.erase(idata->second);	// remove from ori pos in queue
			if (dval.len != vlen)
			{
#ifdef AHOSTS_CACHE_DEBUG
				PELOG_LOG((PLV_DEBUG, "cache data size chaged " PL_SIZET " -> " PL_SIZET ".\n",
					dval.len, vlen));
#endif
				free(dval.data);
				dval.data = NULL;
			}
		}
		else	// should insert new item
		{
			while (m_map.size() >= m_maxsize)	// full, make some room
			{
#ifdef AHOSTS_CACHE_DEBUG
				abuf<char> oldkeydbg;
				nametype2print(m_visit.back().first.data, m_visit.back().first.len, oldkeydbg);
				PELOG_LOG((PLV_DEBUG, "cache set key %s, drop old key %s.\n", keydbg.buf(), oldkeydbg.buf()));
#endif
				m_map.erase(m_visit.back().first);
				free((void *)m_visit.back().first.data);
				free(m_visit.back().second.data);
				m_visit.pop_back();
			}
			dkey = Key::copyKey(dkey.data, dkey.len);
#ifdef AHOSTS_CACHE_DEBUG
			PELOG_LOG((PLV_DEBUG, "cache set key %s, create new item.\n", keydbg.buf()));
#endif
		}
		if (!dval.data)
		{
			dval.data = (char *)malloc(vlen);
			dval.len = vlen;
		}
		dval.uptime = time(NULL);
		dval.ttl = ttl;
		memcpy(dval.data, value, vlen);
		m_visit.push_front(std::make_pair(dkey, dval));
		// update map
		m_map[dkey] = m_visit.begin();
		return 0;
	}
	// ttl is merely a value stored in cache, actually cache expiration is caller's response
	int get(const char *key, size_t klen, abuf<char> &value, time_t &uptime, int32_t &ttl)
	{
		Key dkey = { klen, key };
		auto idata = m_map.find(dkey);
		if (idata == m_map.end())
			return -1;
		// update visit queue and map reference
		dkey = idata->first;
		Value dval = idata->second->second;
		m_visit.erase(idata->second);
		m_visit.push_front(std::make_pair(dkey, dval));
		idata->second = m_visit.begin();
		// set return values
		value.scopyFrom(dval.data, dval.len);
		uptime = dval.uptime;
		ttl = dval.ttl;
		return 0;
	}
private:
	struct Key
	{
		size_t len;
		const char *data;
		static Key copyKey(const char *key, size_t klen)
		{
			Key dkey;
			dkey.len = klen;
			char *data = (char *)malloc(klen);
			memcpy(data, key, klen);
			dkey.data = data;
			return dkey;
		}
		bool operator <(const Key &r) const
		{
			int res = memcmp(data, r.data, std::min(len, r.len));
			return res < 0 || (res == 0 && len < r.len);
		}
	};
	struct Value
	{
		time_t uptime;
		size_t len;
		int32_t ttl;	// A value to store. does not affect cache expiration or replacement
		char *data;
	};
	std::map<const Key, std::list<std::pair<const Key, Value> >::iterator> m_map;
	std::list<std::pair<const Key, Value> > m_visit;
	size_t m_maxsize;
};

