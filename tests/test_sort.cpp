#include <string>
#include <utility>

#include "Sort.h"
#include "test.h"

/* Counts copies, to show the sort moves elements instead of copying them */
struct Tracked
{
	int key = 0;
	int order = 0;
	static inline int copies = 0;

	Tracked() = default;
	Tracked(int k, int o) : key(k), order(o) {}
	Tracked(const Tracked& other) : key(other.key), order(other.order) { copies++; }
	Tracked(Tracked&&) noexcept = default;
	Tracked& operator=(const Tracked& other)
	{
		key = other.key;
		order = other.order;
		copies++;
		return *this;
	}
	Tracked& operator=(Tracked&&) noexcept = default;
};

int main()
{
	auto less = [](const int& a, const int& b) { return a < b; };

	/* Trivial sizes */
	Vector<int> none;
	mergeSort(none, less);
	CHECK(none.count() == 0);

	Vector<int> one;
	one.push(5);
	mergeSort(one, less);
	CHECK(one.count() == 1 && one[0] == 5);

	/* Odd and even lengths, duplicates, already sorted, reversed */
	for (int n : { 2, 3, 7, 8, 100, 101 })
	{
		Vector<int> v;
		for (int i = 0; i < n; i++)
			v.push((i * 37) % 11);
		mergeSort(v, less);
		bool sorted = true;
		for (int i = 1; i < v.count(); i++)
			sorted = sorted && v[i - 1] <= v[i];
		CHECK(sorted && v.count() == n);
	}

	Vector<int> reversed;
	for (int i = 50; i > 0; i--)
		reversed.push(i);
	mergeSort(reversed, less);
	CHECK(reversed[0] == 1 && reversed[49] == 50);

	/* Sorting a permutation must not lose or duplicate anything */
	Vector<int> perm;
	for (int i = 0; i < 64; i++)
		perm.push((i * 17) % 64);
	mergeSort(perm, less);
	bool identity = true;
	for (int i = 0; i < 64; i++)
		identity = identity && perm[i] == i;
	CHECK(identity);

	/* Stability: equal keys keep their original relative order */
	Vector<Tracked> items;
	for (int i = 0; i < 40; i++)
		items.push(Tracked(i % 4, i));
	Tracked::copies = 0;
	mergeSort(items, [](const Tracked& a, const Tracked& b) { return a.key < b.key; });
	bool stable = true;
	for (int i = 1; i < items.count(); i++)
	{
		if (items[i - 1].key > items[i].key)
			stable = false;
		if (items[i - 1].key == items[i].key && items[i - 1].order > items[i].order)
			stable = false;
	}
	CHECK(stable);
	CHECK(Tracked::copies == 0);

	/* Strings, descending */
	Vector<std::string> words;
	for (const char* w : { "pear", "apple", "fig", "banana" })
		words.push(w);
	mergeSort(words, [](const std::string& a, const std::string& b) { return a > b; });
	CHECK(words[0] == "pear" && words[1] == "fig" && words[2] == "banana" && words[3] == "apple");

	return testReport();
}
