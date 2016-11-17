# 9600 * 4 * 8 * 52
F_CPU = 15974400

DEVICE = attiny45

BOOTLOADER_ADDRESS = ec0

FUSE_L = 0xe1
FUSE_H = 0xd3
FUSE_E = 0xfe
FUSEOPT = -U lfuse:w:$(FUSE_L):m -U hfuse:w:$(FUSE_H):m -U efuse:w:$(FUSE_W):m


#---------------------------------------------------------------------
# ATtiny45
#---------------------------------------------------------------------
# Fuse extended byte:
# 0xFE = - - - -   - 1 1 0
#                        ^
#                        |
#                        +---- SELFPRGEN (enable self programming flash)
#
# Fuse high byte:
# 0xd3 = 1 1 0 1   0 1 0 1
#        ^ ^ ^ ^   ^ \-+-/
#        | | | |   |   +------ BODLEVEL 2..0 (brownout trigger level -> 2.7V)
#        | | | |   +---------- EESAVE (preserve EEPROM on Chip Erase -> preserved)
#        | | | +-------------- WDTON (watchdog timer always on -> disable)
#        | | +---------------- SPIEN (enable serial programming -> enabled)
#        | +------------------ DWEN (debug wire enable)
#        +-------------------- RSTDISBL (disable external reset -> enabled)
#
# Fuse low byte:
# 0xe1 = 1 1 1 0   0 0 0 1
#        ^ ^ \+/   \--+--/
#        | |  |       +------- CKSEL 3..0 (clock selection -> HF PLL)
#        | |  +--------------- SUT 1..0 (BOD enabled, fast rising power)
#        | +------------------ CKOUT (clock output on CKOUT pin -> disabled)
#        +-------------------- CKDIV8 (divide clock by 8 -> don't divide)

###############################################################################

