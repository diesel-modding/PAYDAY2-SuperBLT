#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <stdint.h>

class BLTAbstractDataStore
{
  public:
	virtual ~BLTAbstractDataStore()
	{
	}
	// MinGW (and generally, GCC) rearranges the VTable compared to MSVC,
	// (MSVC puts the RTTI typeinfo somewhere else, so we end up with an extra 8 bytes before method signatures)
	// so without write at the bottom (outside of the vtable length MSVC is expecting) we crash/hang.
	// There are no flags or anything to change this behavior in MinGW, either.
	// Realistically, the 'correct' solution would be to use a struct to recreate the MSVC class with a manually-written vtable,
	// and hope it never tries to look for the RTTI relative to these class defs.
	// But, that would be a huge headache, and this will work unless we implement an actual write() for any of the subclasses,
	// but I don't think that would make sense, because we don't want overrides being written to.
#ifdef __MINGW32__
	virtual size_t read(uint64_t position_in_file, uint8_t* data, size_t length) = 0;
	virtual bool close() = 0;
	virtual size_t size() const = 0;
	virtual bool is_asynchronous() const = 0;
	virtual void set_asynchronous_completion_callback(void* /*dsl::LuaRef*/); // ignore this
	virtual uint64_t state(); // ignore this
	virtual bool good() const = 0;
	virtual size_t write(uint64_t position_in_file, uint8_t const* data, size_t length); // Stubbed with an abort
#else
	virtual size_t write(uint64_t position_in_file, uint8_t const* data, size_t length); // Stubbed with an abort
	virtual size_t read(uint64_t position_in_file, uint8_t* data, size_t length) = 0;
	virtual bool close() = 0;
	virtual size_t size() const = 0;
	virtual bool is_asynchronous() const = 0;
	virtual void set_asynchronous_completion_callback(void* /*dsl::LuaRef*/); // ignore this
	virtual uint64_t state(); // ignore this
	virtual bool good() const = 0;
#endif
};

class BLTFileDataStore : public BLTAbstractDataStore
{
  public:
	// Delete default crap
	BLTFileDataStore(const BLTFileDataStore&) = delete;
	BLTFileDataStore& operator=(const BLTFileDataStore&) = delete;

	static BLTFileDataStore* Open(std::string filePath);
	virtual ~BLTFileDataStore();
	virtual size_t read(uint64_t position_in_file, uint8_t* data, size_t length) override;
	virtual bool close() override;
	virtual size_t size() const override;
	virtual bool is_asynchronous() const override;
	virtual bool good() const override;

  private:
	BLTFileDataStore() = default; // Used by Open, which can return null to indicate it didn't open properly
	int fd = -1;
	size_t file_size = 0;
};
class BLTStringDataStore : public BLTAbstractDataStore
{
  public:
	// Delete default crap
	BLTStringDataStore(const BLTStringDataStore&) = delete;
	BLTStringDataStore& operator=(const BLTStringDataStore&) = delete;

	explicit BLTStringDataStore(std::vector<uint8_t> contents);
	virtual size_t read(uint64_t position_in_file, uint8_t* data, size_t length) override;
	virtual bool close() override;
	virtual size_t size() const override;
	virtual bool is_asynchronous() const override;
	virtual bool good() const override;

  private:
	std::vector<uint8_t> contents;
};

void DeleteDatastore(BLTAbstractDataStore* datastore, int refcountId);
