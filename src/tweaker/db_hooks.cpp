//
// Created by ZNix on 23/11/2020.
//

#include "db_hooks.h"
#include "wrenloader.h"
#include "xmltweaker_internal.h"

#include <dbutil/DB.h>
#include <platform.h>
#include <util/util.h>

#include <assert.h>
#include <string.h>

#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>

using blt::db::DieselDB;
using blt::db::DslFile;
using pd2hook::tweaker::dbhook::DBTargetFile;

static const char* MODULE = "base/native/DB_001";

static void wrenRegisterAssetHook(WrenVM* vm);
static void wrenLoadAssetContents(WrenVM* vm);

void DBTargetFile::clear_sources()
{
	plain_file.reset();
	direct_bundle = blt::idfile();
	replacer = nullptr;

	if (wren_loader_obj)
	{
		wrenReleaseHandle(pd2hook::wren::get_wren_vm(), wren_loader_obj);
		wren_loader_obj = nullptr;
	}
}

class DBForeignFile
{
  public:
	// Magic cookie to make sure this is the correct class
	uint64_t magic;
	static const uint64_t MAGIC_COOKIE = 0xb4cb844461d94c07; // random value

	// Note: use unique_ptrs here for strings since our destructor won't be called, so otherwise they could leak
	std::unique_ptr<std::string> filename;
	blt::idfile asset;
	std::unique_ptr<std::string> stringLiteral;

	static void ofFile(WrenVM* vm);
	static void ofAsset(WrenVM* vm);
	static void fromString(WrenVM* vm);

	static void finalise(void* this_data);

  private:
	static DBForeignFile* create(WrenVM* vm);
};

class DBAssetHook
{
  public:
	// Magic cookie to make sure this is the correct class
	uint64_t magic;
	static const uint64_t MAGIC_COOKIE = 0xee3f682656ceebdd; // random value

	static void getFallback(WrenVM* vm);
	static void setFallback(WrenVM* vm);
	static void getMode(WrenVM* vm);
	static void isEnabled(WrenVM* vm);
	static void disable(WrenVM* vm);
	static void getPlainFile(WrenVM* vm);
	static void setPlainFile(WrenVM* vm);
	static void getDirectBundle(WrenVM* vm);
	static void setDirectBundle(WrenVM* vm);
	static void getWrenLoader(WrenVM* vm);
	static void setWrenLoader(WrenVM* vm);

	std::shared_ptr<DBTargetFile> file;

	static void finalise(void* this_data);
};

static std::map<blt::idfile, std::shared_ptr<DBTargetFile>> overriddenFiles;

WrenForeignMethodFn pd2hook::tweaker::dbhook::bind_dbhook_method(WrenVM* vm, const char* module,
                                                                 const char* class_name_s, bool is_static,
                                                                 const char* signature_c)
{
	if (std::string(module) != MODULE)
		return nullptr;

	std::string class_name = class_name_s;
	std::string signature = signature_c;

	if (class_name == "DBManager" && is_static)
	{
		if (signature == "register_asset_hook(_,_)")
		{
			return wrenRegisterAssetHook;
		}
		else if (signature == "load_asset_contents(_,_)")
		{
			return wrenLoadAssetContents;
		}
	}
	else if (class_name == "DBAssetHook" && !is_static)
	{
		if (signature == "fallback")
			return &DBAssetHook::getFallback;
		else if (signature == "fallback=(_)")
			return &DBAssetHook::setFallback;
		else if (signature == "mode")
			return &DBAssetHook::getMode;
		else if (signature == "enabled")
			return &DBAssetHook::isEnabled;
		else if (signature == "disable()")
			return &DBAssetHook::disable;
		else if (signature == "plain_file")
			return &DBAssetHook::getPlainFile;
		else if (signature == "plain_file=(_)")
			return &DBAssetHook::setPlainFile;
		else if (signature == "direct_bundle")
			return &DBAssetHook::getDirectBundle;
		else if (signature == "set_direct_bundle(_,_)")
			return &DBAssetHook::setDirectBundle;
		else if (signature == "wren_loader")
			return &DBAssetHook::getWrenLoader;
		else if (signature == "wren_loader=(_)")
			return &DBAssetHook::setWrenLoader;
	}
	else if (class_name == "DBForeignFile" && is_static)
	{
		if (signature == "of_file(_)")
			return &DBForeignFile::ofFile;
		else if (signature == "of_asset(_,_)")
			return &DBForeignFile::ofAsset;
		else if (signature == "from_string(_)")
			return &DBForeignFile::fromString;
	}

	return nullptr;
}

