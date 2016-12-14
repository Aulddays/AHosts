/*
auto bufer. �����滻���¼�������� 
1. 
void f() 
{ 
    char buf[1000];	// �ö��˺��п��� stack overflow ��ɹ���� core
    .....
} 
2. 
void g() 
{ 
    int buf = new int[1000];	// ��ռstack�ռ䣬���п�������delete���ر�����g()�ж���������Ժ�������һ�������ڵ�ʱ��
    ....
    delete[] buf;
} 
3. 
void h() 
{ 
	std::vector<char> buf(1000);	// ���1000��Ĭ�Ϲ��캯����Ч�ʺܵ� 
} 
 
auto buffer �÷��� 
#include <auto_buf.hpp> 
using aulddays::abuf; 
void f() 
{ 
    abuf<int> buf1(1000);	// �����˰��� 1000 �� int �����顣���� buf ������ʹ�ú� int * ������ȫ��ͬ
    abuf<char> buf2(1000, true); 	// �ڶ�������Ϊ true ����ȫ���� 0
    assert(buf2[0] == 0);
 
    abuf<char> buf3;	// buf3.isNull() == true
    buf3.resize(100);	// ��С�ɸı䡣�� resize ��֮ǰ����ת���õ���ָ�뽫ʧЧ������
    strcpy(buf3, "test");	// ���Ե� char* ʹ��
    char *pbuf3 = buf3;
    fprintf("%s\n", pbuf3);	// OK
    buf3[0] = 'b';
    buf3.resize(200);
    fprintf("%s\n", pbuf3);	// May CRASH. pbuf3 is INVALID after buf3.resize() !!!
    assert(strcmp(buf3, "best") == 0);	// ��� resize ǰ buf3 �ǿգ��� min(old_size, new_size) ���ֵ����ݱ��ֲ���
 
    // ���� char ���������ػ���
    abufchar string1("test string");
    abufchar string1 = "test string";   // ��һ���ڴ���䣬���Խ���ʹ����һ�е���ʽ
 
    // ǳ����
    buf2.scopyFrom(string1);
 
    // �����˳�ʱ������ʽ�ͷſռ䡣�����;���ֶ��ͷţ����Ե��� buf3.resize(0)
} 
 
*/

/*
abuf ֧��debugģʽ�����ڣ���һ���̶��ϣ����������ڴ�Խ��֮��Ĵ���������ķ�������debugģʽ��
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

