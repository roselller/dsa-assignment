#include <cstdint>
#include <memory>
#include <string>

#include "HashMap.h"
#include "test.h"

/* Sends every key to the same bucket, to force long collision chains */
struct ConstantHash
{
	std::uint64_t operator()(const std::string&) const { return 7; }
};

int main()
{
	/* Heap-allocated so a sanitizer sees any out-of-bounds access */
	auto m = std::make_unique<HashMap<std::string, int>>();
	CHECK(m->size() == 0);
	CHECK(m->find("x") == nullptr);
	CHECK(!m->erase("x"));
	CHECK(m->begin() == m->end());

	/* The keys that used to collide in the old hash (every non-letter hashed alike) */
	m->insertOrAssign("user1", 1);
	m->insertOrAssign("user2", 2);
	m->insertOrAssign("user3", 3);
	CHECK(m->size() == 3);
	CHECK(*m->find("user1") == 1 && *m->find("user2") == 2 && *m->find("user3") == 3);

	/* Assigning to an existing key replaces the value and does not grow the map */
	m->insertOrAssign("user2", 22);
	CHECK(m->size() == 3 && *m->find("user2") == 22);

	/* Non-ASCII bytes must not produce a bad bucket index */
	m->insertOrAssign("\xE9\xFF", 5);
	CHECK(*m->find("\xE9\xFF") == 5);

	/* Growth: pointers must survive rehashing, and nothing may be lost */
	int* p = m->find("user1");
	std::size_t before = m->bucketCount();
	for (int i = 0; i < 1000; i++)
		m->insertOrAssign("key" + std::to_string(i), i);
	CHECK(m->bucketCount() > before);
	CHECK(m->find("user1") == p && *p == 1);
	CHECK(m->size() == 1004);
	bool allFound = true;
	for (int i = 0; i < 1000; i++)
	{
		int* v = m->find("key" + std::to_string(i));
		allFound = allFound && v && *v == i;
	}
	CHECK(allFound);
	CHECK(m->size() <= m->bucketCount());

	/* Iteration visits every entry exactly once */
	std::size_t visited = 0;
	long sum = 0;
	for (auto entry : *m)
	{
		visited++;
		sum += entry.value;
	}
	CHECK(visited == m->size());
	CHECK(sum == 1 + 22 + 3 + 5 + 999L * 1000 / 2);

	/* Erase */
	CHECK(m->erase("user2"));
	CHECK(!m->contains("user2") && m->size() == 1003);
	CHECK(!m->erase("user2"));

	/* Chain removal at the head, middle and tail, with every key in one bucket */
	HashMap<std::string, int, ConstantHash> chain;
	for (int i = 0; i < 5; i++)
		chain.insertOrAssign("k" + std::to_string(i), i);
	/* New nodes go to the front, so the chain is k4,k3,k2,k1,k0 */
	CHECK(chain.erase("k4"));      /* head */
	CHECK(chain.erase("k2"));      /* middle */
	CHECK(chain.erase("k0"));      /* tail */
	CHECK(chain.size() == 2);
	CHECK(chain.contains("k3") && chain.contains("k1"));
	CHECK(!chain.contains("k4") && !chain.contains("k2") && !chain.contains("k0"));
	CHECK(*chain.find("k3") == 3 && *chain.find("k1") == 1);

	/* Sparse iteration: few entries across many buckets */
	HashMap<std::string, int> sparse;
	for (int i = 0; i < 3; i++)
		sparse.insertOrAssign("s" + std::to_string(i), i);
	int count = 0;
	for (auto entry : sparse)
	{
		(void)entry;
		count++;
	}
	CHECK(count == 3);

	/* Deep copy */
	HashMap<std::string, int> copy = *m;
	copy.insertOrAssign("only-in-copy", 1);
	CHECK(copy.size() == m->size() + 1);
	CHECK(!m->contains("only-in-copy"));
	CHECK(*copy.find("user1") == 1);

	/* Copy assignment, including self-assignment */
	HashMap<std::string, int> assigned;
	assigned = sparse;
	CHECK(assigned.size() == 3);
	HashMap<std::string, int>& alias = assigned;
	assigned = alias;
	CHECK(assigned.size() == 3 && *assigned.find("s2") == 2);

	/* A moved-from map is empty but still usable */
	HashMap<std::string, int> moved = std::move(assigned);
	CHECK(moved.size() == 3 && assigned.size() == 0);
	CHECK(assigned.find("s0") == nullptr && !assigned.erase("s0"));
	CHECK(assigned.begin() == assigned.end());
	assigned.insertOrAssign("fresh", 9);
	CHECK(*assigned.find("fresh") == 9);

	/* Integer keys */
	HashMap<int, std::string> ids;
	ids.insertOrAssign(3, "three");
	ids.insertOrAssign(-3, "minus three");
	CHECK(*ids.find(3) == "three" && *ids.find(-3) == "minus three");

	return testReport();
}
