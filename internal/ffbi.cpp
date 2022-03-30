/*
 * ffbi.cpp
 *
 *  Created on: Apr 20, 2018
 *      Author: Jesse Tse-Hsu Wang
 */

//NOTE: Current implementation contains the minimum needed subset of bigint math functionality to
//		implement RSA. Negative bigints currently not supported.

#include "ffbi.h"
#include "ffmem.h"
#include "fflog.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <list>
#include "fftime.h"

#define FFBI_RAND_BITS 16
#define FFBI_REALLOC_GROWTH_FACTOR 2.0
#define FFBI_DIV_DEBUG 0
#define FFBI_MIN_ALLOC_DIGITS 3
#define FFBI_PRIME_TEST_NUM_SCRATCHES 4
#define FFBI_MOD_POW_NUM_SCRATCHES 6
#define FFBI_MUL_CACHE_ENABLED 0
#define FFBI_DIV_CACHE_ENABLED 1

#if defined(__GNUC__) && !defined(__ANDROID__) && !defined(__APPLE__)
	#if FFBI_MUL_CACHE_ENABLED
	#define FFBI_CACHE_MUL_BITS_PER_DIGIT 64
	#endif
	#if FFBI_DIV_CACHE_ENABLED
	#define FFBI_CACHE_DIV_BITS_PER_DIGIT 32
	#endif
	#define FFBI_CACHE_WORD_SIZE 128
	typedef unsigned __int128 ffbi_cache_word_t;
#else
	#if FFBI_MUL_CACHE_ENABLED
	#define FFBI_CACHE_MUL_BITS_PER_DIGIT 32
	#endif
	#if FFBI_DIV_CACHE_ENABLED
	#define FFBI_CACHE_DIV_BITS_PER_DIGIT 16
	#endif
	#define FFBI_CACHE_WORD_SIZE 64
	typedef uint64_t ffbi_cache_word_t;
#endif

static ffbi_word_t _digit_max;
static ffbi_word_t _digit_max_plus_1;
static uint8_t _rand_not_seeded = 1;
static const ffbi_word_t _rand_max = (ffbi_word_t)pow(2.0, (double)FFBI_RAND_BITS) - 1;
static const ffbi_word_t _rand_max_plus_1 = (ffbi_word_t)pow(2.0, (double)FFBI_RAND_BITS);
static uint8_t _ffbi_initialized = 0;

#if FFBI_MUL_CACHE_ENABLED
static ffbi_cache_word_t _cache_mul_digit_max;
static uint32_t _min_mul_cache_len;
#endif

#if FFBI_DIV_CACHE_ENABLED
static ffbi_cache_word_t _cache_div_digit_max;
static ffbi_cache_word_t _cache_div_digit_max_plus_1;
#endif

typedef struct FFBI
{
	uint32_t num_allocated_digits;
	uint32_t num_used_digits;
	ffbi_word_t* digits;
	uint8_t reallocation_allowed;
	ffbi_cache_word_t* cache;
	uint32_t cache_num_allocated_digits;
	uint32_t cache_num_used_digits;
	uint8_t cache_valid;
	uint32_t cache_bits_per_digit;
} ffbi_t;

struct FFBI_SCRATCH
{
	ffbi_t** val;
	int num_vals;
	ffbi_scratch_t* child;
	uint32_t num_children;
};

void ffbi_get_digits(ffbi_t* p, ffbi_word_t** digits, uint32_t* num_used_digits, uint32_t* num_allocated_digits, uint32_t* bits_per_digit)
{
	*bits_per_digit = FFBI_BITS_PER_DIGIT;
	*num_used_digits = p->num_used_digits;
	*num_allocated_digits = p->num_allocated_digits;
	*digits = p->digits;
}

void ffbi_set_digits(ffbi_t* p, ffbi_word_t* digits, uint32_t num_used_digits, uint32_t num_allocated_digits, uint32_t bits_per_digit)
{
	if(bits_per_digit != FFBI_BITS_PER_DIGIT)
	{
		fflog_debug_print("Automatic conversion of bits_per_digit is not supported currently. The only accepted value is %d.\n", FFBI_BITS_PER_DIGIT);
		return;
	}
	if(digits)
	{
		ffmem_free_arr(p->digits);
		p->digits = digits;
	}
	p->num_used_digits = num_used_digits;
	p->num_allocated_digits = num_allocated_digits;
}

void ffbi_init()
{
	if(_ffbi_initialized == 0)
	{
		_digit_max_plus_1 = 1;
		_digit_max_plus_1 <<= FFBI_BITS_PER_DIGIT;
		_digit_max = _digit_max_plus_1 - 1;

#if FFBI_MUL_CACHE_ENABLED
		_cache_mul_digit_max = 1;
		_cache_mul_digit_max <<= FFBI_CACHE_MUL_BITS_PER_DIGIT;
		_cache_mul_digit_max--;
		_min_mul_cache_len = FFBI_CACHE_MUL_BITS_PER_DIGIT/FFBI_BITS_PER_DIGIT;
		if(FFBI_CACHE_MUL_BITS_PER_DIGIT%FFBI_BITS_PER_DIGIT > 0)
			_min_mul_cache_len++;
#endif

#if FFBI_DIV_CACHE_ENABLED
		_cache_div_digit_max_plus_1 = 1;
		_cache_div_digit_max_plus_1 <<= FFBI_CACHE_DIV_BITS_PER_DIGIT;
		_cache_div_digit_max = _cache_div_digit_max_plus_1 - 1;
#endif

		_ffbi_initialized = 1;
	}
}

//Makes sure cache in p has at least cache_num_digits allocated.
//Cache contents are invalidated if retain_value is 0.
static void ffbi_cache_prepare(ffbi_t* p, uint32_t cache_num_digits, uint8_t retain_value)
{
	if(p->cache == NULL)
	{
		p->cache = ffmem_alloc_arr(ffbi_cache_word_t, cache_num_digits);
		p->cache_num_allocated_digits = cache_num_digits;
	}
	else if(p->cache_num_allocated_digits < cache_num_digits)
	{
		ffbi_cache_word_t* new_cache = ffmem_alloc_arr(ffbi_cache_word_t, cache_num_digits);
		if(retain_value)
			memcpy(new_cache, p->cache, sizeof(ffbi_cache_word_t)*p->cache_num_used_digits);
		ffmem_free_arr(p->cache);
		p->cache = new_cache;
		p->cache_num_allocated_digits = cache_num_digits;
	}
}

static uint32_t ffbi_significant_bits_uint8(uint8_t num)
{
	if(num == 0)
		return 1;
	uint32_t count = 0;
	while(num>0)
	{
		num>>=1;
		count += 1;
	}
	return count;
}

static uint32_t ffbi_significant_bits_cache_word(ffbi_cache_word_t num)
{
	if(num == 0)
		return 1;
	uint32_t count = 0;
	while(num>0)
	{
		num>>=1;
		count += 1;
	}
	return count;
}

static ffbi_word_t ffbi_significant_bits(ffbi_word_t num)
{
	if(num == 0)
		return 1;
	uint32_t count = 0;
	while(num>0)
	{
		num>>=1;
		count += 1;
	}
	return count;
}

/*
//This function assumes the src cache is already built.
static void ffbi_copy_cache(ffbi_t* dest, ffbi_t* src)
{
	if(dest->cache_num_allocated_digits < src->cache_num_used_digits)
		ffbi_cache_prepare(dest, src->cache_num_used_digits, 0);
	dest->cache_num_used_digits = src->cache_num_used_digits;
	memcpy(dest->cache, src->cache, src->cache_num_used_digits*sizeof(ffbi_cache_word_t));
	dest->cache_bits_per_digit = src->cache_bits_per_digit;
}*/

typedef struct FFBI_BASE_CONVERT
{
	uint32_t dst_bits_per_digit;
	uint32_t src_bits_per_digit;
	uint32_t total_used_bits;
	uint32_t dst_num_full_digits;
	uint32_t dst_remaining_bits;
	uint32_t dst_num_digits;
	uint32_t src_num_digits;
	uint32_t dst_digit_max;
} ffbi_base_convert_t;

static ffbi_base_convert_t* ffbi_base_convert_create(uint32_t dst_bits_per_digit, uint32_t src_bits_per_digit, uint32_t total_used_bits)
{
	ffbi_base_convert_t* ret = ffmem_alloc(ffbi_base_convert_t);
	ret->dst_bits_per_digit = dst_bits_per_digit;
	ret->src_bits_per_digit = src_bits_per_digit;
	ret->total_used_bits = total_used_bits;
	ret->src_num_digits = total_used_bits/src_bits_per_digit + (total_used_bits%src_bits_per_digit > 0);
	ret->dst_num_full_digits = total_used_bits/dst_bits_per_digit;
	ret->dst_remaining_bits = total_used_bits%dst_bits_per_digit;
	ret->dst_num_digits = ret->dst_num_full_digits + (ret->dst_remaining_bits > 0);
	return ret;
}

static void ffbi_base_convert_destroy(ffbi_base_convert_t* ctx)
{
	ffmem_free(ctx);
}

template<typename dst_t, typename src_t>
static void ffbi_base_convert_exec(ffbi_base_convert_t* ctx, dst_t* dst, src_t* src)
{
	memset(dst, 0, ctx->dst_num_digits*sizeof(dst_t));
	dst_t dst_digit_max = 1;
	dst_digit_max <<= ctx->dst_bits_per_digit;
	dst_digit_max--;
	uint32_t src_bit_idx = 0;
	uint32_t src_digit_idx = 0;
	uint32_t i = 0;
	for(;i<ctx->dst_num_full_digits;i++)
	{
		uint32_t dest_bit_idx = 0;
		while(dest_bit_idx < ctx->dst_bits_per_digit)
		{
			uint32_t pullable_bits = ctx->src_bits_per_digit-src_bit_idx;
			uint32_t bits_to_pull = ctx->dst_bits_per_digit-dest_bit_idx;
			if(bits_to_pull >= pullable_bits)
			{
				dst_t temp = src[src_digit_idx]>>src_bit_idx;
				temp <<= dest_bit_idx;
				dst[i] |= temp;
				dest_bit_idx += pullable_bits;
				src_bit_idx = 0;
				src_digit_idx++;
			}
			else
			{
				dst_t temp = src[src_digit_idx]>>src_bit_idx;
				temp <<= dest_bit_idx;
				dst[i] |= temp;
				dst[i] &= dst_digit_max;
				dest_bit_idx += bits_to_pull;
				src_bit_idx += bits_to_pull;
			}
		}
	}
	if(ctx->dst_remaining_bits > 0)
	{
		uint32_t dest_bit_idx = 0;
		while(dest_bit_idx < ctx->dst_remaining_bits)
		{
			uint32_t pullable_bits = ctx->src_bits_per_digit-src_bit_idx;
			uint32_t bits_to_pull = ctx->dst_remaining_bits-dest_bit_idx;
			if(bits_to_pull >= pullable_bits)
			{
				dst_t temp = src[src_digit_idx]>>src_bit_idx;
				temp <<= dest_bit_idx;
				dst[i] |= temp;
				dest_bit_idx += pullable_bits;
				src_bit_idx = 0;
				src_digit_idx++;
			}
			else
			{
				dst_t temp = src[src_digit_idx]>>src_bit_idx;
				temp <<= dest_bit_idx;
				dst[i] |= temp;
				dst[i] &= dst_digit_max;
				dest_bit_idx += bits_to_pull;
				src_bit_idx += bits_to_pull;
			}
		}
	}
}

