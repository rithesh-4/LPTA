; ModuleID = 'test_no_optnone.ll'
target triple = "x86_64-pc-windows-msvc"

define i32 @add(i32 %a, i32 %b) {
entry:
  %result = add i32 %a, %b
  ret i32 %result
}

define i32 @mul(i32 %a, i32 %b) {
entry:
  %result = mul i32 %a, %b
  ret i32 %result
}

define i32 @main() {
entry:
  %call1 = call i32 @add(i32 1, i32 2)
  %call2 = call i32 @mul(i32 3, i32 4)
  %result = add i32 %call1, %call2
  ret i32 %result
}
