#pragma once

#include <functional>
#include <memory>
#include <ostream>
#include <sstream>

namespace raidhook::scriptdata::tools
{
	// Represents a single block of data to be written somewhere in the file
	class write_block
	{
	  public:
		std::ostream& seek(uint32_t pos);

		std::ostream& stream();

		// Offset in the file
		uint32_t offset = NOT_LOCATED;

		static const uint32_t NOT_LOCATED = ~0;

		write_block(write_block&) = delete;
		write_block& operator=(const write_block&) = delete;

		explicit write_block();

		void write_to(std::ostream& stream);

		inline uint32_t tellp()
		{
			return s->tellp();
		}

		bool fake_write_mode = false;

	  private:
		std::unique_ptr<std::stringstream> s;
		std::ostream* main;
	};

	class linkage
	{
	  public:
		write_block* block;

		typedef std::function<void(uint32_t)> on_address_set_t;
		on_address_set_t on_address_set;

		linkage(write_block* block, on_address_set_t cb) : block(block), on_address_set(cb)
		{
		}
	};

	template <typename T> void writeVal(write_block& out, T val)
	{
		out.stream().write((const char*)&val, sizeof(val));
	}

	void writePtr(write_block& out, bool is32bit, uint32_t val);

}; // namespace raidhook::scriptdata::tools
