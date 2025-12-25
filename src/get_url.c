// SPDX-License-Identifier: LicenseRef-Dual-LGPLv3-OR-CC-BY-ND-For-Rust
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <libxml/xpath.h>
#include <libxml/HTMLparser.h>
#include <json-c/json.h>

#include "get_url_struct.h"
#include "get_url_signals.h"

/* Compile with:
 * gcc -ljson-c -lcurl $(xml2-config --libs) $(xml2-config --cflags) -O2 -Wall -Wextra -pedantic cda2url-unoptimized.c -o cda2url
 */

struct known_size_memory_region {
	char * memory;
	size_t size;
};

void libcda_free_get_url(struct cda_results * i) {
	size_t counter = 0;
	if(i != NULL) {
		if(i->quality != NULL) {
			for(counter = 0; counter < i->quality_count; ++counter) {
				if(i->quality[counter] != NULL) {
					free(i->quality[counter]);
					i->quality[counter] = NULL;
				}
			}
		}
		free(i->quality);
		i->quality = NULL;
		if(i->url != NULL) {
			for(counter = 0; counter < i->url_count; ++counter) {
				if(i->url[counter] != NULL) {
					free(i->url[counter]);
					i->url[counter] = NULL;
				}
			}
		}
		free(i->url);
		i->url = NULL;
	}
	free(i);
}

static size_t write_memory_callback(void *contents, size_t size, size_t nmemb, void *userdata) {
    size_t real_size = size * nmemb;
    struct known_size_memory_region *mem = (struct known_size_memory_region *)userdata;

    if (!mem) {
        fprintf(stderr, "write_memory_callback: mem is NULL!\n");
        abort();
    }

    char *ptr = realloc(mem->memory, mem->size + real_size + 1);
    if (!ptr) {
        fprintf(stderr, "write_memory_callback: Not enough memory!\n");
        abort();
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, real_size);
    mem->size += real_size;
    mem->memory[mem->size] = '\0';

    return real_size;
}

static void free_memory_chunk(struct known_size_memory_region * i) {
	free(i->memory);
	i->memory = NULL;
	free(i);
}

static char * get_curl_user_agent(void) {
	static char user_agent_beginning[6] = "curl/";
	static size_t user_agent_beginning_length = 5;
	char * user_agent = NULL;
	size_t version_length = 0;
	size_t user_agent_length = user_agent_beginning_length;
	curl_version_info_data * info = curl_version_info(CURLVERSION_NOW);
	version_length = strlen(info->version);
	user_agent_length += version_length;
	user_agent = malloc(user_agent_length + 1);
	memcpy(user_agent, user_agent_beginning, user_agent_beginning_length);
	memcpy(user_agent + user_agent_beginning_length, info->version, version_length);
	user_agent[user_agent_length] = '\0';
	return user_agent;
}

static struct known_size_memory_region * http_get_with_curl(const char * cda_url) {
	struct known_size_memory_region * chunk = NULL;
	char * user_agent = NULL;
	CURL * curl = NULL;
	CURLcode response = 0;
	chunk = malloc(sizeof(struct known_size_memory_region));
	if(chunk == NULL) return NULL;
	chunk->memory = malloc(1);
	if(chunk->memory == NULL) {
		free(chunk);
		return NULL;
	}
	chunk->size = 0;
	curl = curl_easy_init();
	if(curl == NULL) {
		free(chunk->memory);
		free(chunk);
		return NULL;
	}
	user_agent = get_curl_user_agent();
	curl_easy_setopt(curl, CURLOPT_URL, cda_url);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, chunk);
	response = curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	free(user_agent);
	if(response != CURLE_OK) {
		fprintf(stderr, "curl_easy_perform() of URL %s failed: %s\n", cda_url, curl_easy_strerror(response));
	}
	return chunk;
}

