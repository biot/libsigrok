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

#include "scpi.h"
#include "protocol.h"

SR_PRIV const char *scpi_dmm_ch_cmd(const struct sr_dev_inst *sdi,
		struct sr_channel *ch, int cmd)
{
	const struct scpi_dmm_channel_command *ch_cmd;
	struct dev_context *devc;
	uint32_t channel_mask;
	int i;
	const char *cmdstr;

	devc = sdi->priv;

	cmdstr = NULL;
	ch_cmd = devc->device->ch_cmd;
	channel_mask = 1 << ch->index;
	for (i = 0; ch_cmd[i].channel_mask; i++) {
		if ((ch_cmd[i].channel_mask & channel_mask) != channel_mask)
			continue;
		if (ch_cmd[i].command == cmd) {
			cmdstr = ch_cmd[i].string;
			break;
		}
	}
	if (!cmdstr) {
		sr_err("Unsupported measured quantity.");
		return NULL;
	}

	return cmdstr;
}

static void request_value(struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;
	const char *cmdstr;
	int64_t now;

	scpi = sdi->conn;
	devc = sdi->priv;
	cmdstr = scpi_dmm_ch_cmd(sdi, devc->cur_channel, SCPI_CMD_GET_MEASUREMENT);
	if (!cmdstr) {
		sr_err("Unsupported command for channel %s!",
				devc->cur_channel->name);
		return;
	}
	sr_scpi_send(scpi, cmdstr);
	now = g_get_monotonic_time() / 1000;
	devc->last_value_fetch = now;
	devc->value_incoming = now;
}

static void get_value(struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct dev_context *devc;
	float f;
	int ret, ch_idx;

	scpi = sdi->conn;
	devc = sdi->priv;

	if ((ret = sr_scpi_get_float(scpi, NULL, &f)) == SR_OK) {
		devc->last_input = g_get_monotonic_time() / 1000;

		packet.type = SR_DF_ANALOG;
		packet.payload = &analog;
		analog.channels = g_slist_append(NULL, devc->cur_channel);
		analog.num_samples = 1;
		ch_idx = devc->cur_channel->index;
		analog.mq = devc->channels[ch_idx].mq;
		analog.mqflags = devc->channels[ch_idx].mqflags;
		analog.data = &f;
		switch (devc->channels[ch_idx].mq) {
		case SR_MQ_VOLTAGE:
			analog.unit = SR_UNIT_VOLT;
			break;
		case SR_MQ_CURRENT:
			analog.unit = SR_UNIT_AMPERE;
			break;
		case SR_MQ_RESISTANCE:
			analog.unit = SR_UNIT_OHM;
			break;
		case SR_MQ_CAPACITANCE:
			analog.unit = SR_UNIT_FARAD;
			break;
		case SR_MQ_TEMPERATURE:
			analog.unit = SR_UNIT_CELSIUS;
			break;
		case SR_MQ_FREQUENCY:
			analog.unit = SR_UNIT_HERTZ;
			break;
		case SR_MQ_CONTINUITY:
			analog.unit = SR_UNIT_BOOLEAN;
			break;
		}
		sr_session_send(sdi, &packet);
		g_slist_free(analog.channels);
	} else if (ret == SR_ERR_TIMEOUT) {
		/* TODO: keep? */
	}

	/* Switch to next channel. */
	devc->cur_channel = sr_next_enabled_channel(sdi, devc->cur_channel);

	devc->value_incoming = 0;
}

static void get_mq(struct sr_dev_inst *sdi, int64_t now)
{
	struct sr_channel *ch;
	struct sr_channel_group *cg;
	struct dev_context *devc;
	GSList *l;
	GVariant *gvar;

	devc = sdi->priv;
	for (l = sdi->channel_groups; l; l = l->next) {
		cg = l->data;
		ch = cg->channels->data;
		if (!ch->enabled)
			continue;
		if (sdi->driver->config_get(SR_CONF_MEASURED_QUANTITY, &gvar,
				sdi, cg) == SR_OK) {
			g_variant_unref(gvar);
			devc->last_input = now;
		}
	}
	devc->last_mq_check = now;
}

SR_PRIV int scpi_dmm_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;
	int64_t now;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (revents == G_IO_IN)
		get_value(sdi);

	scpi = sdi->conn;
	devc = sdi->priv;

	now = g_get_monotonic_time() / 1000;
	if (!devc->last_input || now - devc->last_input < VALUE_FETCH_TIMEOUT_MS) {
		/*
		 * Some devices don't lock out the keypad, so the user may switch MQ at
		 * any point. Make sure to check it occasionally.
		 */
		if (devc->device->quirks & SCPI_QUIRK_GET_MQ && !devc->value_incoming) {
			now = g_get_monotonic_time() / 1000;
			 if (now - devc->last_mq_check > MQ_CHECK_INTERVAL_MS) {
			 	get_mq(sdi, now);
			 }
		}

		if (!devc->value_incoming && now - devc->last_value_fetch > VALUE_INTERVAL_MS)
			request_value(sdi);
	} else {
		/* The hardware stopped responding. */
		sr_dbg("whoa!----------------------------------------");
		sr_scpi_send(scpi, "*CLS");
	}

	return TRUE;
}
