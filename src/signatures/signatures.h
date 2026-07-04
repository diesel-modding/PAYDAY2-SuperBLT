#ifndef __SIGNATURE_HEADER__
#define __SIGNATURE_HEADER__

#include <string>
#include <vector>

struct SignatureF
{
	const char* funcname;
	const char* signature;
	const char* mask;
	int offset;
	void* address;
};

class SignatureSearch
{
  public:
	SignatureSearch(const char* funcname, void* address, const char* signature, const char* mask, int offset);
	static bool Search();
	static void* GetFunctionByName(const char* name);
};

#endif // __SIGNATURE_HEADER__