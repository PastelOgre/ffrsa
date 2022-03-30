/*
 * ffmem.cpp
 *
 *  Created on: Apr 16, 2018
 *      Author: Jesse Wang
 */

#include "ffmem.h"
#include <stdio.h>
#include <unordered_map>
#include <stdint.h>
#include <string>
#include "fflog.h"
#include <pthread.h>

#define FFMEM_STATS_ENABLED 1

static pthread_mutex_t _count_mutex = PTHREAD_MUTEX_INITIALIZER;
static int _alloc_count = 0;

#if FFMEM_STATS_ENABLED
class ffmem_stat_entry
{
public:
	std::string file_name;
	std::string func_name;
	int line_num;
	int alloc_size;
};
static std::unordered_map<uint64_t, ffmem_stat_entry*> _stats;
static pthread_mutex_t _stats_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

void ffmem_count_inc()
{
	pthread_mutex_lock(&_count_mutex);
	_alloc_count++;
	pthread_mutex_unlock(&_count_mutex);
}

void ffmem_count_dec()
{
	pthread_mutex_lock(&_count_mutex);
	_alloc_count--;
	pthread_mutex_unlock(&_count_mutex);
}

int ffmem_count_get()
{
	return _alloc_count;
}

void ffmem_alloc_add(void* obj, const char* file_name, const char* func_name, int line_num, int alloc_size)
{
#if FFMEM_STATS_ENABLED
	ffmem_stat_entry* entry = new ffmem_stat_entry();
	entry->file_name = file_name;
	entry->func_name = func_name;
	entry->line_num = line_num;
	entry->alloc_size = alloc_size;
	pthread_mutex_lock(&_stats_mutex);
	_stats[(uint64_t)obj] = entry;
	pthread_mutex_unlock(&_stats_mutex);
#endif
}

void ffmem_alloc_remove(void* obj)
{
#if FFMEM_STATS_ENABLED
	pthread_mutex_lock(&_stats_mutex);
	std::unordered_map<uint64_t, ffmem_stat_entry*>::iterator it = _stats.find((uint64_t)obj);
	delete it->second;
	_stats.erase(it);
	pthread_mutex_unlock(&_stats_mutex);
#endif
}

void ffmem_stats_print()
{
#if FFMEM_STATS_ENABLED
	pthread_mutex_lock(&_stats_mutex);
	fflog_print("Current Allocation Count = %d\n", _alloc_count);
	std::unordered_map<uint64_t, ffmem_stat_entry*>::iterator it;
	int i = 1;
	for(it=_stats.begin();it!=_stats.end();it++)
		fflog_print("#%d: %s, %s, line %d, %d bytes\n", i++, it->second->file_name.c_str(), it->second->func_name.c_str(), it->second->line_num, it->second->alloc_size);
	pthread_mutex_unlock(&_stats_mutex);
#endif
}