static char ensure_last_2bytes_are_hex(const char * bytes) {
	char result = (
		(
			'0' <= bytes[0] && bytes[0] <= '9'
		)|(
			'a' <= bytes[0] && bytes[0] <= 'f'
		)
	) && (
		(
			'0' <= bytes[1] && bytes[1] <= '9'
		)|(
			'a' <= bytes[1] && bytes[1] <= 'f'
		)
	);
	return result;
}

static char * get_video_id(const char * full_url) {
	const size_t full_url_length = strlen(full_url);
	ssize_t counter = full_url_length;
	size_t last_slash = 0;
	size_t sublength = full_url_length;
	size_t where_hex_lives;
	char hex_found;
	char * result = NULL;
	int slash_found = 0;

	do {
		slash_found = (full_url[counter] == '/');
		last_slash = slash_found * counter--;
	} while(counter >= 0 && !slash_found);

	if(!slash_found) {
		fprintf(stderr, "get_video_id: no slash found.\n");
	} else {
		sublength -= last_slash;
		result = malloc(sublength);
		if(result == NULL) {
			fprintf(stderr, "get_video_id: could not allocate memory for result.\n");
		} else {
			// Null termination is not needed because we copy the null terminator with memcpy
			memcpy(result, full_url + 1 + last_slash, sublength);

			where_hex_lives = sublength - 3;
			hex_found = ensure_last_2bytes_are_hex(result + where_hex_lives);
			if(!hex_found) {
				fputs("get_video_id: last 2 bytes in video ID are not an 8 bit hex number\n", stderr);
				free(result);
				result = NULL;
			}
		}
	}

	return result;
}

__attribute__((no_stack_protector, optimize("Os"))) static xmlChar * generate_xpath_query(const char * video_id) {
	static const char beginning[23] = "//div[@id='mediaplayer";
	static const size_t beginning_length = 22;
	static const char ending[3] = "']";
	static const size_t ending_length = 2;

	xmlChar * terminate_this;
	xmlChar terminator_prison[1];

	const size_t video_id_length = strlen(video_id);
	const size_t total_length = beginning_length + video_id_length + ending_length;

	xmlChar * result = malloc(total_length + 1);
	const size_t malloc_status = -(result != NULL);
	const size_t proxy_bl = (beginning_length & malloc_status)|(0 & ~malloc_status);
	const size_t proxy_el = (ending_length & malloc_status)|(0 & ~malloc_status);
	const size_t proxy_vl = (video_id_length & malloc_status)|(0 & ~malloc_status);

	memcpy(result, beginning, proxy_bl);
	memcpy(result + proxy_bl, video_id, proxy_vl);
	memcpy(result + proxy_bl + proxy_vl, ending, proxy_el);
	terminate_this = (xmlChar *)(((size_t)(result + total_length) & malloc_status)|((size_t)terminator_prison & ~malloc_status));
	*terminate_this = '\0';
	return result;
}

