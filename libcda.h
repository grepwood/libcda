// SPDX-License-Identifier: LicenseRef-Dual-LGPLv3-OR-CC-BY-ND-For-Rust
struct cda_results {
	char ** quality;
	char ** url;
	size_t quality_count;
	size_t url_count;
	char json_type;
};

#define LIBCDA_VIDEO_NOT_SUPPORTED	0
#define LIBCDA_VIDEO_IS_FILE		1
#define LIBCDA_VIDEO_IS_M3U8		2

#ifdef NOT_INCLUDED_BY_OUR_DLL
	void free_direct_url_struct(struct cda_results * i);
	struct cda_results * cda_url_to_direct_urls(const char * cda_page_url);
	void cda_results2json(struct cda_results * i);
#endif
