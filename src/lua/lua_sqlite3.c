/*
 * Copyright (c) 2016, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *	 * Redistributions of source code must retain the above copyright
 *	   notice, this list of conditions and the following disclaimer.
 *	 * Redistributions in binary form must reproduce the above copyright
 *	   notice, this list of conditions and the following disclaimer in the
 *	   documentation and/or other materials provided with the distribution.
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


#include "lua_common.h"
#include "util.h"
#include "sqlite_utils.h"

/***
 * @module rspamd_sqlite3
 * This module provides routines to query sqlite3 databases
@example
local sqlite3 = require "rspamd_sqlite3"

local db = sqlite3.open("/tmp/db.sqlite")

if db then
	db:exec([[ CREATE TABLE x (id INT, value TEXT); ]])

	db:exec([[ INSERT INTO x VALUES (?1, ?2); ]], 1, 'test')

	for row in db:rows([[ SELECT * FROM x ]]) do
		print(string.format('%d -> %s', row.id, row.value))
	end
end
 */

LUA_FUNCTION_DEF (sqlite3, open);
LUA_FUNCTION_DEF (sqlite3, sql);
LUA_FUNCTION_DEF (sqlite3, rows);
LUA_FUNCTION_DEF (sqlite3, close);
LUA_FUNCTION_DEF (sqlite3_stmt, close);

static const struct luaL_reg sqlitelib_f[] = {
	LUA_INTERFACE_DEF (sqlite3, open),
	{NULL, NULL}
};

static const struct luaL_reg sqlitelib_m[] = {
	LUA_INTERFACE_DEF (sqlite3, sql),
	LUA_INTERFACE_DEF (sqlite3, rows),
	{"__tostring", rspamd_lua_class_tostring},
	{"__gc", lua_sqlite3_close},
	{NULL, NULL}
};

static const struct luaL_reg sqlitestmtlib_m[] = {
	{"__tostring", rspamd_lua_class_tostring},
	{"__gc", lua_sqlite3_stmt_close},
	{NULL, NULL}
};

static sqlite3 *
lua_check_sqlite3 (lua_State * L, gint pos)
{
	void *ud = luaL_checkudata (L, pos, "rspamd{sqlite3}");
	luaL_argcheck (L, ud != NULL, pos, "'sqlite3' expected");
	return ud ? *((sqlite3 **)ud) : NULL;
}

static sqlite3_stmt *
lua_check_sqlite3_stmt (lua_State * L, gint pos)
{
	void *ud = luaL_checkudata (L, pos, "rspamd{sqlite3_stmt}");
	luaL_argcheck (L, ud != NULL, pos, "'sqlite3_stmt' expected");
	return ud ? *((sqlite3_stmt **)ud) : NULL;
}


/***
 * @function rspamd_sqlite3.open(path)
 * Opens sqlite3 database at the specified path. DB is created if not exists.
 * @param {string} path path to db
 * @return {sqlite3} sqlite3 handle
 */
static gint
lua_sqlite3_open (lua_State *L)
{
	const gchar *path = luaL_checkstring (L, 1);
	sqlite3 *db, **pdb;
	GError *err = NULL;

	if (path == NULL) {
		lua_pushnil (L);
		return 1;
	}

	db = rspamd_sqlite3_open_or_create (NULL, path, NULL, &err);

	if (db == NULL) {
		if (err) {
			msg_err ("cannot open db: %e", err);
			g_error_free (err);
		}
		lua_pushnil (L);

		return 1;
	}

	pdb = lua_newuserdata (L, sizeof (db));
	*pdb = db;
	rspamd_lua_setclass (L, "rspamd{sqlite3}", -1);

	return 1;
}

static void
lua_sqlite3_bind_statements (lua_State *L, gint start, gint end,
		sqlite3_stmt *stmt)
{
	gint i, type, num = 1;
	const gchar *str;
	gsize slen;
	gdouble n;

	g_assert (start < end && start > 0 && end > 0);

	for (i = start; i <= end; i ++) {
		type = lua_type (L, i);

		switch (type) {
		case LUA_TNUMBER:
			n = lua_tonumber (L, i);

			if (n == (gdouble)((gint64)n)) {
				sqlite3_bind_int64 (stmt, num, n);
			}
			else {
				sqlite3_bind_double (stmt, num, n);
			}
			num ++;
			break;
		case LUA_TSTRING:
			str = lua_tolstring (L, i, &slen);
			sqlite3_bind_text (stmt, num, str, slen, SQLITE_TRANSIENT);
			num ++;
			break;
		default:
			msg_err ("invalid type at position %d: %s", i, lua_typename (L, type));
			break;
		}
	}
}

/***
 * @function rspamd_sqlite3:sql(query[, args..])
 * Performs sqlite3 query replacing '?1', '?2' and so on with the subsequent args
 * of the function
 *
 * @param {string} query SQL query
 * @param {string|number} args... variable number of arguments
 * @return {boolean} `true` if a statement has been successfully executed
 */
