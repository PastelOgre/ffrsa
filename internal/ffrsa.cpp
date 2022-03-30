/*
 * ffrsa.cpp
 *
 *  Created on: May 16, 2018
 *      Author: Jesse Wang
 */

#include "../ffrsa.h"
#include "ffbi.h"
#include "ffmem.h"
#include "fflog.h"
#include <string.h>
#include "ffbit.h"
#include "ffdigest.h"
#include <vector>
#include <stdlib.h>
#include <time.h>

#define FFRSA_DEFAULT_KEY_RESERVED_BITS 2048

typedef struct FFRSA
{
	ffbi_t* p;
	ffbi_t* q;
	ffbi_t* n;
	ffbi_t* e;
	ffbi_t* dp;
	ffbi_t* dq;
	ffbi_t* qinv;
	ffbi_t* temp;
	ffbi_t* temp2;
	ffbi_t* temp3;
	ffbi_t* m1;
	ffbi_t* m2;
	ffbi_t* h;
	ffbi_t* m1_inc;
	ffbi_scratch_t* scratch;
	uint8_t* result;
	uint32_t result_alloc_size;
	uint32_t result_used_size;
	uint32_t max_msg_size;
	uint32_t rsa_usable_size;
	uint8_t is_private;
	std::vector<uint8_t>* padding_scratch;
	std::vector<uint8_t>* padding_scratch2;
	std::vector<uint8_t>* padding_scratch3;
	std::vector<uint8_t>* padding_seed;
	std::vector<uint8_t>* padding_mask;
} ffrsa_t;

static void ffrsa_init(ffrsa_t* ret, uint32_t bits, uint8_t is_private)
{
	ret->temp = ffbi_create_reserved_bits(bits);
	ret->rsa_usable_size = (ffbi_get_significant_bits(ret->n) - 1)/8;
	uint32_t padsize = (FFDIGEST_BUFLEN << 1) + 1;
	if(padsize > ret->rsa_usable_size)
		ret->max_msg_size = 0;
	else
		ret->max_msg_size = ret->rsa_usable_size - padsize;
	ret->temp2 = ffbi_create_reserved_bits(bits);
	ret->temp3 = ffbi_create_reserved_bits(bits);
	ret->scratch = ffbi_scratch_create();
	ret->is_private = is_private;
	ret->padding_scratch = ffmem_alloc(std::vector<uint8_t>);
	ret->padding_scratch2 = ffmem_alloc(std::vector<uint8_t>);
	ret->padding_scratch3 = ffmem_alloc(std::vector<uint8_t>);
	ret->padding_seed = ffmem_alloc(std::vector<uint8_t>);
	ret->padding_mask = ffmem_alloc(std::vector<uint8_t>);
	if(is_private)
	{
		ret->m1 = ffbi_create_reserved_bits(bits);
		ret->m2 = ffbi_create_reserved_bits(bits);
		ret->h = ffbi_create_reserved_bits(bits);
		ret->m1_inc = ffbi_create_reserved_bits(bits);
		ffbi_div_impl(ret->m1_inc, ret->q, ret->p, ret->temp2, ret->m1, ret->m2);
		ffbi_add_u(ret->m1_inc, ret->m1_inc, 1);
		ffbi_mul(ret->m1_inc, ret->m1_inc, ret->p);
	}
}

