; demo_input.ll — Hand-crafted LLVM IR for dramatic optimization demos
; Each function targets specific LLVM passes for visual impact in LPTA.
;
; Functions:
;   constant_fold_demo  — interprocedural constant propagation + folding
;   dead_code_demo      — unreachable code + DCE
;   sroa_demo           — scalar replacement of aggregates
;   loop_opt_demo       — strength reduction, LICM, induction variable simplification
;   gvn_demo            — global value numbering, redundant load elimination
;   simplify_cfg_demo   — branch folding, block merging
;   memcpy_demo         — memcpyOpt, dead store elimination
;   alias_analysis_demo — precise alias analysis, store-to-load forwarding

target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; ============================================================
; 1. CONSTANT FOLDING DEMO
;    Target passes: SimplifyCFG, InstCombine, SCCP, ConstProp
;    Expected: entire function body collapses to a single return
; ============================================================
define i32 @constant_fold_demo() {
entry:
  ; Multi-level constant propagation
  %a = add i32 10, 20           ; -> 30
  %b = mul i32 %a, 3            ; -> 90
  %c = sub i32 %b, 5            ; -> 85
  %d = shl i32 %c, 2            ; -> 340
  %e = and i32 %d, 255          ; -> 84

  ; Nested constant expression
  %tmp1 = mul i32 7, 6     ; -> 42
  %tmp2 = sub i32 %tmp1, 40 ; -> 2
  %f = add i32 %e, %tmp2    ; -> 86
  %g = xor i32 %f, -1           ; ~86

  ; Conditional with known outcome
  %h = icmp sgt i32 %g, 0       ; always true (unsigned interpretation)
  br i1 %h, label %then, label %else

then:
  %i = add i32 %g, 100          ; -> 186 + ~86...
  br label %merge

else:
  %j = mul i32 %g, 999          ; dead code
  br label %merge

merge:
  %result = phi i32 [ %i, %then ], [ %j, %else ]
  ret i32 %result
}

; ============================================================
; 2. DEAD CODE ELIMINATION DEMO
;    Target passes: DCE, ADCE, SimplifyCFG, CFGSimplification
;    Expected: large blocks of code removed, function shrinks
; ============================================================
define i32 @dead_code_demo(i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 100
  br i1 %cmp, label %big_path, label %small_path

big_path:
  ; This entire block is dead if n <= 100
  %x1 = add i32 %n, 1
  %x2 = mul i32 %x1, 2
  %x3 = sub i32 %x2, 5
  %x4 = shl i32 %x3, 3
  %x5 = or i32 %x4, 7
  %x6 = xor i32 %x5, -1
  br label %merge

small_path:
  br label %merge

merge:
  %result = phi i32 [ %x6, %big_path ], [ 42, %small_path ]
  ; More dead code after merge — result is never used
  %dead1 = add i32 %result, 1
  %dead2 = mul i32 %dead1, 2
  %dead3 = sub i32 %dead2, 3
  ret i32 %result
}

; ============================================================
; 3. SROA DEMO (Scalar Replacement of Aggregates)
;    Target passes: SROA, mem2reg, InstCombine
;    Expected: alloca + store + load sequences replaced by SSA values
; ============================================================
%Point = type { i32, i32, i32 }

define i32 @sroa_demo(i32 %ax, i32 %ay, i32 %bx, i32 %by) {
entry:
  ; Create struct on stack
  %p = alloca %Point, align 8
  %p.x = getelementptr inbounds %Point, ptr %p, i32 0, i32 0
  %p.y = getelementptr inbounds %Point, ptr %p, i32 0, i32 1
  %p.z = getelementptr inbounds %Point, ptr %p, i32 0, i32 2

  ; Store values
  store i32 %ax, ptr %p.x, align 4
  store i32 %ay, ptr %p.y, align 4
  store i32 0, ptr %p.z, align 4

  ; Distance calculation: sqrt((ax-bx)^2 + (ay-by)^2)
  %dx = sub i32 %ax, %bx
  %dy = sub i32 %ay, %by
  %dx2 = mul i32 %dx, %dx
  %dy2 = mul i32 %dy, %dy
  %dist_sq = add i32 %dx2, %dy2

  ; Store result back through struct
  store i32 %dist_sq, ptr %p.z, align 4

  ; Load and use
  %z_val = load i32, ptr %p.z, align 4
  %x_val = load i32, ptr %p.x, align 4
  %final = add i32 %z_val, %x_val

  ret i32 %final
}

; ============================================================
; 4. LOOP OPTIMIZATION DEMO
;    Target passes: LICM, IndVarSimplify, LoopUnroll, LoopRotate,
;                   StrengthReduction, LoopIdiomRecognize
;    Expected: loop counter simplified, multiply replaced by add,
;              loop-invariant code hoisted, idioms recognized
; ============================================================
define i64 @loop_opt_demo(ptr %arr, i32 %n) {
entry:
  br label %loop.header

loop.header:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop.body ]
  %sum = phi i64 [ 0, %entry ], [ %sum.next, %loop.body ]
  %cmp = icmp slt i32 %i, %n
  br i1 %cmp, label %loop.body, label %exit

