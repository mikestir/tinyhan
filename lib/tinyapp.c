/*
 * tinyapp.c
 *
 *  Created on: 7 Dec 2014
 *      Author: mike
 */

#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "common.h"
#include "tinyapp.h"

/*
 * Conversion tables for mapping TAP profiles and data items
 * to MQTT/JSON.
 */

const tinyapp_item_info_t tinyapp_item_info_status[] = {
		{ "battery", tinyappType_Unsigned },
		{ "runtime", tinyappType_Unsigned },
		{ "coord_rssi", tinyappType_Signed },
		{ NULL },
};

const tinyapp_item_info_t tinyapp_item_info_devinfo[] = {
		{ "type", tinyappType_Unsigned },
		{ "name", tinyappType_String },
		{ "update_rate", tinyappType_Unsigned },
		{ NULL },
};

const tinyapp_item_info_t tinyapp_item_info_generic[] = {
		{ "ana0", tinyappType_Signed },
		{ "ana1", tinyappType_Signed },
		{ "ana2", tinyappType_Signed },
		{ "ana3", tinyappType_Signed },
		{ "ana4", tinyappType_Signed },
		{ "ana5", tinyappType_Signed },
		{ "ana6", tinyappType_Signed },
		{ "ana7", tinyappType_Signed },
		{ "digin", tinyappType_Unsigned },
		{ "digout", tinyappType_Unsigned },
		{ "floattest", tinyappType_SingleFloat },
		{ "doubletest", tinyappType_DoubleFloat },
		{ NULL },
};

const tinyapp_item_info_t tinyapp_item_info_env[] = {
		{ "temperature", tinyappType_Signed },
		{ "rh", tinyappType_Unsigned },
		{ "visible_lux", tinyappType_Unsigned },
		{ "uv_lux", tinyappType_Unsigned },
		{ "ir_lux", tinyappType_Unsigned },
		{ "ph", tinyappType_Signed },
		{ "level", tinyappType_Signed },
		{ "wind_speed_avg", tinyappType_Unsigned },
		{ "wind_speed_gust", tinyappType_Unsigned },
		{ "wind_direction", tinyappType_Unsigned },
		{ "rainfall", tinyappType_Unsigned },
		{ "pressure", tinyappType_Signed },
		{ "spl_rms", tinyappType_Signed },
		{ "spl_peak", tinyappType_Signed },
		{ NULL },
};

const tinyapp_item_info_t tinyapp_item_info_hvac[] = {
		{ "temperature", tinyappType_Signed },
		{ "target", tinyappType_Signed },
		{ "heat_demand", tinyappType_Unsigned },
		{ "cool_demand", tinyappType_Unsigned },
		{ "flow_temp", tinyappType_Signed },
		{ "return_temp", tinyappType_Signed },
		{ "fan_speed", tinyappType_Unsigned },
		{ "burner_power", tinyappType_Unsigned },
		{ NULL },
};

const tinyapp_item_info_t tinyapp_item_info_energy[] = {
		{ NULL },
};

const tinyapp_item_info_t tinyapp_item_info_ha[]  = {
		{ NULL },
};

/* Map profiles to MQTT topics.  Must be in the same order
 * as \see tinyapp_profile_t */
const char* tinyapp_profile_tags[] = {
		"status",
		"info",
		"generic",
		"env",
		"hvac",
		"energy",
		"ha",
};

/* Map device types to textual description.  Must be in the same order
 * as \see tinyapp_nodetype_t */
const char* tinyapp_nodetype_str[] = {
		"Generic",
		"Generic Sensor",
		"Generic Actuator",
		"Environmental Sensor",
		"Heating Control",
		"Lighting Control",
		"Energy Monitoring",
		"Occupancy Detector",
		"Switch",
};

/* Map profiles to item tables.  Must be in the same order
 * as \see tinyapp_profile_t */
const tinyapp_item_info_t* tinyapp_profile_items[] = {
		tinyapp_item_info_status,
		tinyapp_item_info_devinfo,
		tinyapp_item_info_generic,
		tinyapp_item_info_env,
		tinyapp_item_info_hvac,
		tinyapp_item_info_energy,
		tinyapp_item_info_ha,
};

/*****************/
/* Serialisation */
/*****************/

void tinyapp_init(tinyapp_t *ctx, uint8_t flags, uint8_t *buf, size_t size)
{
	ctx->packet_start = ctx->packet_ptr = buf;
	ctx->packet_end = &buf[size];

	*ctx->packet_ptr++ = flags;
}