WrenForeignClassMethods pd2hook::tweaker::dbhook::bind_dbhook_class([[maybe_unused]] WrenVM* vm, const char* module,
                                                                    const char* class_name)
{
	// Since we're using our own factory methods, this should never be called
	WrenForeignMethodFn fakeAllocate = [](WrenVM* vm) { abort(); };

	if (!strcmp(module, MODULE))
	{
		if (!strcmp(class_name, "DBAssetHook"))
		{
			WrenForeignClassMethods def;
			def.allocate = fakeAllocate;
			def.finalize = &DBAssetHook::finalise;
			return def;
		}
		else if (!strcmp(class_name, "DBForeignFile"))
		{
			WrenForeignClassMethods def;
			def.allocate = fakeAllocate;
			def.finalize = &DBForeignFile::finalise;
			return def;
		}
	}
	return {nullptr, nullptr};
}

static blt::idstring parseHash(std::string value)
{
	if (value.size() == 17 && value.at(0) == '@')
	{
		char* endPtr = nullptr;
		errno = 0;
		blt::idstring id = strtoull(value.c_str() + 1, &endPtr, 16);
		if (errno)
		{
			char buff[1024];
			memset(buff, 0, sizeof(buff));
			snprintf(buff, sizeof(buff) - 1, "[wren] Invalid hash value '%s': Parse errno %d %s", value.c_str(), errno,
			         strerror(errno));
			PD2HOOK_LOG_ERROR(buff);
			abort();
		}
		if (endPtr != value.c_str() + 17)
		{
			char buff[1024];
			memset(buff, 0, sizeof(buff));
			snprintf(buff, sizeof(buff) - 1, "[wren] Invalid hash value '%s': Not a valid ID literal", value.c_str());
			PD2HOOK_LOG_ERROR(buff);
			abort();
		}
		return id;
	}

	return blt::idstring_hash(value);
}

static void wrenRegisterAssetHook(WrenVM* vm)
{
	blt::idstring name = parseHash(wrenGetSlotString(vm, 1));
	blt::idstring ext = parseHash(wrenGetSlotString(vm, 2));

	blt::idfile file(name, ext);

	if (overriddenFiles.count(file))
	{
		const char* name_str = wrenGetSlotString(vm, 1);
		const char* ext_str = wrenGetSlotString(vm, 2);

		char buff[1024];
		memset(buff, 0, sizeof(buff));
		snprintf(buff, sizeof(buff) - 1, "[wren] Duplicate asset registration for %s.%s", name_str, ext_str);
		PD2HOOK_LOG_ERROR(buff);
		abort();
	}

	auto entry = std::make_shared<DBTargetFile>(file);
	overriddenFiles[file] = entry;

	wrenGetVariable(vm, MODULE, "DBAssetHook", 0);
	auto* hook = (DBAssetHook*)wrenSetSlotNewForeign(vm, 0, 0, sizeof(DBAssetHook));
	hook->file = entry;
	hook->magic = DBAssetHook::MAGIC_COOKIE;
}

