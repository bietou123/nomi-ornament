"""
STM32F103C6T6 Joystick TX - hand-encoded ARM Thumb-2 binary + flash
"""
import struct, time, sys, serial
import os

def thumb_movw(rd, imm16):
    """MOVW Rd, #imm16 - Thumb-2 encoding"""
    imm4 = (imm16 >> 12) & 0xF
    i = (imm16 >> 11) & 1
    imm3 = (imm16 >> 8) & 7
    imm8 = imm16 & 0xFF
    hw1 = 0xF240 | (i << 10) | imm4
    hw2 = (imm3 << 12) | (rd << 8) | imm8
    return struct.pack('<HH', hw1, hw2)

def thumb_movt(rd, imm16):
    """MOVT Rd, #imm16 - Thumb-2 encoding"""
    imm4 = (imm16 >> 12) & 0xF
    i = (imm16 >> 11) & 1
    imm3 = (imm16 >> 8) & 7
    imm8 = imm16 & 0xFF
    hw1 = 0xF2C0 | (i << 10) | imm4
    hw2 = (imm3 << 12) | (rd << 8) | imm8
    return struct.pack('<HH', hw1, hw2)

def thumb_ldr(rt, rn, imm5):
    """LDR Rt, [Rn, #imm5*4] - 16-bit Thumb"""
    return struct.pack('<H', 0x6800 | ((imm5 & 0x1F) << 6) | (rn << 3) | rt)

def thumb_str(rt, rn, imm5):
    """STR Rt, [Rn, #imm5*4] - 16-bit Thumb"""
    return struct.pack('<H', 0x6000 | ((imm5 & 0x1F) << 6) | (rn << 3) | rt)

def thumb_orrs(rdn, rm):
    """ORRS Rdn, Rm - 16-bit"""
    return struct.pack('<H', 0x4300 | (rm << 3) | rdn)

def thumb_ands(rdn, rm):
    """ANDS Rdn, Rm - 16-bit"""
    return struct.pack('<H', 0x4000 | (rm << 3) | rdn)

def thumb_movs(rd, imm8):
    """MOVS Rd, #imm8 - 16-bit"""
    return struct.pack('<H', 0x2000 | (rd << 8) | (imm8 & 0xFF))

def thumb_mov(rd, rm):
    """MOV Rd, Rm - 16-bit"""
    D = (rd >> 3) & 1
    rd3 = rd & 7
    return struct.pack('<H', 0x4600 | (D << 7) | (rm << 3) | rd3)

def thumb_push(regs):
    """PUSH {regs} - 16-bit, regs is bitmask of r0-r7,lr"""
    lr = 1 if (regs & (1<<14)) else 0
    rlist = regs & 0xFF
    return struct.pack('<H', 0xB400 | (lr << 8) | rlist)

def thumb_pop(regs):
    """POP {regs} - 16-bit, regs is bitmask of r0-r7,pc"""
    pc = 1 if (regs & (1<<15)) else 0
    rlist = regs & 0xFF
    return struct.pack('<H', 0xBC00 | (pc << 8) | rlist)

def thumb_bl(offset):
    """BL offset (from current PC+4)"""
    s = 1 if offset < 0 else 0
    imm = offset & 0x3FFFFF
    imm11 = (imm >> 1) & 0x7FF
    imm10 = (imm >> 12) & 0x3FF
    j1 = 1 - (((imm >> 23) & 1) ^ s)
    j2 = 1 - (((imm >> 22) & 1) ^ s)
    hw1 = 0xF000 | (s << 10) | imm10
    hw2 = 0xD000 | (j1 << 13) | (j2 << 11) | imm11
    return struct.pack('<HH', hw1, hw2)

def thumb_b(offset):
    """B offset - unconditional, 16-bit (offset from PC+4, range +-2KB)"""
    imm11 = ((offset >> 1) & 0x7FF)
    return struct.pack('<H', 0xE000 | imm11)

