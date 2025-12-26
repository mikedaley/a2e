back to project page

C600ROM Disassembly
                   ********************************************************************************
                   * Disk ][ controller card "BOOT0" code, found in the slot ROM.  Reads the      *
                   * BOOT1 code from track 0, sector 0, and jumps to it.                          *
                   *                                                                              *
                   * Copyright Apple Computer Inc.                                                *
                   *                                                                              *
                   * Written by [a genius...Woz?]                                                 *
                   ********************************************************************************
                   * Extracted from AppleWin at $C600.                                            *
                   *                                                                              *
                   * Project created by Andy McFadden, using 6502bench SourceGen v1.5             *
                   * Last updated 2020/01/15                                                      *
                   ********************************************************************************
                   STACK         .eq     $0100  {addr/256}
                   TWOS_BUFFER   .eq     $0300  {addr/86}  ;holds the 2-bit chunks
                   CONV_TAB      .eq     $0356  {addr/128} ;6+2 conversion table
                   BOOT1         .eq     $0800  {addr/256} ;buffer for next stage of loader
                   IWM_PH0_OFF   .eq     $c080             ;stepper motor control
                   IWM_PH0_ON    .eq     $c081             ;stepper motor control
                   IWM_MOTOR_ON  .eq     $c089             ;starts drive spinning
                   IWM_SEL_DRIVE_1 .eq   $c08a             ;selects drive 1
                   IWM_Q6_OFF    .eq     $c08c             ;read
                   IWM_Q7_OFF    .eq     $c08e             ;WP sense/read
                   MON_WAIT      .eq     $fca8             ;delay for (26 + 27*Acc + 5*(Acc*Acc))/2 cycles
                   MON_IORTS     .eq     $ff58             ;JSR here to find out where one is

                                 .org    $c600
                   data_ptr      .var    $26    {addr/2}   ;pointer to BOOT1 data buffer
                   slot_index    .var    $2b    {addr/1}   ;slot number << 4
                   bits          .var    $3c    {addr/1}   ;temp storage for bit manipulation
                   sector        .var    $3d    {addr/1}   ;sector to read
                   found_track   .var    $40    {addr/1}   ;track found
                   track         .var    $41    {addr/1}   ;track to read

c600: a2 20        ENTRY         ldx     #$20              ;20/00/03 is the controller signature
                   ;
                   ; Generate a decoder table for 6+2 encoded data.
                   ;
                   ; This stores the values $00-$3f in a table on page 3.  The byte values that
                   ; will be decoded are non-consecutive, so the decoder entries occupy various
                   ; locations from $36c to $3d5.  Nearby bytes are left unchanged.
                   ;
                   ; We want 64 values that have the high bit set and don't have two consecutive 0
                   ; bits.  This is required by the disk hardware.  There are 70 possible values,
                   ; so we also mandate that there are two adjacent 1 bits, excluding bit 7.  (Note
                   ; that $D5 and $AA, used to identify sector headers, do not meet these criteria,
                   ; which means they never appear in the encoded data.)
                   ;
                   ; In the code below, ASL+BIT+BCS test checks for adjacent 1s: if no two are
                   ; adjacent, the BIT will be zero.  If the high bit is set, ASL will set the
                   ; carry.
                   ;
                   ; When we ORA the original and shifted values together, if there were three
                   ; adjacent 0s, there will still be at least two adjacent 0s.  We EOR to invert
                   ; the bits, and then look for two adjacent 1s.  We do this by just shifting
                   ; right until a 1 shifts into the carry, and if the A-reg is nonzero we know
                   ; there were at least two 1 bits.  We need to ignore the bits on the ends:
                   ; nonzero high bit was handled earlier, and the low bit can false-positive
                   ; because ASL always shifts a 0 in (making it look like a 0 in the low bit is
                   ; adjacent to another 0), so we just mask those off with the AND.
                   ;
                   ; For example, we want to decode $A6 to $07.  Y=$07 when X=$26...
                   ;   TXA --> 0010 0110
                   ;   ASL --> 0100 1100 C=0   (high bit is clear)
                   ;   BIT --> Z=0             (only possible with adjacent bits)
                   ;   ORA --> 0110 1110       (adjacent 0s become visible)
                   ;   EOR --> 1001 0001       (turn them into 1s)
                   ;   AND --> 0001 0000       (ignore the hi/lo)
                   ;   LSR --> 0000 1000, repeat until A=0 C=1
                   ;
c602: a0 00                      ldy     #$00
c604: a2 03                      ldx     #$03
                   CreateDecTabLoop
c606: 86 3c                      stx     bits
c608: 8a                         txa
c609: 0a                         asl     A                 ;shift left, putting high bit in carry
c60a: 24 3c                      bit     bits              ;does shifted version overlap?
c60c: f0 10                      beq     :reject           ;no, doesn't have two adjacent 1s
c60e: 05 3c                      ora     bits              ;merge
c610: 49 ff                      eor     #$ff              ;invert
c612: 29 7e                      and     #$7e              ;clear hi and lo bits
c614: b0 08        :check_dub0   bcs     :reject           ;initial hi bit set *or* adjacent 0 bits set
c616: 4a                         lsr     A                 ;shift right, low bit into carry
c617: d0 fb                      bne     :check_dub0       ;if more bits in byte, loop
c619: 98                         tya                       ;we have a winner... store Y-reg to memory
c61a: 9d 56 03                   sta     CONV_TAB,x        ;actual lookup will be on bytes with hi bit set
c61d: c8                         iny                       ; so they'll read from CONV_TAB-128
c61e: e8           :reject       inx                       ;try next candidate
c61f: 10 e5                      bpl     CreateDecTabLoop
                   ;
                   ; Prep the hardware.
                   ;
c621: 20 58 ff                   jsr     MON_IORTS         ;known RTS
c624: ba                         tsx
c625: bd 00 01                   lda     STACK,x           ;pull hi byte of our address off stack
c628: 0a                         asl     A                 ;(we assume no interrupts have hit)
c629: 0a                         asl     A                 ;multiply by 16
c62a: 0a                         asl     A
c62b: 0a                         asl     A
c62c: 85 2b                      sta     slot_index        ;keep this around
c62e: aa                         tax
c62f: bd 8e c0                   lda     IWM_Q7_OFF,x      ;set to read mode
c632: bd 8c c0                   lda     IWM_Q6_OFF,x
c635: bd 8a c0                   lda     IWM_SEL_DRIVE_1,x ;select drive 1
c638: bd 89 c0                   lda     IWM_MOTOR_ON,x    ;spin it up
                   ;
                   ; Blind-seek to track 0.
                   ;
c63b: a0 50                      ldy     #80               ;80 phases (40 tracks)
c63d: bd 80 c0     :seek_loop    lda     IWM_PH0_OFF,x     ;turn phase N off
c640: 98                         tya
c641: 29 03                      and     #$03              ;mod the phase number to get 0-3
c643: 0a                         asl     A                 ;double it to 0/2/4/6
c644: 05 2b                      ora     slot_index        ;add in the slot index
c646: aa                         tax
c647: bd 81 c0                   lda     IWM_PH0_ON,x      ;turn on phase 0, 1, 2, or 3
c64a: a9 56                      lda     #86
c64c: 20 a8 fc                   jsr     MON_WAIT          ;wait 19664 cycles
c64f: 88                         dey                       ;next phase
c650: 10 eb                      bpl     :seek_loop
c652: 85 26                      sta     data_ptr          ;A-reg is 0 when MON_WAIT returns
c654: 85 3d                      sta     sector            ;so we're looking for T=0 S=0
c656: 85 41                      sta     track
c658: a9 08                      lda     #>BOOT1           ;write the output here
c65a: 85 27                      sta     data_ptr+1
                   ;
                   ; Sector read routine.
                   ;
                   ; Read bytes until we find an address header (D5 AA 96) or data header (D5 AA
                   ; AD), depending on which mode we're in.
                   ;
                   ; This will also be called by the BOOT1 code read from the floppy disk.
                   ;
                   ; On entry:
                   ;   X: slot * 16
                   ;   $26-27: data pointer
                   ;   $3d: desired sector
                   ;   $41: desired track
                   ;
c65c: 18           ReadSector    clc                       ;C=0 to look for addr (C=1 for data)
c65d: 08           ReadSector_C  php
c65e: bd 8c c0     :rdbyte1      lda     IWM_Q6_OFF,x      ;wait for byte
c661: 10 fb                      bpl     :rdbyte1          ;not yet, loop
c663: 49 d5        :check_d5     eor     #$d5              ;is it $d5?
c665: d0 f7                      bne     :rdbyte1          ;no, keep looking
c667: bd 8c c0     :rdbyte2      lda     IWM_Q6_OFF,x      ;grab another byte
c66a: 10 fb                      bpl     :rdbyte2
c66c: c9 aa                      cmp     #$aa              ;is it $aa?
c66e: d0 f3                      bne     :check_d5         ;no, check if it's another $d5
c670: ea                         nop                       ;(?)
c671: bd 8c c0     :rdbyte3      lda     IWM_Q6_OFF,x      ;grab a third byte
c674: 10 fb                      bpl     :rdbyte3
c676: c9 96                      cmp     #$96              ;is it $96?
c678: f0 09                      beq     FoundAddress      ;winner
c67a: 28                         plp                       ;did we want data?
c67b: 90 df                      bcc     ReadSector        ;nope, keep looking
c67d: 49 ad                      eor     #$ad              ;yes, see if it's data prologue
c67f: f0 25                      beq     FoundData         ;got it, read the data (note A-reg = 0)
c681: d0 d9                      bne     ReadSector        ;keep looking

                   ;
                   ; Read the sector address data.  Four fields, in 4+4 encoding: volume, track,
                   ; sector, checksum.
                   ;
c683: a0 03        FoundAddress  ldy     #$03              ;sector # is the 3rd item in header
c685: 85 40        :hdr_loop     sta     found_track       ;store $96, then volume, then track
c687: bd 8c c0     :rdbyte1      lda     IWM_Q6_OFF,x      ;read first part
c68a: 10 fb                      bpl     :rdbyte1
c68c: 2a                         rol     A                 ;first byte has bits 7/5/3/1
c68d: 85 3c                      sta     bits
c68f: bd 8c c0     :rdbyte2      lda     IWM_Q6_OFF,x      ;read second part
c692: 10 fb                      bpl     :rdbyte2
c694: 25 3c                      and     bits              ;merge them
c696: 88                         dey                       ;is this the 3rd item?
c697: d0 ec                      bne     :hdr_loop         ;nope, keep going
c699: 28                         plp                       ;pull this off to keep stack in balance
c69a: c5 3d                      cmp     sector            ;is this the sector we want?
c69c: d0 be                      bne     ReadSector        ;no, go back to looking for addresses
c69e: a5 40                      lda     found_track
c6a0: c5 41                      cmp     track             ;correct track?
c6a2: d0 b8                      bne     ReadSector        ;no, try again
c6a4: b0 b7                      bcs     ReadSector_C      ;correct T/S, go find data (branch-always)

                   ;
                   ; Read the 6+2 encoded sector data.
                   ;
                   ; Values range from $96 - $ff.  They must have the high bit set, and must not
                   ; have three consecutive zeroes.
                   ;
                   ; The data bytes are written to disk with a rolling XOR to compute a checksum,
                   ; so we read them back the same way.  We keep this in the A-reg for the
                   ; duration.  The actual value is always in the range [$00,$3f] (6 bits).
                   ;
                   ; On entry:
                   ;   A: $00
                   ;
c6a6: a0 56        FoundData     ldy     #86               ;read 86 bytes of data into $300-355
                   :read_twos_loop
c6a8: 84 3c                      sty     bits              ;each byte has 3 sets of 2 bits, encoded
c6aa: bc 8c c0     :rdbyte1      ldy     IWM_Q6_OFF,x
c6ad: 10 fb                      bpl     :rdbyte1
c6af: 59 d6 02                   eor     $02d6,y           ;$02d6 + $96 = $36c, our first table entry
c6b2: a4 3c                      ldy     bits
c6b4: 88                         dey
c6b5: 99 00 03                   sta     TWOS_BUFFER,y     ;store these in our page 3 buffer
c6b8: d0 ee                      bne     :read_twos_loop
                   ;
                   :read_sixes_loop
c6ba: 84 3c                      sty     bits              ;read 256 bytes of data into $800
c6bc: bc 8c c0     :rdbyte2      ldy     IWM_Q6_OFF,x      ;each byte has the high 6 bits, encoded
c6bf: 10 fb                      bpl     :rdbyte2
c6c1: 59 d6 02                   eor     CONV_TAB-128,y
c6c4: a4 3c                      ldy     bits
c6c6: 91 26                      sta     (data_ptr),y      ;store these in the eventual data buffer
c6c8: c8                         iny
c6c9: d0 ef                      bne     :read_sixes_loop
                   ;
c6cb: bc 8c c0     :rdbyte3      ldy     IWM_Q6_OFF,x      ;read checksum byte
c6ce: 10 fb                      bpl     :rdbyte3
c6d0: 59 d6 02                   eor     CONV_TAB-128,y    ;does it match?
c6d3: d0 87        :another      bne     ReadSector        ;no, try to find one that's undamaged
                   ;
                   ; Decode the 6+2 encoding.  The high 6 bits of each byte are in place, now we
                   ; just need to shift the low 2 bits of each in.
                   ;
c6d5: a0 00                      ldy     #$00              ;update 256 bytes
c6d7: a2 56        :init_x       ldx     #86               ;run through the 2-bit pieces 3x (86*3=258)
c6d9: ca           :decode_loop  dex
c6da: 30 fb                      bmi     :init_x           ;if we hit $2ff, go back to $355
c6dc: b1 26                      lda     (data_ptr),y      ;foreach byte in the data buffer...
c6de: 5e 00 03                   lsr     TWOS_BUFFER,x     ; grab the low two bits from the stuff at $300-$355
c6e1: 2a                         rol     A                 ; and roll them into the low two bits of the byte
c6e2: 5e 00 03                   lsr     TWOS_BUFFER,x
c6e5: 2a                         rol     A
c6e6: 91 26                      sta     (data_ptr),y
c6e8: c8                         iny
c6e9: d0 ee                      bne     :decode_loop
                   ;
                   ; Advance the data pointer and sector number, and check to see if the sector
                   ; number matches the first byte of BOOT1.  If it does, we're done.  If not, go
                   ; read the next sector.
                   ;
c6eb: e6 27                      inc     data_ptr+1
c6ed: e6 3d                      inc     sector
c6ef: a5 3d                      lda     sector            ;sector we'd read next
c6f1: cd 00 08                   cmp     BOOT1             ;is next sector < BOOT1?
c6f4: a6 2b                      ldx     slot_index
c6f6: 90 db                      bcc     :another          ;yes, go get another sector (note branch x2)
                   ; All done, jump to BOOT1 ($0801).
c6f8: 4c 01 08                   jmp     BOOT1+1

c6fb: 00 00 00 00+               .junk   5                 ;spare bytes
Symbol Table
ENTRY	$c600
ReadSector	$c65c
HTML generated by 6502bench SourceGen v1.5.0-alpha1 on 2020/01/15

Expression style: Common
