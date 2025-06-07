// SPDX-License-Identifier: LicenseRef-Dual-LGPLv3-OR-CC-BY-ND-For-Rust
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <libxml/xpath.h>
#include <libxml/HTMLparser.h>
#include <json-c/json.h>

#include "libcda.h"

/* Compile with:
 * gcc -ljson-c -lcurl $(xml2-config --libs) $(xml2-config --cflags) -O2 -Wall -Wextra -pedantic cda2url-unoptimized.c -o cda2url
 */

struct known_size_memory_region {
	char * memory;
	size_t size;
};

void free_direct_url_struct(struct cda_results * i) {
	size_t counter = 0;
	if(i != NULL) {
		if(i->quality != NULL) {
			for(counter = 0; counter < i->quality_count; ++counter) {
				if(i->quality[counter] != NULL) free(i->quality[counter]);
			}
			free(i->quality);
		}
		if(i->url != NULL) {
			for(counter = 0; counter < i->url_count; ++counter) {
				if(i->url[counter] != NULL) free(i->url[counter]);
			}
			free(i->url);
		}
		free(i);
	}
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

static char * get_video_id(const char * full_url) {
	const size_t full_url_length = strlen(full_url);
	ssize_t counter = full_url_length;
	size_t last_slash = 0;
	size_t sublength = full_url_length;
	char * result = NULL;
	int slash_found = 0;

	do {
		slash_found = (full_url[counter] == '/');
		last_slash = slash_found * counter--;
	} while(counter >= 0 && !slash_found);

	if(!slash_found) {
		fprintf(stderr, "get_video_id: no slash found.\n");
		return NULL;
	}

	sublength -= last_slash;
	result = malloc(sublength + 1);
	if(result == NULL) {
		fprintf(stderr, "get_video_id: could not allocate memory for result.\n");
		return NULL;
	}
	memcpy(result, full_url + 1 + last_slash, sublength);
	result[sublength - 1] = '\0';
	return result;
}

static xmlChar * generate_xpath_query(const char * video_id) {
	static const char beginning[23] = "//div[@id='mediaplayer";
	static const char ending[3] = "']";
	const size_t video_id_length = strlen(video_id);
	const size_t total_length = 22 + video_id_length + 2;
	xmlChar * result = malloc(total_length + 1);
	if(result != NULL) {
		memcpy(result, beginning, 22);
		memcpy(result + 22, video_id, video_id_length);
		memcpy(result + 22 + video_id_length, ending, 2);
		result[total_length] = '\0';
	}
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

static char determine_json_type(struct json_object * small_json) {
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

static struct json_object * find_small_json(struct json_object * big_json) {
	struct json_object * result = NULL;
	int bad = !json_object_object_get_ex(big_json, "video", &result);
	if(bad) {
		fprintf(stderr,"find_small_json: JSON has no video dictionary.\n");
		return NULL;
	}
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

const char * translate_succinct_quality_to_resolutional_quality(const char * squality) {
	static const char * all_known_rqualities[8] = {"144p", "240p", "360p", "480p", "720p", "1080p", "2K",  "4K"};
	static const char * all_known_squalities[8] = {"144p", "240p", "vl",   "lq",   "sd",   "hd",    "qhd", "uhd"};
	static const size_t ak_limit = 8;
	size_t counter = 0;
	const char * result = NULL;
	int comparison = -1;
	for(; counter < ak_limit; ++counter) {
		comparison = strcmp(squality, all_known_squalities[counter]);
		if(comparison == 0) {
			result = all_known_rqualities[counter];
			break;
		}
	}
	return result;
}

/*
const char * translate_succinct_quality_to_resolutional_quality(const char * squality) {
	static const char * all_known_rqualities[8] = {"144p", "240p", "360p", "480p", "720p", "1080p", "2K",  "4K"};
	static const char * all_known_squalities[8] = {"144p", "240p", "vl",   "lq",   "sd",   "hd",    "qhd", "uhd"};
	static const size_t ak_limit = 8;
	const char * result;
	size_t counter = 0;
	ssize_t keep_going = 1;
	while(counter < ak_limit && keep_going) {
		keep_going = !strcmp(squality, all_known_squalities[counter]);
		result = (const char *)(((ssize_t)NULL & -keep_going) | ((ssize_t)all_known_rqualities[counter++] & ~(-keep_going)));
	}
	return result;
}
*/

static const char * get_current_quality(struct json_object * video) {
	struct json_object * quality = NULL;
	const char * result_reference = NULL;
	const char * result = NULL;
	int bad = 0;

	bad = !json_object_object_get_ex(video, "quality", &quality);
	if(bad) {
		fprintf(stderr,"get_current_quality: video object has no quality string.\n");
		return NULL;
	}

	result_reference = json_object_get_string(quality);
	if(result_reference == NULL) {
		fprintf(stderr,"get_current_quality: could not obtain quality string.\n");
		return NULL;
	}

	result = translate_succinct_quality_to_resolutional_quality(result_reference);
	return result;
}

static size_t determine_quality_index(char ** known_qualities, const size_t count, const char * quality) {
	size_t result = 0;
	int stop = 0;
	while(result < count && !stop) stop = strcmp(known_qualities[result++], quality);
	return result;
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
	static char * replace_this_word[] = {"%5Da452%5DA%3D", "%5Db452%5DA%3D"};
	static size_t replace_this_word_length[2] = {14, 14};
	static size_t replace_this_word_count = 2;
	static char new_word[] = "%5D452%5DA%3D";
	static size_t new_word_length = 13;
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

static size_t count_bytes_saved_by_unquoting(const char * input, const size_t length) {
	size_t counter = 0;
	size_t result = 0;
	char comparison = 0;
	while(counter < length) {
		comparison = (input[counter] == '%') << 1;
		counter += comparison;
		result += comparison;
		++counter;
	}
	return result;
}

static void unquote(char * output, char * input) {
	static int buf32 = 0;
	static short int * buf16 = (short int *)(&buf32);
	static char * buf8 = (char *)(&buf32);
	short int * in16 = NULL;
	size_t o_counter = 0;
	size_t i_counter = 0;
	char c = 0;
	size_t i_length = strlen(input);
	for(; i_counter < i_length; ++i_counter, ++o_counter) {
		c = input[i_counter];
		if(c == '%') {
			in16 = (short int *)(input + i_counter + 1);
			buf16[0] = in16[0];
			c = (char)strtol(buf8, NULL, 16);
			i_counter += 2;
		}
		output[o_counter] = c;
	}
}

static void weird_decoding_ritual(char * inplace, const size_t length) {
	size_t counter = 0;
	for(; counter < length; ++counter) inplace[counter] = (33 + ((inplace[counter] + 14) % 94));
}

static char * decode_url(const char * encoded_url, const size_t length) {
	static char protocol[9] = "https://";
	static char extension[5] = ".mp4";
	char * result = NULL;
	char * intermediate = NULL;
	char * real_result = NULL;
	size_t remove_that_many_bytes = 0;
	size_t result_length = length;
	intermediate = malloc(length + 1);
	memcpy(intermediate, encoded_url, length);
	intermediate[length] = '\0';
	remove_that_many_bytes += remove_certain_words(intermediate);
	remove_that_many_bytes += replace_certain_words(intermediate);
	memset(intermediate + length - remove_that_many_bytes, 0, remove_that_many_bytes);
	remove_that_many_bytes += count_bytes_saved_by_unquoting(intermediate, result_length);
	result_length -= remove_that_many_bytes;
	result = malloc(result_length + 1);
	result[result_length] = '\0';
	unquote(result, intermediate);
	free(intermediate);
	weird_decoding_ritual(result, result_length);
	real_result = malloc(result_length + 13);
	memcpy(real_result, protocol, 8);
	memcpy(real_result + 8, result, result_length);
	memcpy(real_result + 8 + result_length, extension, 4);
	real_result[result_length + 12] = '\0';
	free(result);
	return real_result;
}

static char * get_url(struct json_object * video) {
	struct json_object * file = NULL;
	const char * encoded_url_ref = NULL;
	char * result = NULL;
	size_t length = 0;
	int bad = 0;

	bad = !json_object_object_get_ex(video, "file", &file);
	if(bad) {
		fprintf(stderr,"get_url: video has no file object.\n");
		return result;
	}

	encoded_url_ref = json_object_get_string(file);
	if(encoded_url_ref == NULL) {
		fprintf(stderr,"get_url: could not obtain file string.\n");
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

struct cda_results * cda_url_to_direct_urls(const char * cda_page_url) {
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
		fprintf(stderr, "cda_url_to_direct_urls: JSON response does not contain any hints.\n");
		free(video_id);
		json_object_put(big_json);
		return NULL;
	}

	result = malloc(sizeof(struct cda_results));
	if(result == NULL) {
		fprintf(stderr, "cda_url_to_direct_urls: could not allocate memory for result structure.\n");
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
				fprintf(stderr, "cda_url_to_direct_urls: could not allocate memory for URLs inside the result structure.\n");
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
			result->url[default_index] = get_url(small_json);
			json_object_put(big_json);

			for(; counter < result->quality_count; ++counter) {
				if(counter != default_index) {
					extra_url = get_extra_url(cda_page_url, result->quality[counter]);
					if(extra_url == NULL) {
						fprintf(stderr, "cda_url_to_direct_urls: failed to get URL for %s.\n", result->quality[counter]);
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
						fprintf(stderr, "cda_url_to_direct_urls: failed to get JSON for %s.\n", result->quality[counter]);
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
					result->url[counter] = get_url(small_json);
					if(result->url[counter] == NULL) {
						fprintf(stderr, "cda_url_to_direct_urls: failed to decode URL for %s.\n", result->quality[counter]);
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
				fprintf(stderr, "cda_url_to_direct_urls: failed to allocate memory for m3u8 link container.\n");
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
