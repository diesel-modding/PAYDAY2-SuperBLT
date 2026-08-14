#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <stdint.h>

// All this is checked on Linux, verify that it's the same on Windows

class BLTAbstractDataStore
{
  public:
	virtual ~BLTAbstractDataStore()
	{
	}
	virtual size_t write(uint64_t position_in_file, uint8_t const* data, size_t length); // Stubbed with an abort
	virtual size_t read(uint64_t position_in_file, uint8_t* data, size_t length) = 0;
	virtual bool close() = 0;
	virtual size_t size() const = 0;
	virtual bool is_asynchronous() const = 0;
	virtual void set_asynchronous_completion_callback(void* /*dsl::LuaRef*/); // ignore this
	virtual uint64_t state(); // ignore this
	virtual bool good() const = 0;
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
