/*
 * fftime.cpp
 *
 *  Created on: Apr 19, 2018
 *      Author: Jesse Wang
 */

#include "fftime.h"
#include "ffmem.h"
#include <map>
#include <list>
#include <math.h>
#include "fflog.h"
#include <limits.h>

#include <sys/time.h>
#include <unistd.h>

uint32_t fftime_get_time_ms()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

void fftime_sleep_ms(uint32_t ms)
{
	while(ms >= 999)
	{
		usleep(999000); //usleep will refuse to sleep if argument is greater than or equal to 1000000.
		ms-=999;
	}
	if(ms)
		usleep(ms*1000);
}

void fftime_loop(fftime_loop_cb func, void* param, int fps, uint8_t* running)
{
	uint32_t last_time = fftime_get_time_ms();
	uint32_t wait_ms = (uint32_t)roundf(1000.0f/(float)fps);
	while(*running)
	{
		func(param);
		uint32_t current_time = fftime_get_time_ms();
		uint32_t elapsed_time;
		if(current_time < last_time)
			elapsed_time = UINT_MAX - last_time + current_time;
		else
			elapsed_time = current_time - last_time;
		if(elapsed_time < wait_ms)
			fftime_sleep_ms(wait_ms - elapsed_time);
		last_time = fftime_get_time_ms();
	}
}

class fftimeline_event_entry
{
public:
	fftimeline_event_t* handle;
	fftimeline_event_cb cb;
	void* param;
	fftimeline_event_entry()
	{
		handle = NULL;
		param = NULL;
		cb = NULL;
	}
	~fftimeline_event_entry()
	{
		if(handle)
			ffmem_free(handle);
	}
};

class FFTIMELINE
{
public:
	std::map<double, std::list<fftimeline_event_entry>> events;
	std::map<double, std::list<fftimeline_event_entry>>::iterator events_it;
	double duration;
	double position;
	double last_time;
	float time_scale;
	uint8_t playing;
	uint8_t looping;
	double finish_time;
	uint8_t attached;
	double* attached_current_time;
	double* attached_delta_time;
	float* attached_time_scale;
};

class FFTIMELINE_EVENT
{
public:
	std::map<double, std::list<fftimeline_event_entry>>::iterator events_it;
	std::list<fftimeline_event_entry>::iterator entry_it;
};

//Duration is in milliseconds.
fftimeline_t* fftimeline_create(double duration_ms)
{
	fftimeline_t* tl = ffmem_alloc(fftimeline_t);
	tl->duration = duration_ms;
	tl->position = 0.0;
	tl->last_time = 0.0;
	tl->time_scale = 1.0f;
	tl->playing = 0;
	tl->looping = 0;
	tl->finish_time = 0;
	tl->events_it = tl->events.begin();
	tl->attached = 0;
	return tl;
}

void fftimeline_destroy(fftimeline_t* tl)
{
	ffmem_free(tl);
}

