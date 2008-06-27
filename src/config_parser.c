/*
 * Copyright (C) 2008 Frank Aurich (1100101+automatic@gmail.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */


#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/param.h>

#include "automatic.h"
#include "config_parser.h"
#include "output.h"
#include "list.h"

#define MAX_OPT_LEN	50
#define MAX_PARAM_LEN	2000

static const char *delim = ";;";

static int set_regex_patterns(auto_handle *as, const char *pattern_str);
static int set_urls(auto_handle *session, const char *urls);
static char* shorten(const char *str);

void write_string(char *src, char **dst) {
	char *tmp;

	if(src && strlen(src) < MAXPATHLEN) {
		tmp = resolve_path(src);
		if(tmp) {
			am_free(*dst);
			*dst = strdup(tmp);
			am_free(tmp);
		}
	}
}


static int set_option(auto_handle *as, const char *opt, char *param, option_type type) {
	int i;
	int numval;
	dbg_printf(P_INFO2, "%s=%s (type: %d)", opt, param, type);

	assert(as != NULL);
	if(!strcmp(opt, "url")) {
		/*write_string(param, &as->feed_url);*/
		set_urls(as, param);
	} else if(!strcmp(opt, "logfile")) {
		write_string(param, &as->log_file);
	} else if(!strcmp(opt, "transmission-home")) {
		write_string(param, &as->transmission_path);
	} else if(!strcmp(opt, "torrent-folder")) {
		write_string(param, &as->torrent_folder);
	} else if(!strcmp(opt, "statefile")) {
		write_string(param, &as->statefile);
	} else if(!strcmp(opt, "interval")) {
		numval = 1;
		for(i = 0; i < strlen(param); i++) {
			if(isdigit(param[i]) == 0)
				numval--;
		}
		if(numval == 1) {
			if(atoi(param) > 0) {
				as->check_interval = atoi(param);
			} else {
				dbg_printf(P_ERROR, "Interval must be 1 minute or more, reverting to default (10min)\n\t%s=%s", opt, param);
				as->check_interval = 10;
			}
		} else {
			dbg_printf(P_ERROR, "Unknown parameter: %s=%s", opt, param);
		}
	} else if(!strcmp(opt, "use-transmission")) {
		if(!strncmp(param, "0", 1) || !strncmp(param, "no", 2)) {
			as->use_transmission = 0;
		} else if(!strncmp(param, "1", 1) || !strncmp(param, "yes", 3)) {
			as->use_transmission = 1;
		} else {
			dbg_printf(P_ERROR, "Unknown parameter: %s=%s", opt, param);
		}
	} else if(!strcmp(opt, "patterns")) {
		set_regex_patterns(as, param);
	} else {
		dbg_printf(P_ERROR, "Unknown option: %s", opt);
	}
	return 0;
}

static int set_regex_patterns(auto_handle *session, const char *patterns) {
	rss_item re;
	char *buf, *p;
	int len;
	char *pattern_str = shorten(patterns);
	cleanupList(&session->regex_patterns);
	buf = calloc(1, strlen(pattern_str) + 1);
	strncpy(buf, pattern_str, strlen(pattern_str) + 1);
	p = strtok(buf, delim);
	while (p) {
		len = strlen(p);
		re = newRSSItem();
		re->name = am_malloc(len + 1);
		if(re->name) {
			strcpy(re->name, p);
			addItem(re, &session->regex_patterns);
		} else {
			dbg_printf(P_ERROR, "Error allocating memory for regular expression");
		}
		p = strtok(NULL, delim);
	}
	am_free(buf);
	am_free(pattern_str);
	return 0;
}

static int set_urls(auto_handle *session, const char *urls) {
	rss_item re;
	char *buf, *p;
	int len;
	char *url_str = shorten(urls);
	cleanupList(&session->url_list);
	buf = calloc(1, strlen(url_str) + 1);
	strncpy(buf, url_str, strlen(url_str) + 1);
	p = strtok(buf, delim);
	while (p) {
		len = strlen(p);
		re = newRSSItem();
		re->url = am_malloc(len + 1);
		if(re->url) {
			strcpy(re->url, p);
			addItem(re, &session->url_list);
		} else {
			dbg_printf(P_ERROR, "Error allocating memory for regular expression");
		}
		p = strtok(NULL, delim);
	}
	am_free(buf);
	am_free(url_str);
	return 0;
}

