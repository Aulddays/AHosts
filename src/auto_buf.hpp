/*
auto bufer. 用于替换以下几种情况： 
1. 
void f() 
{ 
    char buf[1000];	// 用多了很有可能 stack overflow 造成诡异的 core
    .....
} 
2. 
void g() 
{ 
    int buf = new int[1000];	// 不占stack空间，但有可能忘了delete，特别是在g()有多个（或是以后再增加一个）出口的时候
    ....
    delete[] buf;
} 
3. 
void h() 
{ 
	std::vector<char> buf(1000);	// 会调1000次默认构造函数，效率很低 
} 
 
auto buffer 用法： 
#include <auto_buf.hpp> 
using aulddays::abuf; 
void f() 
{ 
    abuf<int> buf1(1000);	// 分配了包含 1000 个 int 的数组。后续 buf 变量的使用和 int * 几乎完全相同
    abuf<char> buf2(1000, true); 	// 第二个参数为 true 将会全部填 0
    assert(buf2[0] == 0);
 
    abuf<char> buf3;	// buf3.isNull() == true
    buf3.resize(100);	// 大小可改变。但 resize 后之前类型转换得到的指针将失效！！！
    strcpy(buf3, "test");	// 可以当 char* 使用
    char *pbuf3 = buf3;
    fprintf("%s\n", pbuf3);	// OK
    buf3[0] = 'b';
    buf3.resize(200);
    fprintf("%s\n", pbuf3);	// May CRASH. pbuf3 is INVALID after buf3.resize() !!!
    assert(strcmp(buf3, "best") == 0);	// 如果 resize 前 buf3 非空，则 min(old_size, new_size) 部分的内容保持不变
 
    // 对于 char 数组做了特化：
    abufchar string1("test string");
    abufchar string1 = "test string";   // 多一次内存分配，所以建议使用上一行的形式
 
    // 浅拷贝
    buf2.scopyFrom(string1);
 
    // 函数退出时无需显式释放空间。如果中途想手动释放，可以调用 buf3.resize(0)
} 
 
*/

/*
abuf 支持debug模式，用于（在一定程度上）辅助查找内存越界之类的错误。用下面的方法启用debug模式：
#define _ABUF_DEBUG 1
*/

#pragma once

#include <stdlib.h>
#include <string.h>

namespace aulddays
{

#ifdef _DEBUG
#	define _ABUF_DEBUG 1
#endif

template <typename T>
	class basic_abuf
{
protected:
	T *_buf;
#ifdef _ABUF_DEBUG
	uint8_t *_inbuf;
#endif
	size_t _size;	// Available size for user, in number of T's, not bytes
	size_t _capacity;	// Actual size allocated, in T's
private:
	basic_abuf(const basic_abuf &);
	basic_abuf& operator =(const basic_abuf &);
	basic_abuf(const T *);
	basic_abuf &operator =(const T *);

	// in debug mode, append 4 extra bytes (0xfdfdfdfdu) to both beginning and end of _buf
	// and check the values frequently
#ifdef _ABUF_DEBUG
#	define abuf_mem_check_dword 0xfdfdfdfdu
	// Failure of this assertion indicates there was an overflow while using abuf. Check your code!
#	define abuf_mem_check() do { \
	if(_size > 0) \
		assert( (*(uint32_t*)_inbuf) == abuf_mem_check_dword && \
			(*(uint32_t*)(_inbuf + 4 + _size)) ==  abuf_mem_check_dword); \
	} while (false)
#else
#	define abuf_mem_check() ((void)0)
#endif

public:
	basic_abuf() :_buf(NULL), _size(0), _capacity(0)
#ifdef _ABUF_DEBUG
		,_inbuf(NULL)
#endif
	{}

	/**
	 * @param element_count number of elements wanted
	 * @param clear whether fill all elements with 0
	 */
	basic_abuf(size_t element_count, bool clear = false)
	{
		if(clear)
		{
#ifdef _ABUF_DEBUG
			_inbuf = (uint8_t *)calloc(element_count * sizeof(T) + 8, 1);	// calloc automatically fills _buf with 0
			if(_inbuf)
			{
				*(uint32_t*)_inbuf = abuf_mem_check_dword;
				*(uint32_t*)(_inbuf + element_count * sizeof(T) + 4) = abuf_mem_check_dword;
				_buf = (T *)(_inbuf + 4);
			}
			else
				_buf = NULL;
#else
			_buf = (T *)calloc(element_count, sizeof(T));	// calloc automatically fills _buf with 0
#endif
		}
		else
		{
#ifdef _ABUF_DEBUG
			_inbuf = (uint8_t *)malloc(element_count * sizeof(T) + 8);
			if(_inbuf)
			{
				*(uint32_t*)_inbuf = abuf_mem_check_dword;
				*(uint32_t*)(_inbuf + element_count * sizeof(T) + 4) = abuf_mem_check_dword;
				_buf = (T *)(_inbuf + 4);
			}
			else
				_buf = NULL;
#else
			_buf = (T *)malloc(element_count * sizeof(T));
#endif
		}
		_size = _buf ? element_count : 0;
		_capacity = _size;
		abuf_mem_check();
	}

	~basic_abuf()
	{
		abuf_mem_check();
#ifdef _ABUF_DEBUG
		free(_inbuf);
		_inbuf = NULL;
#else
		free(_buf);
#endif
		_buf = NULL;
		_size = 0;
		_capacity = 0;
	}

