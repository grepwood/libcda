// SPDX-License-Identifier: LicenseRef-Dual-LGPLv3-OR-CC-BY-ND-For-Rust
#include <immintrin.h>
#define ALIGNMENT 32
#define ALIGNMENT_BITSHIFT 5
#define ALIGNMENT_MASK 31
#define LIBCDA_URL_DECODING_IS_OPTIMIZED

static void weird_decoding_ritual(__m256i * inplace, const size_t length) {
	size_t counter;
	size_t vec_length = ((length + ALIGNMENT_MASK) & (~ALIGNMENT_MASK)) >> ALIGNMENT_BITSHIFT;
	__m256i forty_seven = _mm256_set1_epi8(47);
	__m256i seventy_nine = _mm256_set1_epi8(79);
	__m256i ninety_four = _mm256_set1_epi8(94);
	__m256i data;
	__m256i mask;
	for(counter = 0; counter < vec_length; ++counter) {
		data = _mm256_load_si256(inplace + counter);
		mask = _mm256_cmpgt_epi8(data, seventy_nine);
		mask = _mm256_and_si256(mask, ninety_four);
		data = _mm256_add_epi8(data, forty_seven);
		data = _mm256_sub_epi8(data, mask);
		_mm256_store_si256(inplace + counter, data);
	}
}