//Create rsa key with specified number of bits.
ffrsa_t* ffrsa_create(uint32_t bits)
{
	srand(time(NULL));
	ffrsa_t* ret = ffmem_alloc(ffrsa_t);
	memset(ret, 0, sizeof(ffrsa_t));
	ffbi_scratch_t* sieve = ffbi_scratch_create();
	ffbi_get_sieve(sieve, 100000);
	uint32_t p_bits = (bits*5)/11;
	uint32_t q_bits = bits - p_bits;
	ret->p = ffbi_create_random_large_prime(p_bits, 20, sieve);
	ret->q = ffbi_create_random_large_prime(q_bits, 20, sieve);
	ret->n = ffbi_create_reserved_bits(bits);
	ffbi_mul(ret->n, ret->p, ret->q);
	ffbi_t* q_minus_1 = ffbi_create_from_bigint(ret->q);
	ffbi_t* p_minus_1 = ffbi_create_from_bigint(ret->p);
	ffbi_t* one = ffbi_create();
	ffbi_add_u(one, one, 1);
	ffbi_sub(q_minus_1, q_minus_1, one);
	ffbi_sub(p_minus_1, p_minus_1, one);
	ffbi_t* totient = ffbi_create_reserved_bits(bits);
	ffbi_mul(totient, p_minus_1, q_minus_1);
	ret->e = ffbi_create();
	ffbi_add_u(ret->e, ret->e, 65537);
	ffbi_t* d = ffbi_create_reserved_bits(bits);
	ffbi_mod_inv(d, ret->e, totient);
	//ffbi_print(d);
	//check d
	ffbi_t* temp1 = ffbi_create_reserved_bits(bits);
	ffbi_t* temp2 = ffbi_create_reserved_bits(bits);
	ffbi_mul(temp1, d, ret->e);
	ffbi_mod(temp2, temp1, totient);
	if(ffbi_cmp(temp2, one) != 0)
	{
		fflog_debug_print("mod inverse failed. remainder=");
		ffbi_print(temp2);
		ffrsa_destroy(ret);
		ret = NULL;
		goto finish;
	}

	ret->dp = ffbi_create_reserved_bits(bits);
	ret->dq = ffbi_create_reserved_bits(bits);
	ret->qinv = ffbi_create_reserved_bits(bits);
	ffbi_mod(ret->dp, d, p_minus_1);
	ffbi_mod(ret->dq, d, q_minus_1);
	ffbi_mod_inv(ret->qinv, ret->q, ret->p);

	ffrsa_init(ret, bits, 0);
	if(ret->max_msg_size == 0)
	{
		ffrsa_destroy(ret);
		ret = NULL;
	}

finish:
	ffbi_destroy(temp1);
	ffbi_destroy(temp2);
	ffbi_destroy(d);
	ffbi_destroy(q_minus_1);
	ffbi_destroy(p_minus_1);
	ffbi_destroy(one);
	ffbi_destroy(totient);
	ffbi_scratch_destroy(sieve);
	return ret;
}

//Create rsa key from a public key buffer. Keys created in this manner can
//only encrypt.
ffrsa_t* ffrsa_create_from_public_key(const uint8_t* key)
{
	ffrsa_t* ret = ffmem_alloc(ffrsa_t);
	memset(ret, 0, sizeof(ffrsa_t));
	ret->e = ffbi_create();
	ret->n = ffbi_create_reserved_bits(FFRSA_DEFAULT_KEY_RESERVED_BITS);
	uint8_t* p = (uint8_t*)key;
	ffbit_t* bp = ffbit_create(p);
	uint32_t e_size = ffbit_read(bp, 32);
	p += 4;
	ffbi_deserialize(ret->e, p, e_size);
	p += e_size;
	ffbit_set(bp, p, 0);
	uint32_t n_size = ffbit_read(bp, 32);
	p += 4;
	ffbi_deserialize(ret->n, p, n_size);
	ffbit_destroy(bp);
	ffrsa_init(ret, ffbi_get_significant_bits(ret->n), 0);
	return ret;
}

