#include <thread>

#include "lua.h"
#include "threading/queue.h"
#include "util.h"

using namespace std;

struct HashInfo
{
	lua_State* L;
	int ref;
	string filename;
	raidhook::Util::DirectoryHashFunction hasher;
	raidhook::Util::HashResultReceiver callback;

	string result;
};

RAIDHOOK_REGISTER_EVENTQUEUE(HashInfo, HashResult)

class AsyncHashManager
{
  private:
	AsyncHashManager();

  public:
	~AsyncHashManager();

	static AsyncHashManager* GetSingleton();

	std::list<std::unique_ptr<std::thread>> threadList;
};

AsyncHashManager::AsyncHashManager()
{
}

AsyncHashManager::~AsyncHashManager()
{
	for_each(threadList.begin(), threadList.end(), [](const unique_ptr<thread>& t) { t->join(); });
}

AsyncHashManager* AsyncHashManager::GetSingleton()
{
	static AsyncHashManager instance;
	return &instance;
}

static void done(HashInfo info)
{
	info.callback(info.L, info.ref, info.filename, info.result);
}

static void run_async(HashInfo info)
{
	info.result = info.hasher(info.filename);

	GetHashResultQueue().AddToQueue(done, info);
}

void raidhook::Util::RunAsyncHash(lua_State* L, int ref, string filename, DirectoryHashFunction hasher,
                                  HashResultReceiver callback)
{
	HashInfo info;

	info.L = L;
	info.ref = ref;
	info.filename = filename;
	info.hasher = hasher;
	info.callback = callback;

	info.result = "<ERR:NOTSET>";

	unique_ptr<thread> ptr = unique_ptr<thread>(new thread(run_async, info));
	AsyncHashManager::GetSingleton()->threadList.push_back(move(ptr));
}
