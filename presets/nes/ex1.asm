
	include "nesdefs.asm"

;;;;; VARIABLES

	seg.u RAM
	org $0

;;;;; NES CARTRIDGE HEADER

	NES_HEADER 0,2,1,0 ; mapper 0, 2 PRGs, 1 CHR, horiz. mirror

;;;;; START OF CODE

Start:
; wait for PPU warmup; clear CPU RAM
	NES_INIT	; set up stack pointer, turn off PPU
        jsr WaitSync	; wait for VSYNC
        jsr ClearRAM	; clear RAM
        jsr WaitSync	; wait for VSYNC (and PPU warmup)
; set palette and nametable VRAM
	jsr SetPalette	; set palette colors
        jsr HelloVRAM	; print message in name table
; reset PPU address and scroll registers
        lda #0
        sta PPU_ADDR
        sta PPU_ADDR	; PPU addr = $0000
        sta PPU_SCROLL
        sta PPU_SCROLL  ; PPU scroll = $0000
; activate PPU graphics
        lda #MASK_BG
        sta PPU_MASK 	; enable rendering
        lda #CTRL_NMI
        sta PPU_CTRL	; enable NMI
.endless
	jmp .endless	; endless loop

; fill video RAM with "Hello World" msg
HelloVRAM: subroutine
; set PPU address to name table A (row 1, col 1)
	PPU_SETADDR $2021
	ldy #0		; set Y counter to 0
.loop:
	lda HelloMsg,y	; get next character
        beq .end	; is 0? exit loop
	sta PPU_DATA	; store+advance PPU
        iny		; next character
	bne .loop	; loop
.end
        rts		; return to caller

; ASCII message to display on screen
HelloMsg:
	.byte "Hello, World!"
        .byte 0		; zero terminator

; set palette colors
SetPalette: subroutine
; set PPU address to palette start
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
	rti		; return from interrupt

;;;;; CONSTANT DATA

Palette:
	hex 1f		;screen color
	hex 01112100	;background 0
        hex 02122200	;background 1
        hex 02112100	;background 2
        hex 01122200	;background 3

;;;;; CPU VECTORS

	NES_VECTORS

;;;;; TILE SETS

	org $10000
        incbin "jroatch.chr"
