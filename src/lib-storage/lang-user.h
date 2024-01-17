#ifndef LANG_USER_H
#define LANG_USER_H

#include "lang-settings.h"

struct language_user {
	const struct language *lang;
	struct lang_filter *filter;
	struct lang_tokenizer *index_tokenizer, *search_tokenizer;
};
ARRAY_DEFINE_TYPE(language_user, struct language_user *);

struct language_user *
fts_user_language_find(struct mail_user *user, const struct language *lang);
struct language_list *fts_user_get_language_list(struct mail_user *user);
const ARRAY_TYPE(language_user) *
fts_user_get_all_languages(struct mail_user *user);
struct language_user *fts_user_get_data_lang(struct mail_user *user);
const ARRAY_TYPE(language_user) *
fts_user_get_data_languages(struct mail_user *user);

const struct langs_settings *fts_user_get_settings(struct mail_user *user);

int fts_mail_user_init(struct mail_user *user, bool initialize_libfts,
		       const char **error_r);
void fts_mail_user_deinit(struct mail_user *user);

#endif
