#pragma once

#include <string>

#include "Account.h"
#include "Topic.h"
#include "Vector.h"

/* Everything the forum persists, in one file (forum.db): line-oriented, tab-separated,
   with tabs, newlines and backslashes inside fields escaped. User text therefore can
   never break the format, and no user-supplied text is ever used as a file name.

     FORUM<TAB>2
     META<TAB>nextId<TAB>cliSortMode
     ACCOUNT<TAB>username<TAB>salt<TAB>hash
     TOPIC<TAB>id<TAB>created<TAB>author<TAB>title<TAB>description[<TAB>liker...]
     POST<TAB>id<TAB>created<TAB>author<TAB>edited<TAB>text[<TAB>liker...]    (in the previous TOPIC)
     REPLY<TAB>id<TAB>created<TAB>author<TAB>text[<TAB>liker...]              (in the previous POST)
*/
namespace storage
{
	struct Snapshot
	{
		int nextId = 1;
		int cliSortMode = 0;
		Vector<Account> accounts;
		Vector<Topic> topics;
	};

	enum class LoadResult
	{
		Missing,  /* no file yet: start empty */
		Ok,
		Corrupt   /* not a file this version understands: do not overwrite it */
	};

	/* Reads path into out. skippedLines counts malformed lines that were ignored. */
	LoadResult load(const std::string& path, Snapshot& out, int& skippedLines);

	/* Writes a complete snapshot, atomically (temp file, then rename) */
	bool save(const std::string& path, int nextId, int cliSortMode,
		const Vector<Account>& accounts, const Vector<Topic>& topics);

	std::string escape(const std::string& field);
	std::string unescape(const std::string& field);
}

/* Exclusive lock on a data directory. The forum keeps all of its state in memory and
   rewrites the file, so two processes sharing a directory would overwrite each other. */
class DataLock
{
	int fd_ = -1;

public:
	DataLock() = default;
	DataLock(const DataLock&) = delete;
	DataLock& operator=(const DataLock&) = delete;
	~DataLock();

	/* Takes the lock for dir. Returns false if another process (or lock) holds it. */
	bool acquire(const std::string& dir);
};
