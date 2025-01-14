
	include "nesdefs.asm"

;;;;; VARIABLES

	seg.u RAM
	org $0

ScrollX	byte	; used during NMI
ScrollY	byte	; used during NMI

;;;;; NES CARTRIDGE HEADER

	NES_HEADER 0,2,1,0 ; mapper 0, 2 PRGs, 1 CHR, horiz. mirror

;;;;; START OF CODE

Start:
	NES_INIT		; set up stack pointer, turn off PPU
        jsr WaitSync
        jsr WaitSync
        jsr ClearRAM
        jsr WaitSync		;wait for VSYNC
	jsr SetPalette		;set colors
        jsr FillVRAM		;set PPU RAM
        jsr WaitSync		;wait for VSYNC (and PPU warmup)
        lda #0
        sta PPU_ADDR
        sta PPU_ADDR		;PPU addr = 0
        sta PPU_SCROLL
        sta PPU_SCROLL		;scroll = 0
        lda #MASK_BG|MASK_SPR
        sta PPU_MASK 	; enable rendering
        lda #CTRL_NMI
        sta PPU_CTRL	; enable NMI
.endless
	jmp .endless		;endless loop

; fill video RAM
FillVRAM: subroutine
	PPU_SETADDR $2000
	ldy #$10
        ldx #0
.loop:
	stx PPU_DATA
	inx
	bne .loop
	dey
	bne .loop
        rts

; set palette colors
SetPalette: subroutine
	PPU_SETADDR $3f00
        ldy #0
.loop:
	lda Palette,y	; lookup byte in ROM
	sta PPU_DATA	; store byte to PPU data
        iny		; Y = Y + 1
        cpy #32		; is Y equal to 32?
	bne .loop	; not yet, loop
        rts		; return to caller


;;;;; COMMON SUBROUTINES

	include "nesppu.asm"

;;;;; INTERRUPT HANDLERS

NMIHandler:
	SAVE_REGS	; save registers
; update scroll position (must be done after VRAM updates)
	jsr ReadJoypad0	; read first controller
        pha		; push joypad bitmask
        and #$03	; only keep first 2 bits
        tay		; A -> Y
        lda ScrollDirTab,y	; lookup table
        clc
        adc ScrollX	; A = A + ScrollX
        sta ScrollX	; -> ScrollX
        sta PPU_SCROLL	; -> first scroll byte
        pla		; pop joypad bitmask
        lsr
        lsr
        and #$03	; take next two bits
        tay		
        lda ScrollDirTab,y ; do another lookup
        clc
        adc ScrollY	; A = A + ScrollY
        sta ScrollY	; -> ScrollY
        sta PPU_SCROLL	; -> second scroll byte
        RESTORE_REGS	; restore registers
	rti

; Scroll direction lookup table
ScrollDirTab:
	hex 00 01 ff 00	; 0,1,-1,0
 
;;;;; CONSTANT DATA

	align $100
Palette:
	hex 1f		;background
	hex 01112100	;background 0
        hex 02122200	;background 1
        hex 02112100	;background 2
        hex 01122200	;background 3
        hex 19293900	;sprite 0
        hex 1a2a3a00	;sprite 1
        hex 1b2b3b00	;sprite 2
        hex 1c2c3c00	;sprite 3

;;;;; CPU VECTORS

	NES_VECTORS

;;;;; TILE SETS

	org $10000
        incbin "jroatch.chr"
