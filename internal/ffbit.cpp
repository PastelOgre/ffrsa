/*
 * ffbit.cpp
 *
 *  Created on: May 20, 2018
 *      Author: Jesse Wang
 */

#include "ffbit.h"
#include "ffmem.h"
#include <string.h>

typedef struct FFBIT
{
	uint8_t* p;
	uint8_t bit_index;
} ffbit_t;

ffbit_t* ffbit_create(void* buf)
{
	ffbit_t* ret = ffmem_alloc(ffbit_t);
	ret->p = (uint8_t*)buf;
	ret->bit_index = 0;
	return ret;
}

void ffbit_destroy(ffbit_t* p)
{
	ffmem_free(p);
}

void ffbit_set(ffbit_t* p, void* buf, uint32_t bit_offset)
{
	p->p = (uint8_t*)buf + bit_offset/8;
	p->bit_index = bit_offset%8;
}

uint64_t ffbit_read(ffbit_t* p, uint32_t num_bits)
{
	uint64_t ret = 0;
	uint32_t i = 0;
	while(num_bits > 0)
	{
		uint32_t bits_left = 8 - p->bit_index;
		if(num_bits < bits_left)
			bits_left = num_bits;
		uint32_t mask = 1;
		mask <<= bits_left;
		mask--;
		ret |= ((uint64_t)((*(p->p+i))>>(8-p->bit_index-bits_left))&mask)<<(num_bits-bits_left);
		p->bit_index += bits_left;
		if(p->bit_index >= 8)
		{
			i++;
			p->bit_index -= 8;
		}
		num_bits -= bits_left;
	}
	p->p += i;
	return ret;
}

void ffbit_write(ffbit_t* p, uint32_t num_bits, uint64_t val)
{
	if(num_bits > 64)
		return;
	uint8_t buf[10];
	memset(buf, 0, 10);
	uint32_t shift = p->bit_index + num_bits%8;
	buf[9] = val << (16-shift) & 0xFF;
	buf[8] = (val << (16-shift) & 0xFF00) >> 8;
	buf[7] = (val >> shift) & 0xFF;
	buf[6] = (val >> (shift + 8)) & 0xFF;
	buf[5] = (val >> (shift + 16)) & 0xFF;
	buf[4] = (val >> (shift + 24)) & 0xFF;
	buf[3] = (val >> (shift + 32)) & 0xFF;
	buf[2] = (val >> (shift + 40)) & 0xFF;
	buf[1] = (val >> (shift + 48)) & 0xFF;
	buf[0] = (val >> (shift + 56)) & 0xFF;
	*p->p >>= 8-p->bit_index;
	*p->p <<= 8-p->bit_index;
	if(shift >= num_bits)
	{
		p->p[0] |= buf[8];
		if(shift > 8)
			p->p[1] = buf[9];
	}
	else
	{
		uint32_t num_bytes = (num_bits-shift) / 8;
		if((num_bits-shift)%8 != 0)
			num_bytes++;
		*p->p |= buf[8-num_bytes];
		if(shift > 8)
			memcpy(p->p+1, buf+8-num_bytes+1, num_bytes-1+2);
		else if(shift != 0)
			memcpy(p->p+1, buf+8-num_bytes+1, num_bytes-1+1);
		else
			memcpy(p->p+1, buf+8-num_bytes+1, num_bytes-1);
	}
	ffbit_skip(p, num_bits);
}

void ffbit_skip(ffbit_t* p, uint32_t num_bits)
{
	uint32_t r = num_bits%8;
	p->bit_index += r;
	r = p->bit_index/8;
	p->p += num_bits/8 + r;
	p->bit_index -= r*8;
}
