#include "Forum.h"

#include <chrono>
#include <cstdio>
#include <filesystem>

#include "Sort.h"
#include "StringSearch.h"

namespace
{
	std::time_t now() { return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()); }

	bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

	std::string trim(const std::string& s)
	{
		std::size_t a = 0, b = s.size();
		while (a < b && isSpace(s[a]))
			a++;
		while (b > a && isSpace(s[b - 1]))
			b--;
		return s.substr(a, b - a);
	}

	/* Single-line field (username, title): trimmed, 1..maxLen bytes, no control characters.
	   Rejecting control characters also keeps terminal escape sequences out of the data. */
	bool cleanLine(std::string& s, std::size_t maxLen)
	{
		s = trim(s);
		if (s.empty() || s.size() > maxLen)
			return false;
		for (unsigned char c : s)
			if (c < 0x20 || c == 0x7f)
				return false;
		return true;
	}

	/* Multi-line text (posts, replies): like cleanLine, but newlines and tabs are fine */
	bool cleanBlock(std::string& s, std::size_t maxLen)
	{
		std::string withoutCr;
		for (char c : s)
			if (c != '\r')
				withoutCr += c;
		s = trim(withoutCr);
		if (s.empty() || s.size() > maxLen)
			return false;
		for (unsigned char c : s)
			if ((c < 0x20 && c != '\n' && c != '\t') || c == 0x7f)
				return false;
		return true;
	}

	/* Description may be empty */
	bool cleanOptionalBlock(std::string& s, std::size_t maxLen)
	{
		if (trim(s).empty())
		{
			s.clear();
			return true;
		}
		return cleanBlock(s, maxLen);
	}
}

std::unique_ptr<Forum> Forum::open(const std::string& dataDir, std::string& error)
{
	std::error_code ec;
	std::filesystem::create_directories(dataDir, ec);
	if (ec)
	{
		error = "cannot create data directory '" + dataDir + "': " + ec.message();
		return nullptr;
	}

	std::unique_ptr<Forum> forum(new Forum());
	forum->path_ = dataDir + "/forum.db";

	if (!forum->lock_.acquire(dataDir))
	{
		error = "data directory '" + dataDir + "' is in use by another forum process";
		return nullptr;
	}

	storage::Snapshot snap;
	int skipped = 0;
	switch (storage::load(forum->path_, snap, skipped))
	{
	case storage::LoadResult::Missing:
		break;
	case storage::LoadResult::Corrupt:
		error = "'" + forum->path_ + "' is not a forum data file this version understands; refusing to overwrite it";
		return nullptr;
	case storage::LoadResult::Ok:
		if (skipped > 0)
		{
			/* The next save drops the unreadable lines, so keep a copy of what was there */
			std::filesystem::copy_file(forum->path_, forum->path_ + ".bak",
				std::filesystem::copy_options::overwrite_existing, ec);
			std::fprintf(stderr, "warning: skipped %d unreadable line(s) in %s (original kept as %s.bak)\n",
				skipped, forum->path_.c_str(), forum->path_.c_str());
		}
		break;
	}

	forum->nextId_ = snap.nextId;
	forum->cliSort_ = (snap.cliSortMode >= 0 && snap.cliSortMode <= 2) ? static_cast<SortMode>(snap.cliSortMode) : SortMode::None;
	for (Account& acc : snap.accounts)
	{
		std::string key = toLowerAscii(acc.username);
		forum->accounts_.insertOrAssign(key, std::move(acc));
	}
	forum->topics_ = std::move(snap.topics);
	return forum;
}

Status Forum::persist()
{
	/* Sorted by name so the file is stable from one save to the next */
	Vector<Account> accounts;
	for (auto entry : accounts_)
		accounts.push(entry.value);
	mergeSort(accounts, [](const Account& a, const Account& b) { return a.username < b.username; });

	return storage::save(path_, nextId_, static_cast<int>(cliSort_), accounts, topics_) ? Status::Ok : Status::Internal;
}

/* ---- accounts ---- */

Status Forum::registerUser(const std::string& username, const std::string& password, std::string& canonical)
{
	std::string name = username;
	if (!cleanLine(name, kMaxUsername) || password.size() < kMinPassword || password.size() > kMaxPassword)
		return Status::Invalid;

	std::string key = toLowerAscii(name);
	if (accounts_.contains(key))
		return Status::Conflict;

	accounts_.insertOrAssign(key, Account::create(name, password));
	canonical = name;
	return persist();
}

Status Forum::login(const std::string& username, const std::string& password, std::string& canonical)
{
	const Account* acc = accounts_.find(toLowerAscii(trim(username)));
	if (!acc)
	{
		/* Do the same amount of hashing as for a real account, so response time
		   does not reveal which usernames exist */
		Account::create("", password);
		return Status::Unauthorized;
	}
	if (!acc->checkPassword(password))
		return Status::Unauthorized;
	canonical = acc->username;
	return Status::Ok;
}

Status Forum::changePassword(const std::string& user, const std::string& oldPassword, const std::string& newPassword)
{
	Account* acc = accounts_.find(toLowerAscii(user));
	if (!acc)
		return Status::NotFound;
	if (!acc->checkPassword(oldPassword))
		return Status::Unauthorized;
	if (newPassword.size() < kMinPassword || newPassword.size() > kMaxPassword)
		return Status::Invalid;

	acc->setPassword(newPassword);
	return persist();
}

/* ---- lookup ---- */

int Forum::indexOfTopic(int topicId) const
{
	for (int i = 0; i < topics_.count(); i++)
		if (topics_[i].id == topicId)
			return i;
	return -1;
}

Topic* Forum::mutableTopic(int topicId)
{
	int i = indexOfTopic(topicId);
	return i < 0 ? nullptr : &topics_[i];
}

