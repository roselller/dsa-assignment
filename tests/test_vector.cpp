#include <memory>
#include <string>

#include "Vector.h"
#include "test.h"

int main()
{
	/* Heap-allocated so a sanitizer sees any out-of-bounds access */
	auto v = std::make_unique<Vector<int>>();
	CHECK(v->count() == 0);
	CHECK(v->empty());
	CHECK(v->begin() == v->end());

	for (int i = 0; i < 100; i++)
		v->push(i);
	CHECK(v->count() == 100);
	CHECK((*v)[0] == 0 && (*v)[99] == 99);

	/* Remove from the head, middle and tail; neighbours must stay intact */
	v->removeAt(0);
	CHECK((*v)[0] == 1 && v->count() == 99);
	v->removeAt(49);
	CHECK((*v)[48] == 49 && (*v)[49] == 51);
	v->removeAt(v->count() - 1);
	CHECK(v->count() == 97 && (*v)[96] == 98);

	/* remove() takes out the first match only, and reports a miss */
	Vector<int> dup;
	for (int x : { 5, 7, 5, 9 })
		dup.push(x);
	CHECK(dup.remove(5));
	CHECK(dup.count() == 3 && dup[0] == 7 && dup[1] == 5 && dup[2] == 9);
	CHECK(!dup.remove(42));
	CHECK(dup.count() == 3);
	CHECK(dup.indexOf(9) == 2 && dup.indexOf(42) == -1);

	/* Copies are deep */
	Vector<std::string> a;
	a.push("one");
	a.push("two");
	Vector<std::string> b = a;
	b[0] = "changed";
	CHECK(a[0] == "one" && b[0] == "changed");

	/* Copy assignment, including self-assignment */
	Vector<std::string> c;
	c = a;
	CHECK(c.count() == 2 && c[1] == "two");
	Vector<std::string>& alias = c;
	c = alias;
	CHECK(c.count() == 2 && c[0] == "one");

	/* Moving leaves the source empty but usable */
	Vector<std::string> d = std::move(c);
	CHECK(d.count() == 2 && d[0] == "one");
	CHECK(c.count() == 0);
	c.push("again");
	CHECK(c.count() == 1 && c[0] == "again");

	Vector<std::string> e;
	e = std::move(d);
	CHECK(e.count() == 2 && d.count() == 0);

	/* Range-for works on empty and non-empty vectors */
	int total = 0;
	Vector<int> empty;
	for (int x : empty)
		total += x;
	CHECK(total == 0);
	for (int x : *v)
		total += x;
	CHECK(total > 0);

	/* References stay valid across the push that returned them (before the next push) */
	Vector<std::string> s;
	std::string& ref = s.push("hello");
	CHECK(ref == "hello");

	v->clear();
	CHECK(v->empty());

	return testReport();
}
