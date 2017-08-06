## CEC Implementation for LG TVs

This is a Consumer Electronics Control (CEC) implementation for LG TVs that
lack CEC support. It's meant to run on an Atmel ATtiny-45 AVR. The code
includes support for decoding IR commands from an NEC protocol remote, sending
and receiving commands to the TV via the RS-232 management port, and sending
and receiving CEC commands.

The code is currently somewhat customized for a single given configuration,
the one I have. Some limitations are that it passes through all remote keys
except power, mute, and volume up/down to the current CEC device rather than
to the TV. Additionally, the firmware only supports inputs that identify as
CEC devices.

## IR Interface

The IR interface is compatible with the NEC IR protocol:

http://www.sbprojects.com/knowledge/ir/nec.php

Support is implemented via as assembly interrupt service routine. An IR decoder
should be connected to INT0. The handler uses the jiffies value generated by
the UART code.

## UART Support

LG TVs have a 9600 baud management port. This port can be used to get TV
status, send remote control codes, and send commands. Because the firmware
must also implement CEC and IR support, UART support is offloaded to the USI
hardware. USI runs at 4 times the serial rate which allows the receive side
to properly align with the incoming asyncrounous signal. This allows up to 4
serial bit times between interrupt handler routines.

## CEC Support

CEC support is provided by the AVR-CEC library using the PWM transmit mode and
the software receive mode. The firmware sends appropriate keys (PLAY, PAUSE,
etc) as deck control commands and all others as CEC UI commands according to
a keymap stored in the EEPROM. The internal CEC logic supports one touch play
(remote device turns on TV and selects input), choosing a new source when an
inactive source message is received, processing routing changes, and responding
to active source messages.

## Bootloader

Sending a 16 byte vendor command with the final byte as 0xb1 causes the
firmware to enter bootloader mode. In this mode new firmware can be programmed
over the CEC bus via the cec_flash.py script.

## Keymap

A keymap between LG TV remote keys and CEC UI key codes is stored in the
EEPROM. Reflashing of the EEPROM can either be done through the programming
pins or via a special hexfile that embeds the keymap and programs it. After
running this hexfile, the device re-enters bootloader mode and the original
hexfile must be reloaded.