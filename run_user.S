        .section        .text, "ax", @progbits
	.org	BOOTLOADER_ADDRESS-4
	rjmp	__init
	nop