//Create rsa key from a private key buffer. Keys created in this manner can
//both encrypt and decrypt.
ffrsa_t* ffrsa_create_from_private_key(const uint8_t* key)
{
	ffrsa_t* ret = ffmem_alloc(ffrsa_t);
	memset(ret, 0, sizeof(ffrsa_t));
	ret->p = ffbi_create_reserved_bits(FFRSA_DEFAULT_KEY_RESERVED_BITS);
	ret->q = ffbi_create_reserved_bits(FFRSA_DEFAULT_KEY_RESERVED_BITS);
	ret->n = ffbi_create_reserved_bits(FFRSA_DEFAULT_KEY_RESERVED_BITS);
	ret->e = ffbi_create();
	ret->dp = ffbi_create_reserved_bits(FFRSA_DEFAULT_KEY_RESERVED_BITS);
	ret->dq = ffbi_create_reserved_bits(FFRSA_DEFAULT_KEY_RESERVED_BITS);
	ret->qinv = ffbi_create_reserved_bits(FFRSA_DEFAULT_KEY_RESERVED_BITS);
	uint8_t* p = (uint8_t*)key;
	ffbit_t* bp = ffbit_create(p);
	uint32_t deserialized_size = ffbit_read(bp, 32);
	p += 4;
	ffbi_deserialize(ret->p, p, deserialized_size);
	p += deserialized_size;
	ffbit_set(bp, p, 0);
	deserialized_size = ffbit_read(bp, 32);
	p += 4;
	ffbi_deserialize(ret->q, p, deserialized_size);
	p += deserialized_size;
	ffbit_set(bp, p, 0);
	deserialized_size = ffbit_read(bp, 32);
	p += 4;
	ffbi_deserialize(ret->n, p, deserialized_size);
	p += deserialized_size;
	ffbit_set(bp, p, 0);
	deserialized_size = ffbit_read(bp, 32);
	p += 4;
	ffbi_deserialize(ret->e, p, deserialized_size);
	p += deserialized_size;
	ffbit_set(bp, p, 0);
	deserialized_size = ffbit_read(bp, 32);
	p += 4;
	ffbi_deserialize(ret->dp, p, deserialized_size);
	p += deserialized_size;
	ffbit_set(bp, p, 0);
	deserialized_size = ffbit_read(bp, 32);
	p += 4;
	ffbi_deserialize(ret->dq, p, deserialized_size);
	p += deserialized_size;
	ffbit_set(bp, p, 0);
	deserialized_size = ffbit_read(bp, 32);
	p += 4;
	ffbi_deserialize(ret->qinv, p, deserialized_size);
	ffbit_destroy(bp);
	ffrsa_init(ret, ffbi_get_significant_bits(ret->n), 1);
	return ret;
}

void ffrsa_destroy(ffrsa_t* rsa)
{
	if(rsa->p)
		ffbi_destroy(rsa->p);
	if(rsa->q)
		ffbi_destroy(rsa->q);
	if(rsa->n)
		ffbi_destroy(rsa->n);
	if(rsa->e)
		ffbi_destroy(rsa->e);
	if(rsa->dp)
		ffbi_destroy(rsa->dp);
	if(rsa->dq)
		ffbi_destroy(rsa->dq);
	if(rsa->qinv)
		ffbi_destroy(rsa->qinv);
	if(rsa->temp)
		ffbi_destroy(rsa->temp);
	if(rsa->temp2)
		ffbi_destroy(rsa->temp2);
	if(rsa->temp3)
		ffbi_destroy(rsa->temp3);
	if(rsa->scratch)
		ffbi_scratch_destroy(rsa->scratch);
	if(rsa->m1)
		ffbi_destroy(rsa->m1);
	if(rsa->m2)
		ffbi_destroy(rsa->m2);
	if(rsa->h)
		ffbi_destroy(rsa->h);
	if(rsa->m1_inc)
		ffbi_destroy(rsa->m1_inc);
	if(rsa->result)
		ffmem_free_arr(rsa->result);
	if(rsa->padding_scratch)
		ffmem_free(rsa->padding_scratch);
	if(rsa->padding_scratch2)
		ffmem_free(rsa->padding_scratch2);
	if(rsa->padding_scratch3)
		ffmem_free(rsa->padding_scratch3);
	if(rsa->padding_seed)
		ffmem_free(rsa->padding_seed);
	if(rsa->padding_mask)
		ffmem_free(rsa->padding_mask);
	ffmem_free(rsa);
}

//Get buffer size required to hold private key.
int ffrsa_get_private_key_size(ffrsa_t* rsa)
{
	int ret = 0;
	ret += ffbi_get_serialized_size(rsa->p) + 4;
	ret += ffbi_get_serialized_size(rsa->q) + 4;
	ret += ffbi_get_serialized_size(rsa->n) + 4;
	ret += ffbi_get_serialized_size(rsa->e) + 4;
	ret += ffbi_get_serialized_size(rsa->dp) + 4;
	ret += ffbi_get_serialized_size(rsa->dq) + 4;
	ret += ffbi_get_serialized_size(rsa->qinv) + 4;
	return ret;
}

