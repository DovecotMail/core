/* Copyright (c) 2006-2017 Dovecot authors, see the included COPYING file */

#include "lib.h"
#include "ioloop.h"
#include "array.h"
#include "str.h"
#include "istream.h"
#include "ostream.h"
#include "iostream-temp.h"
#include "master-service.h"
#include "lmtp-client.h"
#include "lda-settings.h"
#include "mail-deliver.h"
#include "program-client.h"
#include "smtp-client.h"

#include <unistd.h>
#include <sys/wait.h>
#include <sysexits.h>
#include <signal.h>

#define DEFAULT_SUBMISSION_PORT 25

struct smtp_client {
	pool_t pool;
	struct ostream *output;
	struct istream *input;

	const struct lda_settings *set;
	ARRAY_TYPE(const_string) destinations;
	const char *return_path;
	const char *error;

	bool success:1;
	bool finished:1;
	bool tempfail:1;
};

struct smtp_client *
smtp_client_init(const struct lda_settings *set, const char *return_path)
{
	struct smtp_client *client;
	pool_t pool;

	pool = pool_alloconly_create("smtp client", 256);
	client = p_new(pool, struct smtp_client, 1);
	client->pool = pool;
	client->set = set;
	client->return_path = p_strdup(pool, return_path);
	p_array_init(&client->destinations, pool, 2);
	return client;
}

void smtp_client_add_rcpt(struct smtp_client *client, const char *address)
{
	i_assert(client->output == NULL);

	address = p_strdup(client->pool, address);
	array_append(&client->destinations, &address, 1);
}

struct ostream *smtp_client_send(struct smtp_client *client)
{
	i_assert(client->output == NULL);
	i_assert(array_count(&client->destinations) > 0);

	client->output = iostream_temp_create
		(t_strconcat("/tmp/dovecot.",
			master_service_get_name(master_service), NULL), 0);
	o_stream_set_no_error_handling(client->output, TRUE);
	return client->output;
}

static void smtp_client_send_finished(void *context)
{
	struct smtp_client *smtp_client = context;

	smtp_client->finished = TRUE;
	io_loop_stop(current_ioloop);
}

static void
smtp_client_error(struct smtp_client *client,
		 bool tempfail, const char *error)
{
	if (client->error == NULL) {
		client->tempfail = tempfail;
		client->error = p_strdup_printf(client->pool,
			"smtp(%s): %s",
			client->set->submission_host, error);
	}
}

static void
rcpt_to_callback(enum lmtp_client_result result, const char *reply, void *context)
{
	struct smtp_client *client = context;

	if (result != LMTP_CLIENT_RESULT_OK) {
		smtp_client_error(client, (reply[0] != '5'),
			t_strdup_printf("RCPT TO failed: %s", reply));
		smtp_client_send_finished(client);
	}
}

static void
data_callback(enum lmtp_client_result result, const char *reply, void *context)
{
	struct smtp_client *client = context;

	if (result != LMTP_CLIENT_RESULT_OK) {
		smtp_client_error(client, (reply[0] != '5'),
			t_strdup_printf("DATA failed: %s", reply));
		smtp_client_send_finished(client);
	} else {
		client->success = TRUE;
	}
}