//Returns 0 on normal operation or returns the time in milliseconds when the timeline finished playing.
//Events are called in this function.
double fftimeline_step(fftimeline_t* tl)
{
	if(tl == NULL)
	{
		fflog_print("Unable to step timeline. tl=NULL\n");
		return 0.0;
	}
	double current_time;
	if(tl->attached)
		current_time = *tl->attached_current_time;
	else
		current_time = (double)fftime_get_time_ms();
	if(!tl->playing)
	{
		tl->last_time = current_time;
		return tl->finish_time;
	}
	float time_scale;
	if(tl->attached)
		time_scale = *tl->attached_time_scale;
	else
		time_scale = tl->time_scale;
	if(!tl->looping)
	{
		if(time_scale >= 0.0f)
		{
			if(tl->position >= tl->duration)
			{
				tl->playing = 0;
				return tl->finish_time;
			}
		}
		else
		{
			if(tl->position <= 0.0f)
			{
				tl->playing = 0;
				return tl->finish_time;
			}
		}
	}
	if(time_scale != 0.0f)
	{
		double elapsed_time;
		if(tl->attached)
			elapsed_time = *tl->attached_delta_time;
		else
		{
			if(tl->last_time > current_time)
				elapsed_time = (double)(UINT_MAX - tl->last_time + current_time);
			else
				elapsed_time = (double)(current_time - tl->last_time);
			elapsed_time *= (double)tl->time_scale;
		}
		tl->position += elapsed_time;
		if(tl->time_scale >= 0.0f)
		{
			while(tl->events_it != tl->events.end())
			{
				if(tl->events_it->first <= tl->position)
				{
					std::list<fftimeline_event_entry>::iterator entry_it;
					for(entry_it=tl->events_it->second.begin();entry_it!=tl->events_it->second.end();entry_it++)
						entry_it->cb(tl, tl->events_it->first, entry_it->param);
					tl->events_it++;
					continue;
				}
				break;
			}
			if(tl->looping)
			{
				while(tl->position >= tl->duration)
				{
					tl->position -= tl->duration;
					tl->events_it = tl->events.begin();
				}
			}
			else
			{
				if(tl->position >= tl->duration)
				{
					tl->playing = 0;
					tl->finish_time = (double)current_time - (tl->position - tl->duration)/((double)time_scale);
					return tl->finish_time;
				}
			}
		}
		else
		{
			while(tl->events_it != tl->events.begin())
			{
				tl->events_it--;
				if(tl->events_it->first >= tl->position)
				{
					std::list<fftimeline_event_entry>::iterator entry_it;
					for(entry_it=tl->events_it->second.begin();entry_it!=tl->events_it->second.end();entry_it++)
						entry_it->cb(tl, tl->events_it->first, entry_it->param);
					continue;
				}
				tl->events_it++;
				break;
			}
			if(tl->looping)
			{
				while(tl->position <= 0)
				{
					tl->position += tl->duration;
					tl->events_it = tl->events.end();
				}
			}
			else
			{
				if(tl->position <= 0)
				{
					tl->playing = 0;
					tl->finish_time = (double)current_time - (0.0 - tl->position)/fabs((double)time_scale);
					return tl->finish_time;
				}
			}
		}
	}
	tl->last_time = current_time;
	return 0;
}

//Set the timeline to playing so timeline events can be triggered.
void fftimeline_play(fftimeline_t* tl)
{
	tl->playing = 1;
	float time_scale;
	if(tl->attached)
	{
		tl->last_time = *tl->attached_current_time;
		time_scale = *tl->attached_time_scale;
	}
	else
	{
		tl->last_time = (double)fftime_get_time_ms();
		time_scale = tl->time_scale;
	}
	if(time_scale >= 0.0f)
	{
		tl->position = 0.0;
		tl->events_it = tl->events.begin();
	}
	else
	{
		tl->position = tl->duration;
		tl->events_it = tl->events.end();
	}
}

//Sets the position of this timeline in milliseconds. If time scale is negative, position counts from the end.
void fftimeline_seek(fftimeline_t* tl, double position_ms)
{
	tl->position = position_ms;
	float time_scale;
	if(tl->attached)
		time_scale = *tl->attached_time_scale;
	else
		time_scale = tl->time_scale;
	if(time_scale >= 0.0f)
		tl->events_it = tl->events.lower_bound(position_ms);
	else
	{
		tl->events_it = tl->events.lower_bound(position_ms);
		if(tl->events_it != tl->events.end() && tl->events_it->first == position_ms)
			tl->events_it++;
	}
}

//Sets the timeline to stop playing and resets position to the beginning. If time scale is negative, position is set to the end.
void fftimeline_stop(fftimeline_t* tl)
{
	tl->playing = 0;
	float time_scale;
	if(tl->attached)
		time_scale = *tl->attached_time_scale;
	else
		time_scale = tl->time_scale;
	if(time_scale >= 0.0f)
	{
		tl->position = 0.0;
		tl->events_it = tl->events.begin();
	}
	else
	{
		tl->position = tl->duration;
		tl->events_it = tl->events.end();
	}
}

