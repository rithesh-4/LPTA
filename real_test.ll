; ModuleID = 'C:/LLVM-full/real_test.c'
source_filename = "C:/LLVM-full/real_test.c"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc19.44.35222"

@__const.main.arr = private unnamed_addr constant [9 x i32] [i32 5, i32 3, i32 8, i32 1, i32 9, i32 2, i32 7, i32 4, i32 6], align 16

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @bubble_sort(ptr noundef %0, i32 noundef %1) #0 {
  %3 = alloca i32, align 4
  %4 = alloca ptr, align 8
  %5 = alloca i32, align 4
  %6 = alloca i32, align 4
  %7 = alloca i32, align 4
  store i32 %1, ptr %3, align 4
  store ptr %0, ptr %4, align 8
  store i32 0, ptr %5, align 4
  br label %8

8:                                                ; preds = %61, %2
  %9 = load i32, ptr %5, align 4
  %10 = load i32, ptr %3, align 4
  %11 = sub nsw i32 %10, 1
  %12 = icmp slt i32 %9, %11
  br i1 %12, label %13, label %64

13:                                               ; preds = %8
  store i32 0, ptr %6, align 4
  br label %14

14:                                               ; preds = %57, %13
  %15 = load i32, ptr %6, align 4
  %16 = load i32, ptr %3, align 4
  %17 = load i32, ptr %5, align 4
  %18 = sub nsw i32 %16, %17
  %19 = sub nsw i32 %18, 1
  %20 = icmp slt i32 %15, %19
  br i1 %20, label %21, label %60

21:                                               ; preds = %14
  %22 = load ptr, ptr %4, align 8
  %23 = load i32, ptr %6, align 4
  %24 = sext i32 %23 to i64
  %25 = getelementptr inbounds i32, ptr %22, i64 %24
  %26 = load i32, ptr %25, align 4
  %27 = load ptr, ptr %4, align 8
  %28 = load i32, ptr %6, align 4
  %29 = add nsw i32 %28, 1
  %30 = sext i32 %29 to i64
  %31 = getelementptr inbounds i32, ptr %27, i64 %30
  %32 = load i32, ptr %31, align 4
  %33 = icmp sgt i32 %26, %32
  br i1 %33, label %34, label %56

34:                                               ; preds = %21
  %35 = load ptr, ptr %4, align 8
  %36 = load i32, ptr %6, align 4
  %37 = sext i32 %36 to i64
  %38 = getelementptr inbounds i32, ptr %35, i64 %37
  %39 = load i32, ptr %38, align 4
  store i32 %39, ptr %7, align 4
  %40 = load ptr, ptr %4, align 8
  %41 = load i32, ptr %6, align 4
  %42 = add nsw i32 %41, 1
  %43 = sext i32 %42 to i64
  %44 = getelementptr inbounds i32, ptr %40, i64 %43
  %45 = load i32, ptr %44, align 4
  %46 = load ptr, ptr %4, align 8
  %47 = load i32, ptr %6, align 4
  %48 = sext i32 %47 to i64
  %49 = getelementptr inbounds i32, ptr %46, i64 %48
  store i32 %45, ptr %49, align 4
  %50 = load i32, ptr %7, align 4
  %51 = load ptr, ptr %4, align 8
  %52 = load i32, ptr %6, align 4
  %53 = add nsw i32 %52, 1
  %54 = sext i32 %53 to i64
  %55 = getelementptr inbounds i32, ptr %51, i64 %54
  store i32 %50, ptr %55, align 4
  br label %56

56:                                               ; preds = %34, %21
  br label %57

57:                                               ; preds = %56
  %58 = load i32, ptr %6, align 4
  %59 = add nsw i32 %58, 1
  store i32 %59, ptr %6, align 4
  br label %14, !llvm.loop !8

60:                                               ; preds = %14
  br label %61

