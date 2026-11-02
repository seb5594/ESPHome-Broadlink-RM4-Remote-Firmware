"""Sets the Wi-Fi MAC address of an RTL87xx module.

Some modules, among them the BL3357 in Broadlink RM4 devices, have no MAC
address in their eFuse. The Realtek driver then falls back to the same built-in
address (00:e0:4c:b7:23:00) on every unit, so two devices on one network share
a MAC and, through DHCP, an IP address. This component gives the driver the
address you configure before Wi-Fi starts.

The address only lives in RAM: nothing is written to the chip, and a reset
undoes everything. See rtl87xx_mac.cpp for how it reaches the driver.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_MAC_ADDRESS

CODEOWNERS = ["@seb5594"]

rtl87xx_mac_ns = cg.esphome_ns.namespace("rtl87xx_mac")
Rtl87xxMac = rtl87xx_mac_ns.class_("Rtl87xxMac", cg.Component)


def validate_station_address(value):
    """The driver replaces all-zero addresses and multicast ones are invalid."""
    mac = cv.mac_address(value)
    if not any(mac.parts):
        raise cv.Invalid("The MAC address must not be all zeros")
    if mac.parts[0] & 0x01:
        raise cv.Invalid(
            "The first byte of a device MAC address must be even, "
            f"{mac.parts[0]:02x} marks a multicast address"
        )
    return mac


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Rtl87xxMac),
            cv.Required(CONF_MAC_ADDRESS): validate_station_address,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on(["rtl87xx"]),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_mac_address(cg.ArrayInitializer(*config[CONF_MAC_ADDRESS].parts)))
