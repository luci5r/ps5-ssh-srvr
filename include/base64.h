#pragma once
#include <stddef.h>
int b64_encode_block(const unsigned char* in, int inlen, char* out);
int b64_decode_block(const char* in, int inlen, unsigned char* out);