//Loads cache into normal representation. Cache is expected to be correctly built.
static void ffbi_cache_retrieve(ffbi_t* p)
{
	uint32_t sigbits = ffbi_significant_bits_cache_word(p->cache[p->cache_num_used_digits-1]);
	uint32_t total_used_bits = (p->cache_num_used_digits-1)*p->cache_bits_per_digit + sigbits;
	ffbi_base_convert_t* ctx = ffbi_base_convert_create(FFBI_BITS_PER_DIGIT, p->cache_bits_per_digit, total_used_bits);
	if(ctx->dst_num_digits > p->num_allocated_digits)
		ffbi_reallocate_digits(p, ctx->dst_num_digits, 0);
	p->num_used_digits = ctx->dst_num_digits;
	ffbi_base_convert_exec<ffbi_word_t, ffbi_cache_word_t>(ctx, p->digits, p->cache);
	ffbi_base_convert_destroy(ctx);
	p->cache_valid = 1;
}

static void ffbi_cache_update(ffbi_t* p, uint32_t target_bits_per_digit, ffbi_cache_word_t cache_digit_max)
{
	if(p->cache_valid && target_bits_per_digit == p->cache_bits_per_digit)
		return;
	p->cache_bits_per_digit = target_bits_per_digit;
	uint32_t sigbits = ffbi_significant_bits(p->digits[p->num_used_digits-1]);
	uint32_t total_used_bits = (p->num_used_digits-1)*FFBI_BITS_PER_DIGIT + sigbits;
	ffbi_base_convert_t* ctx = ffbi_base_convert_create(p->cache_bits_per_digit, FFBI_BITS_PER_DIGIT, total_used_bits);
	p->cache_num_used_digits = ctx->dst_num_digits;
	if(p->cache)
	{
		if(p->cache_num_used_digits > p->cache_num_allocated_digits)
		{
			ffmem_free_arr(p->cache);
			p->cache = ffmem_alloc_arr(ffbi_cache_word_t, p->cache_num_used_digits);
			p->cache_num_allocated_digits = p->cache_num_used_digits;
		}
	}
	else
	{
		p->cache = ffmem_alloc_arr(ffbi_cache_word_t, p->cache_num_used_digits);
		p->cache_num_allocated_digits = p->cache_num_used_digits;
	}
	ffbi_base_convert_exec<ffbi_cache_word_t, ffbi_word_t>(ctx, p->cache, p->digits);
	ffbi_base_convert_destroy(ctx);
	p->cache_valid = 1;
}

//Create a new bigint with value of 0.
ffbi_t* ffbi_create()
{
	ffbi_t* ret = ffmem_alloc(ffbi_t);
	memset(ret, 0, sizeof(ffbi_t));
	ret->num_allocated_digits = FFBI_MIN_ALLOC_DIGITS;
	ret->digits = ffmem_alloc_arr(ffbi_word_t, ret->num_allocated_digits);
	ret->reallocation_allowed = 1;
	ret->num_used_digits = 1;
	ret->digits[0] = 0;
	ffbi_init();
	return ret;
}

//Create a new bigint with value of 0. Initial memory capacity is allocated to fit up to specified number of bits.
ffbi_t* ffbi_create_reserved_bits(uint32_t bits)
{
	int num_digits = bits/FFBI_BITS_PER_DIGIT;
	if(bits%FFBI_BITS_PER_DIGIT)
		num_digits++;
	if(num_digits < FFBI_MIN_ALLOC_DIGITS)
	{
		fflog_debug_print("bits arg must be at least %d.\n", FFBI_BITS_PER_DIGIT*FFBI_MIN_ALLOC_DIGITS);
		return NULL;
	}
	ffbi_t* ret = ffmem_alloc(ffbi_t);
	memset(ret, 0, sizeof(ffbi_t));
	ret->num_allocated_digits = num_digits;
	ret->digits = ffmem_alloc_arr(ffbi_word_t, num_digits);
	ret->reallocation_allowed = 1;
	ret->num_used_digits = 1;
	ret->digits[0] = 0;
	ffbi_init();
	return ret;
}

ffbi_t* ffbi_create_reserved_digits(uint32_t digits)
{
	if(digits < FFBI_MIN_ALLOC_DIGITS)
		digits = FFBI_MIN_ALLOC_DIGITS;
	ffbi_t* ret = ffmem_alloc(ffbi_t);
	memset(ret, 0, sizeof(ffbi_t));
	ret->num_allocated_digits = digits;
	ret->digits = ffmem_alloc_arr(ffbi_word_t, digits);
	ret->reallocation_allowed = 1;
	ret->num_used_digits = 1;
	ret->digits[0] = 0;
	ffbi_init();
	return ret;
}

//Create a new bigint with value of 0 using a preallocated memory buffer.
//Bigints created in this manner have the special property that the memory used
//cannot grow or shrink. Operations that cause overflow will be aborted.
ffbi_t* ffbi_create_preallocated(uint8_t* buffer, int size_bytes)
{
	if(buffer == NULL)
	{
		fflog_debug_print("invalid argument(s).\n");
		return NULL;
	}
	if(size_bytes < (int)(FFBI_MIN_ALLOC_DIGITS*sizeof(uint32_t)))
	{
		fflog_debug_print("size_bytes can't be less than %d.\n", (int)(FFBI_MIN_ALLOC_DIGITS*sizeof(uint32_t)));
		return NULL;
	}
	ffbi_t* ret = ffmem_alloc(ffbi_t);
	memset(ret, 0, sizeof(ffbi_t));
	ret->num_allocated_digits = size_bytes/sizeof(ffbi_word_t);
	ret->digits = (ffbi_word_t*)buffer;
	ret->reallocation_allowed = 0;
	ret->num_used_digits = 1;
	ret->digits[0] = 0;
	ffbi_init();
	return ret;
}

ffbi_scratch_t* ffbi_scratch_create()
{
	ffbi_scratch_t* ret = ffmem_alloc(ffbi_scratch_t);
	memset(ret, 0, sizeof(ffbi_scratch_t));
	ffbi_init();
	return ret;
}

static void ffbi_scratch_destroy_impl(ffbi_scratch_t* scratch, uint8_t free_scratch, uint8_t is_arr)
{
	if(scratch->num_children > 1)
	{
		for(uint32_t i=1;i<scratch->num_children;i++)
			ffbi_scratch_destroy_impl(&scratch->child[i], 0, 0);
		ffbi_scratch_destroy_impl(scratch->child, 1, 1);
	}
	else if(scratch->num_children == 1)
		ffbi_scratch_destroy_impl(scratch->child, 1, 0);
	if(scratch->num_vals > 0)
	{
		for(int i=0;i<scratch->num_vals;i++)
			ffbi_destroy(scratch->val[i]);
		ffmem_free_arr(scratch->val);
	}
	if(free_scratch)
	{
		if(is_arr)
			ffmem_free_arr(scratch);
		else
			ffmem_free(scratch);
	}
}

void ffbi_scratch_destroy(ffbi_scratch_t* scratch)
{
	ffbi_scratch_destroy_impl(scratch, 1, 0);
}

//Makes sure scratch has num_vals bigints available to use with at least val_digits
//number of allocated digits each.
static void ffbi_scratch_prepare(ffbi_scratch_t* scratch, int num_vals, int val_digits)
{
	int num_existing_vals = scratch->num_vals;
	if(scratch->num_vals == 0)
	{
		scratch->val = ffmem_alloc_arr(ffbi_t*, num_vals);
		scratch->num_vals = num_vals;
		for(int i=0;i<num_vals;i++)
			scratch->val[i] = ffbi_create_reserved_digits(val_digits);
		return;
	}
	if(num_vals > scratch->num_vals)
	{
		ffbi_t** val = ffmem_alloc_arr(ffbi_t*, num_vals);
		int i = 0;
		for(;i<scratch->num_vals;i++)
			val[i] = scratch->val[i];
		for(;i<num_vals;i++)
			val[i] = ffbi_create_reserved_digits(val_digits);
		ffmem_free_arr(scratch->val);
		scratch->val = val;
		scratch->num_vals = num_vals;
	}
	for(int i=0;i<num_existing_vals;i++)
	{
		if(scratch->val[i]->num_allocated_digits < (uint32_t)val_digits)
			ffbi_reallocate_digits(scratch->val[i], val_digits, 1);
	}
}

//Create a new bigint to be used as a sieve for primality testing.
//n is the max possible prime value that the sieve contains.
//A recommended value is 100000. NULL is returned on error.
void ffbi_get_sieve(ffbi_scratch_t* sieve, uint32_t n)
{
	if(n < 3)
	{
		fflog_debug_print("n can't be less than 3.\n");
		return;
	}
	uint8_t* arr =  ffmem_alloc_arr(uint8_t, n);
	memset(arr, 1, n);
	ffbi_word_t k = 2;
	ffbi_word_t k_end = n/2+1;
	ffbi_word_t i;
	while(1)
	{
		//get new k
		for(;k<k_end;k++)
		{
			if(arr[k] == 1)
				break;
		}
		//check if done
		if(k==k_end)
			break;
		//if not done, cross out multiples of k
		for(i=k*2;i<n;i+=k)
			arr[i] = 0;
		k++;
	}
	//count primes
	int num_primes = 0;
	for(k=3;k<n;k++)
		if(arr[k] == 1)
			num_primes++;
	//insert them into a sieve
	if(sieve->num_vals > 0)
	{
		for(i=0;i<(uint32_t)sieve->num_vals;i++)
			ffbi_destroy(sieve->val[i]);
		ffmem_free_arr(sieve->val);
	}
	sieve->num_vals = num_primes;
	sieve->val = ffmem_alloc_arr(ffbi_t*, num_primes);
	int num_digits = 32/FFBI_BITS_PER_DIGIT+((32%FFBI_BITS_PER_DIGIT) > 0);
	int alloc_digits = num_digits;
	if(alloc_digits < FFBI_MIN_ALLOC_DIGITS)
		alloc_digits = FFBI_MIN_ALLOC_DIGITS;
	for(i=0;i<(uint32_t)num_primes;i++)
		sieve->val[i] = ffbi_create_reserved_digits(alloc_digits);
	i=0;
	for(k=3;k<n;k++)
	{
		if(arr[k] == 1)
		{
			int j;
			for(j=0;j<num_digits;j++)
				sieve->val[i]->digits[j] = (k>>(j*FFBI_BITS_PER_DIGIT))&_digit_max;
			j--;
			for(;j>0;j--)
			{
				if(sieve->val[i]->digits[j] != 0)
					break;
			}
			sieve->val[i]->num_used_digits = j+1;
			i++;
		}
	}
	ffmem_free_arr(arr);
}

