#include "DB.h"
#include "Archive.h"
#include "Datastore.h"

#include <subhook.h>

using namespace diesel;
#define HOOK_FLAG subhook::HookFlags::HookFlag64BitOffset

namespace diesel
{
	class Transport
	{
	public:
		virtual ~Transport() = 0;
		virtual Archive open(unsigned int key) = 0;
	};
}

DB* main_db = nullptr;

DB* DB::GetDB()
{
	return main_db;
}

bool DB::ContainsFile(blt::idstring type, blt::idstring name, blt::idstring property)
{
	unsigned int wanted_property = 0;

	for (auto& db_property : _data->_properties.data())
	{
		if (db_property.first == property)
		{
			wanted_property = db_property.second;
		}
	}

	
	DBExtKey upper_key{};
	upper_key.name = name;
	upper_key.type = type;
	upper_key.properties = wanted_property;
	DBExtKey lower_key{};
	lower_key.name = name;
	lower_key.type = type;
	lower_key.properties = wanted_property;

	unsigned int upper = (SortMap<DBExtKey, uint32_t>::VectorType*)dsl__SortMap__DBKey__upper_bound(&_data->_lookup, &upper_key) - _data->_lookup.data().begin();
	unsigned int lower = (SortMap<DBExtKey, uint32_t>::VectorType*)dsl__SortMap__DBKey__lower_bound(&_data->_lookup, &lower_key) - _data->_lookup.data().begin();

	return upper != lower;
}

bool DB::OpenFile(blt::idstring type, blt::idstring name, blt::idstring property, std::vector<uint8_t>& out_data)
{
	unsigned int wanted_property = 0;

	for (auto& db_property : _data->_properties.data())
	{
		if (db_property.first == property)
		{
			wanted_property = db_property.second;
		}
	}

	// To get all instances, with all properties
	/*
	DBExtKey upper_key{};
	upper_key.name = name;
	upper_key.type = type;
	upper_key.properties = -1;
	DBExtKey lower_key{};
	lower_key.name = name;
	lower_key.type = type;
	lower_key.properties = 0;

	for(int i = lower; i < upper; i++) {}

	*/

	DBExtKey upper_key{};
	upper_key.name = name;
	upper_key.type = type;
	upper_key.properties = wanted_property;
	DBExtKey lower_key{};
	lower_key.name = name;
	lower_key.type = type;
	lower_key.properties = wanted_property;


	unsigned int upper = (SortMap<DBExtKey, uint32_t>::VectorType*)dsl__SortMap__DBKey__upper_bound(&_data->_lookup, &upper_key) - _data->_lookup.data().begin();
	unsigned int lower = (SortMap<DBExtKey, uint32_t>::VectorType*)dsl__SortMap__DBKey__lower_bound(&_data->_lookup, &lower_key) - _data->_lookup.data().begin();

	if (upper == lower)
	{
		return false; // does not exist
	}

	diesel::Transport* db_transport = (diesel::Transport*)((uintptr_t)this + 344);



	Archive a = db_transport->open(_data->_lookup.data()[lower].second); // assume there is only one instance for this property/open the first

	out_data.resize(a.size());
	dsl__Archive__checked_read_raw(&a, (char*)out_data.data(), a.size());

	DeleteDatastore(a.datastore, a.datastoreRefCountId);

	return true;
}

subhook::Hook DB_ctor_hook;

void __fastcall dsl__DB__DB_h(diesel::DB* this_, void* fs, const PDString& root, const PDString& name)
{
	main_db = this_;

	subhook::ScopedHookRemove shr(&DB_ctor_hook);
	dsl__DB__DB(this_, fs, root, name);
}

void blt::InitDBHooks()
{
	DB_ctor_hook.Install(dsl__DB__DB, &dsl__DB__DB_h, HOOK_FLAG);
}
