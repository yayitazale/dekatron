import esphome.codegen as cg
from esphome.components import light, time as time_
from esphome.components.esp32 import include_builtin_idf_component
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TIME_ID

CODEOWNERS = []
DEPENDENCIES = ["esp32", "time"]

CONF_LEDS_ID = "leds_id"
CONF_X_OFFSET = "x_offset"
CONF_Y_OFFSET = "y_offset"
CONF_MADCTL = "madctl"

elekstube_ips_ns = cg.esphome_ns.namespace("elekstube_ips")
ElekstubeIps = elekstube_ips_ns.class_("ElekstubeIps", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ElekstubeIps),
        cv.Required(CONF_TIME_ID): cv.use_id(time_.RealTimeClock),
        cv.Required(CONF_LEDS_ID): cv.use_id(light.LightState),
        cv.Optional(CONF_X_OFFSET, default=52): cv.int_range(min=0, max=240),
        cv.Optional(CONF_Y_OFFSET, default=40): cv.int_range(min=0, max=320),
        cv.Optional(CONF_MADCTL, default=0x08): cv.hex_uint8_t,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    include_builtin_idf_component("esp_http_client")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    clock = await cg.get_variable(config[CONF_TIME_ID])
    cg.add(var.set_time(clock))
    leds = await cg.get_variable(config[CONF_LEDS_ID])
    cg.add(var.set_leds(leds))
    cg.add(var.set_x_offset(config[CONF_X_OFFSET]))
    cg.add(var.set_y_offset(config[CONF_Y_OFFSET]))
    cg.add(var.set_madctl(config[CONF_MADCTL]))
