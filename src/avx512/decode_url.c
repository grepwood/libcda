// SPDX-License-Identifier: LicenseRef-Dual-LGPLv3-OR-CC-BY-ND-For-Rust
#include <immintrin.h>
#define ALIGNMENT 64
#define ALIGNMENT_BITSHIFT 6
#define ALIGNMENT_MASK 63
#define LIBCDA_URL_DECODING_IS_OPTIMIZED

/* Since AVX512 is such a clusterfuck, we need to lay down what instructions we
 * use in order to determine what subsets of AVX512 we're using.
 * AVX512F:
 * 	_mm512_set1_epi8,
 * 	_mm512_load_si512,
 * 	_mm512_and_si512,
 * 	_mm512_store_si512
 * AVX512BW:
 * 	_mm512_cmpgt_epi8_mask,
 * 	_mm512_add_epi8,
 * 	_mm512_sub_epi8
 */

static void weird_decoding_ritual(__m512i * inplace, const size_t length) {
	size_t counter;
	size_t vec_length = ((length + ALIGNMENT_MASK) & (~ALIGNMENT_MASK)) >> ALIGNMENT_BITSHIFT;
	__m512i forty_seven = _mm512_set1_epi8(47);
	__m512i seventy_nine = _mm512_set1_epi8(79);
	__m512i ninety_four = _mm512_set1_epi8(94);
	__m512i data;
	__mmask64 mask;
	for(counter = 0; counter < vec_length; ++counter) {
		data = _mm512_load_si512(inplace + counter);
		mask = _mm512_cmpgt_epi8_mask(data, seventy_nine);
		data = _mm512_add_epi8(data, forty_seven);
		data = _mm512_mask_sub_epi8(data, mask, data, ninety_four);
		_mm512_store_si512(inplace + counter, data);
	}
}
