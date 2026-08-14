#pragma once

#include "../platform.h"

#include <vector>

namespace diesel {

    template<typename T1, typename T2> struct Pair
	{
		T1 first;
		T2 second;
    };

    template<typename T> class Vector
	{
	private:
		size_t _size;
		size_t _capacity;
		T* _data;
		void* _allocator;
	public:

		T* begin()
		{
			return _data;
		}
		T* end()
		{
			return _data + _size;
		}
		const T* begin() const
		{
			return _data;
		}
		const T* end() const
		{
			return _data + _size;
		}

		const T& operator[](size_t i) const
		{
			return _data[i];
		}

		T& operator[](size_t i)
		{
			return _data[i];
		}
	};

    template<typename K, typename V> class SortMap
	{
	public:
		typedef Pair<K, V> VectorType;
    private:
		char _less[8];
		Vector<VectorType> _data;
		bool _is_sorted;

	public:
		const Vector<VectorType>& data() const { return _data; }
		Vector<VectorType>& data() { return _data; }
    };

	struct DBExtKey
	{
		blt::idstring type;
		blt::idstring name;
		uint32_t properties;
	};

    class DB
    {
    private:
		struct Data
		{
		public:
			SortMap<blt::idstring, uint32_t> _properties;
			SortMap<DBExtKey, uint32_t> _lookup;
			uint32_t _next_key;
        };


        char PAD[96];
		Data* _data;

	public:
		static DB* GetDB();

		bool ContainsFile(blt::idstring type, blt::idstring name, blt::idstring property);
		bool OpenFile(blt::idstring type, blt::idstring name, blt::idstring property, std::vector<uint8_t>& out_data);

    };

}

namespace blt
{
	void InitDBHooks();
}
