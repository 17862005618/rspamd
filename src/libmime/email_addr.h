/*-
 * Copyright 2016 Vsevolod Stakhov
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef SRC_LIBMIME_EMAIL_ADDR_H_
#define SRC_LIBMIME_EMAIL_ADDR_H_

#include "config.h"
#include "ref.h"

struct raw_header;

enum rspamd_email_address_flags {
	RSPAMD_EMAIL_ADDR_VALID = (1 << 0),
	RSPAMD_EMAIL_ADDR_IP = (1 << 1),
	RSPAMD_EMAIL_ADDR_BRACED = (1 << 2),
	RSPAMD_EMAIL_ADDR_QUOTED = (1 << 3),
	RSPAMD_EMAIL_ADDR_EMPTY = (1 << 4),
	RSPAMD_EMAIL_ADDR_SMTP = (1 << 5),
	RSPAMD_EMAIL_ADDR_ALLOCATED = (1 << 6),
};

/*
 * Structure that represents email address in a convenient way
 */
struct rspamd_email_address {
	const gchar *raw;
	const gchar *addr;
	const gchar *user;
	const gchar *domain;
	const gchar *name;

	guint raw_len;
	guint addr_len;
	guint user_len;
	guint domain_len;
	guint name_len;
	enum rspamd_email_address_flags flags;

	ref_entry_t ref;
};

/**
 * Create email address from a single rfc822 address (e.g. from mail from:)
 * @param str string to use
 * @param len length of string
 * @return
 */
struct rspamd_email_address * rspamd_email_address_from_smtp (
		const gchar *str, guint len);

struct rspamd_email_address * rspamd_email_address_ref (
		struct rspamd_email_address *addr);

void rspamd_email_address_unref (struct rspamd_email_address *addr);

#endif /* SRC_LIBMIME_EMAIL_ADDR_H_ */