static char * extract_raw_json_from_html(const char * video_id, const char * html_page, const size_t html_page_length) {
	static const xmlChar attr_name[12] = "player_data";
	htmlDocPtr document = NULL;
	xmlXPathContextPtr context = NULL;
	xmlXPathObjectPtr xpath_result = NULL;
	xmlNode * node = NULL;
	xmlChar * result_object = NULL;
	xmlChar * xpath_query = NULL;
	char * result = NULL;
	size_t length = 0;

	xpath_query = generate_xpath_query(video_id);
	if(xpath_query == NULL) {
		fprintf(stderr, "extract_raw_json_from_html: failed to generate xpath_query.\n");
		return NULL;
	}

	document = htmlReadMemory(html_page, html_page_length, NULL, NULL, HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);
	if(document == NULL) {
		fprintf(stderr, "extract_raw_json_from_html: failed to parse HTML from memory.\n");
		free(xpath_query);
		return NULL;
	}

	context = xmlXPathNewContext(document);
	if(context == NULL) {
		fprintf(stderr, "extract_raw_json_from_html: failed to create new XPath context.\n");
		xmlFreeDoc(document);
		free(xpath_query);
		return NULL;
	}

	xpath_result = xmlXPathEval(xpath_query, context);
	if(xpath_result == NULL) {
		fprintf(stderr,"extract_raw_json_from_html: unable to evaluate XPath expression %s\n", xpath_query);
		xmlXPathFreeContext(context);
		xmlFreeDoc(document);
		xmlCleanupParser();
		free(xpath_query);
		return NULL;
	}

	if(!(xpath_result->nodesetval->nodeNr)) {
		fputs("extract_raw_json_from_html: xmlXPathEval returned 0 results\n", stderr);
		xmlXPathFreeObject(xpath_result);
		xmlXPathFreeContext(context);
		xmlFreeDoc(document);
		xmlCleanupParser();
		free(xpath_query);
		return NULL;
	}

	node = xpath_result->nodesetval->nodeTab[0];
	result_object = xmlGetProp(node, attr_name);
	if(result_object == NULL) {
		fprintf(stderr,"extract_raw_json_from_html: could not find %s\n", attr_name);
		xmlXPathFreeObject(xpath_result);
		xmlXPathFreeContext(context);
		xmlFreeDoc(document);
		xmlCleanupParser();
		free(xpath_query);
		return NULL;
	}

	length = xmlStrlen(result_object);
	result = malloc(length + 1);
	if(result == NULL) {
		fprintf(stderr,"extract_raw_json_from_html: could not allocate memory for result\n");
		xmlFree(result_object);
		xmlXPathFreeObject(xpath_result);
		xmlXPathFreeContext(context);
		xmlFreeDoc(document);
		xmlCleanupParser();
		free(xpath_query);
		return NULL;
	}
	memcpy(result, result_object, length);
	result[length] = '\0';

	xmlFree(result_object);
	xmlXPathFreeObject(xpath_result);
	xmlXPathFreeContext(context);
	xmlFreeDoc(document);
	xmlCleanupParser();
	free(xpath_query);
	return result;
}

static struct json_object * get_big_json(const char * page_url, const char * video_id) {
	char * raw_json = NULL;
	struct json_object * result = NULL;
	struct known_size_memory_region * html_page = NULL;

	html_page = http_get_with_curl(page_url);
	if(html_page == NULL) {
		fprintf(stderr,"get_big_json: download failed.\n");
		return NULL;
	}

	raw_json = (char *)extract_raw_json_from_html(video_id, html_page->memory, html_page->size);
	free_memory_chunk(html_page);
	html_page = NULL;

	if(raw_json == NULL) {
		fprintf(stderr,"get_big_json: could not find JSON.\n");
		return NULL;
	}

	result = json_tokener_parse(raw_json);
	free(raw_json);
	if(result == NULL) {
		fprintf(stderr,"get_big_json: parsing JSON failed.\n");
		return NULL;
	}

	return result;
}

__attribute__((no_stack_protector)) static char determine_json_type(struct json_object * small_json) {
	struct json_object * jsonic_crosshair;
	char result;
	char is_m3u8;
	char is_file;
	json_object_object_get_ex(small_json, "file", &jsonic_crosshair);
	is_file = !!json_object_get_string_len(jsonic_crosshair);

	json_object_object_get_ex(small_json, "manifest_apple", &jsonic_crosshair);
	is_m3u8 = !!json_object_get_string_len(jsonic_crosshair);

	result = (is_file ^ is_m3u8) * (is_file | (is_m3u8 << 1));
	return result;
}

__attribute__((no_stack_protector)) static struct json_object * find_small_json(struct json_object * big_json) {
	static const char bad_message[48] = "find_small_json: JSON has no video dictionary.\n";
	static const char good_message[1] = {0};
	struct json_object * result = NULL;
	size_t good = -json_object_object_get_ex(big_json, "video", &result);
	char * actual_message = (char *)(((size_t)good_message & good)|((size_t)bad_message & ~good));
	(void)fputs(actual_message, stderr);
	return result;
}

