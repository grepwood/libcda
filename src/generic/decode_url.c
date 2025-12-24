// SPDX-License-Identifier: LicenseRef-Dual-LGPLv3-OR-CC-BY-ND-For-Rust
static void weird_decoding_ritual(char * inplace, const size_t length) {
	size_t counter = 0;
	for(; counter < length; ++counter) inplace[counter] = (33 + ((inplace[counter] + 14) % 94));
}
