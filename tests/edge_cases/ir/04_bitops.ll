; ModuleID = 'tests/edge_cases/04_bitops.c'
source_filename = "tests/edge_cases/04_bitops.c"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc19.44.35222"

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @popcount(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  store i32 0, ptr %3, align 4
  br label %4

4:                                                ; preds = %7, %1
  %5 = load i32, ptr %2, align 4
  %6 = icmp ne i32 %5, 0
  br i1 %6, label %7, label %14

7:                                                ; preds = %4
  %8 = load i32, ptr %2, align 4
  %9 = sub i32 %8, 1
  %10 = load i32, ptr %2, align 4
  %11 = and i32 %10, %9
  store i32 %11, ptr %2, align 4
  %12 = load i32, ptr %3, align 4
  %13 = add nsw i32 %12, 1
  store i32 %13, ptr %3, align 4
  br label %4, !llvm.loop !8

14:                                               ; preds = %4
  %15 = load i32, ptr %3, align 4
  ret i32 %15
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @is_power_of_2(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  %3 = load i32, ptr %2, align 4
  %4 = icmp ne i32 %3, 0
  br i1 %4, label %5, label %12

5:                                                ; preds = %1
  %6 = load i32, ptr %2, align 4
  %7 = load i32, ptr %2, align 4
  %8 = sub i32 %7, 1
  %9 = and i32 %6, %8
  %10 = icmp ne i32 %9, 0
  %11 = xor i1 %10, true
  br label %12

12:                                               ; preds = %5, %1
  %13 = phi i1 [ false, %1 ], [ %11, %5 ]
  %14 = zext i1 %13 to i32
  ret i32 %14
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @xor_swap(ptr noundef %0, ptr noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca ptr, align 8
  store ptr %1, ptr %3, align 8
  store ptr %0, ptr %4, align 8
  %5 = load ptr, ptr %3, align 8
  %6 = load i32, ptr %5, align 4
  %7 = load ptr, ptr %4, align 8
  %8 = load i32, ptr %7, align 4
  %9 = xor i32 %8, %6
  store i32 %9, ptr %7, align 4
  %10 = load ptr, ptr %4, align 8
  %11 = load i32, ptr %10, align 4
  %12 = load ptr, ptr %3, align 8
  %13 = load i32, ptr %12, align 4
  %14 = xor i32 %13, %11
  store i32 %14, ptr %12, align 4
  %15 = load ptr, ptr %3, align 8
  %16 = load i32, ptr %15, align 4
  %17 = load ptr, ptr %4, align 8
  %18 = load i32, ptr %17, align 4
  %19 = xor i32 %18, %16
  store i32 %19, ptr %17, align 4
  ret void
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @set_bit(i32 noundef %0, i32 noundef %1) #0 {
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  store i32 %1, ptr %3, align 4
  store i32 %0, ptr %4, align 4
  %5 = load i32, ptr %4, align 4
  %6 = load i32, ptr %3, align 4
  %7 = shl i32 1, %6
  %8 = or i32 %5, %7
  ret i32 %8
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @clear_bit(i32 noundef %0, i32 noundef %1) #0 {
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  store i32 %1, ptr %3, align 4
  store i32 %0, ptr %4, align 4
  %5 = load i32, ptr %4, align 4
  %6 = load i32, ptr %3, align 4
  %7 = shl i32 1, %6
  %8 = xor i32 %7, -1
  %9 = and i32 %5, %8
  ret i32 %9
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @toggle_bit(i32 noundef %0, i32 noundef %1) #0 {
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  store i32 %1, ptr %3, align 4
  store i32 %0, ptr %4, align 4
  %5 = load i32, ptr %4, align 4
  %6 = load i32, ptr %3, align 4
  %7 = shl i32 1, %6
  %8 = xor i32 %5, %7
  ret i32 %8
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @test_bit(i32 noundef %0, i32 noundef %1) #0 {
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  store i32 %1, ptr %3, align 4
  store i32 %0, ptr %4, align 4
  %5 = load i32, ptr %4, align 4
  %6 = load i32, ptr %3, align 4
  %7 = ashr i32 %5, %6
  %8 = and i32 %7, 1
  ret i32 %8
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @reverse_bits(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  store i32 0, ptr %3, align 4
  store i32 0, ptr %4, align 4
  br label %5

5:                                                ; preds = %16, %1
  %6 = load i32, ptr %4, align 4
  %7 = icmp slt i32 %6, 32
  br i1 %7, label %8, label %19

8:                                                ; preds = %5
  %9 = load i32, ptr %3, align 4
  %10 = shl i32 %9, 1
  %11 = load i32, ptr %2, align 4
  %12 = and i32 %11, 1
  %13 = or i32 %10, %12
  store i32 %13, ptr %3, align 4
  %14 = load i32, ptr %2, align 4
  %15 = lshr i32 %14, 1
  store i32 %15, ptr %2, align 4
  br label %16

16:                                               ; preds = %8
  %17 = load i32, ptr %4, align 4
  %18 = add nsw i32 %17, 1
  store i32 %18, ptr %4, align 4
  br label %5, !llvm.loop !10

19:                                               ; preds = %5
  %20 = load i32, ptr %3, align 4
  ret i32 %20
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @clz(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  store i32 %0, ptr %3, align 4
  %6 = load i32, ptr %3, align 4
  %7 = icmp eq i32 %6, 0
  br i1 %7, label %8, label %9

8:                                                ; preds = %1
  store i32 32, ptr %2, align 4
  br label %28

9:                                                ; preds = %1
  store i32 0, ptr %4, align 4
  store i32 31, ptr %5, align 4
  br label %10

10:                                               ; preds = %23, %9
  %11 = load i32, ptr %5, align 4
  %12 = icmp sge i32 %11, 0
  br i1 %12, label %13, label %26

13:                                               ; preds = %10
  %14 = load i32, ptr %3, align 4
  %15 = load i32, ptr %5, align 4
  %16 = shl i32 1, %15
  %17 = and i32 %14, %16
  %18 = icmp ne i32 %17, 0
  br i1 %18, label %19, label %20

19:                                               ; preds = %13
  br label %26

20:                                               ; preds = %13
  %21 = load i32, ptr %4, align 4
  %22 = add nsw i32 %21, 1
  store i32 %22, ptr %4, align 4
  br label %23

23:                                               ; preds = %20
  %24 = load i32, ptr %5, align 4
  %25 = add nsw i32 %24, -1
  store i32 %25, ptr %5, align 4
  br label %10, !llvm.loop !11

26:                                               ; preds = %19, %10
  %27 = load i32, ptr %4, align 4
  store i32 %27, ptr %2, align 4
  br label %28

28:                                               ; preds = %26, %8
  %29 = load i32, ptr %2, align 4
  ret i32 %29
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @extract_bits(i32 noundef %0, i32 noundef %1, i32 noundef %2) #0 {
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  %6 = alloca i32, align 4
  %7 = alloca i32, align 4
  store i32 %2, ptr %4, align 4
  store i32 %1, ptr %5, align 4
  store i32 %0, ptr %6, align 4
  %8 = load i32, ptr %4, align 4
  %9 = shl i32 1, %8
  %10 = sub i32 %9, 1
  store i32 %10, ptr %7, align 4
  %11 = load i32, ptr %6, align 4
  %12 = load i32, ptr %5, align 4
  %13 = lshr i32 %11, %12
  %14 = load i32, ptr %7, align 4
  %15 = and i32 %13, %14
  ret i32 %15
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @next_pow2(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  %3 = load i32, ptr %2, align 4
  %4 = add i32 %3, -1
  store i32 %4, ptr %2, align 4
  %5 = load i32, ptr %2, align 4
  %6 = lshr i32 %5, 1
  %7 = load i32, ptr %2, align 4
  %8 = or i32 %7, %6
  store i32 %8, ptr %2, align 4
  %9 = load i32, ptr %2, align 4
  %10 = lshr i32 %9, 2
  %11 = load i32, ptr %2, align 4
  %12 = or i32 %11, %10
  store i32 %12, ptr %2, align 4
  %13 = load i32, ptr %2, align 4
  %14 = lshr i32 %13, 4
  %15 = load i32, ptr %2, align 4
  %16 = or i32 %15, %14
  store i32 %16, ptr %2, align 4
  %17 = load i32, ptr %2, align 4
  %18 = lshr i32 %17, 8
  %19 = load i32, ptr %2, align 4
  %20 = or i32 %19, %18
  store i32 %20, ptr %2, align 4
  %21 = load i32, ptr %2, align 4
  %22 = lshr i32 %21, 16
  %23 = load i32, ptr %2, align 4
  %24 = or i32 %23, %22
  store i32 %24, ptr %2, align 4
  %25 = load i32, ptr %2, align 4
  %26 = add i32 %25, 1
  store i32 %26, ptr %2, align 4
  %27 = load i32, ptr %2, align 4
  ret i32 %27
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @ilog2(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  store i32 0, ptr %3, align 4
  br label %4

4:                                                ; preds = %8, %1
  %5 = load i32, ptr %2, align 4
  %6 = lshr i32 %5, 1
  store i32 %6, ptr %2, align 4
  %7 = icmp ne i32 %6, 0
  br i1 %7, label %8, label %11

8:                                                ; preds = %4
  %9 = load i32, ptr %3, align 4
  %10 = add nsw i32 %9, 1
  store i32 %10, ptr %3, align 4
  br label %4, !llvm.loop !12

11:                                               ; preds = %4
  %12 = load i32, ptr %3, align 4
  ret i32 %12
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @abs_branchless(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  %4 = load i32, ptr %2, align 4
  %5 = ashr i32 %4, 31
  store i32 %5, ptr %3, align 4
  %6 = load i32, ptr %2, align 4
  %7 = load i32, ptr %3, align 4
  %8 = add nsw i32 %6, %7
  %9 = load i32, ptr %3, align 4
  %10 = xor i32 %8, %9
  ret i32 %10
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main() #0 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  store i32 0, ptr %1, align 4
  store i32 0, ptr %2, align 4
  %5 = call i32 @popcount(i32 noundef 255)
  %6 = load i32, ptr %2, align 4
  %7 = add nsw i32 %6, %5
  store i32 %7, ptr %2, align 4
  %8 = call i32 @is_power_of_2(i32 noundef 1024)
  %9 = load i32, ptr %2, align 4
  %10 = add nsw i32 %9, %8
  store i32 %10, ptr %2, align 4
  %11 = call i32 @set_bit(i32 noundef 0, i32 noundef 5)
  %12 = load i32, ptr %2, align 4
  %13 = add nsw i32 %12, %11
  store i32 %13, ptr %2, align 4
  %14 = call i32 @clear_bit(i32 noundef 255, i32 noundef 3)
  %15 = load i32, ptr %2, align 4
  %16 = add nsw i32 %15, %14
  store i32 %16, ptr %2, align 4
  %17 = call i32 @toggle_bit(i32 noundef 0, i32 noundef 7)
  %18 = load i32, ptr %2, align 4
  %19 = add nsw i32 %18, %17
  store i32 %19, ptr %2, align 4
  %20 = call i32 @test_bit(i32 noundef 255, i32 noundef 4)
  %21 = load i32, ptr %2, align 4
  %22 = add nsw i32 %21, %20
  store i32 %22, ptr %2, align 4
  %23 = call i32 @reverse_bits(i32 noundef 305419896)
  %24 = load i32, ptr %2, align 4
  %25 = add i32 %24, %23
  store i32 %25, ptr %2, align 4
  %26 = call i32 @clz(i32 noundef 16711680)
  %27 = load i32, ptr %2, align 4
  %28 = add nsw i32 %27, %26
  store i32 %28, ptr %2, align 4
  %29 = call i32 @extract_bits(i32 noundef -559038737, i32 noundef 8, i32 noundef 8)
  %30 = load i32, ptr %2, align 4
  %31 = add i32 %30, %29
  store i32 %31, ptr %2, align 4
  %32 = call i32 @next_pow2(i32 noundef 100)
  %33 = load i32, ptr %2, align 4
  %34 = add i32 %33, %32
  store i32 %34, ptr %2, align 4
  %35 = call i32 @ilog2(i32 noundef 256)
  %36 = load i32, ptr %2, align 4
  %37 = add nsw i32 %36, %35
  store i32 %37, ptr %2, align 4
  %38 = call i32 @abs_branchless(i32 noundef -42)
  %39 = load i32, ptr %2, align 4
  %40 = add nsw i32 %39, %38
  store i32 %40, ptr %2, align 4
  store i32 42, ptr %3, align 4
  store i32 99, ptr %4, align 4
  call void @xor_swap(ptr noundef %3, ptr noundef %4)
  %41 = load i32, ptr %3, align 4
  %42 = load i32, ptr %4, align 4
  %43 = add nsw i32 %41, %42
  %44 = load i32, ptr %2, align 4
  %45 = add nsw i32 %44, %43
  store i32 %45, ptr %2, align 4
  %46 = load i32, ptr %2, align 4
  ret i32 %46
}

attributes #0 = { noinline nounwind optnone uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3, !4, !5, !6}
!llvm.ident = !{!7}

!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, producer: "clang version 22.1.8 (https://github.com/llvm/llvm-project ca7933e47d3a3451d81e72ac174dcb5aa28b59d1)", isOptimized: false, runtimeVersion: 0, emissionKind: NoDebug, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "tests/edge_cases\\04_bitops.c", directory: "C:\\LLVM-full")
!2 = !{i32 2, !"Debug Info Version", i32 3}
!3 = !{i32 1, !"wchar_size", i32 2}
!4 = !{i32 8, !"PIC Level", i32 2}
!5 = !{i32 7, !"uwtable", i32 2}
!6 = !{i32 1, !"MaxTLSAlign", i32 65536}
!7 = !{!"clang version 22.1.8 (https://github.com/llvm/llvm-project ca7933e47d3a3451d81e72ac174dcb5aa28b59d1)"}
!8 = distinct !{!8, !9}
!9 = !{!"llvm.loop.mustprogress"}
!10 = distinct !{!10, !9}
!11 = distinct !{!11, !9}
!12 = distinct !{!12, !9}