static gint
lua_sqlite3_sql (lua_State *L)
{
	sqlite3 *db = lua_check_sqlite3 (L, 1);
	const gchar *query = luaL_checkstring (L, 2);
	sqlite3_stmt *stmt;
	gboolean ret = FALSE;
	gint top, rc;

	if (db && query) {
		if (sqlite3_prepare_v2 (db, query, -1, &stmt, NULL) != SQLITE_OK) {
			msg_err ("cannot prepare query %s: %s", query, sqlite3_errmsg (db));
			lua_pushstring (L, sqlite3_errmsg (db));
			lua_error (L);
		}
		else {
			top = lua_gettop (L);

			if (top > 2) {
				/* Push additional arguments to sqlite3 */
				lua_sqlite3_bind_statements (L, 2, top, stmt);
			}

			rc = sqlite3_step (stmt);

			if (rc == SQLITE_ROW || rc == SQLITE_OK || rc == SQLITE_DONE) {
				ret = TRUE;
			}
			else {
				msg_warn ("sqlite3 error: %s", sqlite3_errmsg (db));
			}

			sqlite3_finalize (stmt);
		}
	}

	lua_pushboolean (L, ret);

	return 1;
}

static void
lua_sqlite3_push_row (lua_State *L, sqlite3_stmt *stmt)
{
	gint nresults, i, type;
	const gchar *str;
	gsize slen;

	nresults = sqlite3_column_count (stmt);
	lua_createtable (L, 0, nresults);

	for (i = 0; i < nresults; i ++) {
		lua_pushstring (L, sqlite3_column_name (stmt, i));
		type = sqlite3_column_type (stmt, i);

		switch (type) {
		case SQLITE_INTEGER:
			lua_pushnumber (L, sqlite3_column_int64 (stmt, i));
			break;
		case SQLITE_FLOAT:
			lua_pushnumber (L, sqlite3_column_double (stmt, i));
			break;
		case SQLITE_TEXT:
			slen = sqlite3_column_bytes (stmt, i);
			str = sqlite3_column_text (stmt, i);
			lua_pushlstring (L, str, slen);
			break;
		case SQLITE_BLOB:
			slen = sqlite3_column_bytes (stmt, i);
			str = sqlite3_column_blob (stmt, i);
			lua_pushlstring (L, str, slen);
			break;
		default:
			lua_pushboolean (L, 0);
			break;
		}

		lua_settable (L, -3);
	}
}

static gint
lua_sqlite3_next_row (lua_State *L)
{
	sqlite3_stmt *stmt = *(sqlite3_stmt **)lua_touserdata (L, lua_upvalueindex (1));
	gint rc;

	if (stmt != NULL) {
		rc = sqlite3_step (stmt);

		if (rc == SQLITE_ROW) {
			lua_sqlite3_push_row (L, stmt);
			return 1;
		}
	}

	return 0;
}

/***
 * @function rspamd_sqlite3:rows(query[, args..])
 * Performs sqlite3 query replacing '?1', '?2' and so on with the subsequent args
 * of the function. This function returns iterator suitable for loop construction:
 *
 * @param {string} query SQL query
 * @param {string|number} args... variable number of arguments
 * @return {function} iterator to get all rows
@example
for row in db:rows([[ SELECT * FROM x ]]) do
  print(string.format('%d -> %s', row.id, row.value))
end
 */
static gint
lua_sqlite3_rows (lua_State *L)
{
	sqlite3 *db = lua_check_sqlite3 (L, 1);
	const gchar *query = luaL_checkstring (L, 2);
	sqlite3_stmt *stmt, **pstmt;
	gint top;

	if (db && query) {
		if (sqlite3_prepare_v2 (db, query, -1, &stmt, NULL) != SQLITE_OK) {
			msg_err ("cannot prepare query %s: %s", query, sqlite3_errmsg (db));
			lua_pushstring (L, sqlite3_errmsg (db));
			lua_error (L);
		}
		else {
			top = lua_gettop (L);

			if (top > 2) {
				/* Push additional arguments to sqlite3 */
				lua_sqlite3_bind_statements (L, 2, top, stmt);
			}

			/* Create C closure */
			pstmt = lua_newuserdata (L, sizeof (stmt));
			*pstmt = stmt;
			rspamd_lua_setclass (L, "rspamd{sqlite3_stmt}", -1);

			lua_pushcclosure (L, lua_sqlite3_next_row, 1);
		}
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

static gint
lua_sqlite3_close (lua_State *L)
{
	sqlite3 *db = lua_check_sqlite3 (L, 1);

	if (db) {
		sqlite3_close (db);
	}

	return 0;
}

static gint
lua_sqlite3_stmt_close (lua_State *L)
{
	sqlite3_stmt *stmt = lua_check_sqlite3_stmt (L, 1);

	if (stmt) {
		sqlite3_finalize (stmt);
	}

	return 0;
}

static gint
lua_load_sqlite3 (lua_State * L)
{
	lua_newtable (L);
	luaL_register (L, NULL, sqlitelib_f);

	return 1;
}
/**
 * Open redis library
 * @param L lua stack
 * @return
 */
void
luaopen_sqlite3 (lua_State * L)
{
	luaL_newmetatable (L, "rspamd{sqlite3}");
	lua_pushstring (L, "__index");
	lua_pushvalue (L, -2);
	lua_settable (L, -3);

	lua_pushstring (L, "class");
	lua_pushstring (L, "rspamd{sqlite3}");
	lua_rawset (L, -3);

	luaL_register (L, NULL, sqlitelib_m);
	lua_pop (L, 1);

	luaL_newmetatable (L, "rspamd{sqlite3_stmt}");
	lua_pushstring (L, "__index");
	lua_pushvalue (L, -2);
	lua_settable (L, -3);

	lua_pushstring (L, "class");
	lua_pushstring (L, "rspamd{sqlite3_stmt}");
	lua_rawset (L, -3);

	luaL_register (L, NULL, sqlitestmtlib_m);
	lua_pop (L, 1);

	rspamd_lua_add_preload (L, "rspamd_sqlite3", lua_load_sqlite3);
}
