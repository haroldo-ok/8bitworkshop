
	include "nesdefs.asm"

;;;;; ZERO-PAGE VARIABLES

	seg.u ZEROPAGE
	org $0
        
Ptr1		.word	; for temporary pointer storage

TrackFrac	.byte	; fractional position along track
Speed		.byte	; speed of car
TimeOfDay	.word	; 16-bit time of day counter
; Variables for preprocessing step
XPos		.word	; 16-bit X position
XVel		.word	; 16-bit X velocity
TPos		.word	; 16-bit track position
TrackLookahead	.byte	; current fractional track increment
; Variables for track generation
Random		.byte	; random counter
GenTarget	.byte	; target of current curve
GenDelta	.byte	; curve increment 
GenCur		.byte	; current curve value

ZOfs		.byte	; counter to draw striped center line
Weather		.byte	; bitmask for weather
Heading		.word	; sky scroll pos.

NumRoadScanlines = 112
NumRoadSegments = 56
; Preprocessing result: X positions for all track segments 
RoadX0		ds NumRoadSegments

; Generated track curve data
TrackLen	equ 5
TrackData	ds TrackLen

InitialSpeed	equ 10	; starting speed

;;;;; OTHER VARIABLES

	seg.u RAM
	org $300

;;;;; NES CARTRIDGE HEADER

	NES_HEADER 0,2,1,1 ; mapper 0, 2 PRGs, 1 CHR, vert mirror

;;;;; START OF CODE

Start:
	NES_INIT	; set up stack pointer, turn off PPU
        jsr WaitSync	; wait for VSYNC
        jsr ClearRAM	; clear RAM
        jsr WaitSync	; wait for VSYNC (and PPU warmup)

        jsr FillVRAM	; set PPU video RAM

        jsr RoadSetup

	lda #0
        sta PPU_ADDR
        sta PPU_ADDR	; PPU addr = $0000
        sta PPU_SCROLL
        sta PPU_SCROLL  ; scroll = $0000
        lda #MASK_BG|MASK_SPR|MASK_BG_CLIP
        sta PPU_MASK 	; enable rendering (first!)
        lda #CTRL_NMI
        sta PPU_CTRL	; enable NMI
        
.endless
	jmp .endless	; endless loop

;;;;; ROAD SUBS

RoadSetup: subroutine
        lda #1
        sta Random
        lda #InitialSpeed
        sta Speed
        lda #0
        sta TimeOfDay+1
        lda #0
        sta Weather
	rts

RoadPreSetup: subroutine
; Set up some values for road curve computation,
; since we have some scanline left over.
	lda #0
        sta XVel
        sta XVel+1
        sta XPos
        sta XPos+1
        lda TrackFrac
        sta TPos
        lda #0
        sta TPos+1
        lda #10		; initial lookahead
        sta TrackLookahead
	rts

RoadPostFrame: subroutine
; Advance position on track
; TrackFrac += Speed
	lda TrackFrac
        clc
        adc Speed
        sta TrackFrac
        bcc .NoGenTrack ; addition overflowed?
        jsr GenTrack	; yes, generate new track segment
.NoGenTrack
; TimeOfDay += 1
        inc TimeOfDay
        bne .NoTODInc
        inc TimeOfDay+1
        lda TimeOfDay+1
; See if it's nighttime yet, and if the stars come out
        clc
        adc #8
        and #$3f
        cmp #$35
        ror
        sta Weather
.NoTODInc
	lda Heading
        sec
        sbc XPos+1
        sta Heading
        bit XPos+1
        bmi .NegHeading
        lda Heading+1
        sbc #0
        sta Heading+1
        rts
.NegHeading
        lda Heading+1
        sbc #$ff
        sta Heading+1
	rts

; Compute road curve from bottom of screen to horizon.
PreprocessCurve subroutine
	ldx #NumRoadSegments-1
.CurveLoop
; Modify X position
; XPos += XVel (16 bit add)
	lda XPos
        clc
        adc XVel
        sta XPos
        lda XPos+1
        adc XVel+1
        sta XPos+1
        sta RoadX0,x	; store in RoadX0 array
; Modify X velocity (slope)
; XVel += TrackData[TPos]
        ldy TPos+1
        lda TrackData,y
        clc		; clear carry for ADC
        bmi .CurveLeft	; track slope negative?
        adc XVel
        sta XVel
        lda XVel+1
        adc #0		; carry +1
        jmp .NoCurveLeft
.CurveLeft
        adc XVel
        sta XVel
        lda XVel+1
        sbc #0		; carry -1
        nop ; make the branch timings are the same
.NoCurveLeft
        sta XVel+1
; Advance TPos (TrackData index)
; TPos += TrackLookahead
	lda TPos
        clc
        adc TrackLookahead
        sta TPos
        lda TPos+1
        adc #0
        sta TPos+1
; Go to next segment
	inc TrackLookahead ; see further along track
        dex
        bpl .CurveLoop
        rts

; Generate next track byte
GenTrack subroutine
; Shift the existing track data one byte up
; (a[i] = a[i+1])
	ldx #0