//Generate a random bigint with specified number of bits.
void ffbi_random(ffbi_t* p, uint32_t num_bits)
{
	if(num_bits < FFBI_BITS_PER_DIGIT)
	{
		fflog_debug_print("num_bits can't be less than %d.\n", FFBI_BITS_PER_DIGIT);
		return;
	}
	int num_full_digits = num_bits/FFBI_BITS_PER_DIGIT;
	int num_digits = num_full_digits;
	int remaining_bits = num_bits%FFBI_BITS_PER_DIGIT;
	if(remaining_bits > 0)
		num_digits++;
	if(p->num_allocated_digits < (uint32_t)num_digits)
			ffbi_reallocate_digits(p, num_digits, 0);
	p->num_used_digits = num_digits;

	//seed the random number generator once per program execution
	if(_rand_not_seeded)
	{
		_rand_not_seeded = 0;
		srand(time(NULL));
	}

	//generate a random value for each digit
	//fflog_print("num_full_digits=%d, num_digits=%d, remaining_bits=%d\n", num_full_digits, num_digits, remaining_bits);
	int i;
	for(i=0;i<num_full_digits;i++)
	{
		int bits_left = FFBI_BITS_PER_DIGIT;
		p->digits[i] = 0;
		while(bits_left >= FFBI_RAND_BITS)
		{
			p->digits[i] <<= FFBI_RAND_BITS;
			p->digits[i] += rand()%(_rand_max_plus_1);
			bits_left-=FFBI_RAND_BITS;
		}
		if(bits_left > 0)
		{
			p->digits[i] <<= bits_left;
			p->digits[i] += rand()%((int)pow(2.0, (double)bits_left));
		}
	}
	if(remaining_bits > 0)
	{
		int bits_left = remaining_bits;
		p->digits[i] = 0;
		while(bits_left >= FFBI_RAND_BITS)
		{
			p->digits[i] <<= FFBI_RAND_BITS;
			p->digits[i] += rand()%(_rand_max_plus_1);
			bits_left-=FFBI_RAND_BITS;
		}
		if(bits_left > 0)
		{
			p->digits[i] <<= bits_left;
			p->digits[i] += rand()%((int)pow(2.0, (double)bits_left));
		}
		p->digits[i] |= ((ffbi_word_t)1) << (remaining_bits-1);
	}
	else
		p->digits[i-1] |= ((ffbi_word_t)1) << (FFBI_BITS_PER_DIGIT-1);
	p->cache_valid = 0;
}

void ffbi_random_with_limit(ffbi_t* p, ffbi_t* limit)
{
	if(p->num_allocated_digits < limit->num_used_digits)
		ffbi_reallocate_digits(p, limit->num_used_digits, 0);
	p->num_used_digits = limit->num_used_digits;

	//seed the random number generator once per program execution
	if(_rand_not_seeded)
	{
		_rand_not_seeded = 0;
		srand(time(NULL));
	}

	//generate a random value for each digit
	uint8_t p_is_less = 0;
	int i;
	//fflog_print("line=%d, rand_max=%u, p_num_digits=%u, lim_num_digits=%u, p_alloc_digit=%u, lim_alloc_digits=%u\n", __LINE__, _rand_max, p->num_used_digits, limit->num_used_digits, p->num_allocated_digits, limit->num_allocated_digits);
	for(i=(int)p->num_used_digits-1;i>=0;i--)
	{
		int bits_left = FFBI_BITS_PER_DIGIT;
		p->digits[i] = 0;
		//fflog_print("line=%d, i=%d, p_digit=%u, lim_digit=%u\n", __LINE__, i, p->digits[i], limit->digits[i]);
		while(bits_left >= FFBI_RAND_BITS)
		{
			p->digits[i] <<= FFBI_RAND_BITS;
			bits_left-=FFBI_RAND_BITS;
			ffbi_word_t rand_val = rand();
			if(!p_is_less)
			{
				ffbi_word_t lim = (limit->digits[i]>>bits_left) & _rand_max;
				rand_val %= lim+1;
				//fflog_print("line=%d, rand_val=%u, lim=%u\n", __LINE__, rand_val, lim);
				if(rand_val < lim)
				{
					p_is_less = 1;
					//fflog_print("line=%d\n", __LINE__);
				}
			}
			else
				rand_val %= _rand_max_plus_1;
			p->digits[i] += rand_val;
		}
		//fflog_print("line=%d, bits_left=%d\n", __LINE__, bits_left);
		if(bits_left > 0)
		{
			p->digits[i] <<= bits_left;
			ffbi_word_t rand_val = rand();
			uint32_t shift_amount = FFBI_WORD_SIZE-bits_left;
			if(!p_is_less)
			{
				ffbi_word_t lim = (limit->digits[i]<<shift_amount)>>shift_amount;
				rand_val %= lim+1;
				//fflog_print("line=%d, rand_val=%u, lim=%u\n", __LINE__, rand_val, lim);
				if(rand_val < lim)
					p_is_less = 1;
			}
			else
			{
				rand_val %= (_digit_max >> shift_amount)+1;
			}
			p->digits[i] += rand_val;
		}
		//fflog_print("line=%d, p_result=%u, p_is_less=%u\n", __LINE__, p->digits[i], p_is_less);
	}
	if(!p_is_less)
	{
		for(i=0;i<(int)p->num_used_digits;i++)
		{
			if(p->digits[i] > 0)
			{
				p->digits[i]--;
				p_is_less = 1;
				break;
			}
		}
		if(!p_is_less)
			fflog_debug_print("limit should be greater than 0.\n");
	}
	for(i=p->num_used_digits-1;i>=0;i--)
	{
		if(p->digits[i] != 0)
			break;
	}
	p->num_used_digits = i+1;
	p->cache_valid = 0;
}

//Generate a random large prime bigint with specified number of bits.
//The more tests, the greater the chance of the bigint to actually be prime,
//but the time it takes is considerably longer. 20 for num_tests is recommended
//as it would mean there is about one-in-a-million chance for the bigint to
//not be prime. A sieve may be optionally provided to quickly weed out
//composites before the Fermat primality test. Pass NULL for sieve to skip
//the sieve test. NULL is returned on error.
ffbi_t* ffbi_create_random_large_prime(uint32_t bits, uint32_t num_tests, ffbi_scratch_t* sieve)
{
	ffbi_t* ret = ffbi_create_reserved_bits(bits);
	if(ret)
	{
		ffbi_scratch_t* scratch = ffbi_scratch_create();
		ffbi_scratch_prepare(scratch, FFBI_PRIME_TEST_NUM_SCRATCHES, ret->num_allocated_digits);
		scratch->num_children = 1;
		scratch->child = ffbi_scratch_create();
		ffbi_scratch_prepare(scratch, FFBI_MOD_POW_NUM_SCRATCHES, ret->num_allocated_digits);
		scratch->child->child = ffbi_scratch_create();
		scratch->child->num_children = 1;
		do
		{
			ffbi_random(ret, bits);
			ret->digits[0] |= 1;
		}
		while(!ffbi_is_large_prime(ret, (int)num_tests, sieve, scratch));
		ffbi_scratch_destroy(scratch);
	}
	return ret;
}

//Create a new bigint by making a copy of p. NULL is returned on error.
ffbi_t* ffbi_create_from_bigint(ffbi_t* p)
{
	ffbi_t* ret = ffbi_create_reserved_digits(p->num_allocated_digits);
	ffbi_copy(ret, p);
	return ret;
}

void ffbi_destroy(ffbi_t* p)
{
	if(p->reallocation_allowed)
		ffmem_free_arr(p->digits);
	if(p->cache)
		ffmem_free_arr(p->cache);
	ffmem_free(p);
}

//Pass 1 for retain_value if retaining the value is important. Passing 0
//will result in memory copying and extra checks to see if target_num_digits
//should be respected.
void ffbi_reallocate_digits(ffbi_t* p, int target_num_digits, uint8_t retain_value)
{
	if(p->reallocation_allowed == 0)
	{
		fflog_debug_print("reallocation not allowed.\n");
		return;
	}
	if(retain_value)
	{
		if(target_num_digits < (int)p->num_used_digits)
			target_num_digits = p->num_used_digits;
	}
	else
	{
		p->num_used_digits = 1;
		p->cache_valid = 0;
	}
	if(target_num_digits == (int)p->num_allocated_digits)
		return;
	if(target_num_digits < FFBI_MIN_ALLOC_DIGITS)
		target_num_digits = FFBI_MIN_ALLOC_DIGITS;
	ffbi_word_t* new_allocation = ffmem_alloc_arr(ffbi_word_t, target_num_digits);
	if(retain_value)
		memcpy(new_allocation, p->digits, p->num_used_digits*sizeof(ffbi_word_t));
	ffmem_free_arr(p->digits);
	p->digits = new_allocation;
	p->num_allocated_digits = target_num_digits;
}

//Attempt to reallocate memory used by p to hold specified number of bits.
//If the specified number of bits is not enough to fit the current bigint value,
//the reallocated memory will be just enough to fit the current value.
void ffbi_reallocate(ffbi_t* p, uint32_t target_bits)
{
	int num_digits = target_bits/FFBI_BITS_PER_DIGIT;
	if(target_bits%FFBI_BITS_PER_DIGIT > 0)
		num_digits++;
	ffbi_reallocate_digits(p, num_digits, 1);
}

//Uses Fermat primality test to determine if p is prime. Increase
//num_tests for higher chance p is actually prime. Probability of p being wrongly
//detected as prime is 2^(-num_tests). If p is less than 16^8, it is not considered
//large enough and will return false. A sieve may be optionally provided to quickly
//weed out composites before the Fermat primality test. Pass NULL for sieve to skip
//the sieve test. 1 is returned if p is prime and 0 is returned if otherwise.
//scratch contains memory buffers used internally to minimize unnecessary allocations
//for use in tight loops. Pass NULL for scratch for no optimizations.
int ffbi_is_large_prime(ffbi_t* p, int num_tests, ffbi_scratch_t* sieve, ffbi_scratch_t* scratch)
{
	if(num_tests < 1 || p == NULL)
	{
		fflog_debug_print("invalid arguments\n");
		return 0;
	}
	int ret = 1;

	//make sure we have scratch space
	uint8_t free_scratch = 0;
	if(scratch == NULL)
	{
		scratch = ffbi_scratch_create();
		free_scratch = 1;
	}
	ffbi_scratch_prepare(scratch, FFBI_PRIME_TEST_NUM_SCRATCHES, p->num_used_digits);
	ffbi_t** temp = scratch->val;
	uint8_t free_child = 0;
	if(scratch->num_children == 0)
	{
		scratch->child = ffbi_scratch_create();
		scratch->num_children = 1;
		free_child = 1;
	}
	ffbi_scratch_prepare(scratch->child, FFBI_MOD_POW_NUM_SCRATCHES, p->num_used_digits);
	uint8_t free_inner_child = 0;
	if(scratch->child->num_children == 0)
	{
		scratch->child->num_children = 1;
		scratch->child->child = ffbi_scratch_create();
		free_inner_child = 1;
	}
	ffbi_t* p_minus_1;

	//if sieve was provided, see if p divides into any value in the sieve first
	//uint32_t start_time = fftime_get_time_ms();
	if(sieve != NULL)
	{
		uint32_t i;
		for(i=0;i<(uint32_t)sieve->num_vals;i++)
		{
			ffbi_t* divisor = sieve->val[i];
			if(ffbi_cmp(divisor, p) == 1)
				break;
			ffbi_div_impl(temp[2], p, divisor, temp[1], temp[3], scratch->child->val[0]);
			if(temp[1]->num_used_digits == 1 && temp[1]->digits[0] == 0)
			{
				ret = 0;
				//fflog_print("sieve detected composite.\n");
				goto finish;
			}
		}
	}
	//fflog_print("finished sieve test in %u ms\n", fftime_get_time_ms() - start_time);

	//run the Fermat primality test
	p_minus_1 = temp[0];
	temp[1]->num_used_digits = 1;
	temp[1]->digits[0] = 1;
	ffbi_sub(p_minus_1, p, temp[1]);
	temp[2]->num_used_digits = 1;
	temp[2]->digits[0] = 2;
	ffbi_sub(temp[1], p_minus_1, temp[2]);
	int k;
	for(k=0;k<num_tests;k++)
	{
		ffbi_t* a = temp[2];
		ffbi_t* dest = temp[3];
		ffbi_random_with_limit(a, temp[1]);
		ffbi_add_u(a, a, 2);
		//fflog_print("line=%d\n", __LINE__);
		//uint32_t start_time = fftime_get_time_ms();
		ffbi_mod_pow(dest, a, p_minus_1, p, scratch->child);
		//fflog_print("mod_pow finished in %u ms. k=%d\n", fftime_get_time_ms() - start_time, k);
		//fflog_print("line=%d\n", __LINE__);
		if(dest->num_used_digits > 1 || dest->digits[0] != 1)
		{
			ret = 0;
			break;
		}
	}
finish:
	if(free_scratch)
		ffbi_scratch_destroy(scratch);
	else
	{
		if(free_child)
		{
			ffbi_scratch_destroy(scratch->child);
			scratch->child = NULL;
			scratch->num_children = 0;
		}
		else if(free_inner_child)
		{
			ffbi_scratch_destroy(scratch->child->child);
			scratch->child->child = NULL;
			scratch->child->num_children = 0;
		}
	}
	return ret;
}