int parse_config_file(struct auto_handle *as, const char *filename) {
	FILE *fp;
	char *line;
	char opt[MAX_OPT_LEN + 1];
	char param[MAX_PARAM_LEN + 1];
	char erbuf[100];
	char c;		/* for the "" and '' check */
	int line_num = 0;
	int line_pos;	/* line pos */
	int opt_pos;	/* opt pos */
	int param_pos;	/* param pos */
	int parse_error = 0;
	int opt_good = 0;
	int param_good = 0;
	struct stat fs;
	option_type type;

	if(stat(filename, &fs) == -1)  {
		return -1;
	}
	dbg_printf(P_INFO2, "Configuration file size: %d", fs.st_size);

	if ((line = am_malloc(fs.st_size + 1)) == NULL) {
		dbg_printf(P_ERROR, "Can't allocate memory for 'line': %s (%ldb)", strerror(errno), fs.st_size + 1);
		return -1;
	}

	if ((fp = fopen(filename, "rb")) == NULL) {
		perror("fopen");
		am_free(line);
		return -1;
	}

	if(fread(line, fs.st_size, 1, fp) != 1) {
		perror("fread");
		fclose(fp);
		am_free(line);
		return -1;
	}
	if(fp)
		fclose(fp);
	line_pos = 0;


	while(line_pos != fs.st_size) {
		/* skip whitespaces */
		while (isspace(line[line_pos])) {
			if(line[line_pos] == '\n') {
				dbg_printf(P_INFO2, "skipping newline (line %d)", line_num);
				line_num++;
			}
			++line_pos;
		}

		/* comment */
		if (line[line_pos] == '#') {
			dbg_printf(P_INFO2, "skipping comment (line %d)", line_num);
			while (line[line_pos] != '\n') {
				++line_pos;
			}
			++line_num;
			++line_pos;  /* skip the newline as well */
			continue;
		}

		/* read option */
		for (opt_pos = 0; isprint(line[line_pos]) && line[line_pos] != ' ' &&
				line[line_pos] != '#' && line[line_pos] != '='; /* NOTHING */) {
			opt[opt_pos++] = line[line_pos++];
			if (opt_pos >= MAX_OPT_LEN) {
				dbg_printf(P_ERROR, "too long option at line %d", line_num);
				parse_error = 1;
				opt_good = 0;
			}
		}
		if (opt_pos == 0 || parse_error == 1) {
			dbg_printf(P_ERROR, "parse error at line %d (pos: %d)", line_num, line_pos);
			parse_error = 1;
			break;
		} else {
			opt[opt_pos] = '\0';
			opt_good = 1;
		}
		/* skip whitespaces */
		while (isspace(line[line_pos])) {
			if(line[line_pos] == '\n') {
				line_num++;
				dbg_printf(P_INFO2, "skipping newline (line %d)", line_num);
			}
			++line_pos;
		}

		/* check for '=' */
		if (line[line_pos++] != '=') {
			snprintf(erbuf, sizeof(erbuf), "Option '%s' needs a parameter (line %d)", opt, line_num);
			parse_error = 1;
			break;
		}

		/* skip whitespaces */
		while (isspace(line[line_pos])) {
			if(line[line_pos] == '\n') {
				line_num++;
				dbg_printf(P_INFO2, "skipping newline (line %d)", line_num);
			}
			++line_pos;
		}

		/* read the parameter */

		/* case 1: single string, no linebreaks allowed */
		if (line[line_pos] == '"' || line[line_pos] == '\'') {
			c = line[line_pos];
			++line_pos;  /* skip quote */
			parse_error = 0;
			for (param_pos = 0; line[line_pos] != c; /* NOTHING */) {
				if(line_pos < fs.st_size && param_pos < MAX_PARAM_LEN && line[line_pos] != '\n') {
					param[param_pos++] = line[line_pos++];
				} else {
					snprintf(erbuf, sizeof(erbuf), "Option %s has a too long parameter (line %d)\n",opt, line_num);
					parse_error = 1;
					break;
				}
			}
			if(parse_error == 0) {
				line_pos++;	/* skip the closing " or ' */
				type = STRING_TYPE;
			} else {
				break;
			}
		/* case 2: multiple items, linebreaks allowed */
		} else if (line[line_pos] == '{') {
			dbg_printf(P_INFO2, "reading multiline param", line_num);
			++line_pos;
			parse_error = 0;
			for (param_pos = 0; line[line_pos] != '}'; /* NOTHING */) {
				if(line_pos < fs.st_size && param_pos < MAX_PARAM_LEN) {
					param[param_pos++] = line[line_pos++];
					if(line[line_pos] == '\n')
						line_num++;
				} else {
					snprintf(erbuf, sizeof(erbuf), "Option %s has a too long parameter (line %d)\n", opt, line_num);
					parse_error = 1;
					break;
				}
			}
			dbg_printf(P_INFO2, "multiline param: param_good=%d", param_good);
			if(parse_error == 0) {
				line_pos++;	/* skip the closing '}' */
				type = STRINGLIST_TYPE;
			} else {
				break;
			}
		/* Case 3: integers */
		} else {
			parse_error = 0;
			for (param_pos = 0; isprint(line[line_pos]) && !isspace(line[line_pos])
					&& line[line_pos] != '#'; /* NOTHING */) {
				param[param_pos++] = line[line_pos++];
				if (param_pos >= MAX_PARAM_LEN) {
					snprintf(erbuf, sizeof(erbuf), "Option %s has a too long parameter (line %d)\n", opt, line_num);
					parse_error = 1;
					break;
				}
			}
			if(parse_error == 0) {
				type = INT_TYPE;
			} else {
				break;
			}

		}
		param[param_pos] = '\0';
		dbg_printf(P_INFO2, "[parse_config_file] option: %s", opt);
		dbg_printf(P_INFO2, "[parse_config_file] param: %s", param);
		dbg_printf(P_INFO2, "[parse_config_file] -----------------");
		set_option(as, opt, param, type);

		/* skip whitespaces */
		while (isspace(line[line_pos])) {
			if(line[line_pos] == '\n')
				line_num++;
			++line_pos;
		}
	}

	am_free(line);

	if(parse_error == 1) {
		return -1;
	} else {
		return 0;
	}
}


