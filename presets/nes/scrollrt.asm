
	include "nesdefs.asm"

;;;;; ZERO-PAGE VARIABLES

	seg.u ZEROPAGE
	org $0
        
SkipY	ds 1

;;;;; OTHER VARIABLES

	seg.u RAM
; page-align to prevent messing up the timing        
	org $300
LineXLo	ds 224
	align $100
LineXHi	ds 224

;;;;; NES CARTRIDGE HEADER

	NES_HEADER 0,2,1,0 ; mapper 0, 2 PRGs, 1 CHR, horiz. mirror

;;;;; START OF CODE

Start:
	NES_INIT	; set up stack pointer, turn off PPU
        jsr WaitSync	; wait for VSYNC
        jsr ClearRAM	; clear RAM
        jsr WaitSync	; wait for VSYNC (and PPU warmup)

	jsr SetPalette	; set palette colors
        jsr FillVRAM	; set PPU video RAM
        lda #0
        sta PPU_ADDR
        sta PPU_ADDR	; PPU addr = $0000
        sta PPU_SCROLL
        sta PPU_SCROLL  ; scroll = $0000
        lda #MASK_BG|MASK_SPR
        sta PPU_MASK 	; enable rendering
        lda #CTRL_NMI
        sta PPU_CTRL	; enable NMI
.endless
	jmp .endless	; endless loop

; fill video RAM
FillVRAM: subroutine
	PPU_SETADDR $2000
	ldy #$10
.loop:
	sta PPU_DATA
        adc #7
	inx
	bne .loop
	dey
	bne .loop
        rts

; set palette colors
SetPalette: subroutine
	PPU_SETADDR $3f00
	ldx #32
.loop:
	lda Palette,y
	sta PPU_DATA
        iny
	dex
	bne .loop
        rts

; set sprite 0
SetSprite0: subroutine
	sta $200	;y
        lda #$a0	;code
        sta $201
        lda #$20	;flags
        sta $202
        lda #$fe	;xpos
        sta $203
	rts

;;;;; COMMON SUBROUTINES

	include "nesppu.asm"

;;;;; INTERRUPT HANDLERS

NMIHandler: subroutine
	SAVE_REGS
        lda #0
        sta PPU_SCROLL
        sta PPU_SCROLL
        lda #111
        jsr SetSprite0
; load sprites
	lda #$02
        sta PPU_OAM_DMA
; wait for sprite 0
.wait0	bit PPU_STATUS
        bvs .wait0
.wait1	bit PPU_STATUS
        bvc .wait1
        ldy #0
.loop
; take out cycle every 4th line
	tya
        and #3
        bne .skipcyc	; clks = 2/3/3/3
.skipcyc
; pad out rest of scanline
	SLEEP 65
; alter horiz. scroll position for each scanline
	tya
        sec
	adc LineXLo,y
        sta LineXLo,y
        lda LineXHi,y
        adc #0
        sta LineXHi,y
        sta PPU_SCROLL	; horiz byte
        lda #0
        sta PPU_SCROLL	; vert byte
; next iteration of loop
        iny
        cpy #112
        bne .loop
        RESTORE_REGS
	rti

;;;;; CONSTANT DATA

	align $100
Palette:
	hex 1f		;background
	hex 09092c00	;bg0
        hex 09091900	;bg1
        hex 09091500	;bg2
        hex 09092500	;bg3

;;;;; CPU VECTORS

	NES_VECTORS

;;;;; TILE SETS

	org $10000
	incbin "jroatch.chr"
