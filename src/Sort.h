#pragma once

#include <memory>
#include <utility>

#include "Vector.h"

namespace detail
{
	/* Sorts a[lo, hi) using tmp as scratch space */
	template <class T, class Less>
	void mergeSortRange(T* a, T* tmp, int lo, int hi, Less& less)
	{
		if (hi - lo < 2)
			return;

		int mid = lo + (hi - lo) / 2;
		mergeSortRange(a, tmp, lo, mid, less);
		mergeSortRange(a, tmp, mid, hi, less);

		int i = lo, j = mid, k = lo;
		while (i < mid && j < hi)
		{
			/* Take from the right half only when strictly smaller, which keeps equal
			   elements in their original order (a stable sort) */
			if (less(a[j], a[i]))
				tmp[k++] = std::move(a[j++]);
			else
				tmp[k++] = std::move(a[i++]);
		}
		while (i < mid)
			tmp[k++] = std::move(a[i++]);
		while (j < hi)
			tmp[k++] = std::move(a[j++]);

		for (k = lo; k < hi; k++)
			a[k] = std::move(tmp[k]);
	}
}

/* Stable merge sort, O(n log n). Elements are moved, never copied, so sorting a
   vector of large objects stays cheap. less(a, b) is true when a belongs before b. */
template <class T, class Less>
void mergeSort(Vector<T>& vec, Less less)
{
	int n = vec.count();
	if (n < 2)
		return;
	std::unique_ptr<T[]> scratch(new T[n]);
	detail::mergeSortRange(vec.begin(), scratch.get(), 0, n, less);
}