static char ** count_qualities(struct json_object * video, size_t * count, const char json_type) {
	static const char ignore_this_quality[5] = "auto";
	struct json_object * qualities = NULL;
	char ** result = NULL;
	size_t counter = 0;
	size_t emergency_counter = 0;
	size_t key_length = 0;
	int bad = 0;
	int comparison = 0;
	bad = !json_object_object_get_ex(video, "qualities", &qualities);
	if(bad) {
		fprintf(stderr,"count_qualities: video object does not contain qualities dictionary.\n");
		return result;
	}
	*count = json_object_object_length(qualities) - (json_type == LIBCDA_VIDEO_IS_M3U8);
	if(!(*count)) {
		fprintf(stderr,"count_qualities: qualities dictionary is empty.\n");
		return result;
	}
	result = malloc(*count * sizeof(char *));
	if(result == NULL) {
		fprintf(stderr,"count_qualities: main allocation error.\n");
		return result;
	}
	json_object_object_foreach(qualities, key, val) {
		comparison = strcmp(key, ignore_this_quality);
		if(comparison != 0) {
			key_length = strlen(key);
			result[counter] = malloc(key_length + 1);
			if(result[counter] == NULL) {
				fprintf(stderr,"count_qualities: allocation error %zu\n", counter);
				while(emergency_counter <= counter) free(result[emergency_counter++]);
				free(result);
				return NULL;
			}
			memcpy(result[counter], key, key_length);

			result[counter++][key_length] = '\0';
		}
	}
	return result;
}

static const char * translate_succinct_quality_to_resolutional_quality(const char * squality) {
	static const char * all_known_rqualities[8] = {"144p", "240p", "360p", "480p", "720p", "1080p", "2K",  "4K"};
	static const char * all_known_squalities[8] = {"144p", "240p", "vl",   "lq",   "sd",   "hd",    "qhd", "uhd"};
	static const size_t ak_limit = 8;
	char * result = NULL;
	size_t counter = 0;
	size_t keep_going = 1;
	while(keep_going) {
		keep_going = !!strcmp(squality, all_known_squalities[counter]);
		result = (char *)(((size_t)all_known_rqualities[counter] & -keep_going)|((size_t)result & ~(-keep_going)));
		keep_going &= (counter++ < ak_limit);
	}
	return result;
}

__attribute__((no_stack_protector)) static const char * get_current_quality(struct json_object * video) {
	static const char bad_message0[82] = "get_current_quality: video object has no quality string.\n";
	static const char bad_message1[55] = "get_current_quality: could not obtain quality string.\n";
	static const char null_message[1] = {0};
	char * print_this;

	struct json_object * quality = NULL;
	const char * result_reference = NULL;
	const char * result = NULL;

	size_t test = -json_object_object_get_ex(video, "quality", &quality);
	print_this = (char *)(((size_t)null_message & test)|((size_t)bad_message0 & ~test));
	fputs(print_this, stderr);

	result_reference = json_object_get_string(quality);
	test = -(result_reference != NULL);
	print_this = (char *)(((size_t)null_message & test)|((size_t)bad_message1 & ~test));
	fputs(print_this, stderr);

	result = translate_succinct_quality_to_resolutional_quality(result_reference);
	return result;
}

static size_t determine_quality_index(char ** known_qualities, const size_t limit, const char * quality) {
	size_t counter = 0;
	char keep_going = 1;
	while(counter < limit && keep_going) keep_going = !!strcmp(known_qualities[counter++], quality);
	return counter;
}

static size_t remove_certain_words(char * input_string) {
// 8th word: .cda.mp4
	static char * removable_word[] = {"_XDDD", "_CDA", "_ADC", "_CXD", "_QWE", "_Q5", "_IKSDE", "%5D452%5D%3EAc"};
	static size_t removable_word_count = 8;
	static size_t removable_word_length[8] = {5, 4, 4, 4, 4, 3, 6, 14};
	size_t saved_bytes = 0;
	size_t current_word = 0;
	char * pos = NULL;
	for(; current_word < removable_word_count; ++current_word) {
		while ((pos = strstr(input_string, removable_word[current_word])) != NULL) {
			memmove(pos, pos + removable_word_length[current_word], strlen(pos + removable_word_length[current_word]) + 1);
			saved_bytes += removable_word_length[current_word];
		}
	}
	return saved_bytes;
}

