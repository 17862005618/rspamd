/* Copyright (c) 2014-2015, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "task.h"
#include "main.h"
#include "filter.h"
#include "protocol.h"
#include "message.h"
#include "lua/lua_common.h"
#include "composites.h"
#include "stat_api.h"

static GQuark
rspamd_task_quark (void)
{
	return g_quark_from_static_string ("task-error");
}

/*
 * Create new task
 */
struct rspamd_task *
rspamd_task_new (struct rspamd_worker *worker)
{
	struct rspamd_task *new_task;

	new_task = g_slice_alloc0 (sizeof (struct rspamd_task));

	new_task->worker = worker;

	if (worker) {
		new_task->cfg = worker->srv->cfg;
		if (new_task->cfg->check_all_filters) {
			new_task->flags |= RSPAMD_TASK_FLAG_PASS_ALL;
		}
	}

	gettimeofday (&new_task->tv, NULL);
	new_task->time_real = rspamd_get_ticks ();
	new_task->time_virtual = rspamd_get_virtual_ticks ();

	new_task->task_pool = rspamd_mempool_new (rspamd_mempool_suggest_size (), "task");

	new_task->results = g_hash_table_new (rspamd_str_hash, rspamd_str_equal);
	rspamd_mempool_add_destructor (new_task->task_pool,
		(rspamd_mempool_destruct_t) g_hash_table_unref,
		new_task->results);
	new_task->re_cache = g_hash_table_new (rspamd_str_hash, rspamd_str_equal);
	rspamd_mempool_add_destructor (new_task->task_pool,
		(rspamd_mempool_destruct_t) g_hash_table_unref,
		new_task->re_cache);
	new_task->raw_headers = g_hash_table_new (rspamd_strcase_hash,
			rspamd_strcase_equal);
	new_task->request_headers = g_hash_table_new_full (rspamd_gstring_icase_hash,
		rspamd_gstring_icase_equal, rspamd_gstring_free_hard,
		rspamd_gstring_free_hard);
	rspamd_mempool_add_destructor (new_task->task_pool,
		(rspamd_mempool_destruct_t) g_hash_table_unref,
		new_task->request_headers);
	new_task->reply_headers = g_hash_table_new_full (rspamd_gstring_icase_hash,
			rspamd_gstring_icase_equal, rspamd_gstring_free_hard,
			rspamd_gstring_free_hard);
	rspamd_mempool_add_destructor (new_task->task_pool,
		(rspamd_mempool_destruct_t) g_hash_table_unref,
		new_task->reply_headers);
	rspamd_mempool_add_destructor (new_task->task_pool,
		(rspamd_mempool_destruct_t) g_hash_table_unref,
		new_task->raw_headers);
	new_task->emails = g_hash_table_new (rspamd_url_hash, rspamd_emails_cmp);
	rspamd_mempool_add_destructor (new_task->task_pool,
		(rspamd_mempool_destruct_t) g_hash_table_unref,
		new_task->emails);
	new_task->urls = g_hash_table_new (rspamd_url_hash, rspamd_urls_cmp);
	rspamd_mempool_add_destructor (new_task->task_pool,
		(rspamd_mempool_destruct_t) g_hash_table_unref,
		new_task->urls);
	new_task->parts = g_ptr_array_sized_new (4);
	rspamd_mempool_add_destructor (new_task->task_pool,
			rspamd_ptr_array_free_hard, new_task->parts);
	new_task->text_parts = g_ptr_array_sized_new (2);
	rspamd_mempool_add_destructor (new_task->task_pool,
			rspamd_ptr_array_free_hard, new_task->text_parts);
	new_task->received = g_ptr_array_sized_new (8);
	rspamd_mempool_add_destructor (new_task->task_pool,
			rspamd_ptr_array_free_hard, new_task->received);

	new_task->sock = -1;
	new_task->flags |= (RSPAMD_TASK_FLAG_MIME|RSPAMD_TASK_FLAG_JSON);
	new_task->pre_result.action = METRIC_ACTION_NOACTION;

	new_task->message_id = new_task->queue_id = "undef";

	return new_task;
}


static void
rspamd_task_reply (struct rspamd_task *task)
{
	if (task->fin_callback) {
		task->fin_callback (task, task->fin_arg);
	}
	else {
		rspamd_protocol_write_reply (task);
	}
}

/*
 * Called if all filters are processed
 * @return TRUE if session should be terminated
 */
