#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

/* 64-bit FNV-1a over every byte of the key */
inline std::uint64_t fnv1a(const char* bytes, std::size_t n)
{
	std::uint64_t h = 14695981039346656037ULL;
	for (std::size_t i = 0; i < n; i++)
	{
		h ^= static_cast<unsigned char>(bytes[i]);
		h *= 1099511628211ULL;
	}
	return h;
}

template <class K>
struct Hasher;

template <>
struct Hasher<std::string>
{
	std::uint64_t operator()(const std::string& s) const { return fnv1a(s.data(), s.size()); }
};

template <>
struct Hasher<int>
{
	std::uint64_t operator()(int v) const { return fnv1a(reinterpret_cast<const char*>(&v), sizeof v); }
};

/* Hash table using separate chaining. The bucket array doubles when the load
   factor passes 0.75, so insert, find and erase are O(1) on average.
   Nodes are relinked (never copied) on rehash, so a V* returned by find or
   insertOrAssign stays valid until that entry is erased. */
template <class K, class V, class Hash = Hasher<K>>
class HashMap
{
	struct Node
	{
		K key;
		V value;
		Node* next;
		Node(K k, V v) : key(std::move(k)), value(std::move(v)), next(nullptr) {}
	};

	static constexpr std::size_t kInitialBuckets = 16;

	Node** buckets_ = nullptr;
	std::size_t bucketCount_ = 0;
	std::size_t size_ = 0;

	std::size_t indexFor(const K& key, std::size_t buckets) const { return Hash{}(key) % buckets; }

	void rehash(std::size_t newCount)
	{
		Node** fresh = new Node*[newCount]();
		for (std::size_t i = 0; i < bucketCount_; i++)
		{
			Node* n = buckets_[i];
			while (n)
			{
				Node* next = n->next;
				std::size_t j = indexFor(n->key, newCount);
				n->next = fresh[j];
				fresh[j] = n;
				n = next;
			}
		}
		delete[] buckets_;
		buckets_ = fresh;
		bucketCount_ = newCount;
	}

	void destroyNodes()
	{
		for (std::size_t i = 0; i < bucketCount_; i++)
		{
			Node* n = buckets_[i];
			while (n)
			{
				Node* next = n->next;
				delete n;
				n = next;
			}
		}
		delete[] buckets_;
		buckets_ = nullptr;
		bucketCount_ = 0;
		size_ = 0;
	}

public:
	/* One key/value pair as seen while iterating */
	struct Entry
	{
		const K& key;
		V& value;
	};

	/* Forward iterator. Walks each chain, then moves to the next non-empty
	   bucket, checking the bucket bound before it dereferences anything. */
	class Iterator
	{
		Node* node_;
		Node* const* bucket_;
		Node* const* end_;

		void settle()
		{
			while (!node_ && bucket_ != end_)
			{
				++bucket_;
				if (bucket_ != end_)
					node_ = *bucket_;
			}
		}

	public:
		Iterator(Node* const* bucket, Node* const* end) : node_(nullptr), bucket_(bucket), end_(end)
		{
			if (bucket_ != end_)
				node_ = *bucket_;
			settle();
		}

		Entry operator*() const { return Entry{ node_->key, node_->value }; }
		Iterator& operator++()
		{
			node_ = node_->next;
			settle();
			return *this;
		}
		friend bool operator==(const Iterator& a, const Iterator& b) { return a.node_ == b.node_; }
		friend bool operator!=(const Iterator& a, const Iterator& b) { return a.node_ != b.node_; }
	};

	HashMap() = default;

	HashMap(const HashMap& other)
	{
		if (other.size_ > 0)
			rehash(other.bucketCount_);
		for (std::size_t i = 0; i < other.bucketCount_; i++)
			for (Node* n = other.buckets_[i]; n; n = n->next)
				insertOrAssign(n->key, n->value);
	}

	HashMap(HashMap&& other) noexcept :
		buckets_(other.buckets_), bucketCount_(other.bucketCount_), size_(other.size_)
	{
		other.buckets_ = nullptr;
		other.bucketCount_ = 0;
		other.size_ = 0;
	}

	HashMap& operator=(HashMap other) noexcept
	{
		std::swap(buckets_, other.buckets_);
		std::swap(bucketCount_, other.bucketCount_);
		std::swap(size_, other.size_);
		return *this;
	}

	~HashMap() { destroyNodes(); }

	/* Pointer to the value stored under key, or nullptr */
	V* find(const K& key)
	{
		if (bucketCount_ == 0)
			return nullptr;
		for (Node* n = buckets_[indexFor(key, bucketCount_)]; n; n = n->next)
			if (n->key == key)
				return &n->value;
		return nullptr;
	}

	const V* find(const K& key) const { return const_cast<HashMap*>(this)->find(key); }

	bool contains(const K& key) const { return find(key) != nullptr; }

	/* Stores value under key, replacing any existing value. Returns the stored value. */
	V& insertOrAssign(K key, V value)
	{
		if (V* existing = find(key))
		{
			*existing = std::move(value);
			return *existing;
		}
		if (bucketCount_ == 0)
			rehash(kInitialBuckets);
		else if ((size_ + 1) * 4 > bucketCount_ * 3)
			rehash(bucketCount_ * 2);

		std::size_t i = indexFor(key, bucketCount_);
		Node* n = new Node(std::move(key), std::move(value));
		n->next = buckets_[i];
		buckets_[i] = n;
		size_++;
		return n->value;
	}

	/* Removes key. Returns false if it was not present. */
	bool erase(const K& key)
	{
		if (bucketCount_ == 0)
			return false;
		Node** link = &buckets_[indexFor(key, bucketCount_)];
		while (*link)
		{
			if ((*link)->key == key)
			{
				Node* doomed = *link;
				*link = doomed->next;
				delete doomed;
				size_--;
				return true;
			}
			link = &(*link)->next;
		}
		return false;
	}

	std::size_t size() const { return size_; }
	std::size_t bucketCount() const { return bucketCount_; }

	Iterator begin() { return Iterator(buckets_, buckets_ + bucketCount_); }
	Iterator end() { return Iterator(buckets_ + bucketCount_, buckets_ + bucketCount_); }
};
