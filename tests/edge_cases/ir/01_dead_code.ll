; ModuleID = 'tests/edge_cases/01_dead_code.c'
source_filename = "tests/edge_cases/01_dead_code.c"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc19.44.35222"

@unused_global = dso_local global i32 999, align 4

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @dead_function(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  %3 = load i32, ptr %2, align 4
  %4 = mul nsw i32 %3, 42
  ret i32 %4
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @has_dead_code(i32 noundef %0, i32 noundef %1) #0 {
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  %6 = alloca i32, align 4
  %7 = alloca i32, align 4
  %8 = alloca i32, align 4
  store i32 %1, ptr %4, align 4
  store i32 %0, ptr %5, align 4
  %9 = load i32, ptr %5, align 4
  %10 = load i32, ptr %4, align 4
  %11 = add nsw i32 %9, %10
  store i32 %11, ptr %6, align 4
  %12 = load i32, ptr %5, align 4
  %13 = load i32, ptr %4, align 4
  %14 = mul nsw i32 %12, %13
  store i32 %14, ptr %7, align 4
  %15 = load i32, ptr %5, align 4
  %16 = icmp sgt i32 %15, 1000
  br i1 %16, label %17, label %22

17:                                               ; preds = %2
  %18 = load i32, ptr %7, align 4
  %19 = load i32, ptr %6, align 4
  %20 = add nsw i32 %18, %19
  store i32 %20, ptr %8, align 4
  %21 = load i32, ptr %8, align 4
  store i32 %21, ptr %3, align 4
  br label %24

22:                                               ; preds = %2
  %23 = load i32, ptr %6, align 4
  store i32 %23, ptr %3, align 4
  br label %24

24:                                               ; preds = %22, %17
  %25 = load i32, ptr %3, align 4
  ret i32 %25
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @unreachable(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  store i32 %0, ptr %3, align 4
  %5 = load i32, ptr %3, align 4
  %6 = icmp sgt i32 %5, 0
  br i1 %6, label %7, label %9

7:                                                ; preds = %1
  %8 = load i32, ptr %3, align 4
  store i32 %8, ptr %2, align 4
  br label %10

9:                                                ; preds = %1
  store i32 0, ptr %2, align 4
  br label %10

10:                                               ; preds = %9, %7
  %11 = load i32, ptr %2, align 4
  ret i32 %11
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @unused_param(i32 noundef %0, i32 noundef %1) #0 {
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  store i32 %1, ptr %3, align 4
  store i32 %0, ptr %4, align 4
  %5 = load i32, ptr %4, align 4
  %6 = mul nsw i32 %5, 2
  ret i32 %6
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @only_in_dead(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  %3 = load i32, ptr %2, align 4
  %4 = add nsw i32 %3, 1
  ret i32 %4
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main() #0 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  store i32 0, ptr %1, align 4
  %3 = call i32 @has_dead_code(i32 noundef 10, i32 noundef 20)
  store i32 %3, ptr %2, align 4
  %4 = load i32, ptr %2, align 4
  %5 = call i32 @unreachable(i32 noundef %4)
  %6 = load i32, ptr %2, align 4
  %7 = add nsw i32 %6, %5
  store i32 %7, ptr %2, align 4
  %8 = load i32, ptr %2, align 4
  %9 = call i32 @unused_param(i32 noundef %8, i32 noundef 999)
  %10 = load i32, ptr %2, align 4
  %11 = add nsw i32 %10, %9
  store i32 %11, ptr %2, align 4
  %12 = load i32, ptr %2, align 4
  ret i32 %12
}

attributes #0 = { noinline nounwind optnone uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3, !4, !5, !6}
!llvm.ident = !{!7}

!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, producer: "clang version 22.1.8 (https://github.com/llvm/llvm-project ca7933e47d3a3451d81e72ac174dcb5aa28b59d1)", isOptimized: false, runtimeVersion: 0, emissionKind: NoDebug, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "tests/edge_cases\\01_dead_code.c", directory: "C:\\LLVM-full")
!2 = !{i32 2, !"Debug Info Version", i32 3}
!3 = !{i32 1, !"wchar_size", i32 2}
!4 = !{i32 8, !"PIC Level", i32 2}
!5 = !{i32 7, !"uwtable", i32 2}
!6 = !{i32 1, !"MaxTLSAlign", i32 65536}
!7 = !{!"clang version 22.1.8 (https://github.com/llvm/llvm-project ca7933e47d3a3451d81e72ac174dcb5aa28b59d1)"}
