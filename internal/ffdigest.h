/*
 * ffdigest.h
 *
 *  Created on: May 27, 2021
 *      Author: jesse
 */

#ifndef FFDIGEST_H_
#define FFDIGEST_H_

#include <stdint.h>

#define FFDIGEST_BUFLEN 32
#define FFDIGEST_STRLEN 64

#ifdef __cplusplus
extern "C" {
#endif

void ffdigest_buf(uint8_t* digest, const void* input, int input_len);
void ffdigest_str(char* digest, const void* input, int input_len);

#ifdef __cplusplus
}
#endif

#endif /* FFDIGEST_H_ */
