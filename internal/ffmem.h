/*
 * ffmem.h
 *
 *  Created on: Apr 16, 2018
 *      Author: Jesse Wang
 */

#ifndef FFMEM_H_
#define FFMEM_H_

#include <exception>
#include <memory>

#ifdef __cplusplus
extern "C" {
#endif

void ffmem_count_inc();
void ffmem_count_dec();
int ffmem_count_get();
void ffmem_alloc_add(void* obj, const char* file_name, const char* func_name, int line_num, int alloc_size);
void ffmem_alloc_remove(void* obj);
void ffmem_stats_print();

#ifdef __cplusplus
}
#endif

template<typename T, typename... Args>
T* ffmem_alloc_impl(const char* file_name, const char* func_name, int line_num, Args... args)
{
	T* obj = new(std::nothrow) T(args...);
	if(!obj)
		return NULL;
	ffmem_count_inc();
	ffmem_alloc_add(obj, file_name, func_name, line_num, sizeof(T));
	return obj;
}

template<typename T>
T* ffmem_alloc_arr_impl(const char* file_name, const char* func_name, int line_num, int num)
{
	T* obj = new(std::nothrow) T[num];
	if(!obj)
		return NULL;
	ffmem_count_inc();
	ffmem_alloc_add(obj, file_name, func_name, line_num, sizeof(T)*num);
	return obj;
}

template<typename T>
void ffmem_free(T* obj)
{
	ffmem_alloc_remove(obj);
	delete obj;
	ffmem_count_dec();
}

template<typename T>
void ffmem_free_arr(T* obj)
{
	ffmem_alloc_remove(obj);
	delete[] obj;
	ffmem_count_dec();
}

#define ffmem_alloc(a) ffmem_alloc_impl<a>(__FILE__, __func__, __LINE__)
#define ffmem_alloc_args(a, ...) ffmem_alloc_impl<a>(__FILE__, __func__, __LINE__, __VA_ARGS__)
#define ffmem_alloc_arr(a, n) ffmem_alloc_arr_impl<a>(__FILE__, __func__, __LINE__, n)
#define COMMA ,

#endif /* FFMEM_H_ */