static void wrenLoadAssetContents(WrenVM* vm)
{
	blt::idstring name = parseHash(wrenGetSlotString(vm, 1));
	blt::idstring ext = parseHash(wrenGetSlotString(vm, 2));

	DslFile* file = DieselDB::Instance()->Find(name, ext);

	if (file == nullptr)
	{
		wrenSetSlotNull(vm, 0);
		return;
	}

	if (!file->HasLength() || !file->Found())
	{
		wrenSetSlotString(vm, 0, "Failed to read bundle file, bundle or length not set? Please report to SBLT");
		wrenAbortFiber(vm, 0);
		return;
	}

	// Ahh yes, be sure to enable binary mode
	// Otherwise stream.read will fail on Windows with failbit set, and won't touch errno
	// I think I'm getting better at tracking this exact same problem down - this time, it only wasted a
	// few perfectly good hours of my time.
	std::ifstream stream(file->bundle->path, std::ios::binary);
	if (stream.fail())
	{
		std::string msg = "Failed to open bundle file containing the asset - " + file->bundle->path;
		wrenSetSlotString(vm, 0, msg.c_str());
		wrenAbortFiber(vm, 0);
		return;
	}

	// Make sure errno is clear before we do anything, so in some unlikely cornercase where an operation fails without
	// setting errno it doesn't have some leftover number.
	errno = 0;

	try
	{
		stream.exceptions(std::ios::failbit | std::ios::eofbit);

		std::vector<uint8_t> data = file->ReadContents(stream);

		wrenSetSlotBytes(vm, 0, (const char*)data.data(), data.size());
	}
	catch (const std::ios::failure& ex)
	{
#ifdef _WIN32
		char err_buff[128];
		strerror_s(err_buff, sizeof(err_buff), errno);
#else
		const char* err_buff = strerror(errno);
#endif
		std::string msg =
			std::string("Failed to read asset - IO error: ") + std::string(err_buff) + " " + std::string(ex.what());
		wrenSetSlotString(vm, 0, msg.c_str());
		wrenAbortFiber(vm, 0);
	}
}

