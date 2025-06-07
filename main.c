// SPDX-License-Identifier: LicenseRef-Dual-LGPLv3-OR-CC-BY-ND-For-Rust
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>

/* Compile with:
 * gcc -ljson-c -lcurl $(xml2-config --libs) $(xml2-config --cflags) -O2 -Wall -Wextra -pedantic cda2url-unoptimized.c -o cda2url
 */

#define NOT_INCLUDED_BY_OUR_DLL
#include "libcda.h"

void print_usage(const char * program_name) {
	fprintf(stderr, "Usage: %s -u <video_url>\n", program_name);
}

int main(int argc, char *argv[]) {
	struct cda_results * result = NULL;
	size_t counter = 0;
	char *video_url = NULL;
	CURLcode http_engine;

	int opt;
	while ((opt = getopt(argc, argv, "u:h")) != -1) {
		switch (opt) {
			case 'u':
				video_url = optarg;
				break;
			case 'h':
			default:
				print_usage(argv[0]);
				return 1;
		}
	}

	if (video_url == NULL) {
		fprintf(stderr, "Error: No video URL specified.\n");
		print_usage(argv[0]);
		return 1;
	}

	http_engine = curl_global_init(CURL_GLOBAL_ALL);
	if (http_engine) {
		fprintf(stderr, "main: could not initialize HTTP engine.\n");
		return 1;
	}

	result = cda_url_to_direct_urls(video_url);
	curl_global_cleanup();

	if (result != NULL) {
		switch(result->json_type) {
			case LIBCDA_VIDEO_IS_FILE:
				for(counter = 0; counter < result->url_count; ++counter) {
					if (result->url[counter] != NULL) {
						printf("Retrieved %s at %s\n", result->quality[counter], result->url[counter]);
					}
				}
				break;

			case LIBCDA_VIDEO_IS_M3U8:
				printf("Retrieved stream at %s\n", result->url[0]);
				for(counter = 0; counter < result->quality_count; ++counter) {
					printf("Stream available in: %s\n", result->quality[counter]);
				}
				break;
		}
		free_direct_url_struct(result);
	} else {
		fprintf(stderr, "main: failed to retrieve direct URLs.\n");
		return 1;
	}

	return 0;
}