static char* shorten(const char *str) {
		char tmp[MAX_PARAM_LEN + 1];
		int tmp_pos;
		char c;
		int line_pos = 0, i;
		int len = strlen(str);

		for(i = 0; i < sizeof(tmp); ++i)
			tmp[i] = '\0';

		while (isspace(str[line_pos])) {
			++line_pos;
		}
		tmp_pos = 0;
		while(line_pos < len) {
			/* case 1: quoted strings */
			if(tmp_pos != 0) {
				for(i = 0; i < strlen(delim); ++i)
					tmp[tmp_pos++] = delim[i];
			}
			if (str[line_pos] == '"' || str[line_pos] == '\'') {
				c = str[line_pos];
				++line_pos;  /* skip quote */
				for (; str[line_pos] != c; /* NOTHING */) {
						tmp[tmp_pos++] = str[line_pos++];
				}
				line_pos++;	/* skip the closing " or ' */
			} else {
				for(; isprint(str[line_pos]) && !isspace(str[line_pos]); /* NOTHING */) {
					tmp[tmp_pos++] = str[line_pos++];
				}
			}
			while (isspace(str[line_pos])) {
				++line_pos;
			}
		}
		tmp[tmp_pos] = '\0';
		assert(strlen(tmp) < MAX_PARAM_LEN);
		return strdup(tmp);
}
