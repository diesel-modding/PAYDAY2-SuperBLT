#include "FormatTools.h"

namespace raidhook::scriptdata::tools
{

	std::ostream& write_block::seek(uint32_t pos)
	{
		if (offset == NOT_LOCATED)
		{
			s->seekp(pos);
			return *s;
		}

		main->seekp(offset + pos);
		return *main;
	}

	std::ostream& write_block::stream()
	{
		if (offset == NOT_LOCATED)
			return *s;

		return *main;
	}

	write_block::write_block()
	{
		if (fake_write_mode)
			return;

		s.reset(new std::stringstream());
	}

	void write_block::write_to(std::ostream& stream)
	{
		if (fake_write_mode)
			return;

		offset = stream.tellp();
		stream << s->str();
		s.reset();
		main = &stream;
	}

	void writePtr(write_block& out, bool is32bit, uint32_t val)
	{
		if (is32bit)
		{
			writeVal<uint32_t>(out, val);
		}
		else
		{
			writeVal<uint64_t>(out, val);
		}
	}
}; // namespace raidhook::scriptdata::tools
