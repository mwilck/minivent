/*
 * Copyright (c) 2021 Martin Wilck, SUSE LLC
 * SPDX-License-Identifier: LGPL-2.1-or-newer
 */
#ifndef _TS_UTIL_H
#define _TS_UTIL_H

/*
 * Utility functions for dealing with "struct timespec".
 * See also tv_util.h, which has the same set of functions
 * for "struct timeval".
 */

/**
 * ts_to_us - convert struct timespec to microseconds
 * @ts: timespec object
 *
 * Return: value of @ts in microseconds.
 */
static inline uint64_t ts_to_us(const struct timespec *ts)
{
	return ts->tv_sec * 1000000ULL + ts->tv_nsec / 1000;
}

/**
 * us_to_ts - convert microseconds to struct timespec
 * @us: microseconds
 * @ts: conversion result
 */
static inline void us_to_ts(uint64_t us, struct timespec *ts)
{
	ts->tv_sec = us / 1000000L;
	ts->tv_nsec = (us % 1000000L) * 1000;
}

/**
 * ts_normalize() - convert a struct timespec to normal form
 * @ts: timespec to normalize
 *
 * "Normalized" means 0 <= ts->tv_nsec < 1000000000.
 */
void ts_normalize(struct timespec *ts);

/**
 * ts_add(): add a struct timespec to another
 * @t1: 1st summand, this one will be modified
 * @t2: 2nd summand, will be added to @t1.
 *
 * @t1 is normalized on return.
 */
void ts_add(struct timespec *t1, const struct timespec *t2);

/**
 * ts_subtract(): subtract a struct timespec from another
 *
 * @t1: minuend, this one will be modified
 * @t2: subtrahend
 *
 * @t1 is normalized on return.
 */
void ts_subtract(struct timespec *t1, const struct timespec *t2);

/**
 * ts_compare - compare two struct timespec objects
 *
 * @t1: 1st timespec object
 * @t2: 2nd timespec object
 *
 * IMPORTANT: this function assumes that both @t1 and  @t2 are normalized.
 * If that's not the case, results will be wrong.
 *
 * Return: 0 if @t1 == @t2, -1 if @t1 < @t2 and 1 if @t1 > @t2.
 */
int ts_compare(const struct timespec *t1, const struct timespec *t2);

/**
 * ts_sort() - sort an array of normalized struct timespec objects
 *
 * @tss: array of of "struct timespec *"
 * @len: number of elements in @tss
 *
 * IMPORTANT: all elements of the array should be normalized before calling
 * this function.
 * The array is sorted in ascending order, using ts_compare() for comparing
 * elements.
 */
void ts_sort(struct timespec **tss, size_t len);

/**
 * ts_search - find insertion point for a timespec object in a sorted array
 *
 * @tvs: sorted array of normalized "struct timespec *"
 * @len: number of elements in @tss
 * @new: new struct timespec object
 *
 * On entry, @tvs must be a sorted array of normalized "struct timespec" pointers.
 * (sorted in the sense of ts_sort()). The function searches the index in the
 * array where @new would need to be inserted, using a bisection algorithm.
 * @new needs not be normalized on entry, it will be when the function returns
 * successfully.
 *
 * Return: On success, the non-negative index at which this element would need to
 * be inserted in the array in order to keep it sorted. If the return value is n,
 * then the timespec @tvs[n-1] is smaller than @new, and @tvs[n] is greater or
 * equal than @new.
 *  -EINVAL if one of the input parameters is invalid.
 */
long ts_search(struct timespec *const *tvs, size_t len, struct timespec *new);

/**
 * ts_insert - insert a new struct timespec into a sorted array
 *
 * @tvs: sorted array of normalized "struct timespec *"
 * @len: number of elements in @tss
 * @size: allocated size (in elements) of @tvs, must be larger than @len on entry
 * @new: new struct timespec object
 *
 * Inserts the element @new into @tvs at the point returned by ts_search(), keeping
 * the array sorted. @new doesn't need to be normalized on entry, it will be on
 * successful return.
 * This function doesn't reallocate @tvs and doesn't take a copy of @new.
 *
 * Return: On success, the non-negative index at which the element was inserted.
 *  -EINVAL if input parameters were invalid (see ts_search()).
 *  -EOVERFLOW if @size is not large enough to add the new element.
 */
long ts_insert(struct timespec **tvs, size_t *len, size_t size, struct timespec *new);

#endif