static size_t replace_certain_words(char * input_string) {
// 1st word: .2cda.pl
// 2nd word: .3cda.pl
// new word: .cda.pl
	static const char * replace_this_word[] = {"%5Da452%5DA%3D", "%5Db452%5DA%3D"};
	static const size_t replace_this_word_length[2] = {14, 14};
	static const size_t replace_this_word_count = 2;
	static const char new_word[] = "%5D452%5DA%3D";
	static const size_t new_word_length = 13;
	size_t counter = 0;
	size_t saved_bytes = 0;
	char * pos = NULL;
	char * cur = NULL;
	for(; counter < replace_this_word_count; ++counter) {
		pos = input_string;
		while ((pos = strstr(pos, replace_this_word[counter])) != NULL) {
			if (new_word_length != replace_this_word_length[counter]) {
				cur = pos + replace_this_word_length[counter];
				memmove(pos + new_word_length, cur, strlen(cur) + 1);
				saved_bytes += (replace_this_word_length[counter] - new_word_length);
			}
			memcpy(pos, new_word, new_word_length);
			pos += new_word_length;
		}
	}
	return saved_bytes;
}



static size_t unquote(char * string, const size_t length) {
	short int prison = 0x3030;
	char * buf;
	size_t read_counter = 0;
	size_t write_counter = 0;
	size_t percent_detected;
	size_t result = 0;
	size_t increase;
	char c;
	while(read_counter < length) {
		c = string[read_counter++];
		percent_detected = -(c == '%');
		increase = percent_detected & 2;
		buf = (char *)(
			(
				(size_t)(string + read_counter) & percent_detected
			)|(
				(size_t)(&prison) & ~percent_detected
			)
		);
		c = (((((buf[0] & 0xF) + (buf[0] >> 6) * 9) << 4) | ((buf[1] & 0xF) + (buf[1] >> 6) * 9)) & percent_detected) | (c & ~percent_detected);
		read_counter += increase;
		result += increase;
		string[write_counter++] = c;
	}
	return result;
}

#if defined(__i386__)
#	if defined(__AVX512F__) && defined(__AVX512BW__)
#		include "avx512/decode_url.c"
#	elif defined(__AVX2__)
#		include "avx2/decode_url.c"
#	elif defined(__SSE2__)
#		include "sse2/decode_url.c"
#	elif defined(__MMX__)
#		include "mmx/decode_url.c"
#	else
#		include "generic/decode_url.c"
#	endif
#elif defined(__amd64__)
#	if defined(__AVX512F__) && defined(__AVX512BW__)
#		include "avx512/decode_url.c"
#	elif defined(__AVX2__)
#		include "avx2/decode_url.c"
#	else
#		include "sse2/decode_url.c"
#	endif
#else
#	include "generic/decode_url.c"
#endif

static char * decode_url(const char * encoded_url, const size_t length) {
	static char protocol[9] = "https://";
	static char extension[5] = ".mp4";
	char * result = NULL;
	void * intermediate = NULL;
	size_t remove_that_many_bytes = 0;
	size_t actionable_length = length;

// For debugging
//	printf("Decoding URL[%zu]: %s\n", length, encoded_url);
#if defined(LIBCDA_URL_DECODING_IS_OPTIMIZED)
	size_t size_for_simd = (length + ALIGNMENT_MASK) & (~ALIGNMENT_MASK);

	intermediate = aligned_alloc(ALIGNMENT, size_for_simd + 1);
#else
	intermediate = malloc(length + 1);
#endif

	memcpy(intermediate, encoded_url, length);
	((char *)intermediate)[length] = '\0';
	
	remove_that_many_bytes += remove_certain_words(intermediate);
	remove_that_many_bytes += replace_certain_words(intermediate);
	actionable_length -= remove_that_many_bytes;

	remove_that_many_bytes += unquote(intermediate, actionable_length);
	actionable_length = length - remove_that_many_bytes;

	weird_decoding_ritual(intermediate, actionable_length);

	result = malloc(actionable_length + 13);
	memcpy(result, protocol, 8);
	memcpy(result + 8, intermediate, actionable_length);
	memcpy(result + 8 + actionable_length, extension, 4);
	result[actionable_length + 12] = '\0';
	free(intermediate);
	return result;
}

