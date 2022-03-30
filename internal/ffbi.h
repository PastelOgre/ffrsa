/*
 * ffbi.h
 *
 *  Created on: Apr 20, 2018
 *      Author: Jesse Tse-Hsu Wang
 */

#ifndef FFBI_H_
#define FFBI_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) && !defined(__ANDROID__) && !defined(__APPLE__)
#define FFBI_WORD_SIZE 128
#define FFBI_BITS_PER_DIGIT 61
typedef unsigned __int128 ffbi_word_t;
#else
#define FFBI_WORD_SIZE 64
#define FFBI_BITS_PER_DIGIT 29
typedef uint64_t ffbi_word_t;
#endif

typedef struct FFBI ffbi_t;
typedef struct FFBI_SCRATCH ffbi_scratch_t;

//Run this function before any other library function.
//The only time omitting this initialization may cause problems is when this library is used in multiple threads.
void ffbi_init();

//Create a new bigint with value of 0.
ffbi_t* ffbi_create();

//Create a new bigint with value of 0. Initial memory capacity is allocated to fit up to specified number of bits.
ffbi_t* ffbi_create_reserved_bits(uint32_t bits);

//Create a new bigint with value of 0. Initial memory capacity is allocated to fit up to specified number of digits.
ffbi_t* ffbi_create_reserved_digits(uint32_t digits);

//Create a new bigint with value of 0 using a preallocated memory buffer owned
//by the user. Bigints created in this manner have the special property that
//the memory used cannot grow or shrink. Operations that cause overflow will
//be aborted.
ffbi_t* ffbi_create_preallocated(uint8_t* buffer, int size_bytes);

//Gets the array that p uses to store digits. This can be directly manipulated but not freed by the user.
//digits, num_used_digits, num_allocated_digits and bits_per_digit are outputs. All params are required.
void ffbi_get_digits(ffbi_t* p, ffbi_word_t** digits, uint32_t* num_used_digits, uint32_t* num_allocated_digits, uint32_t* bits_per_digit);

//Replaces the array that p uses to store digits. Pass NULL for digits if user directly manipulated the internal array and would
//just like to update num_used_digits and num_allocated_digits. Otherwise, the previous array gets freed and the new array (digits)
//gets used internally. This library will then become responsible for freeing it.
void ffbi_set_digits(ffbi_t* p, ffbi_word_t* digits, uint32_t num_used_digits, uint32_t num_allocated_digits, uint32_t bits_per_digit);

//Create and destroy scratch space used in operation optimizations.
ffbi_scratch_t* ffbi_scratch_create();
void ffbi_scratch_destroy(ffbi_scratch_t* scratch);

//Create a sieve for primality testing.
//n is the max possible prime value that the sieve contains.
//A recommended value is at least 100000. NULL is returned on error.
void ffbi_get_sieve(ffbi_scratch_t* sieve, uint32_t n);

//Generate a random bigint with specified number of bits.
void ffbi_random(ffbi_t* p, uint32_t bits);

//Generate a random bigint with a max value of limit-1. This is more optimal than doing it manually with ffbi_random.
void ffbi_random_with_limit(ffbi_t* p, ffbi_t* limit);

//Generate a random large prime bigint with specified number of bits.
//The more tests, the greater the chance of the bigint to actually be prime,
//but the time it takes is considerably longer. 20 for num_tests is recommended
//as it would mean there is about one-in-a-million chance for the bigint to
//not be prime. A sieve may be optionally provided to quickly weed out
//composites before the Fermat primality test. Pass NULL for sieve to skip
//the sieve test. It is an error if the number of bits specified can't hold
//a value of at least 16^8. NULL is returned on error.
ffbi_t* ffbi_create_random_large_prime(uint32_t bits, uint32_t num_tests, ffbi_scratch_t* sieve);

//Create a new bigint by making a copy of p. NULL is returned on error.
ffbi_t* ffbi_create_from_bigint(ffbi_t* p);

void ffbi_destroy(ffbi_t* p);

//Copy contents from src to dest.
void ffbi_copy(ffbi_t* dest, ffbi_t* src);

//Attempt to reallocate memory used by p to hold specified number of bits.
//If the specified number of bits is not enough to fit the current bigint value,
//the reallocated memory will be just enough to fit the current value.
void ffbi_reallocate(ffbi_t* p, uint32_t target_bits);

