; ============================================================
; TINY PROOF IR — Every element countable by hand
;
; Functions:  2 (@entry, @helper)
; BBs:        5 (entry, then, else, merge, helper_entry)
; Instructions: manually verifiable below
; ============================================================

define i32 @entry(i32 %x) {
entry:                          ; BB 1
  %cmp = icmp sgt i32 %x, 0    ; instr 1: icmp
  br i1 %cmp, label %then, label %else  ; instr 2: br

then:                           ; BB 2
  %a = add i32 %x, 1           ; instr 3: add
  br label %merge               ; instr 4: br

else:                           ; BB 3
  %b = sub i32 %x, 1           ; instr 5: sub
  br label %merge               ; instr 6: br

merge:                          ; BB 4
  %phi = phi i32 [ %a, %then ], [ %b, %else ]  ; instr 7: phi
  %c = mul i32 %phi, 2         ; instr 8: mul
  %unused = add i32 %c, 100    ; instr 9: add (dead code)
  ret i32 %c                   ; instr 10: ret
}

define i32 @helper(i32 %y) {
helper_entry:                   ; BB 5
  %d = add i32 %y, 1           ; instr 11: add
  ret i32 %d                   ; instr 12: ret
}