bool pd2hook::tweaker::dbhook::hook_asset_load(const blt::idfile& asset_file, BLTAbstractDataStore** out_datastore,
                                               int64_t* out_pos, int64_t* out_len, std::string& out_name,
                                               bool fallback_mode)
{
	// First zero everything
	*out_datastore = nullptr;
	*out_pos = 0;
	*out_len = 0;

	auto filePtr = overriddenFiles.find(asset_file);

	// If the file isn't defined, we're not overriding anything
	if (filePtr == overriddenFiles.end())
		return false;

	DBTargetFile& target = *filePtr->second;

	// If this target is in fallback mode (it'll only load if the base game doesn't provide such a file), and
	// we haven't yet tried loading the base game's version of the file, then stop here.
	if (target.fallback && !fallback_mode)
	{
		return false;
	}

	// TODO write to out_name somewhere

	// Define these loading functions here, as we can use them either directly or after calling Wren
	auto load_file = [&](const std::string& filename)
	{
		BLTFileDataStore* ds = BLTFileDataStore::Open(filename);

		if (!ds)
		{
			char buff[1024];
			memset(buff, 0, sizeof(buff));
			snprintf(buff, sizeof(buff) - 1, "Failed to open hooked file '%s' while loading " IDPFP, filename.c_str(),
			         asset_file.name, asset_file.ext);
			PD2HOOK_LOG_ERROR(buff);

#ifdef _WIN32
			MessageBox(nullptr, "Failed to load hooked file - file not found. See log for more information.",
			           "Wren Error", MB_OK);
			ExitProcess(1);
#else
			abort();
#endif
		}

		*out_datastore = ds;
		*out_len = ds->size();
	};

	auto load_bundle_item = [&](blt::idfile bundle_item)
	{
		DslFile* file = DieselDB::Instance()->Find(bundle_item.name, bundle_item.ext);

		// Abort if the file isn't found - most likely this would lead to a crash anyway since the PD2 version
		// of this asset probably isn't loaded (otherwise why would you hook it?), this just makes it obvious
		// what happened.
		if (!file)
		{
			char buff[1024];
			memset(buff, 0, sizeof(buff));
			snprintf(buff, sizeof(buff) - 1, "Failed to open hooked asset file " IDPFP " while loading " IDPFP,
			         bundle_item.name, bundle_item.ext, asset_file.name, asset_file.ext);
			PD2HOOK_LOG_ERROR(buff);

#ifdef _WIN32
			MessageBox(nullptr, "Failed to load hooked asset - file not found. See log for more information.",
			           "Wren Error", MB_OK);
			ExitProcess(1);
#else
			abort();
#endif
		}

		BLTAbstractDataStore* ds = DieselDB::Instance()->Open(file->bundle);
		*out_datastore = ds;
		*out_pos = file->offset;

		// If this is an end-of-file asset we have to find it's length
		if (file->HasLength())
			*out_len = file->length;
		else
			*out_len = ds->size() - file->offset;
	};

	if (target.HasPlainFile())
	{
		load_file(target.plain_file.value());
	}
	else if (target.HasDirectBundle())
	{
		load_bundle_item(target.direct_bundle);
	}
	else if (target.HasWrenLoader())
	{
		auto lock = pd2hook::wren::lock_wren_vm();
		WrenVM* vm = pd2hook::wren::get_wren_vm();

		// Probably not ideal to have it as a static, but hey it works fine and we only ever make one Wren context
		static WrenHandle* callHandle = wrenMakeCallHandle(vm, "load_file(_,_)");

		char hex[17]; // 16-chars long +1 for the null
		memset(hex, 0, sizeof(hex));

		wrenEnsureSlots(vm, 3);
		wrenSetSlotHandle(vm, 0, target.wren_loader_obj);

		// Set the name
		snprintf(hex, sizeof(hex), IDPF, asset_file.name);
		wrenSetSlotString(vm, 1, hex);

		// Set the extension
		snprintf(hex, sizeof(hex), IDPF, asset_file.ext);
		wrenSetSlotString(vm, 2, hex);

		// Invoke it - if it fails the game is very likely going to crash anyway, so make it descriptive now
		WrenInterpretResult result = wrenCall(vm, callHandle);
		if (result == WREN_RESULT_COMPILE_ERROR || result == WREN_RESULT_RUNTIME_ERROR)
		{
			char buff[1024];
			memset(buff, 0, sizeof(buff));
			snprintf(buff, sizeof(buff) - 1, "Wren asset load failed for " IDPFP ": compile or runtime error!",
			         asset_file.name, asset_file.ext);
			PD2HOOK_LOG_ERROR(buff);

#ifdef _WIN32
			MessageBox(nullptr, "Failed to load Wren-based asset - see the log for details", "Wren Error", MB_OK);
			ExitProcess(1);
#else
			abort();
#endif
		}

		// Get the wrapper, and make sure it's valid
		auto* ff = (DBForeignFile*)wrenGetSlotForeign(vm, 0);
		if (!ff || ff->magic != DBForeignFile::MAGIC_COOKIE)
		{
			char buff[1024];
			memset(buff, 0, sizeof(buff));
			snprintf(buff, sizeof(buff) - 1,
			         "Wren load_file function return invalid class or null - for asset " IDPFP " ptr %p",
			         asset_file.name, asset_file.ext, ff);
			PD2HOOK_LOG_ERROR(buff);
#ifdef _WIN32
			MessageBox(nullptr, "Failed to load Wren-based asset - see the log for details", "Wren Error", MB_OK);
			ExitProcess(1);
#else
			abort();
#endif
		}

		// Now load it's value
		if (ff->filename)
		{
			load_file(*ff->filename);
		}
		else if (!ff->asset.is_empty())
		{
			load_bundle_item(ff->asset);
		}
		else if (ff->stringLiteral)
		{
			auto* ds = new BLTStringDataStore(*ff->stringLiteral);
			*out_datastore = ds;
			*out_len = ds->size();
		}
		else
		{
			// Should never happen
			PD2HOOK_LOG_ERROR("No output contents set for DBForeignFile");
#ifdef _WIN32
			MessageBox(nullptr, "Failed to load Wren-based asset - see the log for details", "Wren Error", MB_OK);
			ExitProcess(1);
#else
			abort();
#endif
		}
	}
	else if (target.HasReplacer())
	{
		pd2hook::tweaker::dbhook::FileData* fd = new pd2hook::tweaker::dbhook::FileData{
			nullptr,
			0,
			target.id.name,
			target.id.ext,
		};

		target.replacer(fd);

		auto* ds = new BLTStringDataStore(std::string((char*)fd->data, fd->size));

		*out_datastore = ds;
		*out_len = ds->size();
	}
	else
	{
		// File is disabled, use the regular version of the asset
		return false;
	}

	return true;
}

