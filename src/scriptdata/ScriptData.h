#pragma once

#include "FormatTools.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace raidhook::scriptdata
{

	bool determine_is_32bit(size_t length, const uint8_t* data);

	class SItem
	{
	  public:
		virtual ~SItem();
		virtual int GetId() const = 0;

		// Index in the respective vector
		int index = -1;

		virtual std::string Serialise(bool use32bit) const;

		// For internal use, don't actually use this
		class write_info;

		typedef std::function<bool(const SItem*)> const& RegReceiver;
		virtual void Register(RegReceiver receiver) const
		{
			// Fine for plain types, container types must override!
			receiver(this);
		};

	  protected:
		virtual void Serialise(tools::write_block& out, write_info& info) const = 0;
	};

	class SNil : public SItem
	{
	  public:
		virtual int GetId() const override
		{
			return ID;
		}
		static const int ID = 0;
		static const SNil INSTANCE;

	  protected:
		virtual void Serialise(tools::write_block& out, write_info& info) const override
		{
			throw std::exception();
		};
	};

	class SBool : public SItem
	{
	  public:
		SBool() : SBool(false)
		{
		}
		explicit SBool(bool val) : val(val)
		{
		}
		bool val;

		virtual int GetId() const override
		{
			return val ? ID_T : ID_F;
		}

		static const int ID_T = 1;
		static const int ID_F = 2;
		static const SBool STRUE;
		static const SBool SFALSE;

	  protected:
		virtual void Serialise(tools::write_block& out, write_info& info) const override
		{
			throw std::exception();
		};
	};

	class SNum : public SItem
	{
	  public:
		SNum() : SNum(0)
		{
		}
		explicit SNum(float val) : val(val)
		{
		}
		float val;

		virtual int GetId() const override
		{
			return ID;
		}

		static const int ID = 3;

	  protected:
		virtual void Serialise(tools::write_block& out, write_info& info) const override;
	};

	class SString : public SItem
	{
	  public:
		SString() : SString(std::string())
		{
		}
		explicit SString(std::string val) : val(val)
		{
		}
		std::string val;

		virtual int GetId() const override
		{
			return ID;
		}

		operator const std::string() const
		{
			return val;
		}

		static const int ID = 4;

	  protected:
		virtual void Serialise(tools::write_block& out, write_info& info) const override;
	};

	class SVector : public SItem
	{
	  public:
		SVector() : SVector(0, 0, 0)
		{
		}
		explicit SVector(float x, float y, float z) : x(x), y(y), z(z)
		{
		}
		float x, y, z;

		virtual int GetId() const override
		{
			return ID;
		}

		static const int ID = 5;

	  protected:
		virtual void Serialise(tools::write_block& out, write_info& info) const override;
	};

	class SQuaternion : public SItem
	{
	  public:
		SQuaternion() : SQuaternion(0, 0, 0, 0)
		{
		}
		explicit SQuaternion(float x, float y, float z, float w) : x(x), y(y), z(z), w(w)
		{
		}
		float x, y, z, w;

		virtual int GetId() const override
		{
			return ID;
		}

		static const int ID = 6;

	  protected:
		virtual void Serialise(tools::write_block& out, write_info& info) const override;
	};

	class SIdstring : public SItem
	{
	  public:
		SIdstring() : SIdstring(0)
		{
		}
		explicit SIdstring(uint64_t val) : val(val)
		{
		}
		uint64_t val;

		virtual int GetId() const override
		{
			return ID;
		}

		static const int ID = 7;

	  protected:
		virtual void Serialise(tools::write_block& out, write_info& info) const override;
	};

	class STable : public SItem
	{
	  public:
		SString* meta;
		std::map<const SItem*, const SItem*> items;

		virtual int GetId() const override
		{
			return ID;
		}

		static const int ID = 8;

	  protected:
		virtual void Serialise(tools::write_block& out, write_info& info) const override;
		virtual void Register(RegReceiver receiver) const override;
	};

	class ScriptData
	{
	  public:
		ScriptData(size_t length, const uint8_t* data);

		inline const SItem* GetRoot()
		{
			return root;
		}

	  private:
		std::vector<SNum> numbers;
		std::vector<SString> strings;
		std::vector<SVector> vectors;
		std::vector<SQuaternion> quats;
		std::vector<SIdstring> idstrings;
		std::vector<STable> tables;

		const SItem* root;

		const SItem* Read(uint32_t);
		void ReadTable(STable& out, std::pair<uint32_t, uint32_t>* data, size_t count, uint32_t meta);
	};

}; // namespace raidhook::scriptdata