Post* Forum::mutablePost(int topicId, int postId)
{
	Topic* topic = mutableTopic(topicId);
	if (!topic)
		return nullptr;
	for (Post& post : topic->posts)
		if (post.id == postId)
			return &post;
	return nullptr;
}

const Topic* Forum::findTopic(int topicId) const
{
	int i = indexOfTopic(topicId);
	return i < 0 ? nullptr : &topics_[i];
}

const Post* Forum::findPost(int topicId, int postId) const
{
	return const_cast<Forum*>(this)->mutablePost(topicId, postId);
}

Vector<int> Forum::topicOrder(SortMode mode) const
{
	Vector<int> order;
	for (int i = 0; i < topics_.count(); i++)
		order.push(i);

	if (mode == SortMode::Time)
	{
		mergeSort(order, [this](int a, int b) {
			const Topic& x = topics_[a];
			const Topic& y = topics_[b];
			return x.created != y.created ? x.created > y.created : x.id > y.id;
		});
	}
	else if (mode == SortMode::Alpha)
	{
		mergeSort(order, [this](int a, int b) {
			return toLowerAscii(topics_[a].title()) < toLowerAscii(topics_[b].title());
		});
	}
	return order;
}

/* ---- topics, posts, replies ---- */

Status Forum::createTopic(const std::string& user, const std::string& title, const std::string& description, int& newId)
{
	std::string t = title, d = description;
	if (!cleanLine(t, kMaxTitle) || !cleanOptionalBlock(d, kMaxDescription))
		return Status::Invalid;

	std::string key = toLowerAscii(t);
	for (const Topic& existing : topics_)
		if (toLowerAscii(existing.title()) == key)
			return Status::Conflict;

	newId = nextId_++;
	topics_.push(Topic(newId, user, t, d, now()));
	return persist();
}

Status Forum::addPost(const std::string& user, int topicId, const std::string& text, int& newId)
{
	Topic* topic = mutableTopic(topicId);
	if (!topic)
		return Status::NotFound;

	std::string t = text;
	if (!cleanBlock(t, kMaxText))
		return Status::Invalid;

	/* Same rule as the original forum: no two posts in a topic with identical text */
	for (const Post& existing : topic->posts)
		if (existing.text == t)
			return Status::Conflict;

	newId = nextId_++;
	topic->posts.push(Post(newId, user, t, now()));
	return persist();
}

Status Forum::editPost(const std::string& user, int topicId, int postId, const std::string& text)
{
	Topic* topic = mutableTopic(topicId);
	Post* post = mutablePost(topicId, postId);
	if (!topic || !post)
		return Status::NotFound;
	if (post->author != user)
		return Status::Forbidden;

	std::string t = text;
	if (!cleanBlock(t, kMaxText))
		return Status::Invalid;
	if (t == post->text)
		return Status::Ok;

	for (const Post& other : topic->posts)
		if (other.id != postId && other.text == t)
			return Status::Conflict;

	post->text = t;
	post->edited = true;
	return persist();
}

Status Forum::deletePost(const std::string& user, int topicId, int postId)
{
	Topic* topic = mutableTopic(topicId);
	if (!topic)
		return Status::NotFound;

	for (int i = 0; i < topic->posts.count(); i++)
	{
		if (topic->posts[i].id != postId)
			continue;
		if (topic->posts[i].author != user)
			return Status::Forbidden;
		topic->posts.removeAt(i);
		return persist();
	}
	return Status::NotFound;
}

Status Forum::addReply(const std::string& user, int topicId, int postId, const std::string& text, int& newId)
{
	Post* post = mutablePost(topicId, postId);
	if (!post)
		return Status::NotFound;

	std::string t = text;
	if (!cleanBlock(t, kMaxText))
		return Status::Invalid;

	newId = nextId_++;
	post->replies.push(Comment(newId, user, t, now()));
	return persist();
}

/* ---- likes ---- */

Status Forum::likeTopic(const std::string& user, int topicId, bool& liked)
{
	Topic* topic = mutableTopic(topicId);
	if (!topic)
		return Status::NotFound;
	liked = topic->toggleLike(user);
	return persist();
}

Status Forum::likePost(const std::string& user, int topicId, int postId, bool& liked)
{
	Post* post = mutablePost(topicId, postId);
	if (!post)
		return Status::NotFound;
	liked = post->toggleLike(user);
	return persist();
}

Status Forum::likeReply(const std::string& user, int topicId, int postId, int replyId, bool& liked)
{
	Post* post = mutablePost(topicId, postId);
	if (!post)
		return Status::NotFound;
	for (Comment& reply : post->replies)
	{
		if (reply.id == replyId)
		{
			liked = reply.toggleLike(user);
			return persist();
		}
	}
	return Status::NotFound;
}

/* ---- search ---- */

Vector<SearchHit> Forum::search(const std::string& query) const
{
	Vector<SearchHit> hits;
	std::string q = trim(query);
	if (q.empty())
		return hits;

	for (const Topic& topic : topics_)
	{
		if (containsIgnoreCase(topic.title(), q) || containsIgnoreCase(topic.description, q))
		{
			SearchHit hit;
			hit.topicId = topic.id;
			hit.topicTitle = topic.title();
			hit.text = topic.description;
			hits.push(std::move(hit));
		}
		for (const Post& post : topic.posts)
		{
			if (containsIgnoreCase(post.text, q))
			{
				SearchHit hit;
				hit.topicId = topic.id;
				hit.postId = post.id;
				hit.topicTitle = topic.title();
				hit.text = post.text;
				hits.push(std::move(hit));
			}
		}
	}
	return hits;
}

Status Forum::setCliSortMode(SortMode mode)
{
	cliSort_ = mode;
	return persist();
}
