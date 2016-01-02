#include "xombrero.h"

gboolean is_separator(char c)
{
	if (isalpha(c))
		return FALSE;
	if (isdigit(c))
		return FALSE;

	switch (c) {
	case '_':
	case '-':
	case '.':
	case '%':
		return FALSE;
	}
	return TRUE;
}

gboolean adblock_match(const char *pattern, const char *str)
{
	size_t i = 0, j = 0;

	while (pattern[i]) {
		if ( (pattern[i] == '^' && is_separator(str[j])) ||
		     (pattern[i] == str[j]) ) {
			i++;
			j++;
			continue;
		}
		if (str[j] == 0) {
			return (pattern[i] == '|' && pattern[i+1] == 0);
		}
		if (pattern[i] == '*') {
			if (adblock_match(&pattern[i+1], &str[j])) {
				return TRUE;
			} else {
				j++;
				continue;
			}
		}
		return FALSE;
	}
	return TRUE;
}

gboolean adblock_match_pattern(const char *pattern, const char *str)
{
	size_t i = 0;

	if (g_str_has_prefix(pattern, "||")) {
		pattern += 2;
		while (str[i] && isalpha(str[i]))
			i++;
		if (g_str_has_prefix(&str[i], "://")) {
			i += 3;
			while (str[i] && !is_separator(str[i])) {
				if (adblock_match(pattern, &str[i]))
					return TRUE;
				i++;
			}
		}
		return FALSE;
	} else if (g_str_has_prefix(pattern, "|")) {
		pattern++;
		return adblock_match(pattern, str);
	}

	while (str[i]) {
		if (adblock_match(pattern, &str[i])) {
			return TRUE;
		}
		i++;
	}
	
	return FALSE;
}

gboolean
parse_line(struct ad_filter *filter, const char *line)
{
	size_t i = 0;
	gboolean exception = FALSE;
	char *pattern;
	const char *opts;

	if (strlen(line) == 0) {
		return FALSE;
	}

	if (g_str_has_prefix(line, "!")) {
		return FALSE;
	}

	if (g_str_has_prefix(line, "@@")) {
		line += 2;
		exception = TRUE;
	}

	if (g_strrstr(line, "##")) {
		/* TODO: element selectors */
		return FALSE;
	}

	for (i = 0; line[i]; i++) {
		if (line[i] == '$') {
			opts = &line[i+1];
			return FALSE; /* TODO: use options */
			break;
		}
	}
	pattern = g_strndup(line, i);
	if (!pattern)
		return FALSE;

	if (exception) {
		filter->n_exc_patterns++;
		filter->exc_patterns = g_realloc(filter->exc_patterns, filter->n_exc_patterns * sizeof(char *));
		filter->exc_patterns[filter->n_exc_patterns-1] = pattern;
		DPRINTF("adblock: added exception '%s'\n", pattern);
	} else {
		filter->n_patterns++;
		filter->patterns = g_realloc(filter->patterns, filter->n_patterns * sizeof(char *));
		filter->patterns[filter->n_patterns-1] = pattern;
		DPRINTF("adblock: added pattern '%s'\n", pattern);
	}

	return TRUE;
}

int
adblock_load_filter(struct ad_filter *filter, FILE *f)
{
	char *line = NULL;
	char *line_s;
	size_t len = 0;

	filter->patterns = NULL;
	filter->n_patterns = 0;

	filter->exc_patterns = NULL;
	filter->n_exc_patterns = 0;

	while ((line = fgetln(f, &len))) {
		if (line[len-1] == '\n')
			len--;
		line_s = g_strndup(line, len);
		if (line_s) {
			parse_line(filter, line_s);
			g_free(line_s);
		}
	}

	if (line)
		free(line);

	DPRINTF("adblock: %zu rules loaded\n", filter->n_patterns + filter->n_exc_patterns);
	return 0;
}

gboolean
adblock_uri_filter(const struct ad_filter *filter, const char *uri)
{
	size_t i;

	for (i = 0; i < filter->n_exc_patterns; i++) {
		if (adblock_match_pattern(filter->exc_patterns[i], uri)) {
			DPRINTF("adblock: exception for uri '%s'; rule '%s'\n", uri, filter->exc_patterns[i]);
			return TRUE;
		}
	}

	for (i = 0; i < filter->n_patterns; i++) {
		if (adblock_match_pattern(filter->patterns[i], uri)) {
			DPRINTF("adblock: blocked uri '%s'\n", uri);
			return FALSE;
		}
	}

	return TRUE;
}