//Pass 1 for retain_value if retaining the value is important. Passing 0
//will result in memory copying and extra checks to see if target_num_digits
//should be respected.
void ffbi_reallocate_digits(ffbi_t* p, int target_num_digits, uint8_t retain_value);

//Uses Fermat primality test to determine if p is prime. Increase
//num_tests for higher chance p is actually prime. Probability of p being wrongly
//detected as prime is 2^(-num_tests). If p is less than 16^8, it is not considered
//large enough and will return false. A sieve may be optionally provided to quickly
//weed out composites before the Fermat primality test. Pass NULL for sieve to skip
//the sieve test. 1 is returned if p is prime and 0 is returned if otherwise.
//scratch contains memory buffers used internally to minimize unnecessary allocations
//for use in tight loops. Pass NULL for scratch for no optimizations.
int ffbi_is_large_prime(ffbi_t* p, int num_tests, ffbi_scratch_t* sieve, ffbi_scratch_t* scratch);

//Returns the size of the buffer necessary to hold the serialized bigint p in bytes.
int ffbi_get_serialized_size(ffbi_t* p);

//Serialize p into a buffer. Value at size_bytes should initially hold the size of the
//buffer passed in. This function returns the number of bytes actually written to buffer.
//Returns -1 if size_bytes is too small and 0 for other errors.
int ffbi_serialize(ffbi_t* p, uint8_t* buffer, int size_bytes);

//Use v2 if total_bits is already calculated.
int ffbi_serialize_v2(ffbi_t* p, uint8_t* buffer, int size_bytes, uint32_t total_bits);

//Deserializes buffer into bigint p.
void ffbi_deserialize(ffbi_t* p, uint8_t* buffer, int size_bytes);

uint32_t ffbi_get_significant_bits(ffbi_t* p);

//Prints the base 10 string representation of p into stdout.
void ffbi_print(ffbi_t* p);

//Prints the digit words stored internally into stdout.
void ffbi_print_words(ffbi_t* p);

//[compare] returns 0 if a == b, 1 if a > b, or -1 if a < b.
int ffbi_cmp(ffbi_t* a, ffbi_t* b);

//[addition] dest = a + b
//dest can point to the same bigint as a and b.
void ffbi_add(ffbi_t* dest, ffbi_t* a, ffbi_t* b);

//[addition] dest = a + b
//dest can point to the same bigint as a.
void ffbi_add_u(ffbi_t* dest, ffbi_t* a, uint32_t b);

//[subtraction] dest = a - b
//dest can point to the same bigint as a.
void ffbi_sub(ffbi_t* dest, ffbi_t* a, ffbi_t* b);

//[multiplication] dest = a * b
//dest can point to the same bigint as a and b, but will result in an extra internal allocation.
void ffbi_mul(ffbi_t* dest, ffbi_t* a, ffbi_t* b);

//[division] dest = a / b
//dest should not be the same pointer as any other arguments.
void ffbi_div(ffbi_t* dest, ffbi_t* a, ffbi_t* b);

//scratch 1 and 2 are user allocated bigints that has at least b's number of digits plus 1 used for
//internal calculations. This is required.
//remainder may be NULL if user does not need it, in which case an extra allocation takes place internally.
//Returns 0 on success and 1 on error. dest may not be NULL or point to another argument.
int ffbi_div_impl(ffbi_t* dest, ffbi_t* a, ffbi_t* b, ffbi_t* remainder, ffbi_t* scratch1, ffbi_t* scratch2);

//[mod] dest = a % b
//dest should not be the same pointer as any other arguments.
void ffbi_mod(ffbi_t* dest, ffbi_t* a, ffbi_t* b);

//[modular exponentiation] dest = (n ^ e) % m
//dest should also have m's + n's number of digits to avoid a reallocation.
//dest should not be the same pointer as any other arguments.
void ffbi_mod_pow(ffbi_t* dest, ffbi_t* n, ffbi_t* e, ffbi_t* m, ffbi_scratch_t* scratch);

//[modular multiplicative inverse] dest = multiplicative inverse of a mod m.
void ffbi_mod_inv(ffbi_t* dest, ffbi_t* a, ffbi_t* m);

int ffbi_is_zero(ffbi_t* p);

#ifdef __cplusplus
}
#endif

#endif /* FFBI_H_ */
