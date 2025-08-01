#pragma once

#include <stdint.h>
#include <stdio.h>
#include <vector>

void EncodeLZSS(FILE* dst_file, std::vector<uint8_t>& src);
void EncodeYay0(FILE* dst_file, std::vector<uint8_t>& src);
