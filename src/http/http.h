#ifndef __HTTP_HEADER__
#define __HTTP_HEADER__

#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace raidhook
{

	void download_blt();
	struct HTTPItem;

	typedef void (*HTTPCallback)(HTTPItem* httpItem);
	typedef void (*HTTPProgress)(void* data, long progress, long total);

	struct HTTPItem
	{
		HTTPCallback call = nullptr;
		HTTPProgress progress = nullptr;
		std::string url;
		std::string httpContents;
		std::map<std::string, std::string> responseHeaders;
		int errorCode;
		long httpStatusCode;
		void* data = nullptr;

		long byteprogress = 0;
		long bytetotal = 0;
	};

	class HTTPManager
	{
	  private:
		HTTPManager();

	  public:
		~HTTPManager();

		static HTTPManager* GetSingleton();

		void LaunchHTTPRequest(std::unique_ptr<HTTPItem> callback);

	  private:
		std::list<std::unique_ptr<std::thread>> threadList;
	};
} // namespace raidhook

#endif // __HTTP_HEADER__