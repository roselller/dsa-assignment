#pragma once

#include <memory>
#include <string>

#include "Account.h"
#include "HashMap.h"
#include "Storage.h"
#include "Topic.h"
#include "Vector.h"

enum class Status
{
	Ok,
	Invalid,       /* bad or missing input */
	NotFound,
	Forbidden,     /* signed in, but not allowed to do this */
	Conflict,      /* e.g. the name is already taken */
	Unauthorized,  /* wrong credentials */
	Internal       /* the change was made in memory but could not be saved */
};

enum class SortMode
{
	None = 0,   /* order of creation */
	Time = 1,   /* newest first */
	Alpha = 2   /* title, A to Z, ignoring case */
};

struct SearchHit
{
	int topicId = 0;
	int postId = 0;           /* 0 when the hit is the topic itself */
	std::string topicTitle;
	std::string text;         /* topic description, or post text */
};

/* The forum itself: accounts, topics, posts, replies and likes. No input or output
   happens here apart from saving, so both front-ends (terminal and web) share it.

   Every change is saved immediately. The caller passes the acting user's name as it
   was returned by login/registerUser, and all ownership checks use it.
   Not thread safe: callers serialise access themselves.
   Pointers returned by the find* functions are only valid until the next change. */
class Forum
{
public:
	static constexpr std::size_t kMaxUsername = 32;
	static constexpr std::size_t kMinPassword = 4;
	static constexpr std::size_t kMaxPassword = 128;
	static constexpr std::size_t kMaxTitle = 100;
	static constexpr std::size_t kMaxDescription = 500;
	static constexpr std::size_t kMaxText = 2000;

	/* Creates dataDir if needed, locks it, and loads forum.db. On failure returns
	   null and sets error. */
	static std::unique_ptr<Forum> open(const std::string& dataDir, std::string& error);

	/* Accounts. Usernames are unique ignoring case; canonical is set to the name as registered. */
	Status registerUser(const std::string& username, const std::string& password, std::string& canonical);
	Status login(const std::string& username, const std::string& password, std::string& canonical);
	Status changePassword(const std::string& user, const std::string& oldPassword, const std::string& newPassword);

	/* Topics */
	Status createTopic(const std::string& user, const std::string& title, const std::string& description, int& newId);
	const Vector<Topic>& topics() const { return topics_; }
	const Topic* findTopic(int topicId) const;
	/* Indices into topics() in the requested order. Does not change the stored order. */
	Vector<int> topicOrder(SortMode mode) const;

	/* Posts and replies */
	const Post* findPost(int topicId, int postId) const;
	Status addPost(const std::string& user, int topicId, const std::string& text, int& newId);
	/* Edits in place: replies, likes, id and creation time are kept. Author only. */
	Status editPost(const std::string& user, int topicId, int postId, const std::string& text);
	/* Author only */
	Status deletePost(const std::string& user, int topicId, int postId);
	Status addReply(const std::string& user, int topicId, int postId, const std::string& text, int& newId);

	/* Toggles the user's like; liked is set to whether they like it afterwards */
	Status likeTopic(const std::string& user, int topicId, bool& liked);
	Status likePost(const std::string& user, int topicId, int postId, bool& liked);
	Status likeReply(const std::string& user, int topicId, int postId, int replyId, bool& liked);

	/* Topics and posts containing query (ignoring case) */
	Vector<SearchHit> search(const std::string& query) const;

	/* The sort order the terminal client remembers between runs */
	SortMode cliSortMode() const { return cliSort_; }
	Status setCliSortMode(SortMode mode);

private:
	Forum() = default;

	Status persist();
	int indexOfTopic(int topicId) const;
	Topic* mutableTopic(int topicId);
	Post* mutablePost(int topicId, int postId);

	std::string path_;
	DataLock lock_;
	HashMap<std::string, Account> accounts_;  /* keyed by lower-cased username */
	Vector<Topic> topics_;
	int nextId_ = 1;
	SortMode cliSort_ = SortMode::None;
};