gboolean
rspamd_task_fin (void *arg)
{
	struct rspamd_task *task = (struct rspamd_task *) arg;

	/* Task is already finished or skipped */
	if (RSPAMD_TASK_IS_PROCESSED (task)) {
		rspamd_task_reply (task);
		return TRUE;
	}

	if (!rspamd_task_process (task, RSPAMD_TASK_PROCESS_ALL)) {
		rspamd_task_reply (task);
		return TRUE;
	}

	if (RSPAMD_TASK_IS_PROCESSED (task)) {
		rspamd_task_reply (task);
		return TRUE;
	}

	/* One more iteration */
	return FALSE;
}

/*
 * Called if session was restored inside fin callback
 */
void
rspamd_task_restore (void *arg)
{
	/* XXX: not needed now ? */
}

/*
 * Free all structures of worker_task
 */
void
rspamd_task_free (struct rspamd_task *task, gboolean is_soft)
{
	struct mime_part *p;
	struct mime_text_part *tp;
	guint i;

	if (task) {
		debug_task ("free pointer %p", task);

		for (i = 0; i < task->parts->len; i ++) {
			p = g_ptr_array_index (task->parts, i);
			g_byte_array_free (p->content, TRUE);
		}

		for (i = 0; i < task->text_parts->len; i ++) {
			tp = g_ptr_array_index (task->text_parts, i);
			if (tp->words) {
				g_array_free (tp->words, TRUE);
			}
			if (tp->normalized_words) {
				g_array_free (tp->normalized_words, TRUE);
			}
		}

		if (task->images) {
			g_list_free (task->images);
		}

		if (task->messages) {
			g_list_free (task->messages);
		}

		if (task->http_conn != NULL) {
			rspamd_http_connection_unref (task->http_conn);
		}

		if (task->sock != -1) {
			close (task->sock);
		}

		if (task->settings != NULL) {
			ucl_object_unref (task->settings);
		}

		if (task->client_addr) {
			rspamd_inet_address_destroy (task->client_addr);
		}

		if (task->from_addr) {
			rspamd_inet_address_destroy (task->from_addr);
		}

		if (task->err) {
			g_error_free (task->err);
		}

		rspamd_mempool_delete (task->task_pool);
		g_slice_free1 (sizeof (struct rspamd_task), task);
	}
}

void
rspamd_task_free_hard (gpointer ud)
{
	struct rspamd_task *task = ud;

	rspamd_task_free (task, FALSE);
}

void
rspamd_task_free_soft (gpointer ud)
{
	struct rspamd_task *task = ud;

	rspamd_task_free (task, FALSE);
}

static void
rspamd_task_unmapper (gpointer ud)
{
	struct rspamd_task *task = ud;

	munmap ((void *)task->msg.start, task->msg.len);
}

gboolean
rspamd_task_load_message (struct rspamd_task *task,
	struct rspamd_http_message *msg, const gchar *start, gsize len)
{
	guint control_len, r;
	struct ucl_parser *parser;
	ucl_object_t *control_obj;
	gchar filepath[PATH_MAX], *fp;
	gint fd, flen;
	gpointer map;
	struct stat st;

	if (msg) {
		rspamd_protocol_handle_headers (task, msg);
	}

	if (task->flags & RSPAMD_TASK_FLAG_FILE) {
		g_assert (task->msg.len > 0);

		r = rspamd_strlcpy (filepath, task->msg.start,
				MIN (sizeof (filepath), task->msg.len + 1));

		rspamd_decode_url (filepath, filepath, r + 1);
		flen = strlen (filepath);

		if (filepath[0] == '"' && flen > 2) {
			/* We need to unquote filepath */
			fp = &filepath[1];
			fp[flen - 2] = '\0';
		}
		else {
			fp = &filepath[0];
		}

		if (access (fp, R_OK) == -1 || stat (fp, &st) == -1) {
			g_set_error (&task->err, rspamd_task_quark(), RSPAMD_PROTOCOL_ERROR,
					"Invalid file (%s): %s", fp, strerror (errno));
			return FALSE;
		}

		fd = open (fp, O_RDONLY);

		if (fd == -1) {
			g_set_error (&task->err, rspamd_task_quark(), RSPAMD_PROTOCOL_ERROR,
					"Cannot open file (%s): %s", fp, strerror (errno));
			return FALSE;
		}

		map = mmap (NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);


		if (map == MAP_FAILED) {
			close (fd);
			g_set_error (&task->err, rspamd_task_quark(), RSPAMD_PROTOCOL_ERROR,
					"Cannot mmap file (%s): %s", fp, strerror (errno));
			return FALSE;
		}

		close (fd);
		task->msg.start = map;
		task->msg.len = st.st_size;

		rspamd_mempool_add_destructor (task->task_pool, rspamd_task_unmapper, task);
	}
	else {
		debug_task ("got input of length %z", task->msg.len);
		task->msg.start = start;
		task->msg.len = len;

		if (task->msg.len == 0) {
			msg_warn_task ("message has invalid message length: %ud",
					task->msg.len);
			g_set_error (&task->err, rspamd_task_quark(), RSPAMD_PROTOCOL_ERROR,
					"Invalid length");
			return FALSE;
		}

		if (task->flags & RSPAMD_TASK_FLAG_HAS_CONTROL) {
			/* We have control chunk, so we need to process it separately */
			if (task->msg.len < task->message_len) {
				msg_warn_task ("message has invalid message length: %ud and total len: %ud",
						task->message_len, task->msg.len);
				g_set_error (&task->err, rspamd_task_quark(), RSPAMD_PROTOCOL_ERROR,
						"Invalid length");
				return FALSE;
			}
			control_len = task->msg.len - task->message_len;

			if (control_len > 0) {
				parser = ucl_parser_new (UCL_PARSER_KEY_LOWERCASE);

				if (!ucl_parser_add_chunk (parser, task->msg.start, control_len)) {
					msg_warn_task ("processing of control chunk failed: %s",
							ucl_parser_get_error (parser));
					ucl_parser_free (parser);
				}
				else {
					control_obj = ucl_parser_get_object (parser);
					ucl_parser_free (parser);
					rspamd_protocol_handle_control (task, control_obj);
					ucl_object_unref (control_obj);
				}

				task->msg.start += control_len;
				task->msg.len -= control_len;
			}
		}
	}