def thumb_bne(offset):
    """BNE offset - 16-bit conditional"""
    imm8 = ((offset >> 1) & 0xFF)
    return struct.pack('<H', 0xD100 | imm8)

def thumb_beq(offset):
    """BEQ offset - 16-bit conditional"""
    imm8 = ((offset >> 1) & 0xFF)
    return struct.pack('<H', 0xD000 | imm8)

def thumb_subs_imm(rdn, imm8):
    """SUBS Rd, #imm8"""
    return struct.pack('<H', 0x3800 | (rdn << 8) | (imm8 & 0xFF))

def thumb_adds_imm(rdn, imm8):
    """ADDS Rd, #imm8"""
    return struct.pack('<H', 0x3000 | (rdn << 8) | (imm8 & 0xFF))

def thumb_cmp_imm(rn, imm8):
    """CMP Rn, #imm8"""
    return struct.pack('<H', 0x2800 | (rn << 8) | (imm8 & 0xFF))

def thumb_tst(rn, rm):
    """TST Rn, Rm"""
    return struct.pack('<H', 0x4200 | (rm << 3) | rn)

def thumb_nop():
    return struct.pack('<H', 0xBF00)

def load_const(rd, val):
    """Load 32-bit constant into register using MOVW+MOVT"""
    code = thumb_movw(rd, val & 0xFFFF)
    if val > 0xFFFF:
        code += thumb_movt(rd, (val >> 16) & 0xFFFF)
    return code

