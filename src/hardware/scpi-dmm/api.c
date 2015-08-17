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

#include <string.h>
#include <strings.h>
#include "scpi.h"
#include "protocol.h"

SR_PRIV struct sr_dev_driver scpi_dmm_driver_info;

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_MULTIMETER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS | SR_CONF_SET,
	SR_CONF_MEASURED_QUANTITY | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static int init(struct sr_dev_driver *di, struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static struct sr_dev_inst *probe_device(struct sr_scpi_dev_inst *scpi)
{
	struct sr_dev_inst *sdi;
	struct sr_channel *ch;
	struct sr_channel_group *cg;
	struct dev_context *devc;
	struct sr_scpi_hw_info *hw_info;
	const struct scpi_dmm *device;
	GRegex *model_re;
	GMatchInfo *model_mi;
	unsigned int enabled, i;
	const char *vendor;
	char chname[16];

	if (sr_scpi_get_hw_id(scpi, &hw_info) != SR_OK) {
		sr_info("Couldn't get IDN response.");
		return NULL;
	}

	device = NULL;
	for (i = 0; dmm_profiles[0].vendor; i++) {
		vendor = sr_vendor_alias(hw_info->manufacturer);
		if (strcasecmp(vendor, dmm_profiles[i].vendor))
			continue;
		model_re = g_regex_new(dmm_profiles[i].model, 0, 0, NULL);
		if (g_regex_match(model_re, hw_info->model, 0, &model_mi))
			device = &dmm_profiles[i];
		g_match_info_unref(model_mi);
		g_regex_unref(model_re);
		if (device)
			break;
	}
	if (!device) {
		sr_scpi_hw_info_free(hw_info);
		return NULL;
	}

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup(vendor);
	sdi->model = g_strdup(hw_info->model);
	sdi->version = g_strdup(hw_info->firmware_version);
	sdi->conn = scpi;
	sdi->driver = &scpi_dmm_driver_info;
	sdi->inst_type = SR_INST_SCPI;
	sdi->serial_num = g_strdup(hw_info->serial_number);
	scpi->quirks = device->quirks;

	devc = g_malloc0(sizeof(struct dev_context));
	devc->device = device;
	sdi->priv = devc;

	enabled = TRUE;
	for (i = 0; i < device->num_channels; i++) {
		snprintf(chname, sizeof(chname), "DI%d", i + 1);
		ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, enabled, chname);
		cg = g_malloc0(sizeof(struct sr_channel_group));
		cg->name = g_strdup(chname);
		cg->channels = g_slist_append(cg->channels, ch);
		sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);
		/* Enable only the first channel (primary display) by default */
		enabled = FALSE;
	}
	devc->channels = g_malloc0(sizeof(struct scpi_dmm_channel) * device->num_channels);

	sr_scpi_hw_info_free(hw_info);
	hw_info = NULL;

	scpi_cmd(sdi, devc->device->global_cmd, SCPI_CMD_LOCAL);

	return sdi;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	return sr_scpi_scan(di->context, options, probe_device);
}

static GSList *dev_list(const struct sr_dev_driver *di)
{
	return ((struct drv_context *)(di->context))->instances;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;

	if (sdi->status != SR_ST_INACTIVE)
		return SR_ERR;

	scpi = sdi->conn;
	if (sr_scpi_open(scpi) < 0)
		return SR_ERR;

	sdi->status = SR_ST_ACTIVE;

	devc = sdi->priv;
	scpi_cmd(sdi, devc->device->global_cmd, SCPI_CMD_REMOTE);

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;
	scpi = sdi->conn;
	if (scpi) {
		scpi_cmd(sdi, devc->device->global_cmd, SCPI_CMD_LOCAL);
		sr_scpi_close(scpi);
		sdi->status = SR_ST_INACTIVE;
	}

	return SR_OK;
}

static int cleanup(const struct sr_dev_driver *di)
{
	return std_dev_clear(di, NULL);
}

/*
 * If no channel is selected, the first channel is used -- the primary display
 * on a bench DMM. Channel groups always have exactly one channel in this
 * driver, so we can safely dereference them like that.
 */
static uint32_t get_channel_mask(const struct sr_channel_group *cg)
{
	struct sr_channel *ch;
	uint32_t mask;

	if (cg) {
		ch = cg->channels->data;
		mask = 1 << ch->index;
	} else {
		mask = 1 << 0;
	}

	return mask;
}

