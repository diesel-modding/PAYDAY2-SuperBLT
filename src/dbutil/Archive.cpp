//
// Created by Campbell on 7/07/2026.
//

#include "Archive.h"

#include "signatures/sigdef.h"

// This got inlined in the latest build, so we have to recreate it ourselves.
Archive* Archive::Constructor(Archive* archive, const std::string& name, BLTAbstractDataStore* datastore, int64_t pos,
                              int64_t len, bool probablyNotLoadedFlag)
{

	memset(&archive->name, 0, sizeof(Archive::name));

	archive->position = pos;
	archive->length = len;
	archive->probablyReadCounter = 0i64;
	archive->probablyNotLoadedFlag = probablyNotLoadedFlag;
	archive->maybeCompressedSize = 0i64;

#pragma warning(suppress : 6031) // Complains about not using the return data.
	InitializeCriticalSectionAndSpinCount(&archive->lock, 4000);

	archive->datastore = datastore;
	archive->datastoreRefCountId = -1;
	if (datastore)
		archive->datastoreRefCountId = AllocateRefCountId();

	return archive;
}
