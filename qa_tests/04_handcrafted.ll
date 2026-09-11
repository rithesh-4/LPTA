; Hand-crafted LLVM IR for precise verification
define i32 @calc(i32 %x) {
entry:
  %a = add i32 %x, 10
  %b = mul i32 %a, 2
  %c = sub i32 %b, 5
  ret i32 %c
}