void tinyapp_serialise8(tinyapp_t *ctx, uint8_t type, uint8_t value)
{
	assert(ctx->packet_end >= ctx->packet_ptr + 2);

	*ctx->packet_ptr++ = TINYAPP_LENGTH_1 | type;
	*ctx->packet_ptr++ = value;
}

void tinyapp_serialise16(tinyapp_t *ctx, uint8_t type, uint16_t value)
{
	assert(ctx->packet_end >= ctx->packet_ptr + 3);

	*ctx->packet_ptr++ = TINYAPP_LENGTH_2 | type;
	*((uint16_t*)ctx->packet_ptr) = value;
	ctx->packet_ptr += 2;
}

void tinyapp_serialise32(tinyapp_t *ctx, uint8_t type, uint32_t value)
{
	assert(ctx->packet_end >= ctx->packet_ptr + 5);

	*ctx->packet_ptr++ = TINYAPP_LENGTH_4 | type;
	*((uint32_t*)ctx->packet_ptr) = value;
	ctx->packet_ptr += 4;
}

void tinyapp_serialise_string(tinyapp_t *ctx, uint8_t type, const char *str, size_t len)
{
	assert(len < 256);
	assert(ctx->packet_end >= ctx->packet_ptr + 2 + len);

	*ctx->packet_ptr++ = TINYAPP_LENGTH_VAR | type;
	*ctx->packet_ptr++ = (uint8_t)len;
	memcpy(ctx->packet_ptr, str, len);
	ctx->packet_ptr += len;
}

/*******************/
/* Deserialisation */
/*******************/

void tinyapp_deserialise(const uint8_t *buf, size_t size, tinyapp_item_cb_t cb)
{
	const uint8_t *ptr = buf;
	size_t len = 0;
	uint8_t profile = 0; /* default profile */

	/* Skip flags byte */
	ptr++; size--;

	while ((int)size > 0) {
		uint8_t id = *ptr & TINYAPP_ITEM_ID_MASK;
		uint8_t type = *ptr & TINYAPP_LENGTH_MASK;
		ptr++; size--;

		switch (type) {
		case TINYAPP_LENGTH_1:
			len = 1;
			break;
		case TINYAPP_LENGTH_2:
			len = 2;
			break;
		case TINYAPP_LENGTH_4:
			len = 4;
			break;
		case TINYAPP_LENGTH_VAR:
			/* Variable length - consume another byte */
			len = (size_t)*ptr;
			ptr++; size--;
			break;
		}

		if (id == TINYAPP_ITEM_ID_PROFILE) {
			/* Decode profile selection */
			/* TODO: Could handle profile numbers > 8 bits */
			TRACE("New profile %u\n", (unsigned int)*ptr);
			profile = *ptr;
		} else {
			/* Everything else gets routed to callback */
			cb(profile, id, (void*)ptr, len);
		}

		/* Consume data */
		ptr += len; size -= len;
	}
}

/*******************/
/* Table Utilities */
/*******************/

int tinyapp_get_profile_id(const char *tag)
{
	int n;

	for (n = 0; n < ARRAY_SIZE(tinyapp_profile_tags); n++) {
		if (strcasecmp(tag, tinyapp_profile_tags[n]) == 0) {
			return n;
		}
	}
	return -1; /* not found */
}

const char* tinyapp_get_profile_tag(uint8_t profile)
{
	/* Return pointer to profile tag name or NULL if profile ID out of range */
	return (profile < ARRAY_SIZE(tinyapp_profile_tags)) ? tinyapp_profile_tags[profile] : NULL;
}

int tinyapp_get_item_id(uint8_t profile, const char *tag)
{
	const tinyapp_item_info_t *item_info;
	int n;

	if (profile < ARRAY_SIZE(tinyapp_profile_tags)) {
		item_info = tinyapp_profile_items[profile];
		for (n = 0; item_info->tag; item_info++, n++) {
			if (strcasecmp(tag, item_info->tag) == 0) {
				return n;
			}
		}
	}

	return -1; /* not found */
}

const tinyapp_item_info_t* tinyapp_get_item_info(uint8_t profile, uint8_t item)
{
	/* FIXME: Validate item index, currently requires walking the table */
	return (profile < ARRAY_SIZE(tinyapp_profile_items)) ? &tinyapp_profile_items[profile][item] : NULL;
}
