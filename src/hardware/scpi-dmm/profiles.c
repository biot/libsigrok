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
#include "protocol.h"

/* Used in channel masks */
#define CH1 (1 << 0)
#define CH2 (1 << 1)

/* Agilent 34405A */
static const struct supported_mq agilent_34405a_smq[] = {
	{ SR_MQ_VOLTAGE, SR_MQFLAG_AC },
	{ SR_MQ_VOLTAGE, SR_MQFLAG_DC },
	{ SR_MQ_VOLTAGE, SR_MQFLAG_DIODE },
	{ SR_MQ_CURRENT, SR_MQFLAG_AC },
	{ SR_MQ_CURRENT, SR_MQFLAG_DC },
	{ SR_MQ_RESISTANCE, 0 },
	{ SR_MQ_CAPACITANCE, 0 },
	{ SR_MQ_TEMPERATURE, 0 },
	{ SR_MQ_FREQUENCY, 0 },
	{ SR_MQ_CONTINUITY, 0 },
	ALL_ZERO
};

static const struct scpi_dmm_channel_command agilent_34405a_ch_cmd[] = {
	{ CH1, SCPI_CMD_GET_MQ, "CONFigure?" },
	{ CH1, SCPI_CMD_GET_MEASUREMENT, "READ?" },
	ALL_ZERO
};

static const struct mq_string agilent_34405a_mqstr[] = {
	{ CH1, SR_MQ_VOLTAGE, SR_MQFLAG_AC, "CONFigure:VOLTage:AC", "\"VOLT:AC " },
	{ CH1, SR_MQ_VOLTAGE, SR_MQFLAG_DC, "CONFigure:VOLTage:DC", "\"VOLT " },
	{ CH1, SR_MQ_VOLTAGE, SR_MQFLAG_DIODE, "CONFigure:DIODe", "\"DIOD" },
	{ CH1, SR_MQ_CURRENT, SR_MQFLAG_AC, "CONFigure:CURRent:AC", "\"CURR:AC " },
	{ CH1, SR_MQ_CURRENT, SR_MQFLAG_DC, "CONFigure:CURRent:DC", "\"CURR " },
	{ CH1, SR_MQ_RESISTANCE, 0, "CONFigure:RESistance", "\"RES " },
	{ CH1, SR_MQ_CAPACITANCE, 0, "CONFigure:CAPacitance", "\"CAP " },
	{ CH1, SR_MQ_TEMPERATURE, 0, "CONFigure:TEMPerature", "\"TEMP " },
	{ CH1, SR_MQ_FREQUENCY, 0, "CONFigure:FREQuency", "\"FREQ " },
	{ CH1, SR_MQ_CONTINUITY, 0, "CONFigure:CONTinuity", "\"CONT" },
	ALL_ZERO
};

/* Fluke 45 */
static const struct supported_mq fluke_45_smq[] = {
	{ SR_MQ_VOLTAGE, SR_MQFLAG_AC },
	{ SR_MQ_VOLTAGE, SR_MQFLAG_DC },
	{ SR_MQ_VOLTAGE, SR_MQFLAG_AC | SR_MQFLAG_DC },
	{ SR_MQ_VOLTAGE, SR_MQFLAG_DIODE },
	{ SR_MQ_CURRENT, SR_MQFLAG_AC },
	{ SR_MQ_CURRENT, SR_MQFLAG_DC },
	{ SR_MQ_CURRENT, SR_MQFLAG_AC | SR_MQFLAG_DC },
	{ SR_MQ_RESISTANCE, 0 },
	{ SR_MQ_FREQUENCY, 0 },
	{ SR_MQ_CONTINUITY, 0 },
	ALL_ZERO
};

static const struct scpi_dmm_channel_command fluke_45_ch_cmd[] = {
	{ CH1, SCPI_CMD_GET_MQ, "FUNC1?" },
	{ CH1, SCPI_CMD_GET_MEASUREMENT, "VAL1?" },
	{ CH2, SCPI_CMD_GET_MQ, "FUNC2?" },
	{ CH2, SCPI_CMD_GET_MEASUREMENT, "VAL2?" },
	ALL_ZERO
};

static const struct mq_string fluke_45_mqstr[] = {
	{ CH1, SR_MQ_VOLTAGE, SR_MQFLAG_AC, "VAC", "VAC" },
	{ CH2, SR_MQ_VOLTAGE, SR_MQFLAG_AC, "VAC2", "VAC" },
	{ CH1, SR_MQ_VOLTAGE, SR_MQFLAG_DC, "VDC", "VDC" },
	{ CH2, SR_MQ_VOLTAGE, SR_MQFLAG_DC, "VDC2", "VDC" },
	{ CH1, SR_MQ_VOLTAGE, SR_MQFLAG_AC | SR_MQFLAG_DC, "VACDC", "VACDC" },
	{ CH1, SR_MQ_VOLTAGE, SR_MQFLAG_DIODE, "DIODE", "DIODE" },
	{ CH2, SR_MQ_VOLTAGE, SR_MQFLAG_DIODE, "DIODE2", "DIODE" },
	{ CH1, SR_MQ_CURRENT, SR_MQFLAG_AC, "AAC", "AAC" },
	{ CH2, SR_MQ_CURRENT, SR_MQFLAG_AC, "AAC2", "AAC" },
	{ CH1, SR_MQ_CURRENT, SR_MQFLAG_DC, "ADC", "ADC" },
	{ CH2, SR_MQ_CURRENT, SR_MQFLAG_DC, "ADC2", "ADC" },
	{ CH1, SR_MQ_CURRENT, SR_MQFLAG_AC | SR_MQFLAG_DC, "AACDC", "AACDC" },
	{ CH1, SR_MQ_RESISTANCE, 0, "OHMS", "OHMS" },
	{ CH2, SR_MQ_RESISTANCE, 0, "OHMS2", "OHMS" },
	{ CH1, SR_MQ_FREQUENCY, 0, "FREQ", "FREQ" },
	{ CH2, SR_MQ_FREQUENCY, 0, "FREQ2", "FREQ" },
	{ CH1, SR_MQ_CONTINUITY, 0, "CONT", "CONT" },
	ALL_ZERO
};

SR_PRIV const struct scpi_dmm dmm_profiles[] = {
	{
		"Agilent", "34405A", 1, 0, 0,
		agilent_34405a_smq,
		NULL,
		agilent_34405a_ch_cmd,
		agilent_34405a_mqstr,
	},
	{
		"Fluke", "45", 2, 0,
		SCPI_QUIRK_GET_MQ | SCPI_QUIRK_FLUKE_PROMPT ,
		fluke_45_smq,
		NULL,
		fluke_45_ch_cmd,
		fluke_45_mqstr,
	},
	ALL_ZERO
};
