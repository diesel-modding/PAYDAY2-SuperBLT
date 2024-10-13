#ifndef __SIGNATURE_HEADER__
#define __SIGNATURE_HEADER__

#include <string>
#include <vector>

enum SignatureVR
{
	SignatureVR_Both,
	SignatureVR_Desktop,
	SignatureVR_VR
};

struct SignatureF
{
	const char* funcname;
	const char* signature;
	const char* mask;
	int offset;
	void* address;
	SignatureVR vr;
};

class SignatureSearch
{
  public:
	SignatureSearch(const char* funcname, void* address, const char* signature, const char* mask, int offset,
	                SignatureVR vr);
	static void Search();
	static void* GetFunctionByName(const char* name);
};

#endif // __SIGNATURE_HEADER__
