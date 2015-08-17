/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Bert Vermeulen <bert@biot.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBSIGROK_HARDWARE_SCPI_DMM_PROTOCOL_H
#define LIBSIGROK_HARDWARE_SCPI_DMM_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "scpi.h"

#define LOG_PREFIX "scpi-dmm"

#define SCPI_DMM_MAX_DELAY_MS  600
#define MQ_CHECK_INTERVAL_MS   400
#define VALUE_INTERVAL_MS      100
#define VALUE_FETCH_TIMEOUT_MS 2000

/* TODO: merge into libsigrok-internal.h */
enum pps_scpi_cmds {
	SCPI_CMD_REMOTE = 1,
	SCPI_CMD_LOCAL,
	SCPI_CMD_GET_MQ,
	SCPI_CMD_GET_MEASUREMENT,
};

struct supported_mq {
	uint32_t mq;
	uint64_t mqflags;
};

struct scpi_dmm_channel {
	uint32_t mq;
	uint64_t mqflags;
};

struct scpi_dmm_channel_command {
	uint32_t channel_mask;
	int command;
	const char *string;
};

struct mq_string {
	uint32_t channel_mask;
	uint32_t mq;
	uint64_t mqflags;
	const char *set;
	const char *check;
};

struct scpi_dmm {
	const char *vendor;
	const char *model;
	unsigned int num_channels;
	uint64_t features;
	uint64_t quirks;
	const struct supported_mq *smq;
	const struct scpi_command *global_cmd;
	const struct scpi_dmm_channel_command *ch_cmd;
	const struct mq_string *mqstr;
};

/** Private, per-device-instance driver context. */
struct dev_context {
	/* Model-specific information */
	const struct scpi_dmm *device;

	/* Acquisition settings */
	void *cb_data;

	/* Operational state */
	struct scpi_dmm_channel *channels;
	struct sr_channel *cur_channel;

	/* Temporary state across callbacks */
	int64_t last_mq_check;
	int64_t last_value_fetch;
	int64_t value_incoming;
	int64_t last_input;
};

SR_PRIV extern const struct scpi_dmm dmm_profiles[];

SR_PRIV const char *dmm_get_vendor(const char *raw_vendor);
SR_PRIV const char *scpi_dmm_ch_cmd(const struct sr_dev_inst *sdi,
		struct sr_channel *ch, int cmd);
SR_PRIV int scpi_dmm_receive_data(int fd, int revents, void *cb_data);

#endif
