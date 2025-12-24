// SPDX-License-Identifier: LicenseRef-Dual-LGPLv3-OR-CC-BY-ND-For-Rust
#include <mmintrin.h>
#define ALIGNMENT 8
#define ALIGNMENT_BITSHIFT 3
#define ALIGNMENT_MASK 7
#define LIBCDA_URL_DECODING_IS_OPTIMIZED

static void weird_decoding_ritual(__m64 * inplace, const size_t length) {
	size_t counter;
	size_t vec_length = ((length + ALIGNMENT_MASK) & (~ALIGNMENT_MASK)) >> ALIGNMENT_BITSHIFT;
	__m64 forty_seven = _mm_set1_pi8(47);
	__m64 seventy_nine = _mm_set1_pi8(79);
	__m64 ninety_four = _mm_set1_pi8(94);
	__m64 data;
	__m64 mask;
	for(counter = 0; counter < vec_length; ++counter) {
		data = inplace[counter];
		mask = _mm_cmpgt_pi8(data, seventy_nine);
		mask = _mm_and_si64(mask, ninety_four);
		data = _mm_add_pi8(data, forty_seven);
		data = _mm_sub_pi8(data, mask);
		inplace[counter] = data;
	}
	_mm_empty();
}