uint32_t ffbi_get_significant_bits(ffbi_t* p)
{
	return (p->num_used_digits-1)*FFBI_BITS_PER_DIGIT + (uint32_t)ffbi_significant_bits(p->digits[p->num_used_digits-1]);
}

//Returns the size of the buffer necessary to hold the serialized bigint p in bytes.
int ffbi_get_serialized_size(ffbi_t* p)
{
	uint32_t total_bits = ffbi_get_significant_bits(p);
	return (int)(total_bits/8 + (total_bits%8 > 0));
}

int ffbi_serialize_v2(ffbi_t* p, uint8_t* buffer, int size_bytes, uint32_t total_bits)
{
	ffbi_base_convert_t* ctx = ffbi_base_convert_create(8, FFBI_BITS_PER_DIGIT, total_bits);
	int num_write_bytes = (int)ctx->dst_num_digits;
	if(num_write_bytes < size_bytes)
		return -1;
	ffbi_base_convert_exec<uint8_t, ffbi_word_t>(ctx, buffer, p->digits);
	ffbi_base_convert_destroy(ctx);
	return num_write_bytes;
}

int ffbi_serialize(ffbi_t* p, uint8_t* buffer, int size_bytes)
{
	uint32_t total_bits = ffbi_get_significant_bits(p);
	return ffbi_serialize_v2(p, buffer, size_bytes, total_bits);
}

void ffbi_print_words(ffbi_t* p)
{
	for(unsigned i=0;i<p->num_used_digits;i++)
		fflog_print("[%llu]", (uint64_t)p->digits[i]);
	fflog_print("\n");
}

void ffbi_deserialize(ffbi_t* p, uint8_t* buffer, int size_bytes)
{
	uint32_t total_bits = (size_bytes-1)*8 + ffbi_significant_bits_uint8(buffer[size_bytes-1]);
	ffbi_base_convert_t* ctx = ffbi_base_convert_create(FFBI_BITS_PER_DIGIT, 8, total_bits);
	if(ctx->dst_num_digits > p->num_allocated_digits)
		ffbi_reallocate_digits(p, ctx->dst_num_digits, 0);
	p->num_used_digits = ctx->dst_num_digits;
	ffbi_base_convert_exec<ffbi_word_t, uint8_t>(ctx, p->digits, buffer);
	ffbi_base_convert_destroy(ctx);
	p->cache_valid = 0;
}

//Prints the base 10 string representation of p into stdout appended with newline.
void ffbi_print(ffbi_t* p)
{
	if(p->num_used_digits == 1 && p->digits[0] == 0)
	{
		fflog_print("0\n");
		return;
	}
	std::list<uint8_t> p_base_10;
	ffbi_t* temp = ffbi_create_from_bigint(p);
	ffbi_t* ten = ffbi_create();
	ffbi_t* zero = ffbi_create();
	ten->digits[0] = 10;
	ffbi_t* r = ffbi_create_reserved_digits(p->num_allocated_digits);
	ffbi_t* scratch = ffbi_create_reserved_digits(p->num_allocated_digits);
	ffbi_t* scratch2 = ffbi_create_reserved_digits(p->num_allocated_digits);
	ffbi_t* scratch3 = ffbi_create_reserved_digits(p->num_allocated_digits);
	while(ffbi_cmp(temp, zero) == 1)
	{
		ffbi_div_impl(scratch3, temp, ten, r, scratch, scratch2);
		ffbi_copy(temp, scratch3);
		p_base_10.push_front((uint8_t)r->digits[0]);
	}
	std::list<uint8_t>::iterator it;
	for(it=p_base_10.begin();it!=p_base_10.end();it++)
		fflog_print("%u",(*it));
	fflog_print("\n");
	ffbi_destroy(scratch);
	ffbi_destroy(scratch2);
	ffbi_destroy(scratch3);
	ffbi_destroy(r);
	ffbi_destroy(zero);
	ffbi_destroy(ten);
	ffbi_destroy(temp);
}

static int ffbi_cmp_cache(ffbi_t* a, ffbi_t* b)
{
	if(a->cache_num_used_digits > b->cache_num_used_digits)
		return 1;
	if(a->cache_num_used_digits < b->cache_num_used_digits)
		return -1;
	for(int k=(int)a->cache_num_used_digits-1;k>=0;k--)
	{
		if(a->cache[k] > b->cache[k])
			return 1;
		if(a->cache[k] < b->cache[k])
			return -1;
	}
	return 0;
}

//[compare] returns 0 if a == b, 1 if a > b, or -1 if a < b.
int ffbi_cmp(ffbi_t* a, ffbi_t* b)
{
	//if a has more digits than b, it can be concluded that a is bigger
	if(a->num_used_digits > b->num_used_digits)
		return 1;
	//if b has more digits than a, it can be concluded that b is bigger
	if(a->num_used_digits < b->num_used_digits)
		return -1;
	//if code reaches here, a and b take up the same number of digits
	//check digit values starting from most significant
	for(int k=(int)a->num_used_digits-1;k>=0;k--)
	{
		if(a->digits[k] > b->digits[k])
			return 1;
		if(a->digits[k] < b->digits[k])
			return -1;
	}
	return 0;
}

//[addition] dest = a + b
void ffbi_add(ffbi_t* dest, ffbi_t* a, ffbi_t* b)
{
	ffbi_t* larger;
	uint32_t max_used_digits;
	uint32_t min_used_digits;
	if(a->num_used_digits >= b->num_used_digits)
	{
		larger = a;
		min_used_digits = b->num_used_digits;
	}
	else
	{
		larger = b;
		min_used_digits = a->num_used_digits;
	}
	max_used_digits = larger->num_used_digits;
	if(dest->num_allocated_digits < max_used_digits) //first make sure dest is big enough
	{
		if(dest != a && dest != b)
			ffbi_reallocate_digits(dest, (int)(max_used_digits*FFBI_REALLOC_GROWTH_FACTOR+1), 0);
		else
			ffbi_reallocate_digits(dest, (int)(max_used_digits*FFBI_REALLOC_GROWTH_FACTOR+1), 1);
	}

	//add the digits
	uint32_t k;
	dest->digits[0] = a->digits[0] + b->digits[0];
	for(k=1;k<min_used_digits;k++)
	{
		dest->digits[k] = a->digits[k] + b->digits[k] + (dest->digits[k-1] >> FFBI_BITS_PER_DIGIT);
		dest->digits[k-1] &= _digit_max; //take carry out of previous digit sum
	}
	//propagate carry to the rest of the digits
	for(;k<max_used_digits;k++)
	{
		dest->digits[k] = larger->digits[k] + (dest->digits[k-1] >> FFBI_BITS_PER_DIGIT);
		dest->digits[k-1] &= _digit_max; //take carry out of previous digit sum
	}
	//if last digit still has a carry, dest needs one more digit to hold it
	k--;
	if(dest->digits[k] >> FFBI_BITS_PER_DIGIT != 0)
	{
		max_used_digits++;
		if(dest->num_allocated_digits < max_used_digits)
			ffbi_reallocate_digits(dest, (int)(max_used_digits*FFBI_REALLOC_GROWTH_FACTOR+1), 1);
		dest->digits[k] &= _digit_max; //take carry out of previous digit sum
		k++;
		dest->digits[k] = 1; //newly appended digit now holds the carry
	}
	dest->num_used_digits = max_used_digits;
	dest->cache_valid = 0;
}

void ffbi_add_u(ffbi_t* dest, ffbi_t* a, uint32_t b)
{
	if(dest != a)
	{
		if(dest->num_allocated_digits < a->num_used_digits)
			ffbi_reallocate_digits(dest, a->num_used_digits + 2, 0);
		dest->num_used_digits = a->num_used_digits;
	}
	dest->digits[0] = a->digits[0] + (b&_digit_max);
	if(a->num_used_digits == 1)
	{
		dest->digits[1] = dest->digits[0]>>FFBI_BITS_PER_DIGIT;
		dest->num_used_digits += dest->digits[1];
	}
	else
		dest->digits[1] = a->digits[1] + (dest->digits[0]>>FFBI_BITS_PER_DIGIT);
	dest->digits[0] &= _digit_max;
#if FFBI_BITS_PER_DIGIT < 32
	uint32_t hi = b>>FFBI_BITS_PER_DIGIT;
	if(hi)
	{
		dest->digits[1] = a->digits[1] + hi;
		if(dest->num_used_digits == 1)
			dest->num_used_digits++;
	}
#endif
	int i = 1;
	uint32_t carry = dest->digits[i]>>FFBI_BITS_PER_DIGIT;
	while(carry>0)
	{
		i++;
		if(i+1 > (int)dest->num_used_digits)
		{
			dest->num_used_digits = i+1;
			if(dest->num_allocated_digits < dest->num_used_digits)
				ffbi_reallocate_digits(dest, dest->num_used_digits + 2, 1);
			dest->digits[i] = 0;
		}
		dest->digits[i] += carry;
		carry = dest->digits[i]>>FFBI_BITS_PER_DIGIT;
	}
	dest->cache_valid = 0;
}