static char * get_url_from_json(struct json_object * video) {
	struct json_object * file = NULL;
	const char * encoded_url_ref = NULL;
	char * result = NULL;
	size_t length = 0;
	int bad = 0;

	bad = !json_object_object_get_ex(video, "file", &file);
	if(bad) {
		fprintf(stderr,"get_url_from_json: video has no file object.\n");
		return result;
	}

	encoded_url_ref = json_object_get_string(file);
	if(encoded_url_ref == NULL) {
		fprintf(stderr,"get_url_from_json: could not obtain file string.\n");
		return result;
	}
	length = strlen(encoded_url_ref);

	result = decode_url(encoded_url_ref, length);
	return result;
}

static char * get_extra_url(const char * template_url, const char * quality) {
	static const char middle[9] = "?wersja=";
	static size_t middle_length = 8;
	size_t template_url_length = strlen(template_url);
	size_t quality_length = strlen(quality);
	size_t total_length = template_url_length + middle_length + quality_length;
	char * result = malloc(total_length + 1);
	if(result == NULL) {
		fprintf(stderr,"get_qualified_url: allocation error.\n");
	} else {
		memcpy(result, template_url, template_url_length);
		memcpy(result + template_url_length, middle, middle_length);
		memcpy(result + template_url_length + middle_length, quality, quality_length);
		result[total_length] = '\0';
	}
	return result;
}

static char * get_m3u8_link(struct json_object * small_json) {
	size_t length = 0;
	struct json_object * jsonic_crosshair = NULL;
	char * result = NULL;
	const char * tmp = NULL;
	int object_exists = json_object_object_get_ex(small_json, "manifest_apple", &jsonic_crosshair);
	if(object_exists) {
		tmp = json_object_get_string(jsonic_crosshair);
		length = strlen(tmp);
		result = malloc(length + 1);
		if(result == NULL) {
			fprintf(stderr, "get_m3u8_link: could not allocate memory for result.\n");
			return NULL;
		}
		memcpy(result, tmp, length);
		result[length] = 0;
	}
	return result;
}

