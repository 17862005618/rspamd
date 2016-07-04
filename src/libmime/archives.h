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
#ifndef SRC_LIBMIME_ARCHIVES_H_
#define SRC_LIBMIME_ARCHIVES_H_

#include "config.h"

enum rspamd_archive_type {
	RSPAMD_ARCHIVE_ZIP,
};


struct rspamd_archive {
	enum rspamd_archive_type type;
	const gchar *archive_name;
	gsize size;
	GPtrArray *files; /* Array of GStrings */
};

/**
 * Process archives from a worker task
 */
void rspamd_archives_process (struct rspamd_task *task);

/**
 * Get textual representation of an archive's type
 */
const gchar * rspamd_archive_type_str (enum rspamd_archive_type type);

#endif /* SRC_LIBMIME_ARCHIVES_H_ */