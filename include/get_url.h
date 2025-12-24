// SPDX-License-Identifier: LicenseRef-Dual-LGPLv3-OR-CC-BY-ND-For-Rust
#include "get_url_struct.h"
#include "get_url_signals.h"
void libcda_free_get_url(struct cda_results * i);
struct cda_results * libcda_get_url(const char * cda_page_url);
void libcda_get_url2json(struct cda_results * i);