void libcda_get_url2json(struct cda_results * i) {
	static const char * known_json_types[3] = {"none", "file", "m3u8"};
	static const char part_0[15] = "{\"json_type\":\"";
	static const size_t part_0l = 14;
	static const char part_1[16] = "\",\"qualities\":[";
	static const size_t part_1l = 15;
	static const char part_2[11] = "],\"urls\":[";
	static const size_t part_2l = 10;
	static const char part_3[3] = {']','}','\0'};
	static const size_t part_3l = 3;

/* Write-only memory must be as big as biggest part_x */
	char wom[16];

	size_t * quality_lengths;
	size_t * url_lengths;
	size_t condition;
	size_t the_most_important_allocation_succeeded;

	const char * actual_json_type = known_json_types[(size_t)(i->json_type)];
	const size_t actual_json_type_length = strlen(actual_json_type);

	char * result;
	char * dump_here;
	size_t q_limit;
	size_t u_limit;
	size_t counter;

/* Minimum length to store */
	size_t length = part_0l + actual_json_type_length + part_1l + part_2l + part_3l;

	quality_lengths = malloc(sizeof(size_t) * i->quality_count);
	condition = -(quality_lengths != NULL);
	q_limit = (i->quality_count & condition)|(0 & ~condition);
	for(counter = 0; counter < q_limit; ++counter) {
		quality_lengths[counter] = strlen(i->quality[counter]);
		length += quality_lengths[counter];
	}
	length += (q_limit << 1);
	condition = -(q_limit > 1);
	length += ((q_limit - 1) & condition)|(0 & ~condition);

	url_lengths = malloc(sizeof(size_t) * i->url_count);
	condition = -(url_lengths != NULL);
	u_limit = (i->url_count & condition)|(0 & ~condition);
	for(counter = 0; counter < u_limit; ++counter) {
		url_lengths[counter] = strlen(i->url[counter]);
		length += url_lengths[counter];
	}
	length += (u_limit << 1);
	condition = -(u_limit > 1);
	length += ((u_limit - 1) & condition)|(0 & ~condition);

/* Copy: {"json_type":"
*/	result = malloc(length);
	the_most_important_allocation_succeeded = -(result != NULL);
	dump_here = (char *)(((size_t)result & the_most_important_allocation_succeeded)|((size_t)wom & ~the_most_important_allocation_succeeded));
	memcpy(dump_here, part_0, part_0l);
	dump_here += (part_0l & the_most_important_allocation_succeeded)|(0 & ~the_most_important_allocation_succeeded);

/* Copy: json_type
*/	memcpy(dump_here, actual_json_type, actual_json_type_length);
	dump_here += (actual_json_type_length & the_most_important_allocation_succeeded)|(0 & ~the_most_important_allocation_succeeded);

/* Copy: ","qualities":[
*/	memcpy(dump_here, part_1, part_1l);
	dump_here += (part_1l & the_most_important_allocation_succeeded)|(0 & ~the_most_important_allocation_succeeded);

/* Copy: qualities from json
*/	for(counter = 0; counter < q_limit; ++counter) {
		(dump_here++)[0] = '"';
		memcpy(dump_here, i->quality[counter], quality_lengths[counter]);
		dump_here += quality_lengths[counter];
		(dump_here++)[0] = '"';
		condition = -(counter + 1 < q_limit);
		dump_here[0] = (',' & condition)|(dump_here[0] & ~condition);
		dump_here += !!condition;
	}
	free(quality_lengths);

/* Copy: part_2
*/	memcpy(dump_here, part_2, part_2l);
	dump_here += (part_2l & the_most_important_allocation_succeeded)|(0 & ~the_most_important_allocation_succeeded);

/* Copy: urls from json
*/	for(counter = 0; counter < u_limit; ++counter) {
		(dump_here++)[0] = '"';
		memcpy(dump_here, i->url[counter], url_lengths[counter]);
		dump_here += url_lengths[counter];
		(dump_here++)[0] = '"';
		condition = -(counter + 1 < u_limit);
		dump_here[0] = (',' & condition)|(dump_here[0] & ~condition);
		dump_here += !!condition;
	}
	free(url_lengths);

/* Copy: ]}\0
*/	memcpy(dump_here, part_3, part_3l);

/* This memset will ensure, that even if result was wom all this time,
 * no garbage will be printed. */
	memset(wom, 0, 16);
	puts(result);
	free(result);
}

struct cda_results * libcda_get_url(const char * cda_page_url) {
	const char * default_quality = NULL;
	char * video_id = NULL;
	char * extra_url = NULL;
	struct cda_results * result = NULL;
	struct json_object * big_json = NULL;
	struct json_object * small_json = NULL;
	size_t counter = 0;
	size_t escape_counter = 0;
	size_t default_index = 0;
	char json_type = 0;

	video_id = get_video_id(cda_page_url);
	if(video_id == NULL) {
		return NULL;
	}

	big_json = get_big_json(cda_page_url, video_id);
	if(big_json == NULL) {
		free(video_id);
		return NULL;
	}

	small_json = find_small_json(big_json);
	if(small_json == NULL) {
		free(video_id);
		json_object_put(big_json);
		return NULL;
	}

	json_type = determine_json_type(small_json);
	if(json_type == LIBCDA_VIDEO_NOT_SUPPORTED) {
		fprintf(stderr, "libcda_get_url: JSON response does not contain any hints.\n");
		free(video_id);
		json_object_put(big_json);
		return NULL;
	}

