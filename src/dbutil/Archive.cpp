//
// Created by Campbell on 7/07/2026.
//

#include "Archive.h"

#include "signatures/sigdef.h"

// This got inlined in the latest build, so we have to recreate it ourselves.
Archive* Archive::Constructor(Archive* archive, const std::string& name, BLTAbstractDataStore* datastore, int64_t pos,
                              int64_t len, bool probablyNotLoadedFlag)
{
	archive->name.clear();

	archive->position = pos;
	archive->length = len;
	archive->probablyReadCounter = 0;
	archive->probablyNotLoadedFlag = probablyNotLoadedFlag;
	archive->maybeCompressedSize = 0;

#pragma warning(suppress : 6031) // Complains about not using the return data.
	InitializeCriticalSectionAndSpinCount(&archive->lock, 4000);

	archive->datastore = datastore;
	archive->datastoreRefCountId = -1;
	if (datastore)
		archive->datastoreRefCountId = AllocateRefCountId();

	return archive;
}