def build_firmware():
    """Build minimal firmware binary"""
    code = bytearray()

    # --- Vector table (8 bytes) ---
    code += struct.pack('<I', 0x20002800)  # Initial SP
    code += struct.pack('<I', 0x08000009)  # Reset handler (+1 for Thumb)

    # --- Reset handler starts at offset 8 ---

    # Enable RCC APB2ENR: GPIOA(2) | ADC1(9) | USART1(14) = 0x4204
    code += load_const(0, 0x40021018)  # r0 = &RCC_APB2ENR
    code += thumb_ldr(1, 0, 0)         # r1 = [r0]
    code += load_const(2, 0x4204)      # r2 = 0x4204
    code += thumb_orrs(1, 2)           # r1 |= r2
    code += thumb_str(1, 0, 0)         # [r0] = r1

    # GPIOA_CRL: PA0,PA1=analog(0), PA2=input pullup(0x8 at [11:8])
    code += load_const(0, 0x40010800)  # r0 = &GPIOA_CRL
    code += load_const(1, 0x00000800)  # PA0=0,PA1=0,PA2=input(8)
    code += thumb_str(1, 0, 0)         # [r0] = r1

    # GPIOA_ODR: set bit2 for pullup on PA2
    code += load_const(0, 0x4001080C)
    code += thumb_movs(1, 4)
    code += thumb_str(1, 0, 0)

    # GPIOA_CRH: PA9=AF push-pull 50MHz = 0xB at [7:4]
    # Default CRH=0x44444444, we want 0x444444B4
    code += load_const(0, 0x40010804)
    code += load_const(1, 0x444444B4)
    code += thumb_str(1, 0, 0)

    # USART1_BRR = 833 (8MHz / 9600)
    code += load_const(0, 0x40013808)
    code += load_const(1, 833)
    code += thumb_str(1, 0, 0)

    # USART1_CR1 = 0x2008 (UE | TE)
    code += load_const(0, 0x4001380C)
    code += load_const(1, 0x2008)
    code += thumb_str(1, 0, 0)

    # ADC1_CR2 = 1 (ADON)
    code += load_const(0, 0x40012408)
    code += thumb_movs(1, 1)
    code += thumb_str(1, 0, 0)

    # Delay for ADC stabilization
    code += load_const(3, 50000)
    # delay_init loop
    delay_init_pos = len(code)
    code += thumb_subs_imm(3, 1)
    code += thumb_bne(-4)  # branch back 4 bytes (to subs)

    # === Main loop ===
    main_loop_pos = len(code)

    # --- ADC read channel 0 ---
    # ADC1_SQR3 = 0
    code += load_const(0, 0x40012434)
    code += thumb_movs(1, 0)
    code += thumb_str(1, 0, 0)
    # ADC1_CR2 |= 1 (start)
    code += load_const(0, 0x40012408)
    code += thumb_ldr(1, 0, 0)
    code += thumb_movs(2, 1)
    code += thumb_orrs(1, 2)
    code += thumb_str(1, 0, 0)
    # Wait EOC
    code += load_const(0, 0x40012400)  # ADC1_SR
    adc0_wait = len(code)
    code += thumb_ldr(1, 0, 0)
    code += thumb_movs(2, 2)  # EOC bit
    code += thumb_tst(1, 2)
    code += thumb_beq(-8)  # back to ldr
    # Read result
    code += load_const(0, 0x4001244C)  # ADC1_DR
    code += thumb_ldr(4, 0, 0)  # r4 = ADC value (X)

    # --- ADC read channel 1 ---
    code += load_const(0, 0x40012434)
    code += thumb_movs(1, 1)
    code += thumb_str(1, 0, 0)
    code += load_const(0, 0x40012408)
    code += thumb_ldr(1, 0, 0)
    code += thumb_movs(2, 1)
    code += thumb_orrs(1, 2)
    code += thumb_str(1, 0, 0)
    code += load_const(0, 0x40012400)
    adc1_wait = len(code)
    code += thumb_ldr(1, 0, 0)
    code += thumb_movs(2, 2)
    code += thumb_tst(1, 2)
    code += thumb_beq(-8)
    code += load_const(0, 0x4001244C)
    code += thumb_ldr(5, 0, 0)  # r5 = ADC value (Y)

    # --- Read PA2 button ---
    code += load_const(0, 0x40010808)  # GPIOA_IDR
    code += thumb_ldr(0, 0, 0)
    code += thumb_movs(1, 4)  # bit 2
    code += thumb_tst(0, 1)
    # if PA2=0 (pressed), r6=1; else r6=0
    code += thumb_beq(4)   # skip to r6=0... wait need to think about this
    code += thumb_movs(6, 0)  # not pressed
    code += thumb_b(2)     # skip next
    code += thumb_movs(6, 1)  # pressed

    # --- Send digits: simple approach, just send raw number ---
    # For simplicity, send values 0-4095 as decimal
    # We'll use a simple division-by-subtraction approach

    # Send X (r4) as decimal
    code += thumb_mov(0, 4)
    # inline uart_num for X - call subroutine
    # Actually let's put subroutines at the end and BL to them
    # For now, let's just send a fixed test pattern to verify flash works
    # We'll send "1023,512,0\r\n" as a test

    # SIMPLIFIED: just send ADC values as 4-digit hex (much simpler than decimal)
    # Format: XXXX,YYYY,B\r\n where X/Y are hex

    # Actually, let's do the simplest thing that works with the ESP32:
    # Send decimal. Use repeated subtraction for division.

    # Let me use a subroutine approach. Place subroutines after main loop.
    # But calculating BL offsets is tricky. Let me use a simpler approach:
    # Just unroll the UART send inline.

    # --- UART send helper (inline macro-style) ---
    # Send r4 as decimal, then ',', then r5 as decimal, then ',', then r6+'0', then \r\n

    # This is getting complex. Let me just send hex which is much easier.
    # ESP32 side can be modified to parse hex.
    # Actually no, let's keep it compatible. Let me just send 3 comma-separated decimals.

    # SIMPLEST APPROACH: send each digit by repeated subtraction
    # For a 12-bit value (0-4095), max 4 digits

    # I'll build this as a two-pass: first pass to determine code layout,
    # but that's complex. Let me just hardcode a simpler output.

    # FINAL DECISION: Send raw ADC as space-padded 4-digit numbers
    # Actually, let me just keep it super simple: hex output
    # "XXX,YYY,B\r\n" where XXX/YYY are decimal (0-4095)

    pass  # Will rebuild with a cleaner approach below
    return None