// for native plugin stuff
void pd2hook::tweaker::dbhook::register_asset_hook(blt::idstring name, blt::idstring ext, bool fallback,
                                                   DBTargetFile** out_target)
{
	blt::idfile file(name, ext);

	if (overriddenFiles.count(file))
	{
		char buff[1024];
		memset(buff, 0, sizeof(buff));
		snprintf(buff, sizeof(buff) - 1, "Duplicate asset registration for " IDPFP, name, ext);
		PD2HOOK_LOG_WARN(buff);
	}

	auto entry = std::make_shared<DBTargetFile>(file);
	overriddenFiles[file] = entry;
	*out_target = entry.get();

	char buff[1024];
	memset(buff, 0, sizeof(buff));
	snprintf(buff, sizeof(buff) - 1, "Registerd asset hook for " IDPFP, name, ext);
	PD2HOOK_LOG_LOG(buff);
}

bool pd2hook::tweaker::dbhook::file_exists(blt::idstring name, blt::idstring ext)
{
	DslFile* file = DieselDB::Instance()->Find(name, ext);

	return file != nullptr;
}

pd2hook::tweaker::dbhook::FileData pd2hook::tweaker::dbhook::find_file(blt::idstring name, blt::idstring ext)
{
	DslFile* file = DieselDB::Instance()->Find(name, ext);

	if (!file)
	{
		char buff[1024];
		memset(buff, 0, sizeof(buff));
		snprintf(buff, sizeof(buff) - 1, "Failed to find asset " IDPFP, name, ext);
		PD2HOOK_LOG_ERROR(buff);

#ifdef _WIN32
		MessageBox(nullptr, "Failed to find asset - file not found. See log for more information.",
		           "Native Plugin Error", MB_OK);
		ExitProcess(1);
#else
		abort();
#endif
	}

	std::ifstream stream(file->bundle->path, std::ios::binary);

	if (stream.fail())
	{
		std::string message = "Failed to open bundle file containing the asset - " + file->bundle->path;
		PD2HOOK_LOG_ERROR(message.c_str());

#ifdef _WIN32
		MessageBox(nullptr, "Failed to open bundle file containing the asset. See log for more information.",
		           "Native Plugin Error", MB_OK);
		ExitProcess(1);
#else
		abort();
#endif
	}

	std::vector<uint8_t> data = file->ReadContents(stream);

	uint8_t* outData = new uint8_t[data.size()];
	memcpy(outData, data.data(), data.size());

	stream.close();

	pd2hook::tweaker::dbhook::FileData fd = {
		outData,
		data.size(),
		name,
		ext,
	};

	return fd;
}

//////////////////////////////////////
//// DBTargetFile implementation /////
//////////////////////////////////////

void DBTargetFile::SetReplacer(db_file_replacer_t replacer)
{
	this->replacer = replacer;
}
void DBTargetFile::SetFallback(bool fallback)
{
	this->fallback = fallback;
}

void DBTargetFile::SetPlainFile(std::string path)
{
	clear_sources();
	this->plain_file = path;
}

void DBTargetFile::SetDirectBundle(blt::idfile bundle)
{
	clear_sources();
	this->direct_bundle = bundle;
}

bool DBTargetFile::HasReplacer()
{
	return this->replacer != nullptr;
}

bool DBTargetFile::HasPlainFile()
{
	return this->plain_file.has_value();
}

bool DBTargetFile::HasDirectBundle()
{
	return !this->direct_bundle.is_empty();
}

bool DBTargetFile::HasWrenLoader()
{
	return this->wren_loader_obj != nullptr;
}

//////////////////////////////////////
//// Foreign file implementation /////
//////////////////////////////////////

void DBForeignFile::ofFile(WrenVM* vm)
{
	std::unique_ptr<std::string> filename = std::make_unique<std::string>(wrenGetSlotString(vm, 1));
	create(vm)->filename = std::move(filename);
}

void DBForeignFile::ofAsset(WrenVM* vm)
{
	blt::idstring name = parseHash(wrenGetSlotString(vm, 1));
	blt::idstring ext = parseHash(wrenGetSlotString(vm, 2));
	create(vm)->asset = blt::idfile(name, ext);
}

void DBForeignFile::fromString(WrenVM* vm)
{
	std::unique_ptr<std::string> contents = std::make_unique<std::string>(wrenGetSlotString(vm, 1));
	create(vm)->stringLiteral = std::move(contents);
}

