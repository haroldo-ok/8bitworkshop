
	include "nesdefs.asm"

;;;;; VARIABLES

	seg.u RAM
	org $0

ScrollPos	word	; used during NMI

;;;;; NES CARTRIDGE HEADER

	NES_HEADER 0,2,1,1 ; mapper 0, 2 PRGs, 1 CHR, vert. mirror

;;;;; START OF CODE

Start:
; wait for PPU warmup; clear CPU RAM
	NES_INIT	; set up stack pointer, turn off PPU
        jsr WaitSync	; wait for VSYNC
        jsr ClearRAM	; clear RAM
        jsr WaitSync	; wait for VSYNC (and PPU warmup)
; set palette and nametable VRAM
	jsr SetPalette	; set palette colors
        jsr FillVRAM	; set PPU video RAM
; reset PPU address and scroll registers
        lda #0
        sta PPU_ADDR
        sta PPU_ADDR	; PPU addr = $0000
        sta PPU_SCROLL
        sta PPU_SCROLL  ; scroll = $0000
; activate PPU graphics
        lda #MASK_BG
        sta PPU_MASK 	; enable rendering
        lda #CTRL_NMI
        sta PPU_CTRL	; enable NMI
.endless
	jmp .endless	; endless loop

; fill video RAM
FillVRAM: subroutine
	PPU_SETADDR $2000
	ldy #$8		; set $8 pages ($800 bytes)
.loop:
	lda PageData,y	; A -> PageData[Y]
	sta PPU_DATA	; A -> PPU data port
	inx		; X = X + 1
	bne .loop	; repeat until 256 bytes
	dey		; Y = Y - 1
	bne .loop	; repeat until Y is 0
        rts		; return to caller

PageData:
	hex 00
	hex 42424242	; 'B'
	hex 41414141	; 'A'

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
; save registers
	SAVE_REGS
; update scroll position (must be done after VRAM updates)
	inc ScrollPos	; increment low byte
        bne .noinc	; Z flag set if wrapped to 0
        inc ScrollPos+1	; increment high byte
.noinc
; store X and Y scroll position
        lda ScrollPos	; A -> low byte
        sta PPU_SCROLL	; set horiz scroll
        lda #0		; A -> zero
        sta PPU_SCROLL	; set vert scroll
; store 8th bit into name table selector
; name table A or B ($2000 or $2400)
	lda ScrollPos+1	; load high byte
        and #1		; select its low bit
	ora #CTRL_NMI	; set rest of bits
        sta PPU_CTRL
; restore registers, return from interrupt
        RESTORE_REGS
	rti

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