#if FFBI_DIV_CACHE_ENABLED
//a and b's cache is assumed to be already built. dest must not point to a or b.
static void ffbi_sub_cache(ffbi_t* dest, ffbi_t* a, ffbi_t* b)
{
	//dest should have enough allocated digits to fit the larger of a and b
	uint32_t a_used_digits = a->cache_num_used_digits;
	uint32_t b_used_digits = b->cache_num_used_digits;
	uint32_t max_used_digits;
	uint32_t min_used_digits;
	if(b_used_digits > a_used_digits)
	{
		max_used_digits = b_used_digits;
		min_used_digits = a_used_digits;
	}
	else
	{
		max_used_digits = a_used_digits;
		min_used_digits = b_used_digits;
	}
	if(dest->cache_num_allocated_digits < max_used_digits)
		ffbi_cache_prepare(dest, (int)max_used_digits, 0);

	//subtract digits of a and b
	uint32_t k;
	dest->cache[0] = a->cache[0] + _cache_div_digit_max_plus_1 - b->cache[0];
	for(k=1;k<min_used_digits;k++)
	{
		ffbi_cache_word_t carry = 1 - (dest->cache[k-1] >> dest->cache_bits_per_digit);
		dest->cache[k-1] &= _cache_div_digit_max; //clear previous carry
		dest->cache[k] = a->cache[k] + _cache_div_digit_max_plus_1 - b->cache[k] - carry;
	}
	if(b_used_digits > a_used_digits) //then use value of 0 for any digit of a beyond this point
	{
		for(;k<max_used_digits;k++)
		{
			ffbi_cache_word_t carry = 1 - (dest->cache[k-1] >> dest->cache_bits_per_digit);
			dest->cache[k-1] &= _cache_div_digit_max; //clear previous carry
			dest->cache[k] = _cache_div_digit_max_plus_1 - b->cache[k] - carry;
		}
	}
	else
	{
		for(;k<max_used_digits;k++) //continue propagating carry as normal since a has at least b number of digits
		{
			ffbi_cache_word_t carry = 1 - (dest->cache[k-1] >> dest->cache_bits_per_digit);
			dest->cache[k-1] &= _cache_div_digit_max; //clear previous carry
			dest->cache[k] = a->cache[k] + _cache_div_digit_max_plus_1 - carry;
		}
	}
	k--;
	dest->cache[k] &= _cache_div_digit_max; //clear previous carry

	//see if dest can have less used digits than a, then assign number of used digits
	for(;k>0;k--)
	{
		if(dest->cache[k] != 0)
			break;
	}
	dest->cache_num_used_digits = k+1;
}
#endif

//[subtraction] dest = a - b
void ffbi_sub(ffbi_t* dest, ffbi_t* a, ffbi_t* b)
{
	//dest should have enough allocated digits to fit the larger of a and b
	uint32_t a_used_digits = a->num_used_digits;
	uint32_t b_used_digits = b->num_used_digits;
	uint32_t max_used_digits;
	uint32_t min_used_digits;
	if(b_used_digits > a_used_digits)
	{
		max_used_digits = b_used_digits;
		min_used_digits = a_used_digits;
	}
	else
	{
		max_used_digits = a_used_digits;
		min_used_digits = b_used_digits;
	}
	if(dest->num_allocated_digits < max_used_digits)
	{
		if(dest != a)
			ffbi_reallocate_digits(dest, (int)(max_used_digits*FFBI_REALLOC_GROWTH_FACTOR+1), 0);
		else
			ffbi_reallocate_digits(dest, (int)(max_used_digits*FFBI_REALLOC_GROWTH_FACTOR+1), 1);
	}

	//subtract digits of a and b
	uint32_t k;
	dest->digits[0] = a->digits[0] + _digit_max_plus_1 - b->digits[0];
	for(k=1;k<min_used_digits;k++)
	{
		ffbi_word_t carry = 1 - (dest->digits[k-1] >> FFBI_BITS_PER_DIGIT);
		dest->digits[k-1] &= _digit_max; //clear previous carry
		dest->digits[k] = a->digits[k] + _digit_max_plus_1 - b->digits[k] - carry;
	}
	if(b_used_digits > a_used_digits) //then use value of 0 for any digit of a beyond this point
	{
		for(;k<max_used_digits;k++)
		{
			ffbi_word_t carry = 1 - (dest->digits[k-1] >> FFBI_BITS_PER_DIGIT);
			dest->digits[k-1] &= _digit_max; //clear previous carry
			dest->digits[k] = _digit_max_plus_1 - b->digits[k] - carry;
		}
	}
	else
	{
		for(;k<max_used_digits;k++) //continue propagating carry as normal since a has at least b number of digits
		{
			ffbi_word_t carry = 1 - (dest->digits[k-1] >> FFBI_BITS_PER_DIGIT);
			dest->digits[k-1] &= _digit_max; //clear previous carry
			dest->digits[k] = a->digits[k] + _digit_max_plus_1 - carry;
		}
	}
	k--;
	dest->digits[k] &= _digit_max; //clear previous carry

	//see if dest can have less used digits than a, then assign number of used digits
	for(;k>0;k--)
	{
		if(dest->digits[k] != 0)
			break;
	}
	dest->num_used_digits = k+1;
	dest->cache_valid = 0;
}

#if FFBI_MUL_CACHE_ENABLED
static void ffbi_mul_cache(ffbi_t* dest, ffbi_t* a, ffbi_t* b)
{
	ffbi_cache_update(a, FFBI_CACHE_MUL_BITS_PER_DIGIT, _cache_mul_digit_max);
	ffbi_cache_update(b, FFBI_CACHE_MUL_BITS_PER_DIGIT, _cache_mul_digit_max);
	uint32_t product_len = a->cache_num_used_digits+b->cache_num_used_digits;
	ffbi_cache_prepare(dest, product_len, 0);
	dest->cache_num_used_digits = product_len;
	dest->cache_bits_per_digit = FFBI_CACHE_MUL_BITS_PER_DIGIT;
	memset(dest->cache, 0, product_len*sizeof(ffbi_cache_word_t));
	uint32_t k, i;
	for(k=0;k<a->cache_num_used_digits;k++)
	{
		for(i=0;i<b->cache_num_used_digits;i++)
		{
			uint32_t add_index = k+i;
			dest->cache[add_index] += a->cache[k] * b->cache[i];
			dest->cache[add_index+1] += dest->cache[add_index]>>FFBI_CACHE_MUL_BITS_PER_DIGIT;
			dest->cache[add_index] &= _cache_mul_digit_max;
			add_index++;
			while((dest->cache[add_index]>>FFBI_CACHE_MUL_BITS_PER_DIGIT) != 0) //propagate carry as long as it exists
			{
				dest->cache[add_index] &= _cache_mul_digit_max;
				add_index++;
				dest->cache[add_index] += 1;
				if(add_index >= product_len)
					break;
			}
		}
	}
	//see if there are trailing 0-value digits that can be trimmed off of the product
	if(dest->cache_num_used_digits > 1 && dest->cache[product_len-1] == 0)
		dest->cache_num_used_digits--;

	ffbi_cache_retrieve(dest);
}
#endif

#if FFBI_DIV_CACHE_ENABLED
static void ffbi_div_cache_mul(ffbi_t* dest, ffbi_t* a, ffbi_t* b)
{
	uint32_t product_len = a->cache_num_used_digits+b->cache_num_used_digits;
	ffbi_cache_prepare(dest, product_len, 0);
	dest->cache_num_used_digits = product_len;
	dest->cache_bits_per_digit = FFBI_CACHE_DIV_BITS_PER_DIGIT;
	memset(dest->cache, 0, product_len*sizeof(ffbi_cache_word_t));
	uint32_t k, i;
	for(k=0;k<a->cache_num_used_digits;k++)
	{
		for(i=0;i<b->cache_num_used_digits;i++)
		{
			uint32_t add_index = k+i;
			//ffbi_cache_word_t p = a->cache[k] * b->cache[i];
			//dest->cache[add_index+1] += p>>FFBI_CACHE_DIV_BITS_PER_DIGIT;
			//dest->cache[add_index] += p&_cache_div_digit_max;
			dest->cache[add_index] += a->cache[k] * b->cache[i];
			/*
			add_index++;
			while((dest->cache[add_index]>>FFBI_CACHE_DIV_BITS_PER_DIGIT) != 0) //propagate carry as long as it exists
			{
				dest->cache[add_index] &= _cache_div_digit_max;
				add_index++;
				dest->cache[add_index] += 1;
				if(add_index >= product_len)
					break;
			}*/
		}
	}
	for(i=0;i<product_len-1;i++)
	{
		dest->cache[i+1] += dest->cache[i]>>FFBI_CACHE_DIV_BITS_PER_DIGIT;
		dest->cache[i] &= _cache_div_digit_max;
	}
	//see if there are trailing 0-value digits that can be trimmed off of the product
	if(dest->cache_num_used_digits > 1 && dest->cache[product_len-1] == 0)
		dest->cache_num_used_digits--;
}
#endif

//[multiplication] dest = a * b
void ffbi_mul(ffbi_t* dest, ffbi_t* a, ffbi_t* b)
{
	if(ffbi_is_zero(a) || ffbi_is_zero(b))
	{
		dest->num_used_digits = 1;
		dest->digits[0] = 0;
		dest->cache_valid = 0;
		return;
	}
	ffbi_t* product;
	uint32_t a_len = a->num_used_digits;
	uint32_t b_len = b->num_used_digits;
	uint32_t product_len =  a_len+b_len;
	if(dest == a || dest == b)
		product = ffbi_create_reserved_digits(product_len+FFBI_MIN_ALLOC_DIGITS);
	else
	{
		product = dest;
		if(product->num_allocated_digits < product_len)
			ffbi_reallocate_digits(product, product_len+1, 0);
	}
#if FFBI_MUL_CACHE_ENABLED
	if(a_len > _min_mul_cache_len || b_len > _min_mul_cache_len)
		ffbi_mul_cache(product, a, b);
	else
	{
#endif
		product->num_used_digits = product_len;
		//product->digits[product_len-1] = 0;
		memset(product->digits, 0, product_len*sizeof(ffbi_word_t));
		uint32_t k, i;
		for(k=0;k<a_len;k++)
		{
			for(i=0;i<b_len;i++)
			{
				uint32_t add_index = k+i;
				//ffbi_word_t p = a->digits[k] * b->digits[i];
				//product->digits[add_index] += p&_digit_max;
				//product->digits[add_index+1] += p>>FFBI_BITS_PER_DIGIT;
				product->digits[add_index] += a->digits[k] * b->digits[i];
				/*
				add_index++;
				while((product->digits[add_index]>>FFBI_BITS_PER_DIGIT) != 0) //propagate carry as long as it exists
				{
					product->digits[add_index] &= _digit_max;
					add_index++;
					product->digits[add_index] += 1;
					if(add_index >= product_len)
						break;
				}*/
			}
		}
		for(i=0;i<product_len-1;i++)
		{
			product->digits[i+1] += product->digits[i]>>FFBI_BITS_PER_DIGIT;
			product->digits[i] &= _digit_max;
		}
		//see if there are trailing 0-value digits that can be trimmed off of the product
		if(product->num_used_digits > 1 && product->digits[product_len-1] == 0)
			product->num_used_digits--;
		dest->cache_valid = 0;
#if FFBI_MUL_CACHE_ENABLED
	}
#endif
	if(product != dest) //then copy product to dest
	{
		ffbi_copy(dest, product);
		ffbi_destroy(product);
	}
}