def build_firmware_v2():
    """Build firmware using a pre-computed approach - decimal output via repeated subtraction"""

    # Instead of complex subroutine handling, let's build the binary in sections
    # and compute offsets manually

    code = bytearray()

    # ====== VECTOR TABLE (8 bytes) ======
    code += struct.pack('<I', 0x20002800)  # SP
    code += struct.pack('<I', 0x08000009)  # Reset (Thumb)

    # ====== RESET HANDLER (offset 0x08) ======

    # -- Peripheral init --
    # RCC_APB2ENR |= GPIOA | ADC1 | USART1
    code += load_const(0, 0x40021018)
    code += thumb_ldr(1, 0, 0)
    code += load_const(2, 0x4204)
    code += thumb_orrs(1, 2)
    code += thumb_str(1, 0, 0)

    # GPIOA_CRL = 0x00000800 (PA0/1 analog, PA2 input pull-up)
    code += load_const(0, 0x40010800)
    code += load_const(1, 0x00000800)
    code += thumb_str(1, 0, 0)

    # GPIOA_ODR bit2 = 1 (pull-up)
    code += load_const(0, 0x4001080C)
    code += thumb_movs(1, 4)
    code += thumb_str(1, 0, 0)

    # GPIOA_CRH = 0x444444B4 (PA9 AF PP 50MHz)
    code += load_const(0, 0x40010804)
    code += load_const(1, 0x444444B4)
    code += thumb_str(1, 0, 0)

    # USART1_BRR = 833
    code += load_const(0, 0x40013808)
    code += load_const(1, 833)
    code += thumb_str(1, 0, 0)

    # USART1_CR1 = 0x2008
    code += load_const(0, 0x4001380C)
    code += load_const(1, 0x2008)
    code += thumb_str(1, 0, 0)

    # ADC1_CR2 = 1
    code += load_const(0, 0x40012408)
    code += thumb_movs(1, 1)
    code += thumb_str(1, 0, 0)

    # Delay
    code += load_const(3, 50000)
    code += thumb_subs_imm(3, 1)
    code += thumb_bne(-4)

    # ====== MAIN LOOP ======
    main_loop_off = len(code)

    # -- Read ADC0 -> r4 --
    code += load_const(0, 0x40012434)  # SQR3
    code += thumb_movs(1, 0)
    code += thumb_str(1, 0, 0)
    code += load_const(0, 0x40012408)  # CR2
    code += thumb_ldr(1, 0, 0)
    code += thumb_movs(2, 1)
    code += thumb_orrs(1, 2)
    code += thumb_str(1, 0, 0)
    code += load_const(0, 0x40012400)  # SR
    code += thumb_ldr(1, 0, 0)         # wait EOC
    code += thumb_movs(2, 2)
    code += thumb_tst(1, 2)
    code += thumb_beq(-8)
    code += load_const(0, 0x4001244C)  # DR
    code += thumb_ldr(4, 0, 0)

    # -- Read ADC1 -> r5 --
    code += load_const(0, 0x40012434)
    code += thumb_movs(1, 1)
    code += thumb_str(1, 0, 0)
    code += load_const(0, 0x40012408)
    code += thumb_ldr(1, 0, 0)
    code += thumb_movs(2, 1)
    code += thumb_orrs(1, 2)
    code += thumb_str(1, 0, 0)
    code += load_const(0, 0x40012400)
    code += thumb_ldr(1, 0, 0)
    code += thumb_movs(2, 2)
    code += thumb_tst(1, 2)
    code += thumb_beq(-8)
    code += load_const(0, 0x4001244C)
    code += thumb_ldr(5, 0, 0)

    # -- Read PA2 -> r6 (0 or 1, active low) --
    code += load_const(0, 0x40010808)
    code += thumb_ldr(0, 0, 0)
    # Check bit 2
    code += thumb_movs(1, 4)
    code += thumb_tst(0, 1)
    code += thumb_bne(4)     # if bit set (not pressed), jump to r6=0
    code += thumb_movs(6, 1) # pressed
    code += thumb_b(2)       # skip
    code += thumb_movs(6, 0) # not pressed

    # -- Send X as decimal --
    # Use simple approach: divide by 1000, 100, 10, 1
    # r4 has X value. We'll send non-zero digits.
    # UART TX: write to USART1_DR when TXE set in SR

    # Helper: send char in r0
    # Inline: wait TXE (bit7 of USART1_SR), then write USART1_DR
    # This is used many times, let's put it as a subroutine after the main loop

    # For now, let's do something simpler: send each value as hex (4 chars)
    # This avoids complex division logic
    # Format: "XXXX,YYYY,B\r\n" where XXXX is hex

    # Actually let's just send decimal using repeated subtraction.
    # Put uart_ch and send_decimal as subroutines after the loop, use BL.

    # APPROACH: Build code with placeholder BL offsets, then patch them

    # Position of BL to send_num(r4)
    bl_send_x_pos = len(code)
    code += thumb_mov(0, 4)  # r0 = X
    code += b'\x00\x00\x00\x00'  # placeholder BL send_num

    # Send ','
    bl_comma1_pos = len(code)
    code += thumb_movs(0, 44)
    code += b'\x00\x00\x00\x00'  # placeholder BL uart_ch

    # Send Y
    bl_send_y_pos = len(code)
    code += thumb_mov(0, 5)
    code += b'\x00\x00\x00\x00'  # placeholder BL send_num

    # Send ','
    bl_comma2_pos = len(code)
    code += thumb_movs(0, 44)
    code += b'\x00\x00\x00\x00'  # placeholder BL uart_ch

    # Send button digit
    bl_btn_pos = len(code)
    code += thumb_adds_imm(6, 48)  # r6 += '0'... wait this modifies r6 permanently
    # Better: mov r0, r6; adds r0, #48
    code = code[:bl_btn_pos]  # undo
    code += thumb_mov(0, 6)
    code += thumb_adds_imm(0, 48)
    code += b'\x00\x00\x00\x00'  # placeholder BL uart_ch

    # Send '\r'
    bl_cr_pos = len(code)
    code += thumb_movs(0, 13)
    code += b'\x00\x00\x00\x00'  # placeholder BL uart_ch

    # Send '\n'
    bl_lf_pos = len(code)
    code += thumb_movs(0, 10)
    code += b'\x00\x00\x00\x00'  # placeholder BL uart_ch

    # Delay ~50ms
    code += load_const(3, 200000)
    code += thumb_subs_imm(3, 1)
    code += thumb_bne(-4)

    # Jump back to main loop
    loop_back = main_loop_off - (len(code) + 4)
    code += thumb_b(loop_back)

    # ====== SUBROUTINES ======

    # -- uart_ch: send byte in r0 --
    uart_ch_off = len(code)
    code += thumb_push((1<<1)|(1<<2)|(1<<14))  # push {r1,r2,lr}
    code += thumb_mov(2, 0)  # r2 = char to send
    code += load_const(1, 0x40013800)  # USART1_SR
    # wait TXE
    uart_wait_off = len(code)
    code += thumb_ldr(0, 1, 0)
    code += thumb_movs(3, 0x80)
    code += thumb_tst(0, 3)
    code += thumb_beq(-8)
    # write DR
    code += load_const(1, 0x40013804)
    code += thumb_str(2, 1, 0)
    code += thumb_pop((1<<1)|(1<<2)|(1<<15))  # pop {r1,r2,pc}

    # -- send_num: send uint16 in r0 as decimal (0-4095) --
    send_num_off = len(code)
    code += thumb_push((1<<4)|(1<<5)|(1<<6)|(1<<7)|(1<<14))  # push {r4-r7,lr}
    code += thumb_mov(4, 0)  # r4 = value
    code += thumb_movs(7, 0)  # r7 = leading_zero flag

    # Thousands digit (divide by 1000)
    code += load_const(5, 1000)
    code += thumb_movs(6, 0)  # r6 = digit
    # subtract loop
    thou_loop = len(code)
    code += thumb_cmp_imm(4, 0)  # actually need CMP r4, r5 which is not imm8...
    # CMP Rn, Rm encoding:
    code = code[:thou_loop]
    # Use: subtract r5 from r4, if borrow, done
    # SUBS r4, r4, r5 -> check carry
    # Actually simpler: use a flag approach

    # This is getting too complex for hand-encoding. Let me use a MUCH simpler approach.
    # Just send hex! 3 hex chars per value (0-FFF) = "FFF,FFF,1\r\n"
    # ESP32 side just needs to parse hex instead of decimal.

    return None  # abandon this approach


