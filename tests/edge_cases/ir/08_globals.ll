; ModuleID = 'tests/edge_cases/08_globals.c'
source_filename = "tests/edge_cases/08_globals.c"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc19.44.35222"

@CONST_A = dso_local constant i32 42, align 4
@CONST_B = dso_local constant i32 100, align 4
@CONST_SUM = dso_local constant i32 142, align 4
@mutable_global = dso_local global i32 0, align 4
@counter = dso_local global i32 0, align 4
@global_arr = dso_local global [10 x i32] [i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9], align 16
@static_local_func.call_count = internal global i32 0, align 4
@shared_state = external dso_local global i32, align 4
@global_fn_ptr = dso_local global ptr @double_it, align 8
@tentative = dso_local global i32 0, align 4

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @increment_counter() #0 {
  %1 = load i32, ptr @counter, align 4
  %2 = add nsw i32 %1, 1
  store i32 %2, ptr @counter, align 4
  %3 = load i32, ptr @counter, align 4
  ret i32 %3
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @read_and_modify(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  %3 = load i32, ptr %2, align 4
  %4 = mul nsw i32 %3, 2
  store i32 %4, ptr @mutable_global, align 4
  %5 = load i32, ptr @mutable_global, align 4
  %6 = add nsw i32 %5, 42
  ret i32 %6
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @use_constants() #0 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  store i32 42, ptr %1, align 4
  store i32 100, ptr %2, align 4
  store i32 142, ptr %3, align 4
  %4 = load i32, ptr %1, align 4
  %5 = load i32, ptr %2, align 4
  %6 = add nsw i32 %4, %5
  %7 = load i32, ptr %3, align 4
  %8 = add nsw i32 %6, %7
  ret i32 %8
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @lookup(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  store i32 %0, ptr %3, align 4
  %4 = load i32, ptr %3, align 4
  %5 = icmp sge i32 %4, 0
  br i1 %5, label %6, label %14

6:                                                ; preds = %1
  %7 = load i32, ptr %3, align 4
  %8 = icmp slt i32 %7, 10
  br i1 %8, label %9, label %14

9:                                                ; preds = %6
  %10 = load i32, ptr %3, align 4
  %11 = sext i32 %10 to i64
  %12 = getelementptr inbounds [10 x i32], ptr @global_arr, i64 0, i64 %11
  %13 = load i32, ptr %12, align 4
  store i32 %13, ptr %2, align 4
  br label %15

14:                                               ; preds = %6, %1
  store i32 -1, ptr %2, align 4
  br label %15

15:                                               ; preds = %14, %9
  %16 = load i32, ptr %2, align 4
  ret i32 %16
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @static_local_func() #0 {
  %1 = load i32, ptr @static_local_func.call_count, align 4
  %2 = add nsw i32 %1, 1
  store i32 %2, ptr @static_local_func.call_count, align 4
  %3 = load i32, ptr @static_local_func.call_count, align 4
  ret i32 %3
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @read_shared() #0 {
  %1 = load i32, ptr @shared_state, align 4
  ret i32 %1
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @double_it(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  %3 = load i32, ptr %2, align 4
  %4 = mul nsw i32 %3, 2
  ret i32 %4
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @square_it(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  %3 = load i32, ptr %2, align 4
  %4 = load i32, ptr %2, align 4
  %5 = mul nsw i32 %3, %4
  ret i32 %5
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @apply_global_fn(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  %3 = load ptr, ptr @global_fn_ptr, align 8
  %4 = load i32, ptr %2, align 4
  %5 = call i32 %3(i32 noundef %4)
  ret i32 %5
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main() #0 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  store i32 0, ptr %1, align 4
  store i32 0, ptr %2, align 4
  %3 = call i32 @use_constants()
  %4 = load i32, ptr %2, align 4
  %5 = add nsw i32 %4, %3
  store i32 %5, ptr %2, align 4
  %6 = call i32 @increment_counter()
  %7 = load i32, ptr %2, align 4
  %8 = add nsw i32 %7, %6
  store i32 %8, ptr %2, align 4
  %9 = call i32 @increment_counter()
  %10 = load i32, ptr %2, align 4
  %11 = add nsw i32 %10, %9
  store i32 %11, ptr %2, align 4
  %12 = call i32 @read_and_modify(i32 noundef 21)
  %13 = load i32, ptr %2, align 4
  %14 = add nsw i32 %13, %12
  store i32 %14, ptr %2, align 4
  %15 = call i32 @lookup(i32 noundef 5)
  %16 = load i32, ptr %2, align 4
  %17 = add nsw i32 %16, %15
  store i32 %17, ptr %2, align 4
  %18 = call i32 @static_local_func()
  %19 = load i32, ptr %2, align 4
  %20 = add nsw i32 %19, %18
  store i32 %20, ptr %2, align 4
  %21 = call i32 @static_local_func()
  %22 = load i32, ptr %2, align 4
  %23 = add nsw i32 %22, %21
  store i32 %23, ptr %2, align 4
  %24 = call i32 @apply_global_fn(i32 noundef 5)
  %25 = load i32, ptr %2, align 4
  %26 = add nsw i32 %25, %24
  store i32 %26, ptr %2, align 4
  %27 = load i32, ptr %2, align 4
  store i32 %27, ptr @mutable_global, align 4
  %28 = load i32, ptr %2, align 4
  %29 = load i32, ptr @mutable_global, align 4
  %30 = add nsw i32 %28, %29
  ret i32 %30
}

attributes #0 = { noinline nounwind optnone uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3, !4, !5, !6}
!llvm.ident = !{!7}

!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, producer: "clang version 22.1.8 (https://github.com/llvm/llvm-project ca7933e47d3a3451d81e72ac174dcb5aa28b59d1)", isOptimized: false, runtimeVersion: 0, emissionKind: NoDebug, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "tests/edge_cases\\08_globals.c", directory: "C:\\LLVM-full")
!2 = !{i32 2, !"Debug Info Version", i32 3}
!3 = !{i32 1, !"wchar_size", i32 2}
!4 = !{i32 8, !"PIC Level", i32 2}
!5 = !{i32 7, !"uwtable", i32 2}
!6 = !{i32 1, !"MaxTLSAlign", i32 65536}
!7 = !{!"clang version 22.1.8 (https://github.com/llvm/llvm-project ca7933e47d3a3451d81e72ac174dcb5aa28b59d1)"}
