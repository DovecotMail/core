/* Copyright (c) 2017-2018 Dovecot authors, see the included COPYING file */

#include "lib.h"
#include "test-common.h"
#include "str.h"
#include "var-expand.h"
#include "randgen.h"
#include "dcrypt.h"

struct module;

extern void var_expand_crypt_init(struct module *module);
extern void var_expand_crypt_deinit(void);

static struct var_expand_table table[] = {
	{ .key = "iv", .value = "98b3b40a48ca40f998b3b40a48ca40f9" },
	{ .key = "key", .value = "cc2981c8f38aea59cc2981c8f38aea59" },
	{
		.key = "encrypted_raw",
		.value = "46b58741763fe22598014be26331a082" },
	{
		.key = "encrypted",
		.value = "98b3b40a48ca40f998b3b40a48ca40f9$46b58741763fe22598014be26331a082$"
	},
	{ .key = "decrypted", .value = "hello, world" },
	{ .key = "encrypted2", .value = NULL },
	{ NULL, NULL, NULL }
};

static void test_var_expand_crypt_init(void)
{
	var_expand_crypt_init(NULL);
}

static void test_var_expand_crypt(void)
{
	const struct var_expand_params params = {
		.table = table,
	};

	const struct {
		const char *input;
		const char *output;
		int expect_ret;
	} test_cases[] = {
		{ "%{decrypted|encrypt(algorithm='null')}", "", -1 },
		{
			"%{decrypted|encrypt(algorithm='aes-128-cbc',iv=iv,key=key)}",
			"98b3b40a48ca40f998b3b40a48ca40f9$46b58741763fe22598014be26331a082$",
			0
		},
		{
			"%{decrypted|"
			"encrypt(algorithm='aes-128-cbc',iv=iv,key=key,raw=1)}",
			"46b58741763fe22598014be26331a082",
			0
		},
		{ "%{encrypted|decrypt(algorithm='null')}", "", -1 },
		{
			"%{encrypted|decrypt(algorithm='aes-128-cbc',key=key)}",
			"hello, world",
			0
		},
		{
			"%{encrypted_raw|unhexlify|"
			"decrypt(algorithm='aes-128-cbc',iv=iv,key=key,raw=1)}",
			"hello, world",
			0
		},
	};

	unsigned int i;

	test_begin("var_expand_crypt");

	for(i=0; i < N_ELEMENTS(test_cases); i++) T_BEGIN {
		const char *error;
		string_t *dest = t_str_new(32);
		int ret = var_expand(dest, test_cases[i].input, &params, &error);
		if (ret < 0) {
			if (test_cases[i].expect_ret == -1) {
				i_info("Expected: var_expand(%s): %s",
				       test_cases[i].input, error);
			} else {
				i_error("var_expand(%s): %s",
					test_cases[i].input, error);
			}
		}
		test_assert_strcmp_idx(str_c(dest), test_cases[i].output, i);
		test_assert_idx(ret == test_cases[i].expect_ret, i);
	} T_END;

	test_end();
}

static void test_var_expand_crypt_random(void)
{
	test_begin("var_expand_crypt_random");

	string_t *input = t_str_new(32);
	string_t *output = t_str_new(32);
	const struct var_expand_params params = {
		.table = table,
	};
	int ret = 0;

	for (unsigned int i = 0; i < 1000; i++) {
		const char *error;
		str_truncate(input, 0);
		str_truncate(output, 0);
		ret += var_expand(input,
			"%{decrypted|encrypt(algorithm='aes-128-cbc',key=key)}",
				 &params, &error);
		test_assert_cmp_idx(ret, ==, 0, i);
		var_expand_table_set_value(table, "encrypted2", str_c(input));
		ret += var_expand(output,
			"%{encrypted2|decrypt(algorithm='aes-128-cbc',key=key)}",
				  &params, &error);
		test_assert_cmp_idx(ret, ==, 0, i);
		struct var_expand_table *entry =
			var_expand_table_get(table, "decrypted");
		test_assert_strcmp_idx(str_c(output), entry->value, i);
		if (strcmp(str_c(output), entry->value) != 0)
			break;
	};

	test_end();
}

static void test_var_expand_crypt_deinit(void)
{
	var_expand_crypt_deinit();
}

int main(void)
{
	int ret = 0;
	static void (*const test_functions[])(void) = {
		test_var_expand_crypt_init,
		test_var_expand_crypt,
		test_var_expand_crypt_random,
		test_var_expand_crypt_deinit,
		NULL
	};
	struct dcrypt_settings set = {
		.module_dir = DCRYPT_BUILD_DIR"/.libs"
	};
	const char *error;

	if (!dcrypt_initialize(NULL, &set, &error)) {
		i_info("No functional dcrypt backend found - skipping tests: %s", error);
		return 0;
	}

	ret = test_run(test_functions);

	dcrypt_deinitialize();

	return ret;
}