def build_firmware_hex():
    """Build firmware that sends joystick as hex: XXX,YYY,B\\r\\n"""

    code = bytearray()

    # ====== VECTOR TABLE ======
    code += struct.pack('<I', 0x20002800)  # SP
    code += struct.pack('<I', 0x08000009)  # Reset handler

    # ====== INIT ======
    # RCC_APB2ENR |= GPIOA | ADC1 | USART1
    code += load_const(0, 0x40021018)
    code += thumb_ldr(1, 0, 0)
    code += load_const(2, 0x4204)
    code += thumb_orrs(1, 2)
    code += thumb_str(1, 0, 0)

    # GPIOA_CRL = 0x00000800
    code += load_const(0, 0x40010800)
    code += load_const(1, 0x00000800)
    code += thumb_str(1, 0, 0)

    # GPIOA_ODR = 4 (pullup PA2)
    code += load_const(0, 0x4001080C)
    code += thumb_movs(1, 4)
    code += thumb_str(1, 0, 0)

    # GPIOA_CRH = 0x444444B4
    code += load_const(0, 0x40010804)
    code += load_const(1, 0x444444B4)
    code += thumb_str(1, 0, 0)

    # USART1_BRR = 833
    code += load_const(0, 0x40013808)
    code += load_const(1, 833)
    code += thumb_str(1, 0, 0)

    # USART1_CR1 = 0x2008
    code += load_const(0, 0x4001380C)
    code += load_const(1, 0x2008)
    code += thumb_str(1, 0, 0)

    # ADC1_CR2 = 1
    code += load_const(0, 0x40012408)
    code += thumb_movs(1, 1)
    code += thumb_str(1, 0, 0)

    # Delay
    code += load_const(3, 50000)
    code += thumb_subs_imm(3, 1)  # delay loop
    code += thumb_bne(-4)

    # ====== MAIN LOOP ======
    main_loop_off = len(code)

    # Read ADC0 -> r4
    code += load_const(0, 0x40012434)
    code += thumb_movs(1, 0)
    code += thumb_str(1, 0, 0)
    code += load_const(0, 0x40012408)
    code += thumb_ldr(1, 0, 0)
    code += thumb_movs(2, 1)
    code += thumb_orrs(1, 2)
    code += thumb_str(1, 0, 0)
    code += load_const(0, 0x40012400)
    code += thumb_ldr(1, 0, 0)
    code += thumb_movs(2, 2)
    code += thumb_tst(1, 2)
    code += thumb_beq(-8)
    code += load_const(0, 0x4001244C)
    code += thumb_ldr(4, 0, 0)

    # Read ADC1 -> r5
    code += load_const(0, 0x40012434)
    code += thumb_movs(1, 1)
    code += thumb_str(1, 0, 0)
    code += load_const(0, 0x40012408)
    code += thumb_ldr(1, 0, 0)
    code += thumb_movs(2, 1)
    code += thumb_orrs(1, 2)
    code += thumb_str(1, 0, 0)
    code += load_const(0, 0x40012400)
    code += thumb_ldr(1, 0, 0)
    code += thumb_movs(2, 2)
    code += thumb_tst(1, 2)
    code += thumb_beq(-8)
    code += load_const(0, 0x4001244C)
    code += thumb_ldr(5, 0, 0)

    # Read PA2 -> r6
    code += load_const(0, 0x40010808)
    code += thumb_ldr(0, 0, 0)
    code += thumb_movs(1, 4)
    code += thumb_tst(0, 1)
    code += thumb_bne(4)      # not pressed -> skip
    code += thumb_movs(6, 1)  # pressed
    code += thumb_b(2)
    code += thumb_movs(6, 0)  # not pressed

    # === SEND DATA ===
    # We'll inline uart_ch (wait TXE + write DR) as a pattern
    # To keep it simple, send decimal using a lookup table approach
    # Actually, simplest: just send raw 12-bit values as 4 decimal digits with leading zeros
    # 0000-4095 format. This makes parsing trivial.

    # Even simpler for first version: just send the raw number in a fixed format
    # that the ESP32 can parse. Let's send 4 hex nibbles per value.

    # ACTUALLY: Let me just inline a send_byte routine and send data as:
    # high_byte, low_byte for each value (binary protocol, 5 bytes total)
    # ESP32 can parse: [X_H, X_L, Y_H, Y_L, BTN, '\n']
    # This avoids ALL decimal/hex conversion on STM32 side!

    # === BINARY PROTOCOL: 6 bytes per frame ===
    # Byte 0: X high (bits 11-8, OR'd with 0x80 as sync marker)
    # Byte 1: X low (bits 7-0)
    # Byte 2: Y high (bits 11-8)
    # Byte 3: Y low (bits 7-0)
    # Byte 4: Button (0 or 1)
    # Byte 5: '\n' (0x0A) as frame delimiter

    # Send X high nibble | 0x80 (sync marker)
    # r4 has X (12-bit). Send (r4>>8)|0x80, then r4&0xFF

    # MOV r0, r4; LSR r0, #8; ORR r0, #0x80 -> uart_ch
    # Thumb encoding for LSR:
    # LSRS Rd, Rm, #imm5: 0x0800 | (imm5<<6) | (Rm<<3) | Rd

    def thumb_lsrs_imm(rd, rm, imm5):
        return struct.pack('<H', 0x0800 | ((imm5&0x1F)<<6) | (rm<<3) | rd)

    def thumb_lsls_imm(rd, rm, imm5):
        return struct.pack('<H', 0x0000 | ((imm5&0x1F)<<6) | (rm<<3) | rd)

    def thumb_uxtb(rd, rm):
        """UXTB Rd, Rm (zero-extend byte)"""
        return struct.pack('<HH', 0xFA5F, 0xF080 | (rd<<8) | rm)

    # Inline uart_ch: load SR addr, wait TXE, write DR
    def inline_uart_send_r0():
        """Inline: send r0 byte via USART1. Clobbers r1,r2,r3."""
        c = bytearray()
        c += load_const(1, 0x40013800)  # USART1_SR
        c += thumb_ldr(2, 1, 0)         # r2 = SR
        c += thumb_movs(3, 0x80)        # TXE bit
        c += thumb_tst(2, 3)
        c += thumb_beq(-8)              # wait
        c += load_const(1, 0x40013804)  # USART1_DR
        c += thumb_str(0, 1, 0)
        return c

    # Send X_H | 0x80
    code += thumb_lsrs_imm(0, 4, 8)     # r0 = r4 >> 8
    code += thumb_movs(1, 0x80)
    code += thumb_orrs(0, 1)             # r0 |= 0x80
    code += inline_uart_send_r0()

    # Send X_L
    code += thumb_mov(0, 4)
    # Mask low 8 bits: AND with 0xFF
    code += thumb_movs(1, 0xFF)
    code += thumb_ands(0, 1)
    code += inline_uart_send_r0()

    # Send Y_H
    code += thumb_lsrs_imm(0, 5, 8)
    code += inline_uart_send_r0()

    # Send Y_L
    code += thumb_mov(0, 5)
    code += thumb_movs(1, 0xFF)
    code += thumb_ands(0, 1)
    code += inline_uart_send_r0()

    # Send Button
    code += thumb_mov(0, 6)
    code += inline_uart_send_r0()

    # Send '\n'
    code += thumb_movs(0, 10)
    code += inline_uart_send_r0()

    # Delay ~50ms
    code += load_const(3, 200000)
    code += thumb_subs_imm(3, 1)
    code += thumb_bne(-4)

    # Loop back
    back_offset = main_loop_off - (len(code) + 4)
    # Need B.W (Thumb-2 wide branch) for larger offsets
    # B.W encoding: same as BL but with different J bits
    # Actually let's check if offset fits in 16-bit B
    if back_offset >= -2048 and back_offset < 2048:
        code += thumb_b(back_offset)
    else:
        # B.W (unconditional wide branch)
        offset = back_offset
        s = 1 if offset < 0 else 0
        imm = offset & 0x1FFFFF
        imm10 = (imm >> 12) & 0x3FF
        imm11 = (imm >> 1) & 0x7FF
        j1 = 1 - (((imm >> 23) & 1) ^ s)
        j2 = 1 - (((imm >> 22) & 1) ^ s)
        hw1 = 0xF000 | (s << 10) | imm10
        hw2 = 0x9000 | (j1 << 13) | (j2 << 11) | imm11
        code += struct.pack('<HH', hw1, hw2)

    return bytes(code)


