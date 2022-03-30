#include "ffdigest.h"
#include "libkeccak/libkeccak.h"

void ffdigest_buf(uint8_t* digest, const void* input, int input_len)
{
	struct libkeccak_spec spec;
	libkeccak_spec_sha3(&spec, 256);
	struct libkeccak_state state;
	libkeccak_state_initialise(&state, &spec);
	libkeccak_digest(&state, input, input_len, 0, LIBKECCAK_SHA3_SUFFIX, digest);
	libkeccak_state_fast_destroy(&state);
}

void ffdigest_str(char* digest, const void* input, int input_len)
{
	uint8_t buf[FFDIGEST_BUFLEN];
	ffdigest_buf(buf, input, input_len);
	libkeccak_behex_lower(digest, buf, FFDIGEST_BUFLEN);
}
