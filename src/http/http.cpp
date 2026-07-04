#include "http/http.h"
#include "threading/queue.h"
#include "util/util.h"
#include <curl/curl.h>

#include <algorithm>
#include <thread>

namespace raidhook
{
	namespace
	{
		struct InitHttpManager
		{
			InitHttpManager()
			{
				HTTPManager::GetSingleton();
			}
		};

		struct HTTPProgressNotification
		{
			HTTPItem* ourItem;
			long byteProgress;
			long byteTotal;
		};
	} // namespace

	using HTTPProgressNotificationPtr = std::unique_ptr<HTTPProgressNotification>;
	using HTTPItemPtr = std::unique_ptr<HTTPItem>;
	RAIDHOOK_REGISTER_EVENTQUEUE(HTTPProgressNotificationPtr, HTTPProgressNotification)
	RAIDHOOK_REGISTER_EVENTQUEUE(HTTPItemPtr, HTTPItem)

	HTTPManager::HTTPManager()
	{
		// Curl Init
		curl_global_init(CURL_GLOBAL_ALL);
		RAIDHOOK_LOG_LOG("CURL_INITD");
	}

	HTTPManager::~HTTPManager()
	{
		RAIDHOOK_LOG_LOG("CURL CLOSED");
		curl_global_cleanup();

		std::for_each(threadList.begin(), threadList.end(), [](const std::unique_ptr<std::thread>& t) { t->join(); });
	}

	HTTPManager* HTTPManager::GetSingleton()
	{
		static HTTPManager httpSingleton;
		return &httpSingleton;
	}

	size_t write_http_header(char* ptr, size_t size, size_t nmemb, void* data)
	{
		HTTPItem* mainItem = (HTTPItem*)data;
		size_t headerLineSize = size * nmemb;
		std::string headerLine = std::string(ptr, headerLineSize);
		size_t delimiterPosition = headerLine.find(": ");
		if (delimiterPosition != std::string::npos)
		{
			std::string headerKey = headerLine.substr(0, delimiterPosition);
			std::string headerValue = headerLine.substr(delimiterPosition + 2);
			size_t valLen = headerValue.length();
			if (valLen > 2 && headerValue.at(valLen - 1) == '\n' && headerValue.at(valLen - 2) == '\r')
			{
				headerValue = headerValue.substr(0, valLen - 2);
			}
			mainItem->responseHeaders.insert({headerKey, headerValue});
		}
		return headerLineSize;
	}

	size_t write_http_data(char* ptr, size_t size, size_t nmemb, void* data)
	{
		std::string newData = std::string(ptr, size * nmemb);
		HTTPItem* mainItem = (HTTPItem*)data;
		mainItem->httpContents += newData;
		return size * nmemb;
	}

	void run_http_progress_event(std::unique_ptr<HTTPProgressNotification> ourNotify)
	{
		RAIDHOOK_TRACE_FUNC;
		HTTPItem* ourItem = ourNotify->ourItem;
		ourItem->progress(ourItem->data, ourNotify->byteProgress, ourNotify->byteTotal);
	}

	int http_progress_call(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
	{
		RAIDHOOK_TRACE_FUNC;
		HTTPItem* ourItem = (HTTPItem*)clientp;
		if (!ourItem->progress)
			return 0;
		if (dltotal == 0 || dlnow == 0)
			return 0;
		if (dltotal == dlnow)
			return 0;
		if (ourItem->byteprogress >= dlnow)
			return 0;
		ourItem->byteprogress = static_cast<long>(dlnow);
		ourItem->bytetotal = static_cast<long>(dltotal);

		HTTPProgressNotificationPtr notify(new HTTPProgressNotification());
		notify->ourItem = ourItem;
		notify->byteProgress = static_cast<long>(dlnow);
		notify->byteTotal = static_cast<long>(dltotal);

		GetHTTPProgressNotificationQueue().AddToQueue(run_http_progress_event, std::move(notify));
		return 0;
	}

	void run_http_event(std::unique_ptr<HTTPItem> ourItem)
	{
		RAIDHOOK_TRACE_FUNC;
		ourItem->call(ourItem.get());
	}

	void launch_thread_http(HTTPItem* raw_item)
	{
		RAIDHOOK_TRACE_FUNC;
		std::unique_ptr<HTTPItem> item(raw_item);
		CURL* curl;
		curl = curl_easy_init();
		curl_easy_setopt(curl, CURLOPT_URL, item->url.c_str());
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 900L);
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1000L);

		curl_easy_setopt(curl, CURLOPT_USERAGENT, "SuperBLT");

		if (item->progress)
		{
			curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, http_progress_call);
			curl_easy_setopt(curl, CURLOPT_XFERINFODATA, item.get());
			curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
		}

		curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_http_header);
		curl_easy_setopt(curl, CURLOPT_HEADERDATA, item.get());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_http_data);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, item.get());

		item->errorCode = curl_easy_perform(curl);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &(item->httpStatusCode));
		curl_easy_cleanup(curl);

		GetHTTPItemQueue().AddToQueue(run_http_event, std::move(item));
	}

	void HTTPManager::LaunchHTTPRequest(std::unique_ptr<HTTPItem> callback)
	{
		RAIDHOOK_TRACE_FUNC;
		// This shit's gonna end eventually, how many threads are people going to launch?
		// Probably a lot.
		// I'll manage them I guess, but I've no idea when to tell them to join which I believe is part of the
		// constructor.

		// VC++ 2013 bug, can't pass a unique_ptr through a thread
		// should be: threadList.push_back(std::unique_ptr<std::thread>(new std::thread(launch_thread_http,
		// std::move(callback)));
		threadList.push_back(std::unique_ptr<std::thread>(new std::thread(launch_thread_http, callback.release())));
	}
} // namespace raidhook
