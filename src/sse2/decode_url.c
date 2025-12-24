// SPDX-License-Identifier: LicenseRef-Dual-LGPLv3-OR-CC-BY-ND-For-Rust
#include <emmintrin.h>
#define ALIGNMENT 16
#define ALIGNMENT_BITSHIFT 4
#define ALIGNMENT_MASK 15
#define LIBCDA_URL_DECODING_IS_OPTIMIZED

static void weird_decoding_ritual(__m128i * inplace, const size_t length) {
	size_t counter;
	size_t vec_length = ((length + ALIGNMENT_MASK) & (~ALIGNMENT_MASK)) >> ALIGNMENT_BITSHIFT;
	__m128i forty_seven = _mm_set1_epi8(47);
	__m128i seventy_nine = _mm_set1_epi8(79);
	__m128i ninety_four = _mm_set1_epi8(94);
	__m128i data;
	__m128i mask;
	for(counter = 0; counter < vec_length; ++counter) {
		data = _mm_load_si128(inplace + counter);
		mask = _mm_cmpgt_epi8(data, seventy_nine);
		mask = _mm_and_si128(mask, ninety_four);
		data = _mm_add_epi8(data, forty_seven);
		data = _mm_sub_epi8(data, mask);
		_mm_store_si128(inplace + counter, data);
	}
}
