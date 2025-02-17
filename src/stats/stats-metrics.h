#ifndef STATS_METRICS_H
#define STATS_METRICS_H

#include "stats-settings.h"
#include "sha1.h"

#define STATS_EVENT_FIELD_NAME_DURATION "duration"

struct metric;
struct stats_metrics;

struct event_exporter {
	const char *name;

	/*
	 * serialization format options
	 *
	 * the "how do we encode the event before sending it" knobs
	 */
	enum event_exporter_time_fmt time_format;

	/* Max length for string field values */
	size_t format_max_field_len;

	/* function to serialize the event */
	void (*format)(const struct metric *, struct event *, buffer_t *);

	/* mime type for the format */
	const char *format_mime_type;

	const struct event_exporter_transport *transport;
};

struct metric_export_info {
	struct event_exporter *exporter;

	enum event_exporter_includes {
		EVENT_EXPORTER_INCL_NONE       = 0,
		EVENT_EXPORTER_INCL_NAME       = 0x01,
		EVENT_EXPORTER_INCL_HOSTNAME   = 0x02,
		EVENT_EXPORTER_INCL_TIMESTAMPS = 0x04,
		EVENT_EXPORTER_INCL_CATEGORIES = 0x08,
		EVENT_EXPORTER_INCL_FIELDS     = 0x10,
	} include;
};

struct metric_field {
	const char *field_key;
	struct stats_dist *stats;
};

enum metric_value_type {
	METRIC_VALUE_TYPE_STR,
	METRIC_VALUE_TYPE_INT,
	METRIC_VALUE_TYPE_IP,
	METRIC_VALUE_TYPE_BUCKET_INDEX,
};

struct metric_value {
	enum metric_value_type type;
	unsigned char hash[SHA1_RESULTLEN];
	intmax_t intmax;
	struct ip_addr ip;
};

struct metric {
	const struct stats_metric_settings *set;
	const char *name;
	/* When this metric is a sub-metric, then this is the
	   suffix for name and any sub_names before it.

	   So if we have

	   struct metric imap_command {
	       event_name = imap_command_finished
	       group_by = cmd_name
	   }

	   The metric.name will always be imap_command and for each sub-metric
	   metric.sub_name will be whatever the cmd_name is, such as 'select'.

	   This is a display name and does not guarantee uniqueness.
	*/
	const char *sub_name;
	size_t sub_name_used_size;

	/* Timing for how long the event existed */
	struct stats_dist *duration_stats;

	unsigned int fields_count;
	struct metric_field *fields;

	unsigned int group_by_count;
	const struct stats_metric_settings_group_by *group_by;
	struct metric_value group_value;
	ARRAY(struct metric *) sub_metrics;

	struct metric_export_info export_info;
};

bool stats_metrics_add_dynamic(struct stats_metrics *metrics,
			       const struct stats_metric_settings *set,
			       ARRAY_TYPE(stats_metric_settings_group_by) *group_by,
			       const char **error_r);

bool stats_metrics_remove_dynamic(struct stats_metrics *metrics,
				  const char *name);

int stats_metrics_init(struct event *event,
		       const struct stats_settings *set,
		       struct stats_metrics **metrics_r, const char **error_r);
void stats_metrics_deinit(struct stats_metrics **metrics);

/* Reset all metrics */
void stats_metrics_reset(struct stats_metrics *metrics);

/* Returns event filter created from the stats_settings. */
struct event_filter *
stats_metrics_get_event_filter(struct stats_metrics *metrics);

/* Update metrics with given event. */
void stats_metrics_event(struct stats_metrics *metrics, struct event *event,
			 const struct failure_context *ctx);

/* Iterate through all the tracked metrics. */
struct stats_metrics_iter *
stats_metrics_iterate_init(struct stats_metrics *metrics);
const struct metric *stats_metrics_iterate(struct stats_metrics_iter *iter);
void stats_metrics_iterate_deinit(struct stats_metrics_iter **iter);

#endif