static int config_get(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct sr_channel *ch;
	struct sr_scpi_dev_inst *scpi;
	const struct mq_string *mqstr;
	struct dev_context *devc;
	GVariant *gmq[2];
	uint32_t mq, channel_mask;
	uint64_t mqflags;
	int ret, i;
	const char *cmdstr;
	char *s;

	if (!sdi)
		return SR_ERR_ARG;

	if (key != SR_CONF_MEASURED_QUANTITY)
		return SR_ERR_NA;

	if (!cg)
		return SR_ERR_ARG;

	scpi = sdi->conn;
	devc = sdi->priv;
	ch = cg->channels->data;

	cmdstr = scpi_dmm_ch_cmd(sdi, ch, SCPI_CMD_GET_MQ);
	ret = sr_scpi_get_string(scpi, cmdstr, &s);
	if (ret != SR_OK)
		return ret;
	if (scpi->last_status != SR_OK)
		return scpi->last_status;

	mq = 0;
	mqflags = 0;
	mqstr = devc->device->mqstr;
	channel_mask = get_channel_mask(cg);
	for (i = 0; mqstr[i].mq; i++) {
		if ((mqstr[i].channel_mask & channel_mask) != channel_mask)
			continue;
		if (strcmp(mqstr[i].check, s))
			continue;
		mq = mqstr[i].mq;
		mqflags = mqstr[i].mqflags;
		break;
	}
	if (!mq) {
		sr_dbg("Unknown MQ string.");
		return SR_ERR_DATA;
	}

	/* Keep this around for the next time a value comes in. */
	devc->channels[ch->index].mq = mq;
	devc->channels[ch->index].mqflags = mqflags;

	gmq[0] = g_variant_new_uint32(mq);
	gmq[1] = g_variant_new_uint64(mqflags);
	*data = g_variant_new_tuple(gmq, 2);

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct sr_scpi_dev_inst *scpi;
	const struct mq_string *mqstr;
	struct dev_context *devc;
	uint32_t mq, channel_mask;
	uint64_t mqflags;
	int channel_num, ret, i;
	const char *cmd;
	char *response;

	if (!sdi)
		return SR_ERR_ARG;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (key != SR_CONF_MEASURED_QUANTITY)
		return SR_ERR_NA;

	if (!cg)
		return SR_ERR_ARG;

	if (!g_variant_is_of_type(data, G_VARIANT_TYPE_TUPLE)
			||  g_variant_n_children(data) != 2)
		return SR_ERR_ARG;

	scpi = sdi->conn;
	devc = sdi->priv;
	channel_mask = get_channel_mask(cg);

	g_variant_get(data, "(ut)", &mq, &mqflags);

	cmd = NULL;
	mqstr = devc->device->mqstr;
	for (i = 0; mqstr[i].mq; i++) {
		if ((mqstr[i].channel_mask & channel_mask) != channel_mask)
			continue;
		if (mqstr[i].mq == mq && mqstr[i].mqflags == mqflags) {
			cmd = mqstr[i].set;
			break;
		}
	}
	if (!cmd) {
		sr_err("Unsupported measured quantity.");
		return SR_ERR_ARG;
	}

	ret = sr_scpi_get_string(scpi, cmd, &response);

	if (cg)
		channel_num = ((struct sr_channel *)cg->channels->data)->index;
	else
		channel_num = 0;
	devc->channels[channel_num].mq = mq;
	devc->channels[channel_num].mqflags = mqflags;

	return ret;
}

static int config_list(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	GVariant *gvar, *smq[2];
	GVariantBuilder gvb;
	unsigned int i;

	(void)cg;

	/* Always available, even without sdi. */
	if (key == SR_CONF_SCAN_OPTIONS) {
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				scanopts, ARRAY_SIZE(scanopts), sizeof(uint32_t));
		return SR_OK;
	} else if (key == SR_CONF_DEVICE_OPTIONS) {
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
		return SR_OK;
	}

	if (!sdi)
		return SR_ERR_ARG;

	if (key != SR_CONF_MEASURED_QUANTITY)
		return SR_ERR_NA;

	devc = sdi->priv;

	g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
	for (i = 0; devc->device->smq[i].mq; i++) {
		smq[0] = g_variant_new_uint32(devc->device->smq[i].mq);
		smq[1] = g_variant_new_uint64(devc->device->smq[i].mqflags);
		gvar = g_variant_new_tuple(smq, 2);
		g_variant_builder_add_value(&gvb, gvar);
	}
	*data = g_variant_builder_end(&gvb);

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi, void *cb_data)
{
	struct sr_channel *ch;
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;
	int ret;
	// const char *cmdstr;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;
	scpi = sdi->conn;
	devc->cb_data = cb_data;

	if ((ret = sr_scpi_source_add(sdi->session, scpi, G_IO_IN, 10,
			scpi_dmm_receive_data, (void *)sdi)) != SR_OK)
		return ret;
	std_session_send_df_header(sdi, LOG_PREFIX);

	ch = sr_next_enabled_channel(sdi, NULL);
	devc->cur_channel = ch;

	/* Prime the pipe with the first channel's fetch. */
	// TODO:
	// cmdstr = scpi_dmm_ch_cmd(sdi, ch, SCPI_CMD_GET_MEASUREMENT);
	// if (!cmdstr) {
	// 	sr_err("Unsupported command!");
	// 	return SR_ERR_BUG;
	// }
	// ret = sr_scpi_send(scpi, cmdstr);
	// if (ret != SR_OK)
	// 	return SR_OK;

	return ret;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct sr_datafeed_packet packet;
	struct sr_scpi_dev_inst *scpi;
	float f;

	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	scpi = sdi->conn;

	/*
	 * A requested value is certainly on the way. Retrieve it now,
	 * to avoid leaving the device in a state where it's not expecting
	 * commands.
	 */
	sr_scpi_get_float(scpi, NULL, &f);

	sr_scpi_source_remove(sdi->session, scpi);

	packet.type = SR_DF_END;
	sr_session_send(sdi, &packet);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver scpi_dmm_driver_info = {
	.name = "scpi-dmm",
	.longname = "SCPI DMM",
	.api_version = 1,
	.init = init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = dev_list,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
