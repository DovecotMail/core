/* Copyright (c) 2005-2018 Dovecot authors, see the included COPYING file */

#include "lib.h"
#include "array.h"
#include "settings-parser.h"
#include "master-service-settings.h"
#include "config-parser.h"
#include "config-filter.h"
#include "dns-util.h"

static const struct config_filter empty_filter;
static const struct config_filter empty_defaults_filter = {
	.default_settings = TRUE
};

static int config_filter_match_service(const struct config_filter *mask,
				       const struct config_filter *filter)
{
	if (mask->service != NULL) {
		if (filter->service == NULL)
			return -1;
		if (mask->service[0] == '!') {
			/* not service */
			if (strcmp(filter->service, mask->service + 1) == 0)
				return 0;
		} else {
			if (strcmp(filter->service, mask->service) != 0)
				return 0;
		}
	}
	return 1;
}

static bool
config_filter_match_local_name(const struct config_filter *mask,
			       const char *filter_local_name)
{
	/* Handle multiple names separated by spaces in local_name
	   * Ex: local_name "mail.domain.tld domain.tld mx.domain.tld" { ... } */
	const char *ptr, *local_name = mask->local_name;
	while((ptr = strchr(local_name, ' ')) != NULL) {
		if (dns_match_wildcard(filter_local_name,
		    t_strdup_until(local_name, ptr)) == 0)
			return TRUE;
		local_name = ptr+1;
	}
	return dns_match_wildcard(filter_local_name, local_name) == 0;
}

static int config_filter_match_rest(const struct config_filter *mask,
				    const struct config_filter *filter)
{
	bool matched;
	int ret = 1;

	if (mask->local_name != NULL) {
		if (filter->local_name == NULL)
			ret = -1;
		else {
			T_BEGIN {
				matched = config_filter_match_local_name(mask, filter->local_name);
			} T_END;
			if (!matched)
				return 0;
		}
	}
	/* FIXME: it's not comparing full masks */
	if (mask->remote_bits != 0) {
		if (filter->remote_bits == 0)
			ret = -1;
		else if (!net_is_in_network(&filter->remote_net,
					    &mask->remote_net,
					    mask->remote_bits))
			return 0;
	}
	if (mask->local_bits != 0) {
		if (filter->local_bits == 0)
			ret = -1;
		else if (!net_is_in_network(&filter->local_net,
					    &mask->local_net, mask->local_bits))
			return 0;
	}
	return ret;
}

int config_filter_match_no_recurse(const struct config_filter *mask,
				   const struct config_filter *filter)
{
	int ret, ret2;

	if ((ret = config_filter_match_service(mask, filter)) == 0)
		return 0;
	if ((ret2 = config_filter_match_rest(mask, filter)) == 0)
		return 0;
	return ret > 0 && ret2 > 0 ? 1 : -1;
}

bool config_filter_match(const struct config_filter *mask,
			 const struct config_filter *filter)
{
	do {
		if (config_filter_match_no_recurse(mask, filter) <= 0)
			return FALSE;
		mask = mask->parent;
		filter = filter->parent;
	} while (mask != NULL && filter != NULL);
	return mask == NULL && filter == NULL;
}

bool config_filters_equal_no_recursion(const struct config_filter *f1,
				       const struct config_filter *f2)
{
	if (null_strcmp(f1->service, f2->service) != 0)
		return FALSE;

	if (f1->remote_bits != f2->remote_bits)
		return FALSE;
	if (!net_ip_compare(&f1->remote_net, &f2->remote_net))
		return FALSE;

	if (f1->local_bits != f2->local_bits)
		return FALSE;
	if (!net_ip_compare(&f1->local_net, &f2->local_net))
		return FALSE;

	if (null_strcasecmp(f1->local_name, f2->local_name) != 0)
		return FALSE;

	if (null_strcmp(f1->filter_name, f2->filter_name) != 0)
		return FALSE;
	if (f1->filter_name_array != f2->filter_name_array)
		return FALSE;
	return TRUE;
}

static bool
config_filters_equal_without_defaults(const struct config_filter *f1,
				      const struct config_filter *f2)
{
	if (!config_filters_equal_no_recursion(f1, f2))
		return FALSE;
	if (f1->parent != NULL || f2->parent != NULL) {
		/* Check the parents' compatibility also. However, it's
		   possible that one of these parents is the empty root filter,
		   while the other parent is NULL. These are actually equal. */
		return config_filters_equal_without_defaults(
			f1->parent != NULL ? f1->parent : &empty_filter,
			f2->parent != NULL ? f2->parent : &empty_filter);
	}
	return TRUE;
}

bool config_filters_equal(const struct config_filter *f1,
			  const struct config_filter *f2)
{
	if (f1->default_settings != f2->default_settings)
		return FALSE;

	/* For the rest of the settings don't check if the parents'
	   default_settings are equal. This makes it easier for callers to
	   do lookups with the wanted default_settings flag. */
	return config_filters_equal_without_defaults(f1, f2);
}

bool config_filter_is_empty(const struct config_filter *filter)
{
	return config_filters_equal(filter, &empty_filter);
}

bool config_filter_is_empty_defaults(const struct config_filter *filter)
{
	return config_filters_equal(filter, &empty_defaults_filter);
}
