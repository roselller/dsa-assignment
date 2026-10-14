#include <fstream>
#include <sstream>

#include "Storage.h"
#include "test.h"

static std::string slurp(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

static void spit(const std::string& path, const std::string& content)
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out << content;
}

int main()
{
	/* Escaping must round-trip every awkward string */
	for (const char* raw : { "", "plain", "a\tb", "line1\nline2", "back\\slash", "trailing\\", "\\n literal",
		"cr\r\n", "semi;colon/slash <script>alert(1)</script>", "\t\t\n\n\\\\" })
	{
		std::string s = raw;
		std::string escaped = storage::escape(s);
		CHECK(escaped.find('\t') == std::string::npos);
		CHECK(escaped.find('\n') == std::string::npos);
		CHECK(storage::unescape(escaped) == s);
	}

	TempDir dir;
	const std::string path = dir.path() + "/forum.db";

	/* Missing file and wrong header are distinguished */
	storage::Snapshot snap;
	int skipped = 0;
	CHECK(storage::load(path, snap, skipped) == storage::LoadResult::Missing);
	spit(path, "not a forum file\n");
	CHECK(storage::load(path, snap, skipped) == storage::LoadResult::Corrupt);

	/* Round trip of a populated forum with hostile text and awkward topic names */
	Vector<Account> accounts;
	accounts.push(Account::create("user1", "pw1"));
	accounts.push(Account::create("user;2", "pw2"));

	Vector<Topic> topics;
	for (const char* title : { "Topics", "Accounts", "a/b\\c", "forum.db" })
		topics.push(Topic(topics.count() + 1, "user1", title, "desc;with\ttab\nand newline", 1700000000));
	Post post(10, "user;2", "text; with / slash \\ and <script>alert(1)</script>\nsecond line", 1700000100);
	post.edited = true;
	post.toggleLike("user1");
	post.toggleLike("user;2");
	post.replies.push(Comment(11, "user1", "reply\twith tab", 1700000200));
	post.replies[0].toggleLike("user1");
	post.replies.push(Comment(12, "user1", "second reply", 1700000300));
	topics[0].posts.push(post);
	topics[0].posts.push(Post(13, "user1", "second post", 1700000400));
	topics[0].toggleLike("user1");

	CHECK(storage::save(path, 20, 2, accounts, topics));
	CHECK(!std::filesystem::exists(path + ".tmp"));

	storage::Snapshot back;
	CHECK(storage::load(path, back, skipped) == storage::LoadResult::Ok);
	CHECK(skipped == 0);
	CHECK(back.nextId == 20 && back.cliSortMode == 2);
	CHECK(back.accounts.count() == 2);
	CHECK(back.accounts[1].username == "user;2");
	CHECK(back.accounts[0].checkPassword("pw1") && !back.accounts[0].checkPassword("wrong"));
	CHECK(back.topics.count() == 4);
	CHECK(back.topics[0].title() == "Topics" && back.topics[1].title() == "Accounts");
	CHECK(back.topics[2].title() == "a/b\\c" && back.topics[3].title() == "forum.db");
	CHECK(back.topics[0].description == "desc;with\ttab\nand newline");
	CHECK(back.topics[0].likes() == 1 && back.topics[0].isLikedBy("user1"));
	CHECK(back.topics[0].created == 1700000000);
	CHECK(back.topics[0].posts.count() == 2);
	const Post& p = back.topics[0].posts[0];
	CHECK(p.id == 10 && p.author == "user;2" && p.edited);
	CHECK(p.text == "text; with / slash \\ and <script>alert(1)</script>\nsecond line");
	CHECK(p.likes() == 2 && p.isLikedBy("user;2"));
	CHECK(p.replies.count() == 2 && p.replies[0].text == "reply\twith tab" && p.replies[0].likes() == 1);
	CHECK(p.replies[1].text == "second reply" && p.replies[1].likes() == 0);
	CHECK(back.topics[0].posts[1].text == "second post" && !back.topics[0].posts[1].edited);
	CHECK(back.topics[1].posts.count() == 0);

	/* Saving again writes the identical bytes (the format is deterministic) */
	std::string first = slurp(path);
	CHECK(storage::save(path, 20, 2, back.accounts, back.topics));
	CHECK(slurp(path) == first);

	/* Malformed lines are skipped and counted; everything around them still loads */
	spit(path,
		"FORUM\t2\n"
		"META\t5\t0\n"
		"POST\t1\t100\tbob\t0\torphan post before any topic\n"
		"BOGUS\tline\n"
		"TOPIC\t7\t100\tbob\tgood topic\tdesc\n"
		"TOPIC\tnot-a-number\t100\tbob\tbad topic\tdesc\n"
		"POST\t8\t100\tbob\t0\tpost under the bad topic\n"
		"REPLY\t9\t100\tbob\treply under the bad topic\n"
		"\n"
		"TOPIC\t30\t100\tbob\tsecond topic\tdesc\n"
		"POST\t31\t100\tbob\t0\tfine post\n"
		"POST\t32\tNaN\tbob\t0\tbroken post\n"
		"REPLY\t33\t100\tbob\treply under the broken post\n"
		"POST\t34\t100\tbob\t7\tbad edited flag\n");
	storage::Snapshot messy;
	CHECK(storage::load(path, messy, skipped) == storage::LoadResult::Ok);
	CHECK(skipped == 8);
	CHECK(messy.topics.count() == 2);
	CHECK(messy.topics[0].title() == "good topic" && messy.topics[1].title() == "second topic");
	CHECK(messy.topics[1].posts.count() == 1 && messy.topics[1].posts[0].text == "fine post");
	/* META said 5, but id 31 is in use: new ids must start above every id seen */
	CHECK(messy.nextId > 31);

	/* Data lock: a second holder is refused until the first lets go */
	{
		DataLock a, b;
		CHECK(a.acquire(dir.path()));
		CHECK(!b.acquire(dir.path()));
	}
	DataLock c;
	CHECK(c.acquire(dir.path()));

	return testReport();
}