//Get buffer size required to hold public key.
int ffrsa_get_public_key_size(ffrsa_t* rsa)
{
	return ffbi_get_serialized_size(rsa->e) + ffbi_get_serialized_size(rsa->n) + 8;
}

//Write private key into key. Returns number of bytes written on success or
//-1 if buffer isn't big enough. Returns 0 for other errors.
int ffrsa_get_private_key(ffrsa_t* rsa, uint8_t* key, int size_bytes)
{
	uint32_t p_size = (uint32_t)ffbi_get_serialized_size(rsa->p);
	uint32_t q_size = (uint32_t)ffbi_get_serialized_size(rsa->q);
	uint32_t n_size = (uint32_t)ffbi_get_serialized_size(rsa->n);
	uint32_t e_size = (uint32_t)ffbi_get_serialized_size(rsa->e);
	uint32_t dp_size = (uint32_t)ffbi_get_serialized_size(rsa->dp);
	uint32_t dq_size = (uint32_t)ffbi_get_serialized_size(rsa->dq);
	uint32_t qinv_size = (uint32_t)ffbi_get_serialized_size(rsa->qinv);
	int total_size = p_size + 4 + q_size + 4 + n_size + 4 + e_size + 4 + dp_size + 4 + dq_size + 4 + qinv_size + 4;
	if(total_size > size_bytes)
		return -1;
	uint8_t* p = key;
	ffbit_t* bp = ffbit_create(p);
	ffbit_write(bp, 32, p_size);
	p += 4;
	ffbi_serialize(rsa->p, p, p_size);
	p += p_size;
	ffbit_set(bp, p, 0);
	ffbit_write(bp, 32, q_size);
	p += 4;
	ffbi_serialize(rsa->q, p, q_size);
	p += q_size;
	ffbit_set(bp, p, 0);
	ffbit_write(bp, 32, n_size);
	p += 4;
	ffbi_serialize(rsa->n, p, n_size);
	p += n_size;
	ffbit_set(bp, p, 0);
	ffbit_write(bp, 32, e_size);
	p += 4;
	ffbi_serialize(rsa->e, p, e_size);
	p += e_size;
	ffbit_set(bp, p, 0);
	ffbit_write(bp, 32, dp_size);
	p += 4;
	ffbi_serialize(rsa->dp, p, dp_size);
	p += dp_size;
	ffbit_set(bp, p, 0);
	ffbit_write(bp, 32, dq_size);
	p += 4;
	ffbi_serialize(rsa->dq, p, dq_size);
	p += dq_size;
	ffbit_set(bp, p, 0);
	ffbit_write(bp, 32, qinv_size);
	p += 4;
	ffbi_serialize(rsa->qinv, p, qinv_size);
	ffbit_destroy(bp);
	return total_size;
}

//Write public key into key. Returns number of bytes written on success or
//-1 if buffer isn't big enough. Returns 0 for other errors.
int ffrsa_get_public_key(ffrsa_t* rsa, uint8_t* key, int size_bytes)
{
	uint32_t e_size = (uint32_t)ffbi_get_serialized_size(rsa->e);
	uint32_t n_size = (uint32_t)ffbi_get_serialized_size(rsa->n);
	int total_size = (int)(e_size + n_size + 8);
	if(total_size > size_bytes)
		return -1;
	ffbit_t* bp = ffbit_create(key);
	ffbit_write(bp, 32, e_size);
	uint8_t* p = key + 4;
	ffbi_serialize(rsa->e, p, e_size);
	p += e_size;
	ffbit_set(bp, p, 0);
	ffbit_write(bp, 32, n_size);
	p += 4;
	ffbi_serialize(rsa->n, p, n_size);
	ffbit_destroy(bp);
	return total_size;
}

int ffrsa_get_max_msg_len(ffrsa_t* rsa)
{
	return (int)rsa->max_msg_size;
}

static void ffrsa_update_result(ffrsa_t* rsa, ffbi_t* val)
{
	int serialized_size = ffbi_get_serialized_size(val);
	if(rsa->result == NULL)
	{
		rsa->result = ffmem_alloc_arr(uint8_t, serialized_size);
		rsa->result_alloc_size = serialized_size;
	}
	else if(serialized_size > (int)rsa->result_alloc_size)
	{
		ffmem_free_arr(rsa->result);
		rsa->result = ffmem_alloc_arr(uint8_t, serialized_size);
		rsa->result_alloc_size = serialized_size;
	}
	rsa->result_used_size = serialized_size;
	ffbi_serialize(val, rsa->result, rsa->result_used_size);
}