/*
static void ffbi_mul_karatsuba(ffbi_t* dest, ffbi_t* a, ffbi_t* b, ffbi_scratch_t* scratch)
{
	if(a->num_used_digits == 1 || b->num_used_digits == 1)
	{
		ffbi_mul_long(dest, a, b);
		return;
	}
	ffbi_scratch_prepare(scratch, FFBI_KARATSUBA_NUM_VALS, a->num_used_digits+b->num_used_digits);
	if(scratch->num_children == 0)
	{
		scratch->num_children = 1;
		scratch->child = ffbi_scratch_create();
	}
	uint32_t m = a->num_used_digits;
	if(m < b->num_used_digits)
		m = b->num_used_digits;
	uint32_t m2 = m>>1;
	ffbi_t* high_a = scratch->val[0];
	ffbi_t* high_b = scratch->val[1];
	ffbi_t* low_a = scratch->val[2];
	ffbi_t* low_b = scratch->val[3];
	ffbi_t* z0 = scratch->val[4];
	ffbi_t* z1 = scratch->val[5];
	ffbi_t* z2 = scratch->val[6];
	uint8_t z2_is_zero = 0;
	if(m2 >= a->num_used_digits) //then there's no high_a
	{
		z2->num_used_digits = 1;
		z2->digits[0] = 0;
		z2_is_zero = 1;
		high_a->num_used_digits = 1;
		high_a->digits[0] = 0;
	}
	else
	{
		high_a->num_used_digits = a->num_used_digits - m2;
		memcpy(high_a->digits, &a->digits[m2], high_a->num_used_digits*sizeof(uint32_t));
	}
	if(m2 >= b->num_used_digits) //then there's no high_b
	{
		z2->num_used_digits = 1;
		z2->digits[0] = 0;
		z2_is_zero = 1;
		high_b->num_used_digits = 1;
		high_b->digits[0] = 0;
	}
	else
	{
		high_b->num_used_digits = b->num_used_digits - m2;
		memcpy(high_b->digits, &b->digits[m2], high_b->num_used_digits*sizeof(uint32_t));
	}
	if(!z2_is_zero)
		ffbi_mul_karatsuba(z2, high_a, high_b, scratch->child);
	low_a->num_used_digits = m2;
	if(low_a->num_used_digits > a->num_used_digits)
		low_a->num_used_digits = a->num_used_digits;
	memcpy(low_a->digits, a->digits, low_a->num_used_digits*sizeof(uint32_t));
	low_b->num_used_digits = m2;
	if(low_b->num_used_digits > b->num_used_digits)
		low_b->num_used_digits = b->num_used_digits;
	memcpy(low_b->digits, b->digits, low_b->num_used_digits*sizeof(uint32_t));
	ffbi_mul_karatsuba(z0, low_a, low_b, scratch->child);
	ffbi_add(low_a, low_a, high_a);
	ffbi_add(low_b, low_b, high_b);
	ffbi_mul_karatsuba(z1, low_a, low_b, scratch->child);
	uint32_t m3 = m2<<1;
	uint32_t temp = z2->num_used_digits + m3;
	if(temp > scratch->val[0]->num_allocated_digits)
		ffbi_reallocate_digits(scratch->val[0], temp);
	scratch->val[0]->num_used_digits = temp;
	memset(scratch->val[0]->digits, 0, sizeof(uint32_t)*m3);
	memcpy(&scratch->val[0]->digits[m3], z2->digits, sizeof(uint32_t)*z2->num_used_digits);
	uint32_t greatest = z1->num_used_digits;
	if(z2->num_used_digits > greatest)
		greatest = z2->num_used_digits;
	if(z0->num_used_digits > greatest)
		greatest = z0->num_used_digits;
	temp = greatest + m2;
	if(temp > scratch->val[2]->num_allocated_digits)
		ffbi_reallocate_digits(scratch->val[2], temp);
	memset(scratch->val[2]->digits, 0, sizeof(uint32_t)*m2);

	scratch->val[2]->num_used_digits = z1->num_used_digits + m2;
	memcpy(&scratch->val[2]->digits[m2],z1->digits, sizeof(uint32_t)*z1->num_used_digits);
	ffbi_add(scratch->val[0], scratch->val[0], scratch->val[2]);

	scratch->val[2]->num_used_digits = z2->num_used_digits + m2;
	memcpy(&scratch->val[2]->digits[m2],z2->digits, sizeof(uint32_t)*z2->num_used_digits);
	ffbi_sub(scratch->val[0], scratch->val[0], scratch->val[2]);

	scratch->val[2]->num_used_digits = z0->num_used_digits + m2;
	memcpy(&scratch->val[2]->digits[m2],z0->digits, sizeof(uint32_t)*z0->num_used_digits);
	ffbi_sub(scratch->val[0], scratch->val[0], scratch->val[2]);

	ffbi_add(dest, scratch->val[0], z0);
}*/
/*
void ffbi_mul_impl(ffbi_t* dest, ffbi_t* a, ffbi_t* b, ffbi_scratch_t* scratch)
{
	ffbi_t* product = dest;
	uint8_t destroy_product = 0;
	uint32_t product_len = a->num_used_digits+b->num_used_digits;
	if(dest == a || dest == b)
	{
		product = ffbi_create_reserved_digits(product_len);
		destroy_product = 1;
	}
	if(product->num_allocated_digits < product_len)
		ffbi_reallocate_digits(product, product_len);
	uint8_t destroy_scratch = 0;
	if(scratch == NULL)
	{
		scratch = ffbi_scratch_create();
		destroy_scratch = 1;
	}
	ffbi_mul_karatsuba(product, a, b, scratch);
	if(destroy_product)
	{
		ffbi_copy(dest, product);
		ffbi_destroy(product);
	}
	if(destroy_scratch)
		ffbi_scratch_destroy(scratch);
}*/

#if FFBI_DIV_CACHE_ENABLED
static void ffbi_get_quotient_digit_cache(int q_index, ffbi_t * q, ffbi_t* rem, ffbi_t* a, ffbi_t* b, int b_len, ffbi_t* product, uint32_t is_not_last_digit, ffbi_t* scratch)
{
	uint32_t k;
	int cmp = ffbi_cmp_cache(rem, b);
	if(cmp == 0) //if r = b, then this digit of q is 1
	{
		q->cache[q_index] = 1;
		rem->cache_num_used_digits = 1;
		if(q_index > 0)
			rem->cache[0] = a->cache[q_index-1]; //update remainder with next digit in dividend
		else
			rem->cache[0] = 0;
	}
	else if(cmp == -1) //if r < b, then this digit of q is 0
	{
		q->cache[q_index] = 0;
		if(q_index > 0)
		{
			if(rem->cache_num_used_digits > 1 || rem->cache[0] > 0)
			{
				rem->cache_num_used_digits++;
				if(rem->cache_num_allocated_digits < rem->cache_num_used_digits)
					ffbi_cache_prepare(rem, rem->cache_num_used_digits, 1);
				for(k=rem->cache_num_used_digits-1;k>0;k--) //shift remainder digits to the left
					rem->cache[k] = rem->cache[k-1];
			}
			rem->cache[0] = a->cache[q_index-1]; //update remainder with next digit in dividend
		}
	}
	else //if r > b, divide
	{
		uint32_t r_len = rem->cache_num_used_digits;
		//ffbi_cache_word_t leftmost_r;
		ffbi_cache_word_t leftmost_b = 0;
		if(r_len == 1)
		{
			q->cache[q_index] = rem->cache[0]/b->cache[0];
			goto skip_q_set;
		}
		else if(r_len == 2)
		{
			q->cache[q_index] = (rem->cache[1]<<FFBI_CACHE_DIV_BITS_PER_DIGIT) + rem->cache[0];
			if(b_len == 2)
				leftmost_b = (b->cache[1]<<FFBI_CACHE_DIV_BITS_PER_DIGIT) + b->cache[0];
			else
				leftmost_b = b->cache[0];
		}
		else if(r_len == 3)
		{
			ffbi_cache_word_t shift_amount_x_2 = FFBI_CACHE_DIV_BITS_PER_DIGIT*2;
			q->cache[q_index] = (rem->cache[r_len-1]<< shift_amount_x_2) + (rem->cache[r_len-2]<<FFBI_CACHE_DIV_BITS_PER_DIGIT) + rem->cache[r_len-3];
			if(r_len > (uint32_t)b_len)
				leftmost_b = (b->cache[b_len-1]<<FFBI_CACHE_DIV_BITS_PER_DIGIT) + b->cache[b_len-2];
			else
				leftmost_b = (b->cache[b_len-1]<< shift_amount_x_2) + (b->cache[b_len-2]<<FFBI_CACHE_DIV_BITS_PER_DIGIT) + b->cache[b_len-3];
		}
		else
		{
			ffbi_cache_word_t shift_amount_x_2 = FFBI_CACHE_DIV_BITS_PER_DIGIT*2;
			ffbi_cache_word_t shift_amount_x_3 = FFBI_CACHE_DIV_BITS_PER_DIGIT*3;
			q->cache[q_index] = (rem->cache[r_len-1]<<shift_amount_x_3) + (rem->cache[r_len-2]<<shift_amount_x_2) + (rem->cache[r_len-3]<<FFBI_CACHE_DIV_BITS_PER_DIGIT) + rem->cache[r_len-4];
			if(r_len > (uint32_t)b_len)
				leftmost_b = (b->cache[b_len-1]<<shift_amount_x_2) + (b->cache[b_len-2]<<FFBI_CACHE_DIV_BITS_PER_DIGIT) + b->cache[b_len-3];
			else
				leftmost_b = (b->cache[b_len-1]<<shift_amount_x_3) + (b->cache[b_len-2]<<shift_amount_x_2) + (b->cache[b_len-3]<<FFBI_CACHE_DIV_BITS_PER_DIGIT) + b->cache[b_len-4];
		}
		//leftmost_r = q->cache[q_index];
		q->cache[q_index] /= leftmost_b;
	skip_q_set:
		product->cache_num_used_digits = 1;
		product->cache[0] = q->cache[q_index];
		ffbi_div_cache_mul(scratch, product, b);
		uint32_t q_miss_count = 0;
		while(ffbi_cmp_cache(rem, scratch) == -1)
		{
			q_miss_count++;
			//fflog_debug_print("product more than remainder. q_miss_count=%u\n", q_miss_count);
			if(product->cache[0] == 0)
				break;
			product->cache[0]--;
			q->cache[q_index]--;
			ffbi_div_cache_mul(scratch, product, b);
		}
		ffbi_sub_cache(product, rem, scratch); //use product to temporarily hold the new remainder
		//replace the old remainder with the new remainder
		if(ffbi_cmp_cache(product, b) == 1)
		{
			fflog_debug_print("r greater than b.\n");
		}
		if(product->cache_num_used_digits > 1 || product->cache[0] > 0)
		{
			rem->cache_num_used_digits = product->cache_num_used_digits+is_not_last_digit;
			if(rem->cache_num_allocated_digits < rem->cache_num_used_digits)
				ffbi_cache_prepare(rem, rem->cache_num_used_digits, 0);
			rem->cache[0] = 0;
			for(k=0;k<product->cache_num_used_digits;k++)
				rem->cache[k+is_not_last_digit] = product->cache[k];
			rem->cache[0] += a->cache[q_index-is_not_last_digit]*is_not_last_digit; //update remainder with next digit in dividend
		}
		else
		{
			rem->cache_num_used_digits = product->cache_num_used_digits;
			rem->cache[0] = a->cache[q_index-is_not_last_digit]*is_not_last_digit;
		}
	}
}
#endif