# ---- STM32 Bootloader Protocol ----
def stm32_sync(ser):
    ser.write(b'\x7f')
    return ser.read(1) == b'\x79'

def stm32_cmd(ser, cmd):
    ser.write(bytes([cmd, cmd ^ 0xFF]))
    return ser.read(1) == b'\x79'

def stm32_write_memory(ser, addr, data):
    if not stm32_cmd(ser, 0x31):
        return False
    addr_bytes = struct.pack('>I', addr)
    chk = 0
    for b in addr_bytes: chk ^= b
    ser.write(addr_bytes + bytes([chk]))
    if ser.read(1) != b'\x79':
        return False
    n = len(data)
    chk = n - 1
    for b in data: chk ^= b
    ser.write(bytes([n-1]) + data + bytes([chk & 0xFF]))
    return ser.read(1) == b'\x79'

def stm32_go(ser, addr):
    if not stm32_cmd(ser, 0x21):
        return False
    addr_bytes = struct.pack('>I', addr)
    chk = 0
    for b in addr_bytes: chk ^= b
    ser.write(addr_bytes + bytes([chk]))
    return ser.read(1) == b'\x79'

def flash_binary(port, binary):
    ser = serial.Serial(port, 115200, parity=serial.PARITY_EVEN, timeout=2)
    if not stm32_sync(ser):
        print("Sync failed!")
        ser.close()
        return False
    print(f"Bootloader OK. Flashing {len(binary)} bytes...")

    addr = 0x08000000
    for i in range(0, len(binary), 256):
        chunk = binary[i:i+256]
        if len(chunk) < 256:
            chunk = chunk + b'\xFF' * (256 - len(chunk))
        if not stm32_write_memory(ser, addr + i, chunk):
            print(f"\nWrite failed at 0x{addr+i:08X}")
            ser.close()
            return False
        print(f"\r  [{i+256}/{len(binary)}] bytes", end='', flush=True)

    print("\n  Flash OK!")
    stm32_go(ser, 0x08000000)
    print("  Started! Set BOOT0=LOW and press RST for normal run.")
    ser.close()
    return True


if __name__ == '__main__':
    print("Building firmware...")
    fw = build_firmware_hex()
    if fw is None:
        print("Build failed!")
        exit(1)
    print(f"  {len(fw)} bytes")

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'joystick_tx.bin')
    with open(out, 'wb') as f:
        f.write(fw)

    port = sys.argv[1] if len(sys.argv) > 1 else 'COM5'
    print(f"Flashing to {port}...")
    if flash_binary(port, fw):
        print("\nSuccess!")
    else:
        print("\nFailed!")