static void ffrsa_mgf1(ffrsa_t* rsa, std::vector<uint8_t>* mask, std::vector<uint8_t>* seed, int seed_offset, int seed_len, int desired_len)
{
	int hlen = FFDIGEST_BUFLEN;
	int offset = 0;
	int i = 0;
	mask->resize(desired_len);
	rsa->padding_scratch->resize(seed_len+4);
	memcpy(&(*rsa->padding_scratch)[4], &(*seed)[seed_offset], seed_len);
	while(offset < desired_len)
	{
		(*rsa->padding_scratch)[0] = (uint8_t)(i >> 24);
		(*rsa->padding_scratch)[1] = (uint8_t)(i >> 16);
		(*rsa->padding_scratch)[2] = (uint8_t)(i >> 8);
		(*rsa->padding_scratch)[3] = (uint8_t)i;
		int remaining = desired_len - offset;
		if(remaining > hlen)
			remaining = hlen;
		uint8_t buf[FFDIGEST_BUFLEN];
		ffdigest_buf(buf, &(*rsa->padding_scratch)[0], rsa->padding_scratch->size());
		memcpy(&(*mask)[offset], buf, remaining);
		offset += hlen;
		i++;
	}
}

static void ffrsa_pad(ffrsa_t* rsa, std::vector<uint8_t>* out, uint8_t* msg, int msg_len, int desired_len)
{
	out->clear();
	int hlen = FFDIGEST_BUFLEN;
	if(msg_len > desired_len - (hlen << 1) - 1)
		return;
	int zero_pad = desired_len - msg_len - (hlen << 1) - 1;
	rsa->padding_scratch2->resize(desired_len - hlen);
	memset(&(*rsa->padding_scratch2)[0], 8, hlen);
	memcpy(&(*rsa->padding_scratch2)[hlen + zero_pad + 1], msg, msg_len);
	(*rsa->padding_scratch2)[hlen + zero_pad] = 1;
	rsa->padding_seed->resize(hlen);
	for(unsigned i=0;i<rsa->padding_seed->size();i++)
		(*rsa->padding_seed)[i] = rand()%0x100;
	ffrsa_mgf1(rsa, rsa->padding_mask, rsa->padding_seed, 0, hlen, desired_len - hlen);
	for(int i=0;i<desired_len - hlen;i++)
		(*rsa->padding_scratch2)[i] ^= (*rsa->padding_mask)[i];
	ffrsa_mgf1(rsa, rsa->padding_mask, rsa->padding_scratch2, 0, desired_len-hlen, hlen);
	for(int i=0;i<hlen;i++)
		(*rsa->padding_seed)[i] ^= (*rsa->padding_mask)[i];
	out->resize(desired_len);
	memcpy(&(*out)[0], &(*rsa->padding_seed)[0], hlen);
	memcpy(&(*out)[hlen], &(*rsa->padding_scratch2)[0], desired_len-hlen);
}

static void ffrsa_unpad(ffrsa_t* rsa, std::vector<uint8_t>* out, uint8_t* msg, int msg_len)
{
	out->clear();
	int hlen = FFDIGEST_BUFLEN;
	if(msg_len < (hlen << 1) + 1)
		return;
	rsa->padding_scratch2->resize(msg_len);
	memcpy(&(*rsa->padding_scratch2)[0], msg, msg_len);
	ffrsa_mgf1(rsa, rsa->padding_mask, rsa->padding_scratch2, hlen, msg_len-hlen, hlen);
	for(int i=0;i<hlen;i++)
		(*rsa->padding_scratch2)[i] ^= (*rsa->padding_mask)[i];
	uint8_t temp[FFDIGEST_BUFLEN];
	memset(temp, 8, FFDIGEST_BUFLEN);
	ffrsa_mgf1(rsa, rsa->padding_mask, rsa->padding_scratch2, 0, hlen, msg_len-hlen);
	int index = -1;
	for(int i=hlen;i<msg_len;i++)
	{
		(*rsa->padding_scratch2)[i] ^= (*rsa->padding_mask)[i-hlen];
		if(i<(hlen << 1))
		{
			if((*rsa->padding_scratch2)[i] != temp[i-hlen])
				return;
		}
		else if(i < msg_len - FFDIGEST_BUFLEN)
		{
			if((*rsa->padding_scratch2)[i] == 1)
				index = i+1;
		}
	}
	if(index == -1 || index == msg_len)
	{
		printf("index=%d\n", index);
		return;
	}
	out->resize(msg_len-index);
	memcpy(&(*out)[0], &(*rsa->padding_scratch2)[index], msg_len-index);
}