//Sets the start time of the timeline. By default, the start time is the time when play is called.
void fftimeline_set_last_time(fftimeline_t* tl, double last_time)
{
	tl->last_time = last_time;
}

//Set the duration of the timeline in milliseconds.
void fftimeline_set_duration(fftimeline_t* tl, double duration_ms)
{
	tl->duration = duration_ms;
}

double fftimeline_get_duration(fftimeline_t* tl)
{
	return tl->duration;
}

//Time scale of 1.0 is default and plays at normal speed. A negative time scale will play in reverse.
void fftimeline_set_time_scale(fftimeline_t* tl, float scale)
{
	if(scale >= 0.0f && tl->time_scale < 0.0f)
	{
		tl->position = tl->duration - tl->position;
		tl->events_it = tl->events.lower_bound(tl->position);
	}
	else if(scale < 0.0f && tl->time_scale >= 0.0f)
	{
		tl->position = tl->duration - tl->position;
		tl->events_it = tl->events.lower_bound(tl->position);
		if(tl->events_it != tl->events.end() && tl->events_it->first == tl->position)
			tl->events_it++;
	}
	tl->time_scale = scale;
}

float fftimeline_get_time_scale(fftimeline_t* tl)
{
	return tl->time_scale;
}

//Looping is 0 by default which means that the timeline will not loop and fftimeline_step will eventually return non-zero.
//Setting looping to any non-zero value means that the timeline will play again from the beginning once the end is reached.
void fftimeline_set_looping(fftimeline_t* tl, uint8_t looping)
{
	tl->looping = looping;
}

uint8_t fftimeline_get_looping(fftimeline_t* tl)
{
	return tl->looping;
}

//Add an event function to be called at position milliseconds. param is passed into cb. An event handle is returned
//and can be used to remove the event.
fftimeline_event_t* fftimeline_add_event(fftimeline_t* tl, double position_ms, fftimeline_event_cb cb, void* param)
{
	fftimeline_event_entry entry;
	entry.cb = cb;
	entry.param = param;
	std::list<fftimeline_event_entry> entry_list;
	std::pair<std::map<double, std::list<fftimeline_event_entry>>::iterator, bool> ret;
	ret = tl->events.emplace(position_ms, entry_list);
	ret.first->second.push_back(entry);
	std::list<fftimeline_event_entry>::iterator it = --ret.first->second.end();
	it->handle = ffmem_alloc(fftimeline_event_t);
	it->handle->events_it = ret.first;
	it->handle->entry_it = it;
	return it->handle;
}

void fftimeline_remove_event(fftimeline_t* tl, fftimeline_event_t* event)
{
	event->events_it->second.erase(event->entry_it);
	if(event->events_it->second.size() == 0)
	{
		if(tl->events_it == event->events_it)
		{
			float time_scale;
			if(tl->attached)
				time_scale = *tl->attached_time_scale;
			else
				time_scale = tl->time_scale;
			if(time_scale >= 0.0f)
				tl->events_it++;
			else
				tl->events_it--;
		}
		tl->events.erase(event->events_it);
	}
	ffmem_free(event);
}

void fftimeline_clear_events(fftimeline_t* tl)
{
	tl->events.clear();
	tl->events_it = tl->events.end();
}

double fftimeline_get_event_position(fftimeline_event_t* event)
{
	return event->events_it->first;
}

//This is used internally by ffgfx when a timeline is attached to an ffgfx_node. This causes timeline to use node timings.
void fftimeline_use_node(fftimeline_t* tl, double* current_time, double* delta_time, float* time_scale)
{
	tl->attached = 1;
	tl->attached_current_time = current_time;
	tl->attached_delta_time = delta_time;
	tl->attached_time_scale = time_scale;
}

void fftimeline_unuse_node(fftimeline_t* tl)
{
	tl->attached = 0;
}