DBForeignFile* DBForeignFile::create(WrenVM* vm)
{
	wrenGetVariable(vm, MODULE, "DBForeignFile", 0);
	auto* obj = (DBForeignFile*)wrenSetSlotNewForeign(vm, 0, 0, sizeof(DBForeignFile));
	obj->magic = DBForeignFile::MAGIC_COOKIE;
	return obj;
}

void DBForeignFile::finalise(void* this_data)
{
	auto* file = (DBForeignFile*)this_data;
	file->filename.reset();
	file->stringLiteral.reset();
}

//////////////////////////////////////
///// Asset hook implementation //////
//////////////////////////////////////

static DBTargetFile* get_this(WrenVM* vm)
{
	auto* it = (DBAssetHook*)wrenGetSlotForeign(vm, 0);
	return it->file.get();
}

void DBAssetHook::getFallback(WrenVM* vm)
{
	auto* it = get_this(vm);
	wrenEnsureSlots(vm, 1);
	wrenSetSlotBool(vm, 0, it->fallback);
}

void DBAssetHook::setFallback(WrenVM* vm)
{
	auto* it = get_this(vm);
	it->fallback = wrenGetSlotBool(vm, 1);
}

void DBAssetHook::getMode(WrenVM* vm)
{
	auto* it = get_this(vm);
	const char* str;

	// See the documentation on the Wren version of this method for the definitive list of strings
	if (it->plain_file)
		str = "plain_file";
	else if (!it->direct_bundle.is_empty())
		str = "direct_bundle";
	else if (it->wren_loader_obj)
		str = "wren_loader";
	else
		str = "disabled";

	wrenSetSlotString(vm, 0, str);
}

void DBAssetHook::isEnabled(WrenVM* vm)
{
	auto* it = get_this(vm);
	wrenSetSlotBool(vm, 0, it->plain_file->empty() && it->direct_bundle.is_empty() && !it->wren_loader_obj);
}

void DBAssetHook::disable(WrenVM* vm)
{
	auto* it = get_this(vm);
	it->clear_sources();
}

void DBAssetHook::getPlainFile(WrenVM* vm)
{
	auto* it = get_this(vm);
	if (it->plain_file)
		wrenSetSlotString(vm, 0, it->plain_file->c_str());
	else
		wrenSetSlotNull(vm, 0);
}

void DBAssetHook::setPlainFile(WrenVM* vm)
{
	auto* it = get_this(vm);
	it->clear_sources();
	it->plain_file = std::string(wrenGetSlotString(vm, 1));
}

void DBAssetHook::getDirectBundle(WrenVM* vm)
{
	auto* it = get_this(vm);
	if (it->direct_bundle.is_empty())
	{
		wrenSetSlotNull(vm, 0);
		return;
	}

	char buff[128];
	memset(buff, 0, sizeof(buff));
	snprintf(buff, sizeof(buff) - 1, "@" IDPFP, it->direct_bundle.name, it->direct_bundle.ext);
	wrenSetSlotString(vm, 0, buff);
}

void DBAssetHook::setDirectBundle(WrenVM* vm)
{
	auto* it = get_this(vm);
	it->clear_sources();

	blt::idstring name = parseHash(wrenGetSlotString(vm, 1));
	blt::idstring ext = parseHash(wrenGetSlotString(vm, 2));

	it->direct_bundle = blt::idfile(name, ext);
}

void DBAssetHook::getWrenLoader(WrenVM* vm)
{
	auto* it = get_this(vm);
	if (it->wren_loader_obj == nullptr)
	{
		wrenSetSlotNull(vm, 0);
		return;
	}

	wrenSetSlotHandle(vm, 0, it->wren_loader_obj);
}

void DBAssetHook::setWrenLoader(WrenVM* vm)
{
	auto* it = get_this(vm);
	it->clear_sources();

	it->wren_loader_obj = wrenGetSlotHandle(vm, 1);
}

void DBAssetHook::finalise(void* this_data)
{
	auto* this_ptr = (DBAssetHook*)this_data;
	this_ptr->file.reset();
}
