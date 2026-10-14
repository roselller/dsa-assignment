#include <fstream>
#include <memory>

#include "Forum.h"
#include "test.h"

static std::unique_ptr<Forum> openForum(const std::string& dir)
{
	std::string error;
	std::unique_ptr<Forum> forum = Forum::open(dir, error);
	if (!forum)
		std::fprintf(stderr, "open failed: %s\n", error.c_str());
	return forum;
}

int main()
{
	TempDir dir;
	std::string name;
	std::string error;

	std::unique_ptr<Forum> f = openForum(dir.path());
	CHECK(f != nullptr);
	if (!f)
		return 1;

	/* ---- accounts ---- */

	/* The old hash gave these the same bucket and one overwrote the other */
	CHECK(f->registerUser("user1", "password1", name) == Status::Ok && name == "user1");
	CHECK(f->registerUser("user2", "password2", name) == Status::Ok);
	CHECK(f->login("user1", "password1", name) == Status::Ok && name == "user1");
	CHECK(f->login("user2", "password2", name) == Status::Ok && name == "user2");
	CHECK(f->login("user1", "password2", name) == Status::Unauthorized);
	CHECK(f->login("nobody", "password1", name) == Status::Unauthorized);

	/* Usernames are case-insensitive, and login returns the name as registered */
	CHECK(f->registerUser("USER1", "whatever1", name) == Status::Conflict);
	CHECK(f->login("USER1", "password1", name) == Status::Ok && name == "user1");

	CHECK(f->registerUser("", "password", name) == Status::Invalid);
	CHECK(f->registerUser("   ", "password", name) == Status::Invalid);
	CHECK(f->registerUser("ok", "abc", name) == Status::Invalid);  /* too short */
	CHECK(f->registerUser("bad\nname", "password", name) == Status::Invalid);
	CHECK(f->registerUser("esc\x1b[2J", "password", name) == Status::Invalid);
	CHECK(f->registerUser(std::string(33, 'x'), "password", name) == Status::Invalid);

	CHECK(f->changePassword("user1", "wrong", "newpass1") == Status::Unauthorized);
	CHECK(f->changePassword("user1", "password1", "abc") == Status::Invalid);
	CHECK(f->changePassword("user1", "password1", "newpass1") == Status::Ok);
	CHECK(f->login("user1", "password1", name) == Status::Unauthorized);
	CHECK(f->login("user1", "newpass1", name) == Status::Ok);

	/* ---- topics ---- */

	int t1 = 0, t2 = 0, t3 = 0, dummy = 0;
	CHECK(f->createTopic("user1", "Topics", "named like a data file", t1) == Status::Ok);
	CHECK(f->createTopic("user2", "banana", "", t2) == Status::Ok);
	CHECK(f->createTopic("user1", "Apple", "fruit", t3) == Status::Ok);
	CHECK(f->createTopic("user1", "TOPICS", "same title, other case", dummy) == Status::Conflict);
	CHECK(f->createTopic("user1", "", "no title", dummy) == Status::Invalid);
	CHECK(f->createTopic("user1", "two\nlines", "", dummy) == Status::Invalid);
	CHECK(f->createTopic("user1", std::string(101, 'x'), "", dummy) == Status::Invalid);
	CHECK(f->topics().count() == 3);

	/* Sorting returns an order and leaves the stored order alone */
	Vector<int> byTime = f->topicOrder(SortMode::Time);
	Vector<int> byAlpha = f->topicOrder(SortMode::Alpha);
	Vector<int> plain = f->topicOrder(SortMode::None);
	CHECK(plain[0] == 0 && plain[1] == 1 && plain[2] == 2);
	CHECK(f->topics()[byAlpha[0]].title() == "Apple" && f->topics()[byAlpha[1]].title() == "banana" &&
		f->topics()[byAlpha[2]].title() == "Topics");
	CHECK(f->topics()[byTime[0]].title() == "Apple");  /* created last, so newest first */
	CHECK(f->topics()[0].title() == "Topics" && f->topics()[1].title() == "banana" && f->topics()[2].title() == "Apple");

	/* ---- posts, replies, likes ---- */

	const std::string nasty = "semi; slash/ back\\ <script>alert(1)</script>\nsecond line";
	int p1 = 0, p2 = 0, p3 = 0, p4 = 0, r1 = 0, r2 = 0;
	CHECK(f->addPost("user1", t1, nasty, p1) == Status::Ok);
	CHECK(f->addPost("user2", t1, "middle post", p2) == Status::Ok);
	CHECK(f->addPost("user1", t1, "last post", p3) == Status::Ok);
	CHECK(f->addPost("user1", t1, "last post", p4) == Status::Conflict);
	CHECK(f->addPost("user1", t1, "   ", p4) == Status::Invalid);
	CHECK(f->addPost("user1", t1, "has \x1b[2J escape", p4) == Status::Invalid);
	CHECK(f->addPost("user1", 9999, "orphan", p4) == Status::NotFound);
	CHECK(f->findTopic(t1)->posts.count() == 3);

	/* Ids are unique across topics, posts and replies */
	CHECK(f->addReply("user2", t1, p2, "first reply", r1) == Status::Ok);
	CHECK(f->addReply("user1", t1, p2, "second reply", r2) == Status::Ok);
	CHECK(f->addReply("user1", t1, 9999, "x", dummy) == Status::NotFound);
	int ids[] = { t1, t2, t3, p1, p2, p3, r1, r2 };
	bool distinct = true;
	for (int i = 0; i < 8; i++)
		for (int j = i + 1; j < 8; j++)
			distinct = distinct && ids[i] != ids[j];
	CHECK(distinct);

	/* One like per user: a second click removes it */
	bool liked = false;
	CHECK(f->likePost("user1", t1, p2, liked) == Status::Ok && liked);
	CHECK(f->findPost(t1, p2)->likes() == 1);
	CHECK(f->likePost("user2", t1, p2, liked) == Status::Ok && liked);
	CHECK(f->findPost(t1, p2)->likes() == 2);
	CHECK(f->likePost("user1", t1, p2, liked) == Status::Ok && !liked);
	CHECK(f->findPost(t1, p2)->likes() == 1 && !f->findPost(t1, p2)->isLikedBy("user1"));
	CHECK(f->likeTopic("user1", t1, liked) == Status::Ok && liked && f->findTopic(t1)->likes() == 1);
	CHECK(f->likeReply("user1", t1, p2, r1, liked) == Status::Ok && liked);
	CHECK(f->findPost(t1, p2)->replies[0].likes() == 1);
	CHECK(f->likePost("user1", t1, 9999, liked) == Status::NotFound);
	CHECK(f->likeReply("user1", t1, p2, 9999, liked) == Status::NotFound);

	/* Editing keeps replies, likes, id and time, and marks the post edited */
	std::time_t createdBefore = f->findPost(t1, p2)->created;
	CHECK(f->editPost("user1", t1, p2, "hijacked") == Status::Forbidden);  /* user2 wrote it */
	CHECK(f->editPost("user2", t1, p2, "") == Status::Invalid);
	CHECK(f->editPost("user2", t1, p2, "last post") == Status::Conflict);
	CHECK(f->editPost("user2", t1, p2, "middle post, edited") == Status::Ok);
	const Post* edited = f->findPost(t1, p2);
	CHECK(edited->text == "middle post, edited" && edited->edited);
	CHECK(edited->replies.count() == 2 && edited->likes() == 1);
	CHECK(edited->created == createdBefore && edited->id == p2);
	CHECK(f->editPost("user2", t1, p2, "middle post, edited") == Status::Ok);  /* no-op */
	CHECK(f->editPost("user2", t1, 9999, "x") == Status::NotFound);

	/* Deleting the middle post leaves its neighbours alone */
	CHECK(f->deletePost("user1", t1, p2) == Status::Forbidden);
	CHECK(f->findTopic(t1)->posts.count() == 3);
	CHECK(f->deletePost("user2", t1, p2) == Status::Ok);
	CHECK(f->findTopic(t1)->posts.count() == 2);
	CHECK(f->findTopic(t1)->posts[0].id == p1 && f->findTopic(t1)->posts[0].text == nasty);
	CHECK(f->findTopic(t1)->posts[1].id == p3 && f->findTopic(t1)->posts[1].text == "last post");
	CHECK(f->findPost(t1, p2) == nullptr);
	CHECK(f->deletePost("user2", t1, p2) == Status::NotFound);

	/* ---- search ---- */

	Vector<SearchHit> hits = f->search("LAST");
	CHECK(hits.count() == 1 && hits[0].postId == p3 && hits[0].topicId == t1);
	hits = f->search("fruit");
	CHECK(hits.count() == 1 && hits[0].postId == 0 && hits[0].topicId == t3);
	hits = f->search("topics");  /* the title of topic 1 */
	CHECK(hits.count() >= 1 && hits[0].topicId == t1);
	CHECK(f->search("").count() == 0 && f->search("   ").count() == 0);
	CHECK(f->search("zzzzz").count() == 0);

	CHECK(f->setCliSortMode(SortMode::Alpha) == Status::Ok);

	/* ---- the data directory belongs to one process ---- */

	std::unique_ptr<Forum> second = Forum::open(dir.path(), error);
	CHECK(second == nullptr);
	CHECK(error.find("in use") != std::string::npos);

	/* ---- everything survives a restart ---- */

	f.reset();
	f = openForum(dir.path());
	CHECK(f != nullptr);
	if (!f)
		return 1;

	CHECK(f->login("user1", "newpass1", name) == Status::Ok);
	CHECK(f->login("user2", "password2", name) == Status::Ok);
	CHECK(f->topics().count() == 3);
	CHECK(f->topics()[0].title() == "Topics" && f->topics()[2].title() == "Apple");
	CHECK(f->cliSortMode() == SortMode::Alpha);
	CHECK(f->findTopic(t1)->likes() == 1);
	CHECK(f->findTopic(t1)->posts.count() == 2);
	CHECK(f->findPost(t1, p1)->text == nasty);
	CHECK(f->findPost(t1, p3)->text == "last post");

	/* A new id after reload must not collide with an old one */
	int p5 = 0;
	CHECK(f->addPost("user1", t1, "after restart", p5) == Status::Ok);
	CHECK(p5 > r2 && p5 > t3);

	/* Posts keep their replies and likes through a restart too */
	CHECK(f->addReply("user1", t1, p5, "kept reply", r1) == Status::Ok);
	CHECK(f->likePost("user2", t1, p5, liked) == Status::Ok);
	f.reset();
	f = openForum(dir.path());
	CHECK(f != nullptr);
	if (!f)
		return 1;
	CHECK(f->findPost(t1, p5)->replies.count() == 1 && f->findPost(t1, p5)->replies[0].text == "kept reply");
	CHECK(f->findPost(t1, p5)->isLikedBy("user2"));

	/* ---- a file this version cannot read is never overwritten ---- */

	f.reset();
	TempDir other;
	{
		std::ofstream bad(other.path() + "/forum.db");
		bad << "something else entirely\n";
	}
	CHECK(Forum::open(other.path(), error) == nullptr);
	CHECK(error.find("refusing") != std::string::npos);
	{
		std::ifstream check(other.path() + "/forum.db");
		std::string line;
		std::getline(check, line);
		CHECK(line == "something else entirely");
	}

	return testReport();
}
