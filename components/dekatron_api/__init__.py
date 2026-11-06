# Copyright (c) 2026 Joseba Egia and Dekatron contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The HTTP API of the clock, served by the web server that ESPHome already runs."""

import esphome.codegen as cg
from esphome.components import light, number, select, switch, web_server_base
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = []
DEPENDENCIES = ["network"]
AUTO_LOAD = ["web_server_base", "json", "sha256", "socket", "ota"]

CONF_TUBES_ID = "tubes_id"

dekatron_api_ns = cg.esphome_ns.namespace("dekatron_api")
DekatronApi = dekatron_api_ns.class_("DekatronApi", cg.Component)
ElekstubeIps = cg.esphome_ns.namespace("elekstube_ips").class_("ElekstubeIps", cg.Component)

# The entities that the page can change, which are the same that Home Assistant sees, so that the two
# never disagree and what is saved stays saved. Each is optional: what is left out is not offered.
ENTITIES = {
    "displays": switch.Switch,
    "brightness": number.Number,
    "night": switch.Switch,
    "night_brightness": number.Number,
    "night_start": number.Number,
    "night_end": number.Number,
    "hour24": switch.Switch,
    "leading_zero": switch.Switch,
    "transition": select.Select,
    "leds": light.LightState,
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(DekatronApi),
        cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(web_server_base.WebServerBase),
        cv.Required(CONF_TUBES_ID): cv.use_id(ElekstubeIps),
        **{cv.Optional(f"{name}_id"): cv.use_id(kind) for name, kind in ENTITIES.items()},
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    base = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
    cg.add(var.set_web_server_base(base))
    tubes = await cg.get_variable(config[CONF_TUBES_ID])
    cg.add(var.set_tubes(tubes))
    for name in ENTITIES:
        if f"{name}_id" in config:
            entity = await cg.get_variable(config[f"{name}_id"])
            cg.add(getattr(var, f"set_{name}")(entity))
