"""The HTTP API of the clock, served by the web server that ESPHome already runs."""

import esphome.codegen as cg
from esphome.components import web_server_base
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = []
DEPENDENCIES = ["network"]
AUTO_LOAD = ["web_server_base", "json", "sha256", "socket"]

CONF_TUBES_ID = "tubes_id"

opentube_api_ns = cg.esphome_ns.namespace("opentube_api")
OpenTubeApi = opentube_api_ns.class_("OpenTubeApi", cg.Component)
ElekstubeIps = cg.esphome_ns.namespace("elekstube_ips").class_("ElekstubeIps", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OpenTubeApi),
        cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(web_server_base.WebServerBase),
        cv.Required(CONF_TUBES_ID): cv.use_id(ElekstubeIps),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    base = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
    cg.add(var.set_web_server_base(base))
    tubes = await cg.get_variable(config[CONF_TUBES_ID])
    cg.add(var.set_tubes(tubes))
