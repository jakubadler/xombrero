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
parse_line(struct ad_filter *filter, char *line)
{
	size_t i = 0;
	gboolean exception = FALSE;
	char *pattern;
	const char *opts;
	gchar *hash;
	gchar *selector;
	gchar **domains;
	GPtrArray *selectors;

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

	if ((hash = g_strstr_len(line, strlen(line), "##"))) {
		/* element hiding rule */
		selector = hash + 2;
		*hash = 0;
		domains = g_strsplit(line, ",", 0);
		*hash = '#';

		for (i = 0; domains[i]; i++) {
			if (g_str_has_prefix(domains[i], "~"))
				continue; /* TODO: handle exceptions */

			selectors = g_hash_table_lookup(filter->hiding, domains[i]);
			if (!selectors) {
				selectors = g_ptr_array_new();
				g_hash_table_insert(filter->hiding, g_strdup(domains[i]), selectors);
			}
			g_ptr_array_add(selectors, g_strdup(selector));
			DPRINTF("adblock: added hiding rule '%s' for doman '%s'\n", selector, domains[i]);
		}
		g_strfreev(domains);
		return TRUE;
	}

	for (i = 0; line[i]; i++) {
		if (line[i] == '$') {
			opts = &line[i+1];
			return FALSE; /* TODO: use options */
			break;
		}
	}
	pattern = g_strndup(line, i);

	if (exception) {
		/* TODO: more reasonable allocation */
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

void
adblock_init_filter(struct ad_filter *filter)
{
	if (!filter)
		return;

	filter->patterns = NULL;
	filter->n_patterns = 0;

	filter->exc_patterns = NULL;
	filter->n_exc_patterns = 0;

	filter->hiding = g_hash_table_new(g_str_hash, g_str_equal);
}

void
adblock_destroy_filter(struct ad_filter *filter)
{
	GHashTableIter iter;
	gchar *key;
	GPtrArray *selectors;
	char *selector;
	size_t i;

	DPRINTF("adblock: adblock_destroy_filter()\n");

	if (!filter)
		return;

	if (filter->hiding) {
		g_hash_table_iter_init(&iter, filter->hiding);
		while (g_hash_table_iter_next(&iter, &key, &selectors)) {
			g_free(key);
			for (i = 0; i < selectors->len; i++) {
				selector = g_ptr_array_index(selectors, i);
				if (selector)
					g_free(selector);
			}
			g_ptr_array_free(selectors, TRUE);
		}
		g_hash_table_destroy(filter->hiding);
	}
}

int
adblock_load_filter(struct ad_filter *filter, FILE *f)
{
	char *line = NULL;
	char *line_s;
	size_t len = 0;

	while ((line = fgetln(f, &len))) {
		if (line[len-1] == '\n')
			len--;
		line_s = g_strndup(line, len);
		parse_line(filter, line_s);
		g_free(line_s);
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

void
adblock_hide_element(WebKitDOMDocument *doc, const char *selector)
{
	WebKitDOMNodeList *elements;
	WebKitDOMNode *element;
	WebKitDOMCSSStyleDeclaration *style;
	GError *error = NULL;
	gulong i;

	elements = webkit_dom_document_query_selector_all(doc, selector, &error);

	if (error)
		return;

	for (i = 0; i < webkit_dom_node_list_get_length(elements); i++) {
		element = webkit_dom_node_list_item(elements, i);
		style = webkit_dom_element_get_style(WEBKIT_DOM_ELEMENT(element));
		webkit_dom_css_style_declaration_set_css_text(style, "visibility: hidden;", &error);
	}
}

void
adblock_hide(struct tab *t)
{
	WebKitDOMDocument *doc;
	SoupURI *su;
	const char *uri;
	const char *domain;
	GPtrArray *selectors;
	const char *selector;
	size_t i, j;

	doc = webkit_web_view_get_dom_document(t->wv);

	if (!doc)
		return;

	uri = webkit_web_view_get_uri(t->wv);

	su = soup_uri_new(uri);
	if (!su)
		return;

	domain = get_domain(su->host);

	for (i = 0; domain[i]; i++) {
		if (i == 0 ||
		    domain[i-1] == '.') {
			DPRINTF("adblock: element hiding for domain '%s'\n", &domain[i]);
			selectors = g_hash_table_lookup(ad_filter.hiding, &domain[i]);
			if (!selectors)
				continue;
			DPRINTF("adblock: element hiding: found domain '%s'\n", &domain[i]);
			for (j = 0; j < selectors->len; j++) {
				selector = g_ptr_array_index(selectors, j);
				DPRINTF("adblock: hiding elements '%s' from uri '%s'\n", selector, uri);
				adblock_hide_element(doc, selector);
			}

		}
	}
}