	return TRUE;
}

static gint
rspamd_task_select_processing_stage (struct rspamd_task *task, guint stages)
{
	gint st, mask;

	mask = task->processed_stages;

	if (mask == 0) {
		st = 0;
	}
	else {
		for (st = 1; mask != 1; st ++) {
			mask = (unsigned int)mask >> 1;
		}
	}

	st = 1 << st;

	if (stages & st) {
		return st;
	}
	else if (st < RSPAMD_TASK_STAGE_DONE) {
		/* We assume that the stage that was not requested is done */
		task->processed_stages |= st;
		return rspamd_task_select_processing_stage (task, stages);
	}

	/* We are done */
	return RSPAMD_TASK_STAGE_DONE;
}

static gboolean
rspamd_process_filters (struct rspamd_task *task)
{
	/* Process metrics symbols */
	return rspamd_symbols_cache_process_symbols (task, task->cfg->cache);
}

gboolean
rspamd_task_process (struct rspamd_task *task, guint stages)
{
	gint st;
	gboolean ret = TRUE;
	GError *stat_error = NULL;

	/* Avoid nested calls */
	if (task->flags & RSPAMD_TASK_FLAG_PROCESSING) {
		return TRUE;
	}


	if (RSPAMD_TASK_IS_PROCESSED (task)) {
		return TRUE;
	}

	task->flags |= RSPAMD_TASK_FLAG_PROCESSING;

	st = rspamd_task_select_processing_stage (task, stages);

	switch (st) {
	case RSPAMD_TASK_STAGE_READ_MESSAGE:
		if (!rspamd_message_parse (task)) {
			ret = FALSE;
		}
		break;

	case RSPAMD_TASK_STAGE_PRE_FILTERS:
		rspamd_lua_call_pre_filters (task);
		break;

	case RSPAMD_TASK_STAGE_FILTERS:
		if (!rspamd_process_filters (task)) {
			ret = FALSE;
		}
		break;

	case RSPAMD_TASK_STAGE_CLASSIFIERS:
		if (rspamd_stat_classify (task, task->cfg->lua_state, &stat_error) ==
				RSPAMD_STAT_PROCESS_ERROR) {
			msg_err_task ("classify error: %e", stat_error);
			g_error_free (stat_error);
		}
		break;

	case RSPAMD_TASK_STAGE_COMPOSITES:
		rspamd_make_composites (task);
		break;

	case RSPAMD_TASK_STAGE_POST_FILTERS:
		rspamd_lua_call_post_filters (task);
		break;

	case RSPAMD_TASK_STAGE_DONE:
		task->processed_stages |= RSPAMD_TASK_STAGE_DONE;
		break;

	default:
		/* TODO: not implemented stage */
		break;
	}

	if (RSPAMD_TASK_IS_SKIPPED (task)) {
		task->processed_stages |= RSPAMD_TASK_STAGE_DONE;
	}

	task->flags &= ~RSPAMD_TASK_FLAG_PROCESSING;

	if (!ret || RSPAMD_TASK_IS_PROCESSED (task)) {
		return ret;
	}

	if (rspamd_session_events_pending (task->s) != 0) {
		/* We have events pending, so we consider this stage as incomplete */
		msg_debug_task ("need more work on stage %d", st);
	}
	else {
		/* Mark the current stage as done and go to the next stage */
		msg_debug_task ("completed stage %d", st);
		task->processed_stages |= st;

		/* Reset checkpoint */
		task->checkpoint = NULL;

		/* Tail recursion */
		return rspamd_task_process (task, stages);
	}

	return ret;
}

