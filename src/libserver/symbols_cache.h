/*
 * Copyright (c) 2009-2015, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR ''AS IS'' AND ANY
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

#ifndef RSPAMD_SYMBOLS_CACHE_H
#define RSPAMD_SYMBOLS_CACHE_H

#include "config.h"
#include "ucl.h"

#define MAX_SYMBOL 128

struct rspamd_task;
struct rspamd_config;
struct symbols_cache;

typedef void (*symbol_func_t)(struct rspamd_task *task, gpointer user_data);

enum rspamd_symbol_type {
	SYMBOL_TYPE_NORMAL = (1 << 0),
	SYMBOL_TYPE_VIRTUAL = (1 << 1),
	SYMBOL_TYPE_CALLBACK = (1 << 2),
	SYMBOL_TYPE_GHOST = (1 << 3),
	SYMBOL_TYPE_SKIPPED = (1 << 4),
	SYMBOL_TYPE_COMPOSITE = (1 << 5),
	SYMBOL_TYPE_FINE = (1 << 6)
};

/**
 * Creates new cache structure
 * @return
 */
struct symbols_cache* rspamd_symbols_cache_new (void);

/**
 * Remove the cache structure syncing data if needed
 * @param cache
 */
void rspamd_symbols_cache_destroy (struct symbols_cache *cache);

/**
 * Load symbols cache from file, must be called _after_ init_symbols_cache
 */
gboolean rspamd_symbols_cache_init (struct symbols_cache* cache,
	struct rspamd_config *cfg);

/**
 * Generic function to register a symbol
 * @param cache
 * @param name
 * @param weight
 * @param priority
 * @param func
 * @param user_data
 * @param type
 * @param parent
 */
gint rspamd_symbols_cache_add_symbol (struct symbols_cache *cache,
	const gchar *name,
	gint priority,
	symbol_func_t func,
	gpointer user_data,
	enum rspamd_symbol_type type,
	gint parent);

/**
 * Call function for cached symbol using saved callback
 * @param task task object
 * @param cache symbols cache
 * @param saved_item pointer to currently saved item
 */
gboolean rspamd_symbols_cache_process_symbols (struct rspamd_task *task,
	struct symbols_cache *cache);

/**
 * Validate cache items agains theirs weights defined in metrics
 * @param cache symbols cache
 * @param cfg configuration
 * @param strict do strict checks - symbols MUST be described in metrics
 */
gboolean rspamd_symbols_cache_validate (struct symbols_cache *cache,
	struct rspamd_config *cfg,
	gboolean strict);

/**
 * Return statistics about the cache as ucl object (array of objects one per item)
 * @param cache
 * @return
 */
ucl_object_t *rspamd_symbols_cache_counters (struct symbols_cache * cache);

/**
 * Start cache reloading
 * @param cache
 * @param ev_base
 */
void rspamd_symbols_cache_start_refresh (struct symbols_cache * cache,
		struct event_base *ev_base);

/**
 * Increases counter for a specific symbol
 * @param cache
 * @param symbol
 */
void rspamd_symbols_cache_inc_frequency (struct symbols_cache *cache,
		const gchar *symbol);

/**
 * Add dependency relation between two symbols identified by id (source) and
 * a symbolic name (destination). Destination could be virtual or real symbol.
 * Callback destinations are not yet supported.
 * @param id_from source symbol
 * @param to destination name
 */
void rspamd_symbols_cache_add_dependency (struct symbols_cache *cache,
		gint id_from, const gchar *to);

/**
 * Add delayed dependency that is resolved on cache post-load routine
 * @param cache
 * @param from
 * @param to
 */
void rspamd_symbols_cache_add_delayed_dependency (struct symbols_cache *cache,
		const gchar *from, const gchar *to);

#endif
