; ModuleID = 'tests/edge_cases/07_phi_nodes.c'
source_filename = "tests/edge_cases/07_phi_nodes.c"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc19.44.35222"

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @diamond(i32 noundef %0, i32 noundef %1) #0 {
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  store i32 %1, ptr %3, align 4
  store i32 %0, ptr %4, align 4
  %6 = load i32, ptr %4, align 4
  %7 = icmp sgt i32 %6, 0
  br i1 %7, label %8, label %12

8:                                                ; preds = %2
  %9 = load i32, ptr %4, align 4
  %10 = load i32, ptr %3, align 4
  %11 = add nsw i32 %9, %10
  store i32 %11, ptr %5, align 4
  br label %16

12:                                               ; preds = %2
  %13 = load i32, ptr %4, align 4
  %14 = load i32, ptr %3, align 4
  %15 = sub nsw i32 %13, %14
  store i32 %15, ptr %5, align 4
  br label %16

16:                                               ; preds = %12, %8
  %17 = load i32, ptr %5, align 4
  %18 = mul nsw i32 %17, 2
  ret i32 %18
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @three_way_merge(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  %4 = load i32, ptr %2, align 4
  %5 = icmp sgt i32 %4, 100
  br i1 %5, label %6, label %9

6:                                                ; preds = %1
  %7 = load i32, ptr %2, align 4
  %8 = mul nsw i32 %7, 2
  store i32 %8, ptr %3, align 4
  br label %19

9:                                                ; preds = %1
  %10 = load i32, ptr %2, align 4
  %11 = icmp sgt i32 %10, 50
  br i1 %11, label %12, label %15

12:                                               ; preds = %9
  %13 = load i32, ptr %2, align 4
  %14 = add nsw i32 %13, 10
  store i32 %14, ptr %3, align 4
  br label %18

15:                                               ; preds = %9
  %16 = load i32, ptr %2, align 4
  %17 = sub nsw i32 %16, 5
  store i32 %17, ptr %3, align 4
  br label %18

18:                                               ; preds = %15, %12
  br label %19

19:                                               ; preds = %18, %6
  %20 = load i32, ptr %3, align 4
  ret i32 %20
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @multi_phi(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  %6 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  store i32 0, ptr %3, align 4
  store i32 1, ptr %4, align 4
  store i32 0, ptr %5, align 4
  br label %7

7:                                                ; preds = %17, %1
  %8 = load i32, ptr %5, align 4
  %9 = load i32, ptr %2, align 4
  %10 = icmp slt i32 %8, %9
  br i1 %10, label %11, label %20

11:                                               ; preds = %7
  %12 = load i32, ptr %3, align 4
  %13 = load i32, ptr %4, align 4
  %14 = add nsw i32 %12, %13
  store i32 %14, ptr %6, align 4
  %15 = load i32, ptr %4, align 4
  store i32 %15, ptr %3, align 4
  %16 = load i32, ptr %6, align 4
  store i32 %16, ptr %4, align 4
  br label %17

17:                                               ; preds = %11
  %18 = load i32, ptr %5, align 4
  %19 = add nsw i32 %18, 1
  store i32 %19, ptr %5, align 4
  br label %7, !llvm.loop !8

20:                                               ; preds = %7
  %21 = load i32, ptr %3, align 4
  %22 = load i32, ptr %4, align 4
  %23 = add nsw i32 %21, %22
  ret i32 %23
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @nested_diamond(i32 noundef %0, i32 noundef %1, i32 noundef %2) #0 {
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  %6 = alloca i32, align 4
  %7 = alloca i32, align 4
  %8 = alloca i32, align 4
  store i32 %2, ptr %4, align 4
  store i32 %1, ptr %5, align 4
  store i32 %0, ptr %6, align 4
  %9 = load i32, ptr %6, align 4
  %10 = icmp sgt i32 %9, 0
  br i1 %10, label %11, label %23

11:                                               ; preds = %3
  %12 = load i32, ptr %5, align 4
  %13 = icmp sgt i32 %12, 0
  br i1 %13, label %14, label %18

14:                                               ; preds = %11
  %15 = load i32, ptr %6, align 4
  %16 = load i32, ptr %5, align 4
  %17 = add nsw i32 %15, %16
  store i32 %17, ptr %7, align 4
  br label %22

18:                                               ; preds = %11
  %19 = load i32, ptr %6, align 4
  %20 = load i32, ptr %5, align 4
  %21 = sub nsw i32 %19, %20
  store i32 %21, ptr %7, align 4
  br label %22

22:                                               ; preds = %18, %14
  br label %35

23:                                               ; preds = %3
  %24 = load i32, ptr %4, align 4
  %25 = icmp sgt i32 %24, 0
  br i1 %25, label %26, label %30

26:                                               ; preds = %23
  %27 = load i32, ptr %6, align 4
  %28 = load i32, ptr %4, align 4
  %29 = add nsw i32 %27, %28
  store i32 %29, ptr %7, align 4
  br label %34

30:                                               ; preds = %23
  %31 = load i32, ptr %6, align 4
  %32 = load i32, ptr %4, align 4
  %33 = sub nsw i32 %31, %32
  store i32 %33, ptr %7, align 4
  br label %34

34:                                               ; preds = %30, %26
  br label %35

35:                                               ; preds = %34, %22
  %36 = load i32, ptr %7, align 4
  %37 = icmp sgt i32 %36, 0
  br i1 %37, label %38, label %41

38:                                               ; preds = %35
  %39 = load i32, ptr %7, align 4
  %40 = mul nsw i32 %39, 2
  store i32 %40, ptr %8, align 4
  br label %44

41:                                               ; preds = %35
  %42 = load i32, ptr %7, align 4
  %43 = sub nsw i32 0, %42
  store i32 %43, ptr %8, align 4
  br label %44

44:                                               ; preds = %41, %38
  %45 = load i32, ptr %8, align 4
  ret i32 %45
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @switch_merge(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  %4 = load i32, ptr %2, align 4
  %5 = srem i32 %4, 5
  switch i32 %5, label %11 [
    i32 0, label %6
    i32 1, label %7
    i32 2, label %8
    i32 3, label %9
    i32 4, label %10
  ]

6:                                                ; preds = %1
  store i32 10, ptr %3, align 4
  br label %12

7:                                                ; preds = %1
  store i32 20, ptr %3, align 4
  br label %12

8:                                                ; preds = %1
  store i32 30, ptr %3, align 4
  br label %12

9:                                                ; preds = %1
  store i32 40, ptr %3, align 4
  br label %12

10:                                               ; preds = %1
  store i32 50, ptr %3, align 4
  br label %12

11:                                               ; preds = %1
  store i32 0, ptr %3, align 4
  br label %12

12:                                               ; preds = %11, %10, %9, %8, %7, %6
  %13 = load i32, ptr %3, align 4
  %14 = add nsw i32 %13, 1
  ret i32 %14
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @complex_merge(i32 noundef %0, i32 noundef %1, i32 noundef %2) #0 {
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  %6 = alloca i32, align 4
  %7 = alloca i32, align 4
  %8 = alloca i32, align 4
  %9 = alloca i32, align 4
  store i32 %2, ptr %4, align 4
  store i32 %1, ptr %5, align 4
  store i32 %0, ptr %6, align 4
  %10 = load i32, ptr %6, align 4
  %11 = icmp sgt i32 %10, 0
  br i1 %11, label %12, label %13

12:                                               ; preds = %3
  store i32 1, ptr %7, align 4
  br label %14

13:                                               ; preds = %3
  store i32 0, ptr %7, align 4
  br label %14

14:                                               ; preds = %13, %12
  %15 = load i32, ptr %5, align 4
  %16 = icmp sgt i32 %15, 0
  br i1 %16, label %17, label %18

17:                                               ; preds = %14
  store i32 1, ptr %8, align 4
  br label %19

18:                                               ; preds = %14
  store i32 0, ptr %8, align 4
  br label %19

19:                                               ; preds = %18, %17
  %20 = load i32, ptr %4, align 4
  %21 = icmp sgt i32 %20, 0
  br i1 %21, label %22, label %23

22:                                               ; preds = %19
  store i32 1, ptr %9, align 4
  br label %24

23:                                               ; preds = %19
  store i32 0, ptr %9, align 4
  br label %24

24:                                               ; preds = %23, %22
  %25 = load i32, ptr %7, align 4
  %26 = load i32, ptr %8, align 4
  %27 = add nsw i32 %25, %26
  %28 = load i32, ptr %9, align 4
  %29 = add nsw i32 %27, %28
  ret i32 %29
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @chained(i32 noundef %0, i32 noundef %1, i32 noundef %2) #0 {
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  %6 = alloca i32, align 4
  %7 = alloca i32, align 4
  store i32 %2, ptr %4, align 4
  store i32 %1, ptr %5, align 4
  store i32 %0, ptr %6, align 4
  store i32 0, ptr %7, align 4
  %8 = load i32, ptr %6, align 4
  %9 = icmp sgt i32 %8, 0
  br i1 %9, label %10, label %14

10:                                               ; preds = %3
  %11 = load i32, ptr %6, align 4
  %12 = load i32, ptr %7, align 4
  %13 = add nsw i32 %12, %11
  store i32 %13, ptr %7, align 4
  br label %14

14:                                               ; preds = %10, %3
  %15 = load i32, ptr %5, align 4
  %16 = icmp sgt i32 %15, 0
  br i1 %16, label %17, label %21

17:                                               ; preds = %14
  %18 = load i32, ptr %5, align 4
  %19 = load i32, ptr %7, align 4
  %20 = add nsw i32 %19, %18
  store i32 %20, ptr %7, align 4
  br label %21

21:                                               ; preds = %17, %14
  %22 = load i32, ptr %4, align 4
  %23 = icmp sgt i32 %22, 0
  br i1 %23, label %24, label %28

24:                                               ; preds = %21
  %25 = load i32, ptr %4, align 4
  %26 = load i32, ptr %7, align 4
  %27 = add nsw i32 %26, %25
  store i32 %27, ptr %7, align 4
  br label %28

28:                                               ; preds = %24, %21
  %29 = load i32, ptr %7, align 4
  ret i32 %29
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @fib(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  %6 = alloca i32, align 4
  %7 = alloca i32, align 4
  store i32 %0, ptr %3, align 4
  %8 = load i32, ptr %3, align 4
  %9 = icmp sle i32 %8, 0
  br i1 %9, label %10, label %11

10:                                               ; preds = %1
  store i32 0, ptr %2, align 4
  br label %31

11:                                               ; preds = %1
  %12 = load i32, ptr %3, align 4
  %13 = icmp eq i32 %12, 1
  br i1 %13, label %14, label %15

14:                                               ; preds = %11
  store i32 1, ptr %2, align 4
  br label %31

15:                                               ; preds = %11
  store i32 0, ptr %4, align 4
  store i32 1, ptr %5, align 4
  store i32 2, ptr %6, align 4
  br label %16

16:                                               ; preds = %26, %15
  %17 = load i32, ptr %6, align 4
  %18 = load i32, ptr %3, align 4
  %19 = icmp sle i32 %17, %18
  br i1 %19, label %20, label %29

20:                                               ; preds = %16
  %21 = load i32, ptr %5, align 4
  %22 = load i32, ptr %4, align 4
  %23 = add nsw i32 %21, %22
  store i32 %23, ptr %7, align 4
  %24 = load i32, ptr %5, align 4
  store i32 %24, ptr %4, align 4
  %25 = load i32, ptr %7, align 4
  store i32 %25, ptr %5, align 4
  br label %26

26:                                               ; preds = %20
  %27 = load i32, ptr %6, align 4
  %28 = add nsw i32 %27, 1
  store i32 %28, ptr %6, align 4
  br label %16, !llvm.loop !10

29:                                               ; preds = %16
  %30 = load i32, ptr %5, align 4
  store i32 %30, ptr %2, align 4
  br label %31

31:                                               ; preds = %29, %14, %10
  %32 = load i32, ptr %2, align 4
  ret i32 %32
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @dependency(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  store i32 0, ptr %3, align 4
  store i32 1, ptr %4, align 4
  br label %5

5:                                                ; preds = %13, %1
  %6 = load i32, ptr %4, align 4
  %7 = load i32, ptr %2, align 4
  %8 = icmp sle i32 %6, %7
  br i1 %8, label %9, label %16

9:                                                ; preds = %5
  %10 = load i32, ptr %3, align 4
  %11 = load i32, ptr %4, align 4
  %12 = add nsw i32 %10, %11
  store i32 %12, ptr %3, align 4
  br label %13

13:                                               ; preds = %9
  %14 = load i32, ptr %4, align 4
  %15 = add nsw i32 %14, 1
  store i32 %15, ptr %4, align 4
  br label %5, !llvm.loop !11

16:                                               ; preds = %5
  %17 = load i32, ptr %3, align 4
  ret i32 %17
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @multi_exit(i32 noundef %0) #0 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  store i32 0, ptr %3, align 4
  store i32 0, ptr %4, align 4
  br label %5

5:                                                ; preds = %22, %1
  %6 = load i32, ptr %4, align 4
  %7 = load i32, ptr %2, align 4
  %8 = icmp slt i32 %6, %7
  br i1 %8, label %9, label %25

9:                                                ; preds = %5
  %10 = load i32, ptr %4, align 4
  %11 = srem i32 %10, 7
  %12 = icmp eq i32 %11, 0
  br i1 %12, label %13, label %14

13:                                               ; preds = %9
  br label %22

14:                                               ; preds = %9
  %15 = load i32, ptr %4, align 4
  %16 = icmp sgt i32 %15, 50
  br i1 %16, label %17, label %18

17:                                               ; preds = %14
  br label %25

18:                                               ; preds = %14
  %19 = load i32, ptr %4, align 4
  %20 = load i32, ptr %3, align 4
  %21 = add nsw i32 %20, %19
  store i32 %21, ptr %3, align 4
  br label %22

22:                                               ; preds = %18, %13
  %23 = load i32, ptr %4, align 4
  %24 = add nsw i32 %23, 1
  store i32 %24, ptr %4, align 4
  br label %5, !llvm.loop !12

25:                                               ; preds = %17, %5
  %26 = load i32, ptr %3, align 4
  ret i32 %26
}

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main() #0 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  store i32 0, ptr %1, align 4
  store i32 0, ptr %2, align 4
  %3 = call i32 @diamond(i32 noundef 10, i32 noundef 5)
  %4 = load i32, ptr %2, align 4
  %5 = add nsw i32 %4, %3
  store i32 %5, ptr %2, align 4
  %6 = call i32 @three_way_merge(i32 noundef 75)
  %7 = load i32, ptr %2, align 4
  %8 = add nsw i32 %7, %6
  store i32 %8, ptr %2, align 4
  %9 = call i32 @multi_phi(i32 noundef 20)
  %10 = load i32, ptr %2, align 4
  %11 = add nsw i32 %10, %9
  store i32 %11, ptr %2, align 4
  %12 = call i32 @nested_diamond(i32 noundef 5, i32 noundef -3, i32 noundef 7)
  %13 = load i32, ptr %2, align 4
  %14 = add nsw i32 %13, %12
  store i32 %14, ptr %2, align 4
  %15 = call i32 @switch_merge(i32 noundef 3)
  %16 = load i32, ptr %2, align 4
  %17 = add nsw i32 %16, %15
  store i32 %17, ptr %2, align 4
  %18 = call i32 @complex_merge(i32 noundef 1, i32 noundef -1, i32 noundef 1)
  %19 = load i32, ptr %2, align 4
  %20 = add nsw i32 %19, %18
  store i32 %20, ptr %2, align 4
  %21 = call i32 @chained(i32 noundef 5, i32 noundef -1, i32 noundef 3)
  %22 = load i32, ptr %2, align 4
  %23 = add nsw i32 %22, %21
  store i32 %23, ptr %2, align 4
  %24 = call i32 @fib(i32 noundef 20)
  %25 = load i32, ptr %2, align 4
  %26 = add nsw i32 %25, %24
  store i32 %26, ptr %2, align 4
  %27 = call i32 @dependency(i32 noundef 100)
  %28 = load i32, ptr %2, align 4
  %29 = add nsw i32 %28, %27
  store i32 %29, ptr %2, align 4
  %30 = call i32 @multi_exit(i32 noundef 100)
  %31 = load i32, ptr %2, align 4
  %32 = add nsw i32 %31, %30
  store i32 %32, ptr %2, align 4
  %33 = load i32, ptr %2, align 4
  ret i32 %33
}

attributes #0 = { noinline nounwind optnone uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3, !4, !5, !6}
!llvm.ident = !{!7}

!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, producer: "clang version 22.1.8 (https://github.com/llvm/llvm-project ca7933e47d3a3451d81e72ac174dcb5aa28b59d1)", isOptimized: false, runtimeVersion: 0, emissionKind: NoDebug, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "tests/edge_cases\\07_phi_nodes.c", directory: "C:\\LLVM-full")
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
