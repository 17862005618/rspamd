/**
 * @file message.h
 * Message processing functions and structures
 */

#ifndef RSPAMD_MESSAGE_H
#define RSPAMD_MESSAGE_H

#include "config.h"

struct rspamd_task;
struct controller_session;
struct html_content;

struct mime_part {
	GMimeContentType *type;
	GByteArray *content;
	GMimeObject *parent;
	GMimeObject *mime;
	GHashTable *raw_headers;
	gchar *checksum;
	const gchar *filename;
};

#define RSPAMD_MIME_PART_FLAG_UTF (1 << 0)
#define RSPAMD_MIME_PART_FLAG_BALANCED (1 << 1)
#define RSPAMD_MIME_PART_FLAG_EMPTY (1 << 2)
#define RSPAMD_MIME_PART_FLAG_HTML (1 << 3)

#define IS_PART_EMPTY(part) ((part)->flags & RSPAMD_MIME_PART_FLAG_EMPTY)
#define IS_PART_UTF(part) ((part)->flags & RSPAMD_MIME_PART_FLAG_UTF)
#define IS_PART_RAW(part) (!((part)->flags & RSPAMD_MIME_PART_FLAG_UTF))
#define IS_PART_HTML(part) ((part)->flags & RSPAMD_MIME_PART_FLAG_HTML)

struct mime_text_part {
	guint flags;
	GUnicodeScript script;
	const gchar *lang_code;
	const gchar *language;
	const gchar *real_charset;
	GByteArray *orig;
	GByteArray *content;
	struct html_content *html;
	GList *urls_offset;	/**< list of offsets of urls						*/
	GMimeObject *parent;
	struct mime_part *mime_part;
	GArray *words;
	GArray *normalized_words;
	guint nlines;
};

struct received_header {
	gchar *from_hostname;
	gchar *from_ip;
	gchar *real_hostname;
	gchar *real_ip;
	gchar *by_hostname;
	gint is_error;
};

struct raw_header {
	gchar *name;
	gchar *value;
	gboolean tab_separated;
	gboolean empty_separator;
	gchar *separator;
	gchar *decoded;
	struct raw_header *prev, *next;
};

/**
 * Parse and pre-process mime message
 * @param task worker_task object
 * @return
 */
gboolean rspamd_message_parse (struct rspamd_task *task);

/*
 * Get a list of header's values with specified header's name using raw headers
 * @param task worker task structure
 * @param field header's name
 * @param strong if this flag is TRUE header's name is case sensitive, otherwise it is not
 * @return A list of header's values or NULL. Unlike previous function it is NOT required to free list or values. I should rework one of these functions some time.
 */
GList * rspamd_message_get_header (struct rspamd_task *task,
	const gchar *field,
	gboolean strong);

#endif