61:                                               ; preds = %60
  %62 = load i32, ptr %5, align 4
  %63 = add nsw i32 %62, 1
  store i32 %63, ptr %5, align 4
  br label %8, !llvm.loop !10

64:                                               ; preds = %8
  ret void
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @fib(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  store i32 %0, ptr %3, align 4
  %4 = load i32, ptr %3, align 4
  %5 = icmp sle i32 %4, 1
  br i1 %5, label %6, label %8

6:                                                ; preds = %1
  %7 = load i32, ptr %3, align 4
  store i32 %7, ptr %2, align 4
  br label %16

8:                                                ; preds = %1
  %9 = load i32, ptr %3, align 4
  %10 = sub nsw i32 %9, 1
  %11 = call i32 @fib(i32 noundef %10)
  %12 = load i32, ptr %3, align 4
  %13 = sub nsw i32 %12, 2
  %14 = call i32 @fib(i32 noundef %13)
  %15 = add nsw i32 %11, %14
  store i32 %15, ptr %2, align 4
  br label %16

16:                                               ; preds = %8, %6
  %17 = load i32, ptr %2, align 4
  ret i32 %17
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @dot_product(ptr noundef %0, ptr noundef %1, i32 noundef %2) #0 {
  %4 = alloca i32, align 4
  %5 = alloca ptr, align 8
  %6 = alloca ptr, align 8
  %7 = alloca i32, align 4
  %8 = alloca i32, align 4
  store i32 %2, ptr %4, align 4
  store ptr %1, ptr %5, align 8
  store ptr %0, ptr %6, align 8
  store i32 0, ptr %7, align 4
  store i32 0, ptr %8, align 4
  br label %9

9:                                                ; preds = %27, %3
  %10 = load i32, ptr %8, align 4
  %11 = load i32, ptr %4, align 4
  %12 = icmp slt i32 %10, %11
  br i1 %12, label %13, label %30

13:                                               ; preds = %9
  %14 = load ptr, ptr %6, align 8
  %15 = load i32, ptr %8, align 4
  %16 = sext i32 %15 to i64
  %17 = getelementptr inbounds i32, ptr %14, i64 %16
  %18 = load i32, ptr %17, align 4
  %19 = load ptr, ptr %5, align 8
  %20 = load i32, ptr %8, align 4
  %21 = sext i32 %20 to i64
  %22 = getelementptr inbounds i32, ptr %19, i64 %21
  %23 = load i32, ptr %22, align 4
  %24 = mul nsw i32 %18, %23
  %25 = load i32, ptr %7, align 4
  %26 = add nsw i32 %25, %24
  store i32 %26, ptr %7, align 4
  br label %27

27:                                               ; preds = %13
  %28 = load i32, ptr %8, align 4
  %29 = add nsw i32 %28, 1
  store i32 %29, ptr %8, align 4
  br label %9, !llvm.loop !11

30:                                               ; preds = %9
  %31 = load i32, ptr %7, align 4
  ret i32 %31
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @mat_mul(ptr noundef %0, ptr noundef %1, ptr noundef %2, i32 noundef %3) #0 {
  %5 = alloca i32, align 4
  %6 = alloca ptr, align 8
  %7 = alloca ptr, align 8
  %8 = alloca ptr, align 8
  %9 = alloca i32, align 4
  %10 = alloca i32, align 4
  %11 = alloca i32, align 4
  %12 = alloca i32, align 4
  store i32 %3, ptr %5, align 4
  store ptr %2, ptr %6, align 8
  store ptr %1, ptr %7, align 8
  store ptr %0, ptr %8, align 8
  store i32 0, ptr %9, align 4
  br label %13

13:                                               ; preds = %66, %4
  %14 = load i32, ptr %9, align 4
  %15 = load i32, ptr %5, align 4
  %16 = icmp slt i32 %14, %15
  br i1 %16, label %17, label %69

17:                                               ; preds = %13
  store i32 0, ptr %10, align 4
  br label %18

18:                                               ; preds = %62, %17
  %19 = load i32, ptr %10, align 4
  %20 = load i32, ptr %5, align 4
  %21 = icmp slt i32 %19, %20
  br i1 %21, label %22, label %65

22:                                               ; preds = %18
  store i32 0, ptr %11, align 4
  store i32 0, ptr %12, align 4
  br label %23

23:                                               ; preds = %49, %22
  %24 = load i32, ptr %12, align 4
  %25 = load i32, ptr %5, align 4
  %26 = icmp slt i32 %24, %25
  br i1 %26, label %27, label %52

27:                                               ; preds = %23
  %28 = load ptr, ptr %8, align 8
  %29 = load i32, ptr %9, align 4
  %30 = load i32, ptr %5, align 4
  %31 = mul nsw i32 %29, %30
  %32 = load i32, ptr %12, align 4
  %33 = add nsw i32 %31, %32
  %34 = sext i32 %33 to i64
  %35 = getelementptr inbounds i32, ptr %28, i64 %34
  %36 = load i32, ptr %35, align 4
  %37 = load ptr, ptr %7, align 8
  %38 = load i32, ptr %12, align 4
  %39 = load i32, ptr %5, align 4
  %40 = mul nsw i32 %38, %39
  %41 = load i32, ptr %10, align 4
  %42 = add nsw i32 %40, %41
  %43 = sext i32 %42 to i64
  %44 = getelementptr inbounds i32, ptr %37, i64 %43
  %45 = load i32, ptr %44, align 4
  %46 = mul nsw i32 %36, %45
  %47 = load i32, ptr %11, align 4
  %48 = add nsw i32 %47, %46
  store i32 %48, ptr %11, align 4
  br label %49

49:                                               ; preds = %27
  %50 = load i32, ptr %12, align 4
  %51 = add nsw i32 %50, 1
  store i32 %51, ptr %12, align 4
  br label %23, !llvm.loop !12

52:                                               ; preds = %23
  %53 = load i32, ptr %11, align 4
  %54 = load ptr, ptr %6, align 8
  %55 = load i32, ptr %9, align 4
  %56 = load i32, ptr %5, align 4
  %57 = mul nsw i32 %55, %56
  %58 = load i32, ptr %10, align 4
  %59 = add nsw i32 %57, %58
  %60 = sext i32 %59 to i64
  %61 = getelementptr inbounds i32, ptr %54, i64 %60
  store i32 %53, ptr %61, align 4
  br label %62

62:                                               ; preds = %52
  %63 = load i32, ptr %10, align 4
  %64 = add nsw i32 %63, 1
  store i32 %64, ptr %10, align 4
  br label %18, !llvm.loop !13

65:                                               ; preds = %18
  br label %66

66:                                               ; preds = %65
  %67 = load i32, ptr %9, align 4
  %68 = add nsw i32 %67, 1
  store i32 %68, ptr %9, align 4
  br label %13, !llvm.loop !14

69:                                               ; preds = %13
  ret void
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @compute_with_dead_code(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  %6 = alloca i32, align 4
  %7 = alloca i32, align 4
  %8 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  %9 = load i32, ptr %2, align 4
  %10 = mul nsw i32 %9, 2
  store i32 %10, ptr %3, align 4
  %11 = load i32, ptr %3, align 4
  %12 = add nsw i32 %11, 1
  store i32 %12, ptr %4, align 4
  %13 = load i32, ptr %4, align 4
  %14 = mul nsw i32 %13, 42
  store i32 %14, ptr %5, align 4
  store i32 21, ptr %6, align 4
  %15 = load i32, ptr %2, align 4
  %16 = icmp sgt i32 %15, 0
  br i1 %16, label %17, label %19

17:                                               ; preds = %1
  %18 = load i32, ptr %2, align 4
  br label %20

19:                                               ; preds = %1
  br label %20

20:                                               ; preds = %19, %17
  %21 = phi i32 [ %18, %17 ], [ 0, %19 ]
  store i32 %21, ptr %7, align 4
  %22 = load i32, ptr %7, align 4
  %23 = load i32, ptr %6, align 4
  %24 = add nsw i32 %22, %23
  store i32 %24, ptr %8, align 4
  %25 = load i32, ptr %8, align 4
  ret i32 %25
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @classify(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  store i32 %0, ptr %3, align 4
  %4 = load i32, ptr %3, align 4
  switch i32 %4, label %9 [
    i32 0, label %5
    i32 1, label %6
    i32 2, label %7
    i32 3, label %8
  ]

5:                                                ; preds = %1
  store i32 -1, ptr %2, align 4
  br label %11

6:                                                ; preds = %1
  store i32 0, ptr %2, align 4
  br label %11

7:                                                ; preds = %1
  store i32 1, ptr %2, align 4
  br label %11

8:                                                ; preds = %1
  store i32 2, ptr %2, align 4
  br label %11

9:                                                ; preds = %1
  %10 = load i32, ptr %3, align 4
  store i32 %10, ptr %2, align 4
  br label %11

11:                                               ; preds = %9, %8, %7, %6, %5
  %12 = load i32, ptr %2, align 4
  ret i32 %12
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @popcount(i64 noundef %0) #0 {
  %2 = alloca i64, align 8
  %3 = alloca i32, align 4
  store i64 %0, ptr %2, align 8
  store i32 0, ptr %3, align 4
  br label %4

4:                                                ; preds = %7, %1
  %5 = load i64, ptr %2, align 8
  %6 = icmp ne i64 %5, 0
  br i1 %6, label %7, label %16

7:                                                ; preds = %4
  %8 = load i64, ptr %2, align 8
  %9 = and i64 %8, 1
  %10 = load i32, ptr %3, align 4
  %11 = sext i32 %10 to i64
  %12 = add i64 %11, %9
  %13 = trunc i64 %12 to i32
  store i32 %13, ptr %3, align 4
  %14 = load i64, ptr %2, align 8
  %15 = lshr i64 %14, 1
  store i64 %15, ptr %2, align 8
  br label %4, !llvm.loop !15

16:                                               ; preds = %4
  %17 = load i32, ptr %3, align 4
  ret i32 %17
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main(i32 noundef %0, ptr noundef %1) #0 {
  %3 = alloca i32, align 4
  %4 = alloca ptr, align 8
  %5 = alloca i32, align 4
  %6 = alloca [9 x i32], align 16
  %7 = alloca i32, align 4
  %8 = alloca i32, align 4
  %9 = alloca i32, align 4
  %10 = alloca i32, align 4
  %11 = alloca i32, align 4
  %12 = alloca i32, align 4
  %13 = alloca i32, align 4
  %14 = alloca i32, align 4
  store i32 0, ptr %3, align 4
  store ptr %1, ptr %4, align 8
  store i32 %0, ptr %5, align 4
  call void @llvm.memcpy.p0.p0.i64(ptr align 16 %6, ptr align 16 @__const.main.arr, i64 36, i1 false)
  store i32 9, ptr %7, align 4
  %15 = load i32, ptr %7, align 4
  %16 = getelementptr inbounds [9 x i32], ptr %6, i64 0, i64 0
  call void @bubble_sort(ptr noundef %16, i32 noundef %15)
  store i32 0, ptr %8, align 4
  store i32 0, ptr %9, align 4
  br label %17

17:                                               ; preds = %28, %2
  %18 = load i32, ptr %9, align 4
  %19 = load i32, ptr %7, align 4
  %20 = icmp slt i32 %18, %19
  br i1 %20, label %21, label %31

21:                                               ; preds = %17
  %22 = load i32, ptr %9, align 4
  %23 = sext i32 %22 to i64
  %24 = getelementptr inbounds [9 x i32], ptr %6, i64 0, i64 %23
  %25 = load i32, ptr %24, align 4
  %26 = load i32, ptr %8, align 4
  %27 = add nsw i32 %26, %25
  store i32 %27, ptr %8, align 4
  br label %28

28:                                               ; preds = %21
  %29 = load i32, ptr %9, align 4
  %30 = add nsw i32 %29, 1
  store i32 %30, ptr %9, align 4
  br label %17, !llvm.loop !16

31:                                               ; preds = %17
  %32 = call i32 @fib(i32 noundef 10)
  store i32 %32, ptr %10, align 4
  %33 = load i32, ptr %8, align 4
  %34 = call i32 @compute_with_dead_code(i32 noundef %33)
  store i32 %34, ptr %11, align 4
  %35 = load i32, ptr %11, align 4
  %36 = call i32 @clamp(i32 noundef %35, i32 noundef 0, i32 noundef 100)
  store i32 %36, ptr %12, align 4
  %37 = load i32, ptr %10, align 4
  %38 = call i32 @classify(i32 noundef %37)
  store i32 %38, ptr %13, align 4
  %39 = load i32, ptr %10, align 4
  %40 = sext i32 %39 to i64
  %41 = call i32 @popcount(i64 noundef %40)
  store i32 %41, ptr %14, align 4
  %42 = load i32, ptr %8, align 4
  %43 = load i32, ptr %10, align 4
  %44 = add nsw i32 %42, %43
  %45 = load i32, ptr %12, align 4
  %46 = add nsw i32 %44, %45
  %47 = load i32, ptr %13, align 4
  %48 = add nsw i32 %46, %47
  %49 = load i32, ptr %14, align 4
  %50 = add nsw i32 %48, %49
  ret i32 %50
}

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: readwrite)
declare void @llvm.memcpy.p0.p0.i64(ptr noalias writeonly captures(none), ptr noalias readonly captures(none), i64, i1 immarg) #1

; Function Attrs: noinline nounwind optnone uwtable
define internal i32 @clamp(i32 noundef %0, i32 noundef %1, i32 noundef %2) #0 {
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  %6 = alloca i32, align 4
  %7 = alloca i32, align 4
  store i32 %2, ptr %5, align 4
  store i32 %1, ptr %6, align 4
  store i32 %0, ptr %7, align 4
  %8 = load i32, ptr %7, align 4
  %9 = load i32, ptr %6, align 4
  %10 = icmp slt i32 %8, %9
  br i1 %10, label %11, label %13

11:                                               ; preds = %3
  %12 = load i32, ptr %6, align 4
  store i32 %12, ptr %4, align 4
  br label %21

13:                                               ; preds = %3
  %14 = load i32, ptr %7, align 4
  %15 = load i32, ptr %5, align 4
  %16 = icmp sgt i32 %14, %15
  br i1 %16, label %17, label %19

17:                                               ; preds = %13
  %18 = load i32, ptr %5, align 4
  store i32 %18, ptr %4, align 4
  br label %21

19:                                               ; preds = %13
  %20 = load i32, ptr %7, align 4
  store i32 %20, ptr %4, align 4
  br label %21

21:                                               ; preds = %19, %17, %11
  %22 = load i32, ptr %4, align 4
  ret i32 %22
}

attributes #0 = { noinline nounwind optnone uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { nocallback nofree nounwind willreturn memory(argmem: readwrite) }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3, !4, !5, !6}
!llvm.ident = !{!7}

!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, producer: "clang version 22.1.8 (https://github.com/llvm/llvm-project ca7933e47d3a3451d81e72ac174dcb5aa28b59d1)", isOptimized: false, runtimeVersion: 0, emissionKind: NoDebug, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "C:/LLVM-full\\real_test.c", directory: "C:\\LLVM-full")
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
!13 = distinct !{!13, !9}
!14 = distinct !{!14, !9}
!15 = distinct !{!15, !9}
!16 = distinct !{!16, !9}