const gchar *
rspamd_task_get_sender (struct rspamd_task *task)
{
	InternetAddress *iaelt = NULL;
#ifdef GMIME24
	InternetAddressMailbox *imb;

	if (task->from_envelope != NULL) {
		iaelt = internet_address_list_get_address (task->from_envelope, 0);
	}
	else if (task->from_mime != NULL) {
		iaelt = internet_address_list_get_address (task->from_mime, 0);
	}
	imb = INTERNET_ADDRESS_IS_MAILBOX(iaelt) ?
			INTERNET_ADDRESS_MAILBOX (iaelt) : NULL;

	return (imb ? internet_address_mailbox_get_addr (imb) : NULL);
#else
	if (task->from_envelope != NULL) {
		iaelt = internet_address_list_get_address (task->from_envelope);
	}
	else if (task->from_mime != NULL) {
		iaelt = internet_address_list_get_address (task->from_mime);
	}

	return (iaelt != NULL ? internet_address_get_addr (iaelt) : NULL);
#endif
}

gboolean
rspamd_task_add_recipient (struct rspamd_task *task, const gchar *rcpt)
{
	InternetAddressList *tmp_addr;

	if (task->rcpt_envelope == NULL) {
		task->rcpt_envelope = internet_address_list_new ();
#ifdef GMIME24
		rspamd_mempool_add_destructor (task->task_pool,
				(rspamd_mempool_destruct_t) g_object_unref,
				task->rcpt_envelope);
#else
		rspamd_mempool_add_destructor (task->task_pool,
				(rspamd_mempool_destruct_t) internet_address_list_destroy,
				task->rcpt_envelope);
#endif
	}
	tmp_addr = internet_address_list_parse_string (rcpt);

	if (tmp_addr) {
		internet_address_list_append (task->rcpt_envelope, tmp_addr);
#ifdef GMIME24
		g_object_unref (tmp_addr);
#else
		internet_address_list_destroy (tmp_addr);
#endif
		return TRUE;
	}

	return FALSE;
}

gboolean
rspamd_task_add_sender (struct rspamd_task *task, const gchar *sender)
{
	InternetAddressList *tmp_addr;

	if (task->from_envelope == NULL) {
		task->from_envelope = internet_address_list_new ();
#ifdef GMIME24
		rspamd_mempool_add_destructor (task->task_pool,
				(rspamd_mempool_destruct_t) g_object_unref,
				task->from_envelope);
#else
		rspamd_mempool_add_destructor (task->task_pool,
				(rspamd_mempool_destruct_t) internet_address_list_destroy,
				task->from_envelope);
#endif
	}

	if (strcmp (sender, "<>") == 0) {
		/* Workaround for gmime */
		internet_address_list_add (task->from_envelope,
				internet_address_mailbox_new ("", ""));
		return TRUE;
	}
	else {
		tmp_addr = internet_address_list_parse_string (sender);

		if (tmp_addr) {
			internet_address_list_append (task->from_envelope, tmp_addr);
#ifdef GMIME24
			g_object_unref (tmp_addr);
#else
			internet_address_list_destroy (tmp_addr);
#endif
			return TRUE;
		}
	}

	return FALSE;
}


guint
rspamd_task_re_cache_add (struct rspamd_task *task, const gchar *re,
		guint value)
{
	guint ret = RSPAMD_TASK_CACHE_NO_VALUE;
	static const guint32 mask = 1 << 31;
	gpointer p;

	p = g_hash_table_lookup (task->re_cache, re);

	if (p != NULL) {
		ret = GPOINTER_TO_INT (p) & ~mask;
	}

	g_hash_table_insert (task->re_cache, (gpointer)re,
			GINT_TO_POINTER (value | mask));

	return ret;
}

guint
rspamd_task_re_cache_check (struct rspamd_task *task, const gchar *re)
{
	guint ret = RSPAMD_TASK_CACHE_NO_VALUE;
	static const guint32 mask = 1 << 31;
	gpointer p;

	p = g_hash_table_lookup (task->re_cache, re);

	if (p != NULL) {
		ret = GPOINTER_TO_INT (p) & ~mask;
	}

	return ret;
}

gboolean
rspamd_learn_task_spam (struct rspamd_classifier_config *cl,
	struct rspamd_task *task,
	gboolean is_spam,
	GError **err)
{
	return rspamd_stat_learn (task, is_spam, task->cfg->lua_state, err);
}
