; SPDX-License-Identifier: GPL-3.0-or-later
; E-ray DOS OPL Emulator - QEMM QPI I/O callback glue (16-bit V86), derived from ADLCOM's res_glue.s (GPLv3).
;
; QEMM calls this routine (far call, V86 mode) for EVERY trapped I/O access of every program that asked QEMM to trap ports:
;   DX = port, CX = flags (bit 2 set = OUT / write), AL/AX = data for OUT; for IN the handler returns the value in AL/AX.
;   The stack holds QEMM's frame: the interrupted program's CS:IP (dword, IP low) is at [sp+8] on entry.
; The routine is an IBM Interrupt Sharing Protocol (IISP) header followed by the code, so that several callbacks can be chained
; and later unhooked in any order. Ports that are not ours (not 388h/389h) are passed to the next handler in the chain.
; For our ports it switches DS to DGROUP and calls  unsigned __cdecl eopl_trap(port, flags, ax, ip_off, ip_seg)  (src/eopl.c),
; which runs its work on the driver's private stack. Everything except AX is preserved.
; Explicit USE16 segments (no .model) so the object links with the 16-bit medium-model C code.

        .386

DGROUP  group   _DATA

_DATA   segment word use16 public 'DATA'
_DATA   ends

TRAP_TEXT segment word use16 public 'CODE'
        assume  cs:TRAP_TEXT

        extrn   _eopl_trap : far
        public  _qemm_handler
        public  _qemm_end

_qemm_handler:
        jmp     short @@start           ; IISP: jump over the header (EB 10)
qemm_chain:
        dd      0                       ; +2  next handler in the chain (filled in by the installer)
        dw      424Bh                   ; +6  IISP signature 'KB'
        db      0                       ; +8  flags
        jmp     short @@reset           ; +9  hardware reset routine
        db      7 dup (0)               ; +0B reserved
                                        ; +12h code starts here
@@start:
        cmp     dx, 388h
        je      @@ours
        cmp     dx, 389h
        je      @@ours
        cmp     dx, 220h                ; Sound Blaster block 220h..22Fh (only delivered to us when /DIAG enabled the traps)
        jb      @@chain
        cmp     dx, 22Fh
        ja      @@chain
@@ours:
        pushad                          ; EDI ESI EBP ESP EBX EDX ECX EAX
        push    ds
        push    es
        mov     bp, sp
        mov     ebx, dword ptr ss:[bp+2Ch]      ; interrupted program's IP (low) : CS (high)
        mov     ax, DGROUP
        mov     ds, ax
        mov     eax, ebx
        shr     eax, 16
        push    ax                              ; ip_seg
        push    bx                              ; ip_off
        push    word ptr ss:[bp+20h]            ; ax   (saved EAX)
        push    word ptr ss:[bp+1Ch]            ; flags (saved ECX)
        push    word ptr ss:[bp+18h]            ; port  (saved EDX)
        call    far ptr _eopl_trap
        add     sp, 10
        mov     word ptr ss:[bp+20h], ax        ; value returned to the trapped program (IN)
        pop     es
        pop     ds
        popad
@@reset:
        retf

@@chain:
        jmp     dword ptr cs:qemm_chain

_qemm_end:
TRAP_TEXT ends
        end