loop.body:
  ; Strength reduction: i * 8 -> pointer arithmetic
  %idx = sext i32 %i to i64
  %offset = mul nsw i64 %idx, 8
  %ptr = getelementptr i8, ptr %arr, i64 %offset
  %val = load i64, ptr %ptr, align 8

  ; Loop-invariant: %n * 4 computed every iteration but never changes
  %licm_waste = mul nsw i32 %n, 4

  ; Accumulate
  %sum.next = add nsw i64 %sum, %val

  ; Loop increment
  %i.next = add nsw i32 %i, 1
  br label %loop.header

exit:
  ; Return sum
  ret i64 %sum
}

; ============================================================
; 5. GLOBAL VALUE NUMBERING DEMO
;    Target passes: GVN, EarlyCSE, LICM, DSE
;    Expected: redundant loads eliminated, common subexpressions merged
; ============================================================
define i32 @gvn_demo(ptr %p, i32 %cond) {
entry:
  ; First load
  %v1 = load i32, ptr %p, align 4

  ; Redundant load — same pointer, no store in between
  %v2 = load i32, ptr %p, align 4

  ; Should be identical to v1
  %same = icmp eq i32 %v1, %v2
  br i1 %same, label %path_a, label %path_b

path_a:
  ; Redundant computation: (v1 + v1) == (v1 * 2)
  %sum = add i32 %v1, %v1
  %prod = mul i32 %v1, 2
  %check = icmp eq i32 %sum, %prod  ; always true
  br i1 %check, label %merge, label %path_b

path_b:
  br label %merge

merge:
  ; More GVN: v1 and v2 are same value
  %r1 = add i32 %v1, %v2  ; == v1 + v1
  %r2 = sub i32 %v1, 0    ; == v1
  %result = add i32 %r1, %r2
  ret i32 %result
}

; ============================================================
; 6. CFG SIMPLIFICATION DEMO
;    Target passes: SimplifyCFG, BlockPlacement, JumpThreading
;    Expected: unreachable blocks removed, branches simplified,
;              empty blocks merged, switches converted to jumps
; ============================================================
define i32 @simplify_cfg_demo(i32 %x) {
entry:
  %tobool = icmp ne i32 %x, 0
  br i1 %tobool, label %if.then, label %if.end

if.then:
  %add = add i32 %x, 1
  br label %if.end

if.end:
  ; Redundant branch: both paths lead here
  %result = phi i32 [ %add, %if.then ], [ 0, %entry ]

  ; Dead switch: only one case possible
  switch i32 %result, label %default [
    i32 0, label %case_zero
    i32 1, label %case_one
  ]

case_zero:
  br label %return

case_one:
  br label %return

default:
  br label %return

return:
  %ret = phi i32 [ 0, %case_zero ], [ 1, %case_one ], [ %result, %default ]
  ret i32 %ret
}

; ============================================================
; 7. MEMCPY OPTIMIZATION DEMO
;    Target passes: memcpyOpt, DSE, InstCombine
;    Expected: dead stores eliminated, memcpy simplified
; ============================================================
define void @memcpy_demo(ptr %dst, ptr %src) {
entry:
  ; Dead store #1 — overwritten immediately
  store i32 99999, ptr %dst, align 4
  store i32 42, ptr %dst, align 4

  ; Dead store #2 — overwritten by memcpy
  store i64 -1, ptr %dst, align 8
  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %dst, ptr align 8 %src, i64 64, i1 false)

  ; Dead store #3 — result never used
  store i32 12345, ptr %dst, align 4
  ret void
}

; ============================================================
; 8. ALIAS ANALYSIS DEMO
;    Target passes: BasicAA, ScopedNoAliasAA, GVN, DSE, LICM
;    Expected: stores/loads reordered when no alias proven,
;              redundant loads eliminated across alias-safe stores
; ============================================================
define i32 @alias_analysis_demo(ptr %a, ptr %b, i32 %n) {
entry:
  ; Store to %a
  store i32 100, ptr %a, align 4

  ; Load from %b — if a and b don't alias, this is independent
  %v1 = load i32, ptr %b, align 4

  ; Store to %b — can't alias %a if proven disjoint
  store i32 200, ptr %b, align 4

  ; Load from %a — should still be 100 if no alias
  %v2 = load i32, ptr %a, align 4

  ; Compute
  %sum = add i32 %v1, %v2

  ; Loop with no-alias pointer access
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %body ]
  %acc = phi i32 [ %sum, %entry ], [ %acc.next, %body ]
  %cmp = icmp slt i32 %i, %n
  br i1 %cmp, label %body, label %exit

body:
  ; These accesses should be recognized as non-aliasing
  store i32 %i, ptr %a, align 4
  %lv = load i32, ptr %b, align 4
  %acc.next = add i32 %acc, %lv
  %i.next = add i32 %i, 1
  br label %loop

exit:
  ret i32 %acc
}

; ============================================================
; Helper declarations
; ============================================================
declare void @llvm.memcpy.p0.p0.i64(ptr nocapture writeonly, ptr nocapture readonly, i64, i1 immarg)