	/** 
	 * first min(new_element_count, old_element_count) elements will
	 * remain unchanged after resize. 
	 * IMPORTANT: Any previously T* obtained through type 
	 * cast on abuf will be INVALIDATED after a resize(): 
	 * void f() 
	 * {
	 *  	abuf<char> buf(100);
	 *  	char *pbuf = buf;
	 *  	buf.resize(200);	// pbuf is INVALID now!
	 * } 
	 * @return int 0 if succeeded. other if failed (original buffer 
	 *  	   will be kept even if resize fails)
	 */
	int resize(size_t new_element_count)
	{
		abuf_mem_check();
		if(new_element_count > _capacity)
		{
#ifdef _ABUF_DEBUG
			uint8_t *nbuf = (uint8_t *)realloc(_inbuf, new_element_count * sizeof(T) + 8);
#else
			uint8_t *nbuf = (uint8_t *)realloc(_buf, new_element_count * sizeof(T));
#endif
			if(!nbuf && new_element_count != 0)	// if realloc fails, return value is NULL and the input _buf would be untouched
				return 1;
#ifdef _ABUF_DEBUG
			_inbuf = nbuf;
			*(uint32_t*)_inbuf = abuf_mem_check_dword;
			*(uint32_t*)(_inbuf + new_element_count * sizeof(T) + 4) = abuf_mem_check_dword;
			_buf = (T *)(_inbuf + 4);
#else
			_buf = (T *)nbuf;
#endif
			_size = new_element_count;
			_capacity = new_element_count;
		}
		else if (new_element_count != _size)	// new_element_count <= _capacity implied
		{
			// do not shrink buf even if making me smaller, just change the value of size.
			// Actual buffer size stays in capacity, in case would make me larger later
			_size = new_element_count;
#ifdef _ABUF_DEBUG
			*(uint32_t*)(_inbuf + new_element_count * sizeof(T) + 4) = abuf_mem_check_dword;
#endif
		}
		else if(new_element_count == 0)
		{
#ifdef _ABUF_DEBUG
			free(_inbuf);
			_inbuf = NULL;
#else
			free(_buf);
#endif
			_buf = NULL;
			_size = 0;
		}
		abuf_mem_check();
		return 0;
	}

	// Usable size, in number of T's
	size_t size() const
	{
		abuf_mem_check();
		return _size;
	}

	// Actual memory allocated, in number of T's
	size_t capacity() const
	{
		abuf_mem_check();
		return _capacity;
	}

	// Pre-allocate some memory, not usable for now
	int reserve(size_t desired_element_count)
	{
		abuf_mem_check();
		int ret = 0;
		if (desired_element_count > _capacity)
		{
			size_t osize = size();
			ret = resize(desired_element_count);
			resize(osize);
			abuf_mem_check();
		}
		return ret;
	}

	operator T *()
	{
		abuf_mem_check();
		return _buf;
	}

	operator const T *() const
	{
		abuf_mem_check();
		return _buf;
	}

	T *buf()
	{
		abuf_mem_check();
		return _buf;
	}

	const T *buf() const
	{
		abuf_mem_check();
		return _buf;
	}

	//  operator bool() const
//  {
//  	return _size != 0;
//  }

	T &operator [](int n)
	{
		abuf_mem_check();
		return _buf[n];
	}

	const T &operator [](int n) const
	{
		abuf_mem_check();
		return _buf[n];
	}

	bool isNull() const
	{
		abuf_mem_check();
		return _size == 0;
	}

	// Shallow copy
	int scopyFrom(const basic_abuf<T> &src)
	{
		abuf_mem_check();
		return scopyFrom(src._buf, src.size());
	}

	int scopyFrom(const T *src, size_t size)
	{
		abuf_mem_check();
		if(resize(size))
			return 1;
		if(size > 0)
			memcpy(_buf, src, size * sizeof(T));
		abuf_mem_check();
		return 0;
	}

	void swap(basic_abuf<T> &src)
	{
		std::swap(_buf, src._buf);
#ifdef _ABUF_DEBUG
		std::swap(_inbuf, src._inbuf);
#endif
		std::swap(_size, src._size);
		std::swap(_capacity, src._capacity);
	}

	// operator <, callable if operator < is defined for T
	bool operator <(const basic_abuf &r) const
	{
		for (size_t cur = 0; cur < _size && cur < r.size(); ++cur)
		{
			if (_buf[cur] < r[cur])
				return true;
			else if (r[cur] < _buf[cur])
				return false;
		}
		return _size < r.size();
	}
};

template <typename T>
    class abuf : public basic_abuf<T>
{
};

template <>
    class abuf<char> : public basic_abuf<char>
{
public:
	abuf(const abuf &copy): basic_abuf<char>()
	{
		if(copy.isNull())
			return;
		if(0 == resize(copy.size()))
			memcpy(_buf, copy._buf, _size);
		abuf_mem_check();
	}
	abuf(const char *copy = NULL): basic_abuf<char>()
	{
		if(!copy)
			return;
		if(0 == resize(strlen(copy) + 1))
			strcpy(_buf, copy);
		abuf_mem_check();
	}
	abuf(size_t element_count, bool clear = false) : basic_abuf(element_count, clear)
	{}
};

}

