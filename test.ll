; Simple test IR for LPTA
; This file contains functions with obvious redundancies
; that O2 passes will optimize.

define i32 @simple_add(i32 %a, i32 %b) {
  %sum = add i32 %a, %b
  ret i32 %sum
}

define i32 @redundant_math(i32 %x) {
  %a = mul i32 %x, 2
  %b = add i32 %a, 0
  %c = mul i32 %b, 1
  ret i32 %c
}

define i32 @branch_simplify(i32 %n) {
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %then, label %else

then:
  %result = add i32 %n, 1
  br label %merge

else:
  %result2 = add i32 %n, -1
  br label %merge

merge:
  %phi = phi i32 [ %result, %then ], [ %result2, %else ]
  ret i32 %phi
}

define i32 @loop_test(i32 %n) {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i_next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum_next, %loop ]
  %i_next = add i32 %i, 1
  %sum_next = add i32 %sum, %i
  %done = icmp sge i32 %i_next, %n
  br i1 %done, label %exit, label %loop

exit:
  ret i32 %sum
}

define i32 @dead_code(i32 %x) {
  %a = add i32 %x, 1
  %b = mul i32 %a, %a
  %c = add i32 %b, 100
  %unused = mul i32 %c, 42
  ret i32 %c
}
