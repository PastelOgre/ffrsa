/*
 * fftime.h
 *
 *  Created on: Apr 19, 2018
 *      Author: Jesse Wang
 */

#ifndef FFTIME_H_
#define FFTIME_H_

#include <stdint.h>

typedef void (*fftime_loop_cb)(void* param);
typedef class FFTIMELINE fftimeline_t;
typedef class FFTIMELINE_EVENT fftimeline_event_t;
typedef void (*fftimeline_event_cb)(fftimeline_t* tl, double position_ms, void* param);


#ifdef __cplusplus
extern "C" {
#endif

uint32_t fftime_get_time_ms();

void fftime_sleep_ms(uint32_t ms);

//Calls the function specified in an infinite loop at specified frames per second (fps).
//running should be set to 1 initially. Set it to 0 at any point in time to exit this function.
void fftime_loop(fftime_loop_cb func, void* param, int fps, uint8_t* running);

//Duration is in milliseconds.
fftimeline_t* fftimeline_create(double duration_ms);
void fftimeline_destroy(fftimeline_t* tl);

//Returns 0 on normal operation or returns the time in milliseconds when the timeline finished playing.
//Events are called in this function.
double fftimeline_step(fftimeline_t* tl);

//Set the timeline to play from the beginning so timeline events can be triggered.
void fftimeline_play(fftimeline_t* tl);

//Sets the position of this timeline in milliseconds. If time scale is negative, position counts from the end.
void fftimeline_seek(fftimeline_t* tl, double position_ms);

//Sets the timeline to stop playing and resets position to the beginning. If time scale is negative, position is set to the end.
void fftimeline_stop(fftimeline_t* tl);

//Overrides the time of the last step. This is useful when you want to play a timeline right after another accurately.
//For example, fftimeline_step returns the finish time. You can play another timeline right after by calling fftimeline_play
//on the new timeline, then call this function on the new timeline with the finish time of the last timeline.
void fftimeline_set_last_time(fftimeline_t* tl, double start_time);

//Set the duration of the timeline in milliseconds.
void fftimeline_set_duration(fftimeline_t* tl, double duration_ms);
double fftimeline_get_duration(fftimeline_t* tl);

//Time scale of 1.0 is default and plays at normal speed. A negative time scale will play in reverse.
void fftimeline_set_time_scale(fftimeline_t* tl, float scale);
float fftimeline_get_time_scale(fftimeline_t* tl);

//Looping is 0 by default which means that the timeline will not loop and fftimeline_step will eventually return non-zero.
//Setting looping to any non-zero value means that the timeline will play again from the beginning once the end is reached.
void fftimeline_set_looping(fftimeline_t* tl, uint8_t looping);
uint8_t fftimeline_get_looping(fftimeline_t* tl);

//Add an event function to be called at position milliseconds. param is passed into cb. An event handle is returned
//and can be used to remove the event.
fftimeline_event_t* fftimeline_add_event(fftimeline_t* tl, double position_ms, fftimeline_event_cb cb, void* param);
void fftimeline_remove_event(fftimeline_t* tl, fftimeline_event_t* event);
void fftimeline_clear_events(fftimeline_t* tl);
double fftimeline_get_event_position(fftimeline_event_t* event);

//This is used internally by ffgfx when a timeline is attached to an ffgfx_node. This causes timeline to use node timings.
void fftimeline_use_node(fftimeline_t* tl, double* current_time, double* delta_time, float* time_scale);
void fftimeline_unuse_node(fftimeline_t* tl);

#ifdef __cplusplus
}
#endif

#endif /* FFTIME_H_ */
