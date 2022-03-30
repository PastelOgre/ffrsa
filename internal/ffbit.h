/*
 * ffbit.h
 *
 *  Created on: May 20, 2018
 *      Author: Jesse Wang
 */

#ifndef FFBIT_H_
#define FFBIT_H_

//Utility library to aid reading and writing of bits into and out of a buffer in an endian unaware way.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FFBIT ffbit_t;

ffbit_t* ffbit_create(void* buf);

void ffbit_destroy(ffbit_t* p);

void ffbit_set(ffbit_t* p, void* buf, uint32_t bit_offset);

uint64_t ffbit_read(ffbit_t* p, uint32_t num_bits);

//num_bits can't be greater than 64. If it is, this function is a NO-OP.
void ffbit_write(ffbit_t* p, uint32_t num_bits, uint64_t val);

void ffbit_skip(ffbit_t* p, uint32_t num_bits);

#ifdef __cplusplus
}
#endif

#endif /* FFBIT_H_ */