static void ffbi_get_quotient_digit(int q_index, ffbi_t * q, ffbi_t* r, ffbi_t* a, ffbi_t* b, int b_len, ffbi_t* product, uint32_t is_not_last_digit, ffbi_t* scratch)
{
	uint32_t k;
	int cmp = ffbi_cmp(r, b);
	r->cache_valid = 0;
	q->cache_valid = 0;
	if(cmp == 0) //if r = b, then this digit of q is 1
	{
		q->digits[q_index] = 1;
		r->num_used_digits = 1;
		if(q_index > 0)
			r->digits[0] = a->digits[q_index-1]; //update remainder with next digit in dividend
		else
			r->digits[0] = 0;
	}
	else if(cmp == -1) //if r < b, then this digit of q is 0
	{
		q->digits[q_index] = 0;
		if(q_index > 0)
		{
			if(r->num_used_digits > 1 || r->digits[0] > 0)
			{
				r->num_used_digits++;
				if(r->num_allocated_digits<r->num_used_digits)
					ffbi_reallocate_digits(r, r->num_used_digits, 1);
				for(k=r->num_used_digits-1;k>0;k--) //shift remainder digits to the left
					r->digits[k] = r->digits[k-1];
			}
			r->digits[0] = a->digits[q_index-1]; //update remainder with next digit in dividend
		}
	}
	else //if r > b, divide
	{
		uint32_t r_len = r->num_used_digits;
		ffbi_word_t leftmost_b = 0;
		if(r_len == 1)
		{
			q->digits[q_index] = r->digits[0]/b->digits[0];
			goto skip_q_set;
		}
		else if(r_len == 2)
		{
			q->digits[q_index] = (r->digits[1]<<FFBI_BITS_PER_DIGIT) + r->digits[0];
			if(b_len == 2)
				leftmost_b = (b->digits[1]<<FFBI_BITS_PER_DIGIT) + b->digits[0];
			else
				leftmost_b = b->digits[0];
		}
		else if(r_len == 3)
		{
			ffbi_word_t shift_amount_x_2 = FFBI_BITS_PER_DIGIT*2;
			q->digits[q_index] = (r->digits[r_len-1]<< shift_amount_x_2) + (r->digits[r_len-2]<<FFBI_BITS_PER_DIGIT) + r->digits[r_len-3];
			if(r_len > (uint32_t)b_len)
				leftmost_b = (b->digits[b_len-1]<<FFBI_BITS_PER_DIGIT) + b->digits[b_len-2];
			else
				leftmost_b = (b->digits[b_len-1]<< shift_amount_x_2) + (b->digits[b_len-2]<<FFBI_BITS_PER_DIGIT) + b->digits[b_len-3];
		}
		else
		{
			ffbi_word_t shift_amount_x_2 = FFBI_BITS_PER_DIGIT*2;
			ffbi_word_t shift_amount_x_3 = FFBI_BITS_PER_DIGIT*3;
			q->digits[q_index] = (r->digits[r_len-1]<<shift_amount_x_3) + (r->digits[r_len-2]<<shift_amount_x_2) + (r->digits[r_len-3]<<FFBI_BITS_PER_DIGIT) + r->digits[r_len-4];
			if(r_len > (uint32_t)b_len)
				leftmost_b = (b->digits[b_len-1]<<shift_amount_x_2) + (b->digits[b_len-2]<<FFBI_BITS_PER_DIGIT) + b->digits[b_len-3];
			else
				leftmost_b = (b->digits[b_len-1]<<shift_amount_x_3) + (b->digits[b_len-2]<<shift_amount_x_2) + (b->digits[b_len-3]<<FFBI_BITS_PER_DIGIT) + b->digits[b_len-4];
		}
#if FFBI_DIV_DEBUG
		fflog_print("%llu/%llu\n", q->digits[q_index], leftmost_b);
#endif
		q->digits[q_index] /= leftmost_b;
	skip_q_set:
#if FFBI_DIV_DEBUG
		fflog_print("found q = %d, val = %llu\n", q_index, (uint64_t)q->digits[q_index]);
#endif
		product->num_used_digits = 1;
		product->digits[0] = q->digits[q_index];
		ffbi_mul(scratch, product, b);
		uint32_t q_miss_count = 0;
		while(ffbi_cmp(r, scratch) == -1)
		{
			q_miss_count++;
			fflog_debug_print("product more than remainder. q_miss_count=%u\n", q_miss_count);
			if(product->digits[0] == 0)
				break;
			product->digits[0]--;
			q->digits[q_index]--;
			ffbi_mul(scratch, product, b);
		}
/*
		fflog_print("mul_result=");
		for(int i=scratch->num_used_digits-1;i>=0;i--)
			fflog_print("[%d,%llu]", i, (uint64_t)scratch->digits[i]);
		fflog_print("\n");*/
		//ffbi_copy(product, scratch);
/*
		fflog_print("r=");
		for(int i=r->num_used_digits-1;i>=0;i--)
			fflog_print("[%d,%u]", i, r->digits[i]);
		fflog_print("\n");*/
/*
		fflog_print("rdigits=%u, product_digits=%u\n", r->num_used_digits, product->num_used_digits);
		if(ffbi_cmp(r, product) == 1)
			fflog_print("r >product.\n");
		else if(ffbi_cmp(r, product) == 0)
			fflog_print("r =product.\n");
		else
			fflog_print("r <product.\n");*/
		ffbi_sub(product, r, scratch); //use product to temporarily hold the new remainder
		//replace the old remainder with the new remainder
		if(ffbi_cmp(product, b) == 1)
		{
			fflog_debug_print("r greater than b.\n");
			fflog_print("dig=%u r=", product->num_used_digits);
			for(int i=product->num_used_digits-1;i>=0;i--)
				fflog_print("[%d,%llu]", i, (uint64_t)product->digits[i]);
			fflog_print("\n");
			fflog_print("dig=%u b=", b->num_used_digits);
			for(int i=b->num_used_digits-1;i>=0;i--)
				fflog_print("[%d,%llu]", i, (uint64_t)b->digits[i]);
			fflog_print("\n");
		}
		if(product->num_used_digits > 1 || product->digits[0] > 0)
		{
			r->num_used_digits = product->num_used_digits+is_not_last_digit;
			if(r->num_allocated_digits < r->num_used_digits)
				ffbi_reallocate_digits(r, r->num_used_digits, 0);
			r->digits[0] = 0;
			for(k=0;k<product->num_used_digits;k++)
				r->digits[k+is_not_last_digit] = product->digits[k];
			r->digits[0] += a->digits[q_index-is_not_last_digit]*is_not_last_digit; //update remainder with next digit in dividend
		}
		else
		{
			r->num_used_digits = product->num_used_digits;
			r->digits[0] = a->digits[q_index-is_not_last_digit]*is_not_last_digit;
		}
#if FFBI_DIV_DEBUG
		fflog_print("new remainder = ");
		for(int i=r->num_used_digits-1;i>=0;i--)
			fflog_print("[%u]", r->digits[i]);
		fflog_print("\n");
#endif
	}
}

#if FFBI_DIV_CACHE_ENABLED
void ffbi_div_cache(ffbi_t* dest, ffbi_t* a, ffbi_t* b, ffbi_t* rem, ffbi_t* scratch1, ffbi_t* scratch2)
{
	ffbi_cache_update(a, FFBI_CACHE_DIV_BITS_PER_DIGIT, _cache_div_digit_max);
	ffbi_cache_update(b, FFBI_CACHE_DIV_BITS_PER_DIGIT, _cache_div_digit_max);
	scratch1->cache_bits_per_digit = FFBI_CACHE_DIV_BITS_PER_DIGIT;
	ffbi_cache_prepare(scratch1, b->cache_num_used_digits+1, 0);

	//determine the starting quotient digit index
	int a_len = a->cache_num_used_digits;
	int b_len = b->cache_num_used_digits;
	int q_index = a_len - b_len;

	//create or reallocate quotient with appropriate size
	ffbi_cache_prepare(dest, q_index+1, 0);
	dest->cache_num_used_digits = q_index+1;
	dest->cache_bits_per_digit = FFBI_CACHE_DIV_BITS_PER_DIGIT;

	//create or reallocate remainder with size of b. remainder bigint is also used as temporary dividends on iteration
	ffbi_t* r;
	if(rem == NULL)
		r = ffbi_create_reserved_digits(b->num_used_digits+1+FFBI_MIN_ALLOC_DIGITS);
	else
		r = rem;
	ffbi_cache_prepare(r, b_len+1, 0);
	r->cache_bits_per_digit = FFBI_CACHE_DIV_BITS_PER_DIGIT;

	uint32_t k;
	for(k=0;k<(uint32_t)b_len;k++) //fill r with first partial iteration dividend
		r->cache[k] = a->cache[q_index+k];
	r->cache_num_used_digits = b_len;
	//iterate and divide once per quotient digit, starting with leftmost digit and ending on one before the rightmost
	for(;q_index>0;q_index--)
		ffbi_get_quotient_digit_cache(q_index, dest, r, a, b, b_len, scratch1, 1, scratch2);
	ffbi_get_quotient_digit_cache(q_index, dest, r, a, b, b_len, scratch1, 0, scratch2);
	//see if the most significant digit of the quotient is 0, if so, trim it off.
	if(dest->cache_num_used_digits > 1 && dest->cache[dest->cache_num_used_digits-1] == 0)
		dest->cache_num_used_digits--;

	if(rem == NULL)
		ffbi_destroy(r);
	else
		ffbi_cache_retrieve(rem);
	ffbi_cache_retrieve(dest);
	scratch1->cache_valid = 0;
	scratch2->cache_valid = 0;
}
#endif