static int
smtp_client_send_host(struct smtp_client *client,
		       unsigned int timeout_secs, const char **error_r)
{
	struct lmtp_client_settings client_set;
	struct lmtp_client *lmtp_client;
	struct ioloop *ioloop;
	const char *host, *const *destp;
	in_port_t port;

	if (net_str2hostport(client->set->submission_host,
			     DEFAULT_SUBMISSION_PORT, &host, &port) < 0) {
		*error_r = t_strdup_printf(
			"Invalid submission_host: %s", host);
		return -1;
	}

	i_zero(&client_set);
	client_set.mail_from = client->return_path == NULL ? "<>" :
		t_strconcat("<", client->return_path, ">", NULL);
	client_set.my_hostname = client->set->hostname;
	client_set.timeout_secs = timeout_secs;

	ioloop = io_loop_create();
	lmtp_client = lmtp_client_init(&client_set, smtp_client_send_finished,
				  client);

	if (lmtp_client_connect_tcp(lmtp_client, LMTP_CLIENT_PROTOCOL_SMTP,
				    host, port) < 0) {
		lmtp_client_deinit(&lmtp_client);
		io_loop_destroy(&ioloop);
		*error_r = t_strdup_printf("Couldn't connect to %s:%u",
					   host, port);
		return -1;
	}

	array_foreach(&client->destinations, destp) {
		lmtp_client_add_rcpt(lmtp_client, *destp, rcpt_to_callback,
				     data_callback, client);
	}

	lmtp_client_send(lmtp_client, client->input);
	i_stream_unref(&client->input);

	if (!client->finished)
		io_loop_run(ioloop);
	lmtp_client_deinit(&lmtp_client);
	io_loop_destroy(&ioloop);

	if (client->success)
		return 1;
	else if (client->tempfail) {
		i_assert(client->error != NULL);
		*error_r = t_strdup(client->error);
		return -1;
	} else {
		i_assert(client->error != NULL);
		*error_r = t_strdup(client->error);
		return 0;
	}
}

static int
smtp_client_send_sendmail(struct smtp_client *client,
		       unsigned int timeout_secs, const char **error_r)
{
	const char *const *sendmail_args, *sendmail_bin, *str;
	ARRAY_TYPE(const_string) args;
	unsigned int i;
	struct program_client_settings pc_set;
	struct program_client *pc;
	int ret;

	sendmail_args = t_strsplit(client->set->sendmail_path, " ");
	t_array_init(&args, 16);
	i_assert(sendmail_args[0] != NULL);
	sendmail_bin = sendmail_args[0];
	for (i = 1; sendmail_args[i] != NULL; i++)
		array_append(&args, &sendmail_args[i], 1);

	str = "-i"; array_append(&args, &str, 1); /* ignore dots */
	str = "-f"; array_append(&args, &str, 1);
	str = (client->return_path != NULL &&
		*client->return_path != '\0' ?
			client->return_path : "<>");
	array_append(&args, &str, 1);

	str = "--"; array_append(&args, &str, 1);
	array_append_array(&args, &client->destinations);
	array_append_zero(&args);

	i_zero(&pc_set);
	pc_set.client_connect_timeout_msecs = timeout_secs * 1000;
	pc_set.input_idle_timeout_msecs = timeout_secs * 1000;
	restrict_access_init(&pc_set.restrict_set);

	pc = program_client_local_create
		(sendmail_bin, array_idx(&args, 0), &pc_set);

	program_client_set_input(pc, client->input);
	i_stream_unref(&client->input);

	ret = program_client_run(pc);

	program_client_destroy(&pc);

	if (ret < 0) {
		*error_r = "Failed to execute sendmail";
		return -1;
	} else if (ret == 0) {
		*error_r = "Sendmail program returned error";
		return -1;
	}
	return 1;
}

void smtp_client_abort(struct smtp_client **_client)
{
	struct smtp_client *client = *_client;

	*_client = NULL;

	if (client->output != NULL) {
		o_stream_ignore_last_errors(client->output);
		o_stream_destroy(&client->output);
	}
	if (client->input != NULL)
		i_stream_destroy(&client->input);
	pool_unref(&client->pool);
}

int smtp_client_deinit(struct smtp_client *client, const char **error_r)
{
	return smtp_client_deinit_timeout(client, 0, error_r);
}

int smtp_client_deinit_timeout(struct smtp_client *client,
			       unsigned int timeout_secs, const char **error_r)
{
	int ret;

	/* the mail has been written to a file. now actually send it. */
	client->input = iostream_temp_finish
		(&client->output, IO_BLOCK_SIZE);

	if (*client->set->submission_host != '\0') {
		ret = smtp_client_send_host
			(client, timeout_secs, error_r);
	} else {
		ret = smtp_client_send_sendmail
			(client, timeout_secs, error_r);
	}

	smtp_client_abort(&client);
	return ret;
}
