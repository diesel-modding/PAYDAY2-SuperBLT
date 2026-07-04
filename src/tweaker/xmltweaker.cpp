#include "global.h"
#include "util/util.h"
#include "xmltweaker_internal.h"
#include <fstream>
#include <set>
#include <stdio.h>
#include <string.h>
#include <unordered_set>

#include <wren.hpp>

using namespace std;
using namespace raidhook;
using namespace tweaker;
using blt::idfile;
using blt::idstring;

bool raidhook::tweaker::tweaker_enabled = true;

static unordered_set<char*> buffers;
static set<idfile> ignored_files;

// The file we last parsed. If we try to parse the same file more than
// once, nothing should happen as a file from the filesystem is being loaded.
idfile last_parsed;

char* tweaker::tweak_raid_xml(char* text, int text_length)
{
	if (!tweaker_enabled)
	{
		return text;
	}

	idfile file = idfile(*blt::platform::last_loaded_name, *blt::platform::last_loaded_ext);

	// Don't parse the same file more than once, as it's not actually the same file.
	if (last_parsed == file)
	{
		return text;
	}

	last_parsed = file;

	// Don't bother with .model or .texture files
	if (file.ext == 0xaf612bbc207e00bd || // idstring("model")
	    file.ext == 0x5368e150b05a5b8c // idstring("texture")
	)
	{
		return text;
	}

	// Check the exclusion list
	if (ignored_files.count(file))
	{
		return text;
	}

	const char* new_text = transform_file(text);

	// If the text is not to be altered, we can return it as is.
	if (new_text == text)
		return text;

	// Otherwise, copy it so it's not invalidated by another Wren call

	size_t length = strlen(new_text) + 1; // +1 for the null

	char* buffer = (char*)malloc(length);
	buffers.insert(buffer);

	portable_strncpy(buffer, new_text, length);

	// if (!strncmp(new_text, "<network>", 9)) {
	//	std::ofstream out("output.txt");
	//	out << new_text;
	//	out.close();
	// }

	/*static int counter = 0;
	if (counter++ == 1) {
	    printf("%s\n", buffer);

	    const int kMaxCallers = 62;
	    void* callers[kMaxCallers];
	    int count = CaptureStackBackTrace(0, kMaxCallers, callers, NULL);
	    for (int i = 0; i < count; i++)
	        printf("*** %d called from .text:%08X\n", i, callers[i]);
	    Sleep(20000);
	}*/

	return (char*)buffer;
}

void tweaker::free_tweaked_raid_xml(char* text)
{
	if (buffers.erase(text))
	{
		free(text);
	}
}

void raidhook::tweaker::ignore_file(idfile file)
{
	ignored_files.insert(file);
}