//Returns 0 on success and 1 on error.
int ffrsa_encrypt(ffrsa_t* rsa, uint8_t* src, int msg_len)
{
	if(msg_len > (int)rsa->max_msg_size)
	{
		fflog_print("ffrsa_encrypt failed. msg_len (%d) can't be greater than max message size of %u, dictated by the bit length of the RSA key.\n", msg_len, rsa->max_msg_size);
		return 1;
	}
	rsa->padding_scratch3->resize(rsa->rsa_usable_size);
	while(1)
	{
		ffrsa_pad(rsa, rsa->padding_scratch3, src, msg_len, rsa->rsa_usable_size);
		if(((*rsa->padding_scratch3)[rsa->rsa_usable_size-1]&1) == 1)
			break;
	}
	ffbi_deserialize(rsa->temp, &(*rsa->padding_scratch3)[0], rsa->rsa_usable_size);
	ffbi_mod_pow(rsa->temp2, rsa->temp, rsa->e, rsa->n, rsa->scratch);
	ffrsa_update_result(rsa, rsa->temp2);
	return 0;
}

//Returns 0 on success and 1 on error.
int ffrsa_decrypt(ffrsa_t* rsa, uint8_t* src, int msg_len)
{
	if(msg_len > (int)rsa->rsa_usable_size+1)
	{
		fflog_print("ffrsa_decrypt failed. msg_len (%d) can't be greater than %u, dictated by the bit length of the RSA key.\n", msg_len, rsa->rsa_usable_size+1);
		return 1;
	}
	if(rsa->is_private == 0)
	{
		fflog_print("ffrsa_decrypt failed. RSA public key used and cannot be used for decryption.\n");
		return 1;
	}
	ffbi_deserialize(rsa->temp, src, msg_len);
	ffbi_mod_pow(rsa->m1, rsa->temp, rsa->dp, rsa->p, rsa->scratch);
	ffbi_mod_pow(rsa->m2, rsa->temp, rsa->dq, rsa->q, rsa->scratch);
	if(ffbi_cmp(rsa->m1, rsa->m2) == -1)
		ffbi_add(rsa->m1, rsa->m1, rsa->m1_inc);
	ffbi_sub(rsa->m1, rsa->m1, rsa->m2);
	ffbi_mul(rsa->temp2, rsa->m1, rsa->qinv);
	ffbi_div_impl(rsa->temp, rsa->temp2, rsa->p, rsa->h, rsa->m1, rsa->temp3);
	ffbi_mul(rsa->temp2, rsa->h, rsa->q);
	ffbi_add(rsa->temp2, rsa->temp2, rsa->m2);
	ffrsa_update_result(rsa, rsa->temp2);
	ffrsa_unpad(rsa, rsa->padding_scratch3, rsa->result, rsa->result_used_size);
	if(rsa->padding_scratch3->size() == 0)
	{
		fflog_print("ffrsa_decrypt failed. Unpadding failed for unknown reasons. result_used_size=%u\n", rsa->result_used_size);
		return 1;
	}
	memcpy(rsa->result, &(*rsa->padding_scratch3)[0], rsa->padding_scratch3->size());
	rsa->result_used_size = rsa->padding_scratch3->size();
	return 0;
}

void ffrsa_get_result(ffrsa_t* rsa, uint8_t** result, int* msg_len)
{
	*result = rsa->result;
	*msg_len = (int)rsa->result_used_size;
}
