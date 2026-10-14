#pragma once

#include <cassert>
#include <utility>

/* A growable array. Capacity doubles when full, so push is amortised O(1).
   T must be default-constructible and move-assignable. */
template <class T>
class Vector
{
	T* data_ = nullptr;
	int size_ = 0;
	int cap_ = 0;

	void grow(int newCap)
	{
		T* fresh = new T[newCap];
		for (int i = 0; i < size_; i++)
			fresh[i] = std::move(data_[i]);
		delete[] data_;
		data_ = fresh;
		cap_ = newCap;
	}

public:
	Vector() = default;

	Vector(const Vector& other)
	{
		if (other.size_ > 0)
		{
			data_ = new T[other.size_];
			cap_ = other.size_;
			for (int i = 0; i < other.size_; i++)
				data_[i] = other.data_[i];
			size_ = other.size_;
		}
	}

	Vector(Vector&& other) noexcept :
		data_(other.data_), size_(other.size_), cap_(other.cap_)
	{
		other.data_ = nullptr;
		other.size_ = 0;
		other.cap_ = 0;
	}

	/* Taking the argument by value gives copy-assignment and move-assignment
	   in one function and makes self-assignment safe. */
	Vector& operator=(Vector other) noexcept
	{
		std::swap(data_, other.data_);
		std::swap(size_, other.size_);
		std::swap(cap_, other.cap_);
		return *this;
	}

	~Vector() { delete[] data_; }

	/* Appends an item and returns a reference to the stored copy. The reference
	   is invalidated by the next push or removal. */
	T& push(T item)
	{
		if (size_ == cap_)
			grow(cap_ == 0 ? 4 : cap_ * 2);
		data_[size_] = std::move(item);
		return data_[size_++];
	}

	/* Removes the element at index i, shifting later elements down. O(n). */
	void removeAt(int i)
	{
		assert(i >= 0 && i < size_);
		for (int j = i + 1; j < size_; j++)
			data_[j - 1] = std::move(data_[j]);
		/* Release whatever the vacated slot still holds */
		data_[size_ - 1] = T();
		size_--;
	}

	/* Index of the first element equal to item, or -1 */
	int indexOf(const T& item) const
	{
		for (int i = 0; i < size_; i++)
			if (data_[i] == item)
				return i;
		return -1;
	}

	/* Removes the first element equal to item. Returns false if there is none. */
	bool remove(const T& item)
	{
		int i = indexOf(item);
		if (i < 0)
			return false;
		removeAt(i);
		return true;
	}

	void clear()
	{
		for (int i = 0; i < size_; i++)
			data_[i] = T();
		size_ = 0;
	}

	int count() const { return size_; }
	bool empty() const { return size_ == 0; }

	T& operator[](int i)
	{
		assert(i >= 0 && i < size_);
		return data_[i];
	}
	const T& operator[](int i) const
	{
		assert(i >= 0 && i < size_);
		return data_[i];
	}

	/* Plain pointers are valid iterators, and nullptr + 0 is well defined, so an
	   empty vector needs no special case. */
	T* begin() { return data_; }
	T* end() { return data_ + size_; }
	const T* begin() const { return data_; }
	const T* end() const { return data_ + size_; }
};