	result = malloc(sizeof(struct cda_results));
	if(result == NULL) {
		fprintf(stderr, "libcda_get_url: could not allocate memory for result structure.\n");
		free(video_id);
		json_object_put(big_json);
		return NULL;
	}
	result->quality = NULL;
	result->quality_count = 0;
	result->url_count = 0;
	result->json_type = json_type;

	result->quality = count_qualities(small_json, &(result->quality_count), json_type);
	if(result->quality == NULL) {
		free(video_id);
		json_object_put(big_json);
		free(result);
		return NULL;
	}

	switch(json_type) {
		case LIBCDA_VIDEO_IS_FILE:
			result->url_count = result->quality_count;
			result->url = malloc(result->url_count * sizeof(char *));
			if(result->url == NULL) {
				fprintf(stderr, "libcda_get_url: could not allocate memory for URLs inside the result structure.\n");
				free(result->quality);
				free(video_id);
				json_object_put(big_json);
				free(result);
				return NULL;
			}

			default_quality = get_current_quality(small_json);
			if(default_quality == NULL) {
				free(result->quality);
				free(video_id);
				json_object_put(big_json);
				free(result);
				return NULL;
			}

			default_index = determine_quality_index(result->quality, result->quality_count, default_quality);
			result->url[default_index] = get_url_from_json(small_json);
			json_object_put(big_json);

			for(; counter < result->quality_count; ++counter) {
				if(counter != default_index) {
					extra_url = get_extra_url(cda_page_url, result->quality[counter]);
					if(extra_url == NULL) {
						fprintf(stderr, "libcda_get_url: failed to get URL for %s.\n", result->quality[counter]);
						while(escape_counter < counter) {
							free(result->url[escape_counter++]);
						}
						for(escape_counter = 0; escape_counter < result->quality_count; ++escape_counter) {
							free(result->quality[escape_counter]);
						}
						free(result);
						free(video_id);
						return NULL;
					}

					big_json = get_big_json(extra_url, video_id);
					free(extra_url);
					if(big_json == NULL) {
						fprintf(stderr, "libcda_get_url: failed to get JSON for %s.\n", result->quality[counter]);
						while(escape_counter < counter) {
							free(result->url[escape_counter++]);
						}
						for(escape_counter = 0; escape_counter < result->quality_count; ++escape_counter) {
							free(result->quality[escape_counter]);
						}
						free(result);
						free(video_id);
						return NULL;
					}
					small_json = find_small_json(big_json);
					result->url[counter] = get_url_from_json(small_json);
					if(result->url[counter] == NULL) {
						fprintf(stderr, "libcda_get_url: failed to decode URL for %s.\n", result->quality[counter]);
						while(escape_counter < counter) {
							free(result->url[escape_counter++]);
						}
						for(escape_counter = 0; escape_counter < result->quality_count; ++escape_counter) {
							free(result->quality[escape_counter]);
						}
						free(result);
						free(video_id);
						json_object_put(big_json);
						return NULL;
					}
					json_object_put(big_json);
				}
			}
			break;

		case LIBCDA_VIDEO_IS_M3U8:
			result->url_count = 1;
			result->url = malloc(sizeof(char *));
			if(result->url == NULL) {
				fprintf(stderr, "libcda_get_url: failed to allocate memory for m3u8 link container.\n");
				for(escape_counter = 0; escape_counter < result->quality_count; ++escape_counter) {
					free(result->quality[escape_counter]);
				}
				free(result->quality);
				free(result);
				free(video_id);
				json_object_put(big_json);
			}
			result->url[0] = get_m3u8_link(small_json);
			if(result->url[0] == NULL) {
				free(result->url);
				for(escape_counter = 0; escape_counter < result->quality_count; ++escape_counter) {
					free(result->quality[escape_counter]);
				}
				free(result->quality);
				free(result);
				free(video_id);
				json_object_put(big_json);
			}
			json_object_put(big_json);
			break;
	}

	free(video_id);
	return result;
}