//scratch 1 and 2 are user allocated bigints that has at least b's number of digits plus 1 used for
//internal calculations. This is required.
//remainder may be NULL if user does not need it, in which case an extra allocation takes place internally.
//Ideally, remainder is also a user allocated bigint with at least b's number of digits plus 1
//for internal calculations in the case where optimizations may be possible with bigint reuse.
//Returns 0 on success and 1 on error. dest may not be NULL or point to another argument.
int ffbi_div_impl(ffbi_t* dest, ffbi_t* a, ffbi_t* b, ffbi_t* rem, ffbi_t* scratch1, ffbi_t* scratch2)
{
#if FFBI_DIV_DEBUG
	fflog_print("\nstarting div\n");
	fflog_print("a=");
	for(int i=a->num_used_digits-1;i>=0;i--)
		fflog_print("[%u]", a->digits[i]);
	fflog_print("\nb=");
	for(int i=b->num_used_digits-1;i>=0;i--)
		fflog_print("[%u]", b->digits[i]);
	fflog_print("\n");
#endif
	int cmp = ffbi_cmp(b, a);
	if(cmp == 1) //if b>a, return 0
	{
		if(rem)
		{
			if(rem != a)
			{
				if(a->num_used_digits > rem->num_allocated_digits)
					ffbi_reallocate_digits(rem, a->num_used_digits, 0);
				rem->num_used_digits = a->num_used_digits;
				memcpy(rem->digits, a->digits, a->num_used_digits*sizeof(ffbi_word_t));
				rem->cache_valid = 0;
			}
		}
		dest->num_used_digits = 1;
		dest->digits[0] = 0;
		dest->cache_valid = 0;
		return 0;
	}
	if(cmp == 0) //if b=a, return 1
	{
		dest->num_used_digits = 1;
		dest->digits[0] = 1;
		dest->cache_valid = 0;
		if(rem)
		{
			rem->num_used_digits = 1;
			rem->digits[0] = 0;
			rem->cache_valid = 0;
		}
		return 0;
	}

#if FFBI_DIV_CACHE_ENABLED
	if(a->num_used_digits > 1 || b->num_used_digits > 1)
	{
		ffbi_div_cache(dest, a, b, rem, scratch1, scratch2);
		return 0;
	}
#endif

#if FFBI_DIV_DEBUG
	fflog_print("confirmed a>b\n");
#endif
	//determine the starting quotient digit index
	int a_len = a->num_used_digits;
	int b_len = b->num_used_digits;
	int q_index = a_len - b_len;

#if FFBI_DIV_DEBUG
	fflog_print("start q_index = %d\n", q_index);
#endif

	//create or reallocate quotient with appropriate size
	if(dest->num_allocated_digits <= (uint32_t)q_index)
		ffbi_reallocate_digits(dest, q_index+2, 0);
	dest->num_used_digits = q_index+1;

	ffbi_t* product = scratch1;
	//create or reallocate remainder with size of b. remainder bigint is also used as temporary dividends on iteration
	ffbi_t* r;
	if(rem == NULL)
		r = ffbi_create_reserved_digits(b_len+1+FFBI_MIN_ALLOC_DIGITS);
	else
	{
		r = rem;
		if(r->num_allocated_digits < (uint32_t)b_len+1)
			ffbi_reallocate_digits(r, (b_len+1)*FFBI_REALLOC_GROWTH_FACTOR+1, 0);
	}
	uint32_t k;
	for(k=0;k<(uint32_t)b_len;k++) //fill r with first partial iteration dividend
		r->digits[k] = a->digits[q_index+k];
	r->num_used_digits = b_len;
	//iterate and divide once per quotient digit, starting with leftmost digit and ending on one before the rightmost
	for(;q_index>0;q_index--)
		ffbi_get_quotient_digit(q_index, dest, r, a, b, b_len, product, 1, scratch2);
	ffbi_get_quotient_digit(q_index, dest, r, a, b, b_len, product, 0, scratch2);
	//see if the most significant digit of the quotient is 0, if so, trim it off.
	if(dest->num_used_digits > 1 && dest->digits[dest->num_used_digits-1] == 0)
		dest->num_used_digits--;

	int ret = 0;
	if(rem == NULL)
		ffbi_destroy(r);
	dest->cache_valid = 0;
	scratch1->cache_valid = 0;
	scratch2->cache_valid = 0;
	return ret;
}

//[division] dest = a / b
void ffbi_div(ffbi_t* dest, ffbi_t* a, ffbi_t* b)
{
	ffbi_t* scratch = ffbi_create_reserved_digits(b->num_used_digits+1);
	ffbi_t* scratch2 = ffbi_create_reserved_digits(b->num_used_digits+1);
	ffbi_div_impl(dest, a, b, NULL, scratch, scratch2);
	ffbi_destroy(scratch);
	ffbi_destroy(scratch2);
}

//[mod] dest = a % b
void ffbi_mod(ffbi_t* dest, ffbi_t* a, ffbi_t* b)
{
	ffbi_t* scratch = ffbi_create_reserved_digits(b->num_used_digits+1);
	ffbi_t* scratch2 = ffbi_create_reserved_digits(b->num_used_digits+1);
	ffbi_t* quotient = ffbi_create_reserved_digits(a->num_used_digits);
	ffbi_div_impl(quotient, a, b, dest, scratch, scratch2);
	ffbi_destroy(scratch);
	ffbi_destroy(scratch2);
	ffbi_destroy(quotient);
}

//[modular exponentiation] dest = (n ^ e) % m
//dest should have m's + n's number of digits to avoid a reallocation.
//dest should not be the same pointer as any other arguments.
void ffbi_mod_pow(ffbi_t* dest, ffbi_t* n, ffbi_t* e, ffbi_t* m, ffbi_scratch_t* scratch)
{
	if(m->num_used_digits == 1 && m->digits[0] == 1)
	{
		dest->num_used_digits = 1;
		dest->digits[0] = 0;
		return;
	}
	uint8_t free_scratches = 0;
	if(scratch == NULL)
	{
		scratch = ffbi_scratch_create();
		free_scratches = 1;
	}
	uint32_t num_digits = m->num_used_digits+n->num_used_digits;
	ffbi_scratch_prepare(scratch, FFBI_MOD_POW_NUM_SCRATCHES, (int)num_digits);
	ffbi_t* ret = dest;
	if(ret->num_allocated_digits < num_digits)
		ffbi_reallocate_digits(ret, num_digits, 0);
	ret->num_used_digits = 1;
	ret->digits[0] = 1;
	ret->cache_valid = 0;
	ffbi_t* x = scratch->val[0];
	ffbi_copy(x, e);
	ffbi_t* apow = scratch->val[1];
	ffbi_copy(apow, n);
	while(x->num_used_digits > 1 || x->digits[0] > 0)
	{
		if(x->digits[0]&1)
		{
			ffbi_mul(scratch->val[2], ret, apow);
			ffbi_div_impl(scratch->val[5], scratch->val[2], m, ret, scratch->val[3], scratch->val[4]);
		}
		ffbi_word_t carry = 0;
		for(int i=x->num_used_digits-1;i>=0;i--)
		{
			x->digits[i] += carry << FFBI_BITS_PER_DIGIT;
			carry = x->digits[i]&1;
			x->digits[i]>>=1;
		}
		if(x->num_used_digits > 1 && x->digits[x->num_used_digits-1] == 0)
			x->num_used_digits--;
		ffbi_mul(scratch->val[2], apow, apow);
		ffbi_div_impl(scratch->val[5], scratch->val[2], m, apow, scratch->val[3], scratch->val[4]);
	}
	x->cache_valid = 0;
	if(free_scratches)
		ffbi_scratch_destroy(scratch);
}

//[modular multiplicative inverse] dest = multiplicative inverse of a mod m.
void ffbi_mod_inv(ffbi_t* dest, ffbi_t* a, ffbi_t* m)
{
	if(dest->num_allocated_digits < m->num_used_digits)
		ffbi_reallocate_digits(dest, m->num_used_digits, 0);
	if(m->num_used_digits == 1 && m->digits[0] == 1)
	{
		dest->num_used_digits = 1;
		dest->digits[0] = 0;
		dest->cache_valid = 0;
		return;
	}
	ffbi_t* m_temp = ffbi_create_from_bigint(m);
	ffbi_t* a_temp = ffbi_create_from_bigint(a);
	ffbi_t* t = ffbi_create_reserved_digits(m->num_used_digits);
	ffbi_t* y = ffbi_create_reserved_digits(m->num_used_digits);
	ffbi_t* x = ffbi_create_reserved_digits(m->num_used_digits);
	x->num_used_digits = 1;
	x->digits[0] = 1;
	ffbi_t* q = ffbi_create_reserved_digits(m->num_used_digits);
	ffbi_t* scratch = ffbi_create_reserved_digits(m->num_used_digits+1);
	ffbi_t* scratch2 = ffbi_create_reserved_digits(m->num_used_digits+1);
	ffbi_t* temp = ffbi_create_reserved_digits(m->num_used_digits);
	ffbi_t* temp2 = ffbi_create_reserved_digits(m->num_used_digits);
	uint8_t y_is_negative = 0;
	uint8_t x_is_negative = 0;
	while(a_temp->num_used_digits > 1 || a_temp->digits[0] > 1)
	{
		ffbi_div_impl(q, a_temp, m_temp, temp, scratch, scratch2);
		ffbi_copy(a_temp, m_temp);
		ffbi_copy(m_temp, temp);
		ffbi_copy(t, y);
		uint8_t t_is_negative = y_is_negative;
		ffbi_mul(temp, q, y);
		int cmp = ffbi_cmp(x, temp);
		if(cmp == -1)
		{
			if(x_is_negative)
			{
				if(y_is_negative)
				{
					ffbi_sub(y, temp, x);
					y_is_negative = 0;
				}
				else
				{
					ffbi_add(y, x, temp);
					y_is_negative = 1;
				}
			}
			else
			{
				if(y_is_negative)
				{
					ffbi_add(y, temp, x);
					y_is_negative = 0;
				}
				else
				{
					ffbi_sub(y, temp, x);
					y_is_negative = 1;
				}
			}
		}
		else if(cmp == 1)
		{
			if(x_is_negative)
			{
				if(y_is_negative)
					ffbi_sub(y, x, temp);
				else
				{
					ffbi_add(y, x, temp);
					y_is_negative = 1;
				}
			}
			else
			{
				if(y_is_negative)
				{
					ffbi_add(y, x, temp);
					y_is_negative = 0;
				}
				else
					ffbi_sub(y, x, temp);
			}
		}
		else
		{
			if(x_is_negative)
			{
				if(y_is_negative)
				{
					y->num_used_digits = 1;
					y->digits[0] = 0;
					y->cache_valid = 0;
					y_is_negative = 0;
				}
				else
				{
					ffbi_add(y, x, temp);
					y_is_negative = 1;
				}
			}
			else
			{
				if(y_is_negative)
				{
					ffbi_add(y, x, temp);
					y_is_negative = 0;
				}
				else
				{
					y->num_used_digits = 1;
					y->digits[0] = 0;
					y->cache_valid = 0;
				}
			}
		}
		if(y->num_used_digits == 1 && y->digits[0] == 0)
			y_is_negative = 0;
		ffbi_copy(x, t);
		x_is_negative = t_is_negative;
	}
	if(x_is_negative)
		ffbi_sub(t, m, x);
	ffbi_copy(dest, t);
	ffbi_destroy(temp2);
	ffbi_destroy(x);
	ffbi_destroy(temp);
	ffbi_destroy(scratch);
	ffbi_destroy(scratch2);
	ffbi_destroy(q);
	ffbi_destroy(y);
	ffbi_destroy(t);
	ffbi_destroy(a_temp);
	ffbi_destroy(m_temp);
}

void ffbi_copy(ffbi_t* dest, ffbi_t* src)
{
	if(dest->num_allocated_digits < src->num_used_digits)
		ffbi_reallocate_digits(dest, src->num_used_digits, 0);
	dest->num_used_digits = src->num_used_digits;
	memcpy(dest->digits, src->digits, src->num_used_digits*sizeof(ffbi_word_t));
	dest->cache_valid = 0;
}

int ffbi_is_zero(ffbi_t* p)
{
	if(p->num_used_digits == 1 && p->digits[0] == 0)
		return 1;
	return 0;
}
