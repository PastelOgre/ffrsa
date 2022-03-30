/*
 * ffrsa.h
 *
 *  Created on: Apr 19, 2018
 *      Author: Jesse Tse-Hsu Wang
 *
 *  This is an RSA library with OAEP scheme built in. The encrypt and decrypt functions automatically
 *  take care of the padding. The OAEP implementation references the one found on
 *  https://github.com/mbakkar/OAEP
 *  which is changed to use SHA-3, utilizing libkeccak from
 *  https://github.com/maandree/libkeccak
 *  The RSA implementation uses e with value of 65537.
 */

#ifndef FFRSA_H_
#define FFRSA_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FFRSA ffrsa_t;

//Create rsa key with specified number of bits.
//Returns NULL if bits is too small due to the used padding scheme.
//Keep in mind that this function may block for a couple seconds as
//it generates a key. Performance sensitive applications should
//already have the key saved and the handle created using one of the
//ffrsa_create_from functions.
ffrsa_t* ffrsa_create(uint32_t bits);

//Create rsa key from a public key buffer. Keys created in this manner can
//only encrypt.
ffrsa_t* ffrsa_create_from_public_key(const uint8_t* key);

//Create rsa key from a private key buffer. Keys created in this manner can
//both encrypt and decrypt.
ffrsa_t* ffrsa_create_from_private_key(const uint8_t* key);

void ffrsa_destroy(ffrsa_t* rsa);

//Get buffer size required to hold private key.
int ffrsa_get_private_key_size(ffrsa_t* rsa);

//Get buffer size required to hold public key.
int ffrsa_get_public_key_size(ffrsa_t* rsa);

//Write private key into key. Returns number of bytes written on success or
//-1 if buffer isn't big enough. Returns 0 for other errors.
int ffrsa_get_private_key(ffrsa_t* rsa, uint8_t* key, int size_bytes);

//Write public key into key. Returns number of bytes written on success or
//-1 if buffer isn't big enough. Returns 0 for other errors.
int ffrsa_get_public_key(ffrsa_t* rsa, uint8_t* key, int size_bytes);

//Get the max size in bytes of the encryption buffer. This is dictated by the bit length
//of the RSA key.
int ffrsa_get_max_msg_len(ffrsa_t* rsa);

//Max value of msg_len is dictated by the bit length of the RSA key.
//Returns 0 on success and 1 on error.
int ffrsa_encrypt(ffrsa_t* rsa, uint8_t* src, int msg_len);

//Max value of msg_len is dictated by the bit length of the RSA key.
//Returns 0 on success and 1 on error.
int ffrsa_decrypt(ffrsa_t* rsa, uint8_t* src, int msg_len);

//Get the resulting encrypted or decrypted message. The result buffer is managed internally and
//the output pointer is invalidated upon every encrypt or decrypt call. msg_len is the output
//number of bytes of the result.
void ffrsa_get_result(ffrsa_t* rsa, uint8_t** result, int* msg_len);

#ifdef __cplusplus
}
#endif

#endif /* FFRSA_H_ */