.ShiftTrackLoop
	lda TrackData+1,x
        sta TrackData,x
        inx
        cpx #TrackLen-1
        bne .ShiftTrackLoop
; Modify our current track value and
; see if it intersects the target value
	lda GenCur
        clc
        adc GenDelta
        cmp GenTarget
        beq .ChangeTarget   ; target == cur?
        bit GenTarget	    ; we need the sign flag 
        bmi .TargetNeg	    ; target<0?
        bcs .ChangeTarget   ; target>=0 && cur>=target?
        bcc .NoChangeTarget ; branch always taken
.TargetNeg
        bcs .NoChangeTarget ; target<0 && cur<target?
; Generate a new target value and increment value,
; and make sure the increment value is positive if
; the target is above the current value, and negative
; otherwise
.ChangeTarget
	lda Random
	jsr NextRandom	; get a random value
	sta Random
        and #$3f	; range 0..63
        sec
        sbc #$1f	; range -31..32
        sta GenTarget	; -> target
        cmp GenCur
        bmi .TargetBelow ; current > target?
	lda Random
        jsr NextRandom	; get a random value
	sta Random
        and #$f		; mask to 0..15
        jmp .TargetAbove
.TargetBelow
	lda Random
	jsr NextRandom
	sta Random
        ora #$f0	; mask to -16..0
.TargetAbove
        ora #1		; to avoid 0 values
        sta GenDelta	; -> delta
        lda GenCur
.NoChangeTarget
; Store the value in GenCur, and also
; at the end of the TrackData array
	sta GenCur
	sta TrackData+TrackLen-1
	rts


; fill video RAM
FillVRAM: subroutine
	PPU_SETADDR $2000
        lda #<RoadTables
        sta Ptr1
        lda #>RoadTables
        sta Ptr1+1
	ldx #8
        ldy #0
.loop:
	lda (Ptr1),y
	sta PPU_DATA
        iny
	bne .loop
        inc Ptr1+1
	dex
	bne .loop
        rts

; set palette colors
SetPalette: subroutine
	PPU_SETADDR $3f00
        ldx #16
.loop:
	lda Palette,y
	sta PPU_DATA
        iny
        dex
	bne .loop
        rts

; set sprite 0
SetSprite0: subroutine
	sta $200	;ypos
        lda #$00	;code
        sta $201
        lda #$20	;flags
        sta $202
        lda #$fe	;xpos
        sta $203
	rts

;;;;; COMMON SUBROUTINES

	include "nesppu.asm"

;;;;; INTERRUPT HANDLERS

            MAC SLEEP            ;usage: SLEEP n (n>1)
.CYCLES     SET {1}

                IF .CYCLES < 2
                    ECHO "MACRO ERROR: 'SLEEP': Duration must be > 1"
                    ERR
                ENDIF

                IF .CYCLES & 1
                    bit $00
.CYCLES             SET .CYCLES - 3
                ENDIF
            
                REPEAT .CYCLES / 2
                    nop
                REPEND
            ENDM

NMIHandler: subroutine
; save registers
	SAVE_REGS
; cycle palette colors
	lda TPos		; track position
        lsr			; / 2
        and #16			; now either 0 or 16
        tay			; Y is palette offset
	jsr SetPalette		; call SetPalette
        PPU_SETADDR $2000	; reset PPU address
; load sprite zero
        lda #109
        jsr SetSprite0
	lda #$02
        sta PPU_OAM_DMA
; setup sky scroll
	lda Heading+1
        sta PPU_SCROLL		; X scroll
        lda #0
        sta PPU_SCROLL		; Y scroll = 0
; do road calculations
        jsr RoadPreSetup
        jsr PreprocessCurve
        jsr RoadPostFrame
; wait for sprite 0
.wait0	bit PPU_STATUS
        bvs .wait0
.wait1	bit PPU_STATUS
        bvc .wait1
; alter horiz. scroll position for each scanline
        ldy #0
.loop
; pad out rest of scanline
        SLEEP 78
; change scroll register
	tya
        lsr		; / 2
        tax
	lda RoadX0,x	; get X offset of scanline
        eor #$80	; + 128
        sta PPU_SCROLL	; horiz byte
        lda #0
        sta PPU_SCROLL	; vert byte = 0
; take extra cycle every 4th line
	tya
        and #3
        bne .skipcyc
.skipcyc
; next loop iteration
        iny
        cpy #NumRoadScanlines
        bne .loop	; do next scanline
; restore registers and return from interrupt
        RESTORE_REGS
	rti

;;;;; CONSTANT DATA

;;{pal:"nes",layout:"nes"};;
Palette:
	hex 0F3F06300F19063F0F1916063F180801
	hex 0F30163F0F19163F0F1906160F180801
;;
RoadTables:
	incbin "road/nametable.dat"
	incbin "road/attribute.dat"
	incbin "road/nametable1.dat"
	incbin "road/attribute1.dat"

;;;;; CPU VECTORS

	NES_VECTORS

;;;;; TILE SETS

	org $10000
	incbin "road/road.chr"